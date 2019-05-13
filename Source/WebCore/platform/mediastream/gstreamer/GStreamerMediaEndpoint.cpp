/*
 *  Copyright (C) 2017 Igalia S.L. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "GStreamerMediaEndpoint.h"

#if USE(GSTREAMER_WEBRTC)

#include "EventNames.h"
// #include "GStreamerPeerConnectionBackend.h"
#include "GStreamerCommon.h"
#include "GStreamerRtpTransceiverBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "MediaEndpointConfiguration.h"
// #include "MediaStreamEvent.h"
#include "NotImplemented.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include "RealtimeOutgoingVideoSourceGStreamer.h"
#include "RTCIceCandidate.h"
#include "RTCOfferOptions.h"
#include "RTCPeerConnection.h"
#include "RTCSessionDescription.h"
#include "RTCTrackEvent.h"
#include "RuntimeEnabledFeatures.h"
#include <gst/sdp/sdp.h>

#include <wtf/glib/RunLoopSourcePriority.h>
#include <wtf/MainThread.h>

GST_DEBUG_CATEGORY(webkit_webrtc_endpoint_debug);
#define GST_CAT_DEFAULT webkit_webrtc_endpoint_debug

namespace WebCore {

static GUniquePtr<gchar> enumValue(GType type, guint value)
{
    GEnumClass* enumClass;
    GEnumValue* enumValue;
    gchar* str = nullptr;

    enumClass = reinterpret_cast<GEnumClass*>(g_type_class_ref(type));
    enumValue = g_enum_get_value(enumClass, value);

    if (enumValue)
        str = g_strdup(enumValue->value_nick);

    g_type_class_unref(enumClass);
    return GUniquePtr<gchar>(str);
}

void GStreamerMediaEndpoint::signalingStateChangedCallback(GStreamerMediaEndpoint* endpoint)
{
    endpoint->OnSignalingStateChange();
}

void GStreamerMediaEndpoint::iceConnectionStateChangedCallback(GStreamerMediaEndpoint* endpoint)
{
    endpoint->OnIceConnectionChange();
}

void GStreamerMediaEndpoint::iceGatheringStateChangedCallback(GStreamerMediaEndpoint* endpoint)
{
    endpoint->OnIceGatheringChange();
}

void GStreamerMediaEndpoint::negotiationNeededCallback(GStreamerMediaEndpoint* endpoint)
{
    endpoint->OnNegotiationNeeded();
}

void GStreamerMediaEndpoint::iceCandidateCallback(GstElement*, guint sdpMLineIndex, gchararray candidate, GStreamerMediaEndpoint* endpoint)
{
    endpoint->OnIceCandidate(sdpMLineIndex, candidate);
}

void GStreamerMediaEndpoint::webrtcBinPadAddedCallback(GstElement*, GstPad* pad, GStreamerMediaEndpoint* endpoint)
{
    // Ignore outgoing pad notifications.
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;

    endpoint->OnAddStream(pad);
}

void GStreamerMediaEndpoint::webrtcBinPadRemovedCallback(GstElement*, GstPad* pad, GStreamerMediaEndpoint* endpoint)
{
    // Ignore outgoing pad notifications.
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;

    endpoint->OnRemoveStream(pad);
}

void GStreamerMediaEndpoint::webrtcBinReadyCallback(GstElement*, GStreamerMediaEndpoint* endpoint)
{
    endpoint->start();
}

GStreamerMediaEndpoint::GStreamerMediaEndpoint(GStreamerPeerConnectionBackend& peerConnection)
    : m_peerConnectionBackend(peerConnection)
    , m_statsLogTimer(*this, &GStreamerMediaEndpoint::gatherStatsForLogging)
{

    initializeGStreamer();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_endpoint_debug, "webkitwebrtcendpoint", 0, "WebKit WebRTC end-point");
    });

    m_pipeline = gst_pipeline_new(nullptr);
    // gst_pipeline_use_clock(GST_PIPELINE(m_pipeline.get()), gst_system_clock_obtain());
    // gst_element_set_base_time(m_pipeline.get(), getWebRTCBaseTime());
    // gst_element_set_start_time(m_pipeline.get(), GST_CLOCK_TIME_NONE);

    connectSimpleBusMessageCallback(m_pipeline.get());

    m_webrtcBin = gst_element_factory_make("webrtcbin", "webkit-webrtcbin");
    g_object_set(m_webrtcBin.get(), "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, nullptr);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::signaling-state", G_CALLBACK(signalingStateChangedCallback), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::ice-connection-state", G_CALLBACK(iceConnectionStateChangedCallback), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::ice-gathering-state", G_CALLBACK(iceGatheringStateChangedCallback), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "on-negotiation-needed", G_CALLBACK(negotiationNeededCallback), this);
    g_signal_connect(m_webrtcBin.get(), "on-ice-candidate", G_CALLBACK(iceCandidateCallback), this);
    g_signal_connect(m_webrtcBin.get(), "pad-added", G_CALLBACK(webrtcBinPadAddedCallback), this);
    g_signal_connect(m_webrtcBin.get(), "pad-removed", G_CALLBACK(webrtcBinPadRemovedCallback), this);
    g_signal_connect(m_webrtcBin.get(), "no-more-pads", G_CALLBACK(webrtcBinReadyCallback), this);
    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), m_webrtcBin.get());

    GST_DEBUG_OBJECT(m_pipeline.get(), "Yo");
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
}

void GStreamerMediaEndpoint::start()
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "End-point ready");
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);

    // if (RuntimeEnabledFeatures::sharedFeatures().webRTCLegacyAPIEnabled()) {
    //     callOnMainThread([protectedThis = makeRef(*this)] {
    //             protectedThis->emitMediaStream();
    //     });
    // }
}

void GStreamerMediaEndpoint::emitMediaStream()
{
    MediaStreamTrackPrivateVector tracks;
    for (auto& source : m_audioSources.values()) {
        auto track = MediaStreamTrackPrivate::create(source.leakRef());
        tracks.append(WTFMove(track));
        //stream->addTrackFromPlatform(*track);
    }
    for (auto& source : m_videoSources.values()) {
        auto track = MediaStreamTrackPrivate::create(source.leakRef());
        tracks.append(WTFMove(track));
        //stream->addTrackFromPlatform(*track);
    }

    auto stream = MediaStream::create(*m_peerConnectionBackend.connection().scriptExecutionContext(), MediaStreamPrivate::create(tracks));
    auto streamPointer = stream.ptr();
    // m_peerConnectionBackend.addRemoteStream(WTFMove(stream));
    // FIXME?
    // m_peerConnectionBackend.connection().fireEvent(MediaStreamEvent::create(eventNames().addstreamEvent, false, false, *&streamPointer));
}

bool GStreamerMediaEndpoint::setConfiguration(MediaEndpointConfiguration& configuration)
{
    //     rtcConfiguration.type = iceTransportPolicyfromConfiguration(configuration);
    //     rtcConfiguration.bundle_policy = bundlePolicyfromConfiguration(configuration);

    for (auto& server : configuration.iceServers) {
        bool turnSet = false;
        bool stunSet = false;
        for (auto& url : server.urls) {
            if (!turnSet && url.protocol().startsWith("turn")) {
                String valid = String(url.string()).replace("turn:", "turn://");
                valid = valid.replace("turns:", "turns://");
                URL validUrl(URL(), valid);
                validUrl.setUser(server.username);
                validUrl.setPass(server.credential);
                g_object_set(m_webrtcBin.get(), "turn-server", validUrl.string().utf8().data(), nullptr);
                turnSet = true;
            }
            if (!stunSet && url.protocol().startsWith("stun")) {
                String valid = String(url.string()).replace("stun:", "stun://");
                URL validUrl(URL(), valid);
                // url.setUser(server.username);
                // url.setPass(server.credential);
                g_object_set(m_webrtcBin.get(), "stun-server", validUrl.string().utf8().data(), nullptr);
                stunSet = true;
            }
        }
    }
    return true;
}

static inline RefPtr<RTCSessionDescription> fromSessionDescription(GstWebRTCSessionDescription* description)
{
    if (!description)
        return nullptr;

    String sdpString = gst_sdp_message_as_text(description->sdp);
    GST_DEBUG("Session from SDP: %s", sdpString.utf8().data());
    return RTCSessionDescription::create(fromSessionDescriptionType(description), WTFMove(sdpString));
}

// FIXME: We might want to create a new object only if the session actually changed for all description getters.
RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::currentLocalDescription() const
{
    // return m_backend ? fromSessionDescription(m_backend->current_local_description()) : nullptr;
    notImplemented();
    return nullptr;
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::currentRemoteDescription() const
{
    // return m_backend ? fromSessionDescription(m_backend->current_remote_description()) : nullptr;
    notImplemented();
    return nullptr;
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::pendingLocalDescription() const
{
    // return m_backend ? fromSessionDescription(m_backend->pending_local_description()) : nullptr;
    notImplemented();
    return nullptr;
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::pendingRemoteDescription() const
{
    // return m_backend ? fromSessionDescription(m_backend->pending_remote_description()) : nullptr;
    notImplemented();
    return nullptr;
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::localDescription() const
{
    if (!m_webrtcBin)
        return nullptr;

    GstWebRTCSessionDescription* localDescription;
    g_object_get(m_webrtcBin.get(), "local-description", &localDescription, nullptr);
    return fromSessionDescription(localDescription);
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::remoteDescription() const
{
    if (!m_webrtcBin)
        return nullptr;

    GstWebRTCSessionDescription* remoteDescription;
    g_object_get(m_webrtcBin.get(), "remote-description", &remoteDescription, nullptr);
    return fromSessionDescription(remoteDescription);
}

static void setLocalDescriptionPromiseChanged(GstPromise* promise, gpointer userData)
{
    auto endPoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
    GstPromiseResult result = gst_promise_wait(promise);

    gst_promise_unref(promise);

    if (result != GST_PROMISE_RESULT_REPLIED) {
        endPoint->emitSetLocalDescriptionFailed();
        return;
    }
    endPoint->emitSetLocalDescriptionSuceeded();
}

void GStreamerMediaEndpoint::emitSetLocalDescriptionSuceeded()
{
    setLocalSessionDescriptionSucceeded();
}

void GStreamerMediaEndpoint::emitSetLocalDescriptionFailed()
{
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setLocalDescriptionFailed(Exception { OperationError, "Unable to apply session local description" });
    });
}

void GStreamerMediaEndpoint::doSetLocalDescription(RTCSessionDescription& description)
{
    GstSDPMessage* message;
    gst_sdp_message_new(&message);
    gst_sdp_message_parse_buffer((guint8*)description.sdp().utf8().data(), description.sdp().length(), message);
    GstWebRTCSDPType type = toSessionDescriptionType(description.type());
    GUniquePtr<gchar> sdp(gst_sdp_message_as_text(message));
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating session for SDP %s: %s", gst_webrtc_sdp_type_to_string(type), sdp.get());
    GstWebRTCSessionDescription* sessionDescription = gst_webrtc_session_description_new(type, message);
    GstPromise* promise = gst_promise_new_with_change_func(setLocalDescriptionPromiseChanged, this, nullptr);
    g_signal_emit_by_name(m_webrtcBin.get(), "set-local-description", sessionDescription, promise);
    gst_webrtc_session_description_free(sessionDescription);
}

static void setRemoteDescriptionPromiseChanged(GstPromise* promise, gpointer userData)
{
    auto endPoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
    GstPromiseResult result = gst_promise_wait(promise);

    gst_promise_unref(promise);

    if (result != GST_PROMISE_RESULT_REPLIED) {
        endPoint->emitSetRemoteDescriptionFailed();
        return;
    }
    endPoint->emitSetRemoteDescriptionSuceeded();
}

void GStreamerMediaEndpoint::emitSetRemoteDescriptionSuceeded()
{
    setRemoteSessionDescriptionSucceeded();
    startLoggingStats();
}

void GStreamerMediaEndpoint::emitSetRemoteDescriptionFailed()
{
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setRemoteDescriptionFailed(Exception { OperationError, "Unable to apply session remote description" });
    });
}

void GStreamerMediaEndpoint::doSetRemoteDescription(RTCSessionDescription& description)
{
    GstSDPMessage* message;
    gst_sdp_message_new(&message);
    gst_sdp_message_parse_buffer((guint8*)description.sdp().utf8().data(), description.sdp().length(), message);
    GstWebRTCSDPType type = toSessionDescriptionType(description.type());
    GUniquePtr<gchar> sdp(gst_sdp_message_as_text(message));
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating session for SDP %s: %s", gst_webrtc_sdp_type_to_string(type), sdp.get());
    GstWebRTCSessionDescription* sessionDescription = gst_webrtc_session_description_new(type, message);
    GstPromise* promise = gst_promise_new_with_change_func(setRemoteDescriptionPromiseChanged, this, nullptr);
    g_signal_emit_by_name(m_webrtcBin.get(), "set-remote-description", sessionDescription, promise);
    gst_webrtc_session_description_free(sessionDescription);
}

bool GStreamerMediaEndpoint::addTrack(GStreamerRtpSenderBackend& sender, MediaStreamTrack& track, const Vector<String>& mediaStreamIds)
{
    // auto parameters = sender.getParameters();
    // for (auto& codec : parameters.codecs) {
    //     printf(">> payloadType: %u, mimeType: %s, clockRate: %u, channels: %lu\n", codec.payloadType, codec.mimeType.utf8().data(), codec.clockRate, codec.channels);
    // }

    // switch (track.privateTrack().type()) {
    // case RealtimeMediaSource::Type::Audio: {
    //     auto trackSource = RealtimeOutgoingAudioSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
    //     m_peerConnectionBackend.addAudioSource(WTFMove(trackSource));
    //     break;
    // }
    // case RealtimeMediaSource::Type::Video: {
    //     auto trackSource = RealtimeOutgoingVideoSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
    //     m_peerConnectionBackend.addVideoSource(WTFMove(trackSource));
    //     break;
    // }
    // case RealtimeMediaSource::Type::None:
    //     ASSERT_NOT_REACHED();
    // }

    // ASSERT(m_backend);

    // if (!RuntimeEnabledFeatures::sharedFeatures().webRTCUnifiedPlanEnabled()) {
    //     String mediaStreamId = mediaStreamIds.isEmpty() ? createCanonicalUUIDString() : mediaStreamIds[0];
    //     m_localStreams.ensure(mediaStreamId, [&] {
    //         auto mediaStream = m_peerConnectionFactory.CreateLocalMediaStream(mediaStreamId.utf8().data());
    //         m_backend->AddStream(mediaStream);
    //         return mediaStream;
    //     });
    // }

    GStreamerRtpSenderBackend::Source source;
    // rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> rtcTrack;
    switch (track.privateTrack().type()) {
    case RealtimeMediaSource::Type::Audio: {
        auto audioSource = RealtimeOutgoingAudioSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
        // rtcTrack = m_peerConnectionFactory.CreateAudioTrack(track.id().utf8().data(), audioSource.ptr());
        source = WTFMove(audioSource);
        break;
    }
    case RealtimeMediaSource::Type::Video: {
        auto videoSource = RealtimeOutgoingVideoSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
        // rtcTrack = m_peerConnectionFactory.CreateVideoTrack(track.id().utf8().data(), videoSource.ptr());
        source = WTFMove(videoSource);
        break;
    }
    case RealtimeMediaSource::Type::None:
        ASSERT_NOT_REACHED();
        return false;
    }

    g_printerr("GStreamerMediaEndpoint::addTrack 0\n");
    sender.setSource(WTFMove(source));
    if (auto rtpSender = sender.rtcSender()) {
        // FIXME: gst_webrtc_rtp_sender_set_transport or gst_webrtc_rtp_sender_set_rtcp_transport?
        // rtpSender->SetTrack(rtcTrack.get());
        gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
        g_printerr("GStreamerMediaEndpoint::addTrack 1\n");
        return true;
    }

    g_printerr("GStreamerMediaEndpoint::addTrack 2\n");
    // std::vector<std::string> ids;
    // for (auto& id : mediaStreamIds)
    //     ids.push_back(id.utf8().data());

    // auto newRTPSender = m_backend->AddTrack(rtcTrack.get(), WTFMove(ids));
    // if (!newRTPSender.ok())
    //     return false;
    // sender.setRTCSender(newRTPSender.MoveValue());

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    return true;
}

void GStreamerMediaEndpoint::removeTrack(GStreamerRtpSenderBackend& sender)
{
    // ASSERT(m_backend);
    notImplemented();
    sender.clearSource();
}

bool GStreamerMediaEndpoint::shouldOfferAllowToReceiveAudio() const
{
    for (const auto& transceiver : m_peerConnectionBackend.connection().getTransceivers()) {
        if (transceiver->sender().trackKind() != "audio")
            continue;

        if (transceiver->direction() == RTCRtpTransceiverDirection::Recvonly)
            return true;

        // if (transceiver->direction() == RTCRtpTransceiverDirection::Sendrecv && !m_senders.contains(&transceiver->sender()))
        //     return true;
        if (transceiver->direction() == RTCRtpTransceiverDirection::Sendrecv)
            return true;
    }
    return false;
}

bool GStreamerMediaEndpoint::shouldOfferAllowToReceiveVideo() const
{
    for (const auto& transceiver : m_peerConnectionBackend.connection().getTransceivers()) {
        if (transceiver->sender().trackKind() != "video")
            continue;

        if (transceiver->direction() == RTCRtpTransceiverDirection::Recvonly)
            return true;

        // if (transceiver->direction() == RTCRtpTransceiverDirection::Sendrecv && !m_senders.contains(&transceiver->sender()))
        //     return true;
        if (transceiver->direction() == RTCRtpTransceiverDirection::Sendrecv)
            return true;
    }
    return false;
}

static void offerPromiseChanged(GstPromise* promise, gpointer userData)
{
    auto endpoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
    GstPromiseResult result = gst_promise_wait(promise);
    if (result != GST_PROMISE_RESULT_REPLIED) {
        endpoint->createSessionDescriptionFailed("Error...");
        gst_promise_unref(promise);
        return;
    }

    GstWebRTCSessionDescription* offer = nullptr;
    const GstStructure* reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
    gst_promise_unref(promise);

    GUniquePtr<gchar> desc(gst_sdp_message_as_text(offer->sdp));
    GST_DEBUG_OBJECT(endpoint->pipeline(), "Created offer: %s", desc.get());
    endpoint->createSessionDescriptionSucceeded(offer);
}

static void answerPromiseChanged(GstPromise* promise, gpointer userData)
{
    auto endpoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
    GstPromiseResult result = gst_promise_wait(promise);
    if(result != GST_PROMISE_RESULT_REPLIED) {
        endpoint->createSessionDescriptionFailed("Error...");
        gst_promise_unref(promise);
        return;
    }

    GstWebRTCSessionDescription* answer = nullptr;
    const GstStructure* reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr);
    gst_promise_unref(promise);

    GUniquePtr<gchar> desc(gst_sdp_message_as_text(answer->sdp));
    GST_DEBUG_OBJECT(endpoint->pipeline(), "Created answer: %s", desc.get());
    endpoint->createSessionDescriptionSucceeded(answer);
}

void GStreamerMediaEndpoint::doCreateOffer(const RTCOfferOptions&)
{
    m_isInitiator = true;

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating offer");
    // TODO(philn): fill options (currently unused in webrtcbin anyway).
    GstPromise* promise = gst_promise_new_with_change_func(offerPromiseChanged, this, nullptr);
    GstStructure* offerOptions = NULL;
    g_signal_emit_by_name(m_webrtcBin.get(), "create-offer", offerOptions, promise);
}

void GStreamerMediaEndpoint::doCreateAnswer()
{
    m_isInitiator = false;

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating answer");
    // TODO(philn): fill options (currently unused in webrtcbin anyway).
    GstPromise* promise = gst_promise_new_with_change_func(answerPromiseChanged, this, nullptr);
    GstStructure* options = NULL;
    g_signal_emit_by_name(m_webrtcBin.get(), "create-answer", options, promise);
}

void GStreamerMediaEndpoint::getStats(Ref<DeferredPromise>&&)
{
    notImplemented();
}

void GStreamerMediaEndpoint::getStats(GstWebRTCRTPReceiver*, Ref<DeferredPromise>&&)
{
    notImplemented();
}

void GStreamerMediaEndpoint::getStats(GstWebRTCRTPSender*, Ref<DeferredPromise>&&)
{
    notImplemented();
}

void GStreamerMediaEndpoint::OnSignalingStateChange()
{
    GstWebRTCSignalingState gstState;
    g_object_get(m_webrtcBin.get(), "signaling-state", &gstState, nullptr);
    GUniquePtr<gchar> desc(enumValue(GST_TYPE_WEBRTC_SIGNALING_STATE, gstState));
    GST_DEBUG_OBJECT(m_pipeline.get(), "Signaling state changed to %s", desc.get());
    auto state = toSignalingState(gstState);
    callOnMainThread([protectedThis = makeRef(*this), state] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.updateSignalingState(state);
    });
}

void GStreamerMediaEndpoint::sourceFromGstPad(GstPad* pad)
{
    GRefPtr<GstCaps> caps = adoptGRef(gst_pad_query_caps(pad, nullptr));

    GstStructure* structure = gst_caps_get_structure(caps.get(), 0);
    const gchar* mediaType = gst_structure_get_string(structure, "media");
    if (!mediaType)
        return;

    if (g_strcmp0(mediaType, "audio") && g_strcmp0(mediaType, "video")) {
        GST_WARNING("Unsupported RTP media type: %s", mediaType);
        return;
    }

    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding remote stream for pad %" GST_PTR_FORMAT " caps: %" GST_PTR_FORMAT, pad, caps.get());
    // FIXME
    // GRefPtr<GstElement> gstSink = gst_element_factory_make("proxysink", nullptr);
    // GRefPtr<GstElement> gstSource = gst_element_factory_make("proxysrc", nullptr);
    // g_object_set(gstSource.get(), "proxysink", gstSink.get(), nullptr);
    // gst_bin_add(GST_BIN_CAST(m_pipeline.get()), gstSink.get());
    // gst_element_sync_state_with_parent(gstSink.get());

    if (!g_strcmp0(mediaType, "audio")) {
        m_audioSources.ensure(pad, [this, pad] {
            return RealtimeIncomingAudioSourceGStreamer::create(m_pipeline.get(), pad);
        });
    } else if (!g_strcmp0(mediaType, "video")) {
        m_videoSources.ensure(pad, [this, pad] {
            return RealtimeIncomingVideoSourceGStreamer::create(m_pipeline.get(), pad);
        });
    }

    // GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(gstSink.get(), "sink"));
    // gst_pad_link(pad, sinkPad.get());

    if (!m_audioSources.isEmpty() && !m_videoSources.isEmpty())
        start();
}

void GStreamerMediaEndpoint::addRemoteStream(GstPad* pad)
{
    // GstWebRTCRTPTransceiver* transceiver = nullptr;
    // g_object_get(pad, "transceiver", &transceiver, nullptr);
    // GST_DEBUG_OBJECT(m_pipeline.get(), "Pad transceiver: %" GST_PTR_FORMAT, transceiver);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding remote stream to %" GST_PTR_FORMAT, pad);
    // if (RuntimeEnabledFeatures::sharedFeatures().webRTCLegacyAPIEnabled())
    sourceFromGstPad(pad);

    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-incoming-stream");

    // TODO(philn): what about the non-legacy API... Track-based.
    GRefPtr<GstCaps> caps = adoptGRef(gst_pad_query_caps(pad, nullptr));
    GstStructure* capsStructure = gst_caps_get_structure(caps.get(), 0);
    const gchar* mediaType = gst_structure_get_string(capsStructure, "media");
    // if (!g_strcmp0(mediaType, "audio")) {
    //     auto audioReceiver = m_peerConnectionBackend.audioReceiver(trackId(*rtcTrack));
    // } else if (!g_strcmp0(mediaType, "video")) {
    //     auto videoReceiver = m_peerConnectionBackend.videoReceiver(trackId(*rtcTrack));
    // }

    // m_peerConnectionBackend.connection().fireEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false, WTFMove(receiver), track, WTFMove(streams), nullptr));
}


void GStreamerMediaEndpoint::removeRemoteStream(GstPad*)
{
    notImplemented();
    // auto* mediaStream = m_streams.take(pad);
    // if (mediaStream)
    //     m_peerConnectionBackend.removeRemoteStream(mediaStream);
}

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::addTransceiver(const String& trackKind, const RTCRtpTransceiverInit&)
{
    notImplemented();
}

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::addTransceiver(MediaStreamTrack& track, const RTCRtpTransceiverInit& init)
{
    // auto sourceAndTrack = createSourceAndRTCTrack(track);
    // return createTransceiverBackends(WTFMove(sourceAndTrack.second), init, WTFMove(sourceAndTrack.first));
    g_printerr("addTransceiver\n");
    notImplemented();
}

void GStreamerMediaEndpoint::setSenderSourceFromTrack(GStreamerRtpSenderBackend& sender, MediaStreamTrack& track)
{
    // auto sourceAndTrack = createSourceAndRTCTrack(track);
    // sender.setSource(WTFMove(sourceAndTrack.first));
    // sender.rtcSender()->SetTrack(WTFMove(sourceAndTrack.second));
    notImplemented();
}

std::unique_ptr<GStreamerRtpTransceiverBackend> GStreamerMediaEndpoint::transceiverBackendFromSender(GStreamerRtpSenderBackend& backend)
{
    GstWebRTCRTPTransceiver* current;
    GArray* transceivers;

    // TODO: GArray smart pointer?
    g_signal_emit_by_name(m_webrtcBin.get(), "get-transceivers", &transceivers);
    g_printerr("webrtcBin %p has %u transceivers\n", m_webrtcBin.get(), transceivers->len);
    g_printerr("backend rtcSender %p\n", backend.rtcSender());
    for (unsigned i = 0; i < transceivers->len; i++) {
        current = g_array_index(transceivers, GstWebRTCRTPTransceiver*, i);

        GstWebRTCRTPSender* sender = nullptr;
        g_object_get(current, "sender", &sender, nullptr);
        g_printerr("index: %u, sender: %p\n", i, sender);
        if (!sender)
            continue;
        if (!backend.rtcSender()) {
            // FIXME: GStreamerRtpTransceiverBackend should use a RefPtr for GstWebRTCRTPTransceiver.
            auto result = std::make_unique<GStreamerRtpTransceiverBackend>(reinterpret_cast<GstWebRTCRTPTransceiver*>(gst_object_ref(current)));
            g_array_unref(transceivers);
            return result;
        }
        if (sender == backend.rtcSender()) {
            g_array_unref(transceivers);
            return std::make_unique<GStreamerRtpTransceiverBackend>(current);
        }

    }
    g_array_unref(transceivers);
    return nullptr;
}

void GStreamerMediaEndpoint::OnAddStream(GstPad* pad)
{
    if (isStopped())
        return;
    addRemoteStream(pad);
}

void GStreamerMediaEndpoint::OnRemoveStream(GstPad* pad)
{
    callOnMainThread([protectedThis = makeRef(*this), pad] {
        if (protectedThis->isStopped())
            return;
        protectedThis->removeRemoteStream(pad);
    });
}

bool GStreamerMediaEndpoint::addIceCandidate(GStreamerIceCandidate& candidate)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding ICE candidate %s", candidate.candidate.utf8().data());
    g_signal_emit_by_name(m_webrtcBin.get(), "add-ice-candidate", candidate.sdpMLineIndex, candidate.candidate.utf8().data());
    return true;
}

void GStreamerMediaEndpoint::stop()
{
    stopLoggingStats();
}

void GStreamerMediaEndpoint::OnNegotiationNeeded()
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Negotiation needed!");
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.markAsNeedingNegotiation();
    });
}

void GStreamerMediaEndpoint::OnIceConnectionChange()
{
    GstWebRTCICEConnectionState state;
    g_object_get(m_webrtcBin.get(), "ice-connection-state", &state, nullptr);
    auto connectionState = toRTCIceConnectionState(state);
    callOnMainThread([protectedThis = makeRef(*this), connectionState] {
        if (protectedThis->isStopped())
            return;
        if (protectedThis->m_peerConnectionBackend.connection().iceConnectionState() != connectionState)
            protectedThis->m_peerConnectionBackend.connection().updateIceConnectionState(connectionState);
    });
}

void GStreamerMediaEndpoint::OnIceGatheringChange()
{
    GstWebRTCICEGatheringState state;
    g_object_get(m_webrtcBin.get(), "ice-gathering-state", &state, nullptr);
    callOnMainThread([protectedThis = makeRef(*this), state] {
        if (protectedThis->isStopped())
            return;
        if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE)
            protectedThis->m_peerConnectionBackend.doneGatheringCandidates();
        else if (state == GST_WEBRTC_ICE_GATHERING_STATE_GATHERING)
            protectedThis->m_peerConnectionBackend.connection().updateIceGatheringState(RTCIceGatheringState::Gathering);
    });
}


void GStreamerMediaEndpoint::OnIceCandidate(guint sdpMLineIndex, gchararray candidate)
{
    String candidateSDP(candidate);

    // auto mid = rtcCandidate->sdp_mid();
    // String candidateMid(mid.data(), mid.size());
    // FIXME(phil): somehow find a=mid: line in SDP?
    String candidateMid("video");
    GST_DEBUG_OBJECT(m_pipeline.get(), "ICE candidate: %s", candidate);

    callOnMainThread([protectedThis = makeRef(*this), mid = WTFMove(candidateMid), sdp = WTFMove(candidateSDP), sdpMLineIndex] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.newICECandidate(String(sdp), String(mid), sdpMLineIndex, String());
    });
}

void GStreamerMediaEndpoint::createSessionDescriptionSucceeded(GstWebRTCSessionDescription* description)
{
    gchar* desc = gst_sdp_message_as_text(description->sdp);
    String sdpString(desc);
    g_free(desc);

    callOnMainThread([protectedThis = makeRef(*this), sdp = WTFMove(sdpString)] {
        if (protectedThis->isStopped())
            return;
        if (protectedThis->m_isInitiator)
            protectedThis->m_peerConnectionBackend.createOfferSucceeded(String(sdp));
        else
            protectedThis->m_peerConnectionBackend.createAnswerSucceeded(String(sdp));
    });
}

void GStreamerMediaEndpoint::createSessionDescriptionFailed(const std::string& errorMessage)
{
    String error(errorMessage.data(), errorMessage.size());
    callOnMainThread([protectedThis = makeRef(*this), error = WTFMove(error)] {
        if (protectedThis->isStopped())
            return;
        if (protectedThis->m_isInitiator)
            protectedThis->m_peerConnectionBackend.createOfferFailed(Exception { OperationError, String(error) });
        else
            protectedThis->m_peerConnectionBackend.createAnswerFailed(Exception { OperationError, String(error) });
    });
}

void GStreamerMediaEndpoint::setLocalSessionDescriptionSucceeded()
{
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setLocalDescriptionSucceeded();
    });
}

void GStreamerMediaEndpoint::setRemoteSessionDescriptionSucceeded()
{
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setRemoteDescriptionSucceeded();
    });
}


// RTCRtpParameters GStreamerMediaEndpoint::getRTCRtpSenderParameters(RTCRtpSender& sender)
// {
//     notImplemented();
//     return { };
// }
void GStreamerMediaEndpoint::collectTransceivers()
{
    notImplemented();
}

void GStreamerMediaEndpoint::gatherStatsForLogging()
{
    notImplemented();
}

void GStreamerMediaEndpoint::startLoggingStats()
{
    notImplemented();
}

void GStreamerMediaEndpoint::stopLoggingStats()
{
    notImplemented();
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
