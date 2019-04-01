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
#include "GStreamerPeerConnectionBackend.h"
#include "GStreamerCommon.h"
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

static void busMessageCallback(GstBus*, GstMessage* message, GStreamerMediaEndpoint* endpoint)
{
    endpoint->handleMessage(message);
}

GStreamerMediaEndpoint::GStreamerMediaEndpoint(GStreamerPeerConnectionBackend& peerConnection)
    : m_peerConnectionBackend(peerConnection)
    , m_statsLogTimer(*this, &GStreamerMediaEndpoint::gatherStatsForLogging)
{

    initializeGStreamer();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_endpoint_debug, "webkitwebrtendpoint", 0, "WebKit WebRTC end-point");
    });

    m_pipeline = adoptGRef(gst_pipeline_new(nullptr));
    gst_pipeline_use_clock(GST_PIPELINE(m_pipeline.get()), gst_system_clock_obtain());
    gst_element_set_base_time(m_pipeline.get(), getWebRTCBaseTime());
    gst_element_set_start_time(m_pipeline.get(), GST_CLOCK_TIME_NONE);

    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_add_signal_watch_full(bus.get(), RunLoopSourcePriority::RunLoopDispatcher);
    g_signal_connect(bus.get(), "message", G_CALLBACK(busMessageCallback), this);

    m_webrtcBin = adoptGRef(gst_element_factory_make("webrtcbin", "webkit-webrtcbin"));
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::signaling-state", G_CALLBACK(signalingStateChangedCallback), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::ice-connection-state", G_CALLBACK(iceConnectionStateChangedCallback), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::ice-gathering-state", G_CALLBACK(iceGatheringStateChangedCallback), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "on-negotiation-needed", G_CALLBACK(negotiationNeededCallback), this);
    g_signal_connect(m_webrtcBin.get(), "on-ice-candidate", G_CALLBACK(iceCandidateCallback), this);
    g_signal_connect(m_webrtcBin.get(), "pad-added", G_CALLBACK(webrtcBinPadAddedCallback), this);
    g_signal_connect(m_webrtcBin.get(), "pad-removed", G_CALLBACK(webrtcBinPadRemovedCallback), this);
    g_signal_connect(m_webrtcBin.get(), "no-more-pads", G_CALLBACK(webrtcBinReadyCallback), this);
    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), m_webrtcBin.get());

    gst_element_set_state(m_pipeline.get(), GST_STATE_READY);
}

void GStreamerMediaEndpoint::handleMessage(GstMessage* message)
{
    GUniqueOutPtr<GError> err;
    GUniqueOutPtr<gchar> debug;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
        gst_message_parse_error(message, &err.outPtr(), &debug.outPtr());
        GST_ERROR("Error %d: %s", err->code, err->message);

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc.error");
        break;
    default:
        break;
    }
}

void GStreamerMediaEndpoint::start()
{
    GST_DEBUG("End-point ready");
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);

    if (RuntimeEnabledFeatures::sharedFeatures().webRTCLegacyAPIEnabled()) {
        callOnMainThread([protectedThis = makeRef(*this)] {
                protectedThis->emitMediaStream();
        });
    }
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
    m_peerConnectionBackend.addRemoteStream(WTFMove(stream));
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

static inline GstWebRTCSDPType sessionDescriptionType(RTCSdpType sdpType)
{
    switch (sdpType) {
    case RTCSdpType::Offer:
        return GST_WEBRTC_SDP_TYPE_OFFER;
    case RTCSdpType::Pranswer:
        return GST_WEBRTC_SDP_TYPE_PRANSWER;
    case RTCSdpType::Answer:
        return GST_WEBRTC_SDP_TYPE_ANSWER;
    case RTCSdpType::Rollback:
        return GST_WEBRTC_SDP_TYPE_ROLLBACK;
    }

    ASSERT_NOT_REACHED();
    return GST_WEBRTC_SDP_TYPE_OFFER;
}

static inline RTCSdpType fromSessionDescriptionType(GstWebRTCSessionDescription* description)
{
    auto type = description->type;
    if (type == GST_WEBRTC_SDP_TYPE_OFFER)
        return RTCSdpType::Offer;
    if (type == GST_WEBRTC_SDP_TYPE_ANSWER)
        return RTCSdpType::Answer;
    ASSERT(type == GST_WEBRTC_SDP_TYPE_PRANSWER);
    return RTCSdpType::Pranswer;
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
    GstWebRTCSDPType type = sessionDescriptionType(description.type());
    GUniquePtr<gchar> sdp(gst_sdp_message_as_text(message));
    GST_DEBUG("Creating session for SDP %s: %s", gst_webrtc_sdp_type_to_string(type), sdp.get());
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
    GstWebRTCSDPType type = sessionDescriptionType(description.type());
    GUniquePtr<gchar> sdp(gst_sdp_message_as_text(message));
    GST_DEBUG("Creating session for SDP %s: %s", gst_webrtc_sdp_type_to_string(type), sdp.get());
    GstWebRTCSessionDescription* sessionDescription = gst_webrtc_session_description_new(type, message);
    GstPromise* promise = gst_promise_new_with_change_func(setRemoteDescriptionPromiseChanged, this, nullptr);
    g_signal_emit_by_name(m_webrtcBin.get(), "set-remote-description", sessionDescription, promise);
    gst_webrtc_session_description_free(sessionDescription);
}

void GStreamerMediaEndpoint::addTrack(RTCRtpSender&, MediaStreamTrack& track, const Vector<String>& mediaStreamIds)
{
    // auto parameters = sender.getParameters();
    // for (auto& codec : parameters.codecs) {
    //     printf(">> payloadType: %u, mimeType: %s, clockRate: %u, channels: %lu\n", codec.payloadType, codec.mimeType.utf8().data(), codec.clockRate, codec.channels);
    // }

    switch (track.privateTrack().type()) {
    case RealtimeMediaSource::Type::Audio: {
        auto trackSource = RealtimeOutgoingAudioSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
        m_peerConnectionBackend.addAudioSource(WTFMove(trackSource));
        break;
    }
    case RealtimeMediaSource::Type::Video: {
        auto trackSource = RealtimeOutgoingVideoSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
        m_peerConnectionBackend.addVideoSource(WTFMove(trackSource));
        break;
    }
    case RealtimeMediaSource::Type::None:
        ASSERT_NOT_REACHED();
    }
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
}

void GStreamerMediaEndpoint::removeTrack(RTCRtpSender&)
{
    notImplemented();
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
    GST_DEBUG("Created offer: %s", desc.get());
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
    GST_DEBUG("Created answer: %s", desc.get());
    endpoint->createSessionDescriptionSucceeded(answer);
}

void GStreamerMediaEndpoint::doCreateOffer(const RTCOfferOptions&)
{
    m_isInitiator = true;

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG("Creating offer");
    // TODO(philn): fill options (currently unused in webrtcbin anyway).
    GstPromise* promise = gst_promise_new_with_change_func(offerPromiseChanged, this, nullptr);
    GstStructure* offerOptions = NULL;
    g_signal_emit_by_name(m_webrtcBin.get(), "create-offer", offerOptions, promise);
}

void GStreamerMediaEndpoint::doCreateAnswer()
{
    m_isInitiator = false;

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG("Creating answer");
    // TODO(philn): fill options (currently unused in webrtcbin anyway).
    GstPromise* promise = gst_promise_new_with_change_func(answerPromiseChanged, this, nullptr);
    GstStructure* options = NULL;
    g_signal_emit_by_name(m_webrtcBin.get(), "create-answer", options, promise);
}

void GStreamerMediaEndpoint::getStats(MediaStreamTrack*, const DeferredPromise&)
{
    notImplemented();
}

static RTCSignalingState signalingState(GstWebRTCSignalingState state)
{
    switch (state) {
    case GST_WEBRTC_SIGNALING_STATE_STABLE:
        return RTCSignalingState::Stable;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER:
        return RTCSignalingState::HaveLocalOffer;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER:
        return RTCSignalingState::HaveLocalPranswer;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER:
        return RTCSignalingState::HaveRemoteOffer;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER:
        return RTCSignalingState::HaveRemotePranswer;
    case GST_WEBRTC_SIGNALING_STATE_CLOSED:
        return RTCSignalingState::Stable;
    }

    ASSERT_NOT_REACHED();
    return RTCSignalingState::Stable;
}


void GStreamerMediaEndpoint::OnSignalingStateChange()
{
    GstWebRTCSignalingState gstState;
    g_object_get(m_webrtcBin.get(), "signaling-state", &gstState, nullptr);
    GUniquePtr<gchar> desc(enumValue(GST_TYPE_WEBRTC_SIGNALING_STATE, gstState));
    GST_DEBUG("Signaling state changed to %s", desc.get());
    auto state = signalingState(gstState);
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

    GST_DEBUG("Adding remote stream for pad %" GST_PTR_FORMAT " caps: %" GST_PTR_FORMAT, pad, caps.get());
    GRefPtr<GstElement> gstSink = gst_element_factory_make("proxysink", nullptr);
    GRefPtr<GstElement> gstSource = gst_element_factory_make("proxysrc", nullptr);
    g_object_set(gstSource.get(), "proxysink", gstSink.get(), nullptr);
    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), gstSink.get());
    gst_element_sync_state_with_parent(gstSink.get());

    if (!g_strcmp0(mediaType, "audio")) {
        m_audioSources.ensure(pad, [&gstSource] {
            return RealtimeIncomingAudioSourceGStreamer::create(gstSource.get());
        });
    } else if (!g_strcmp0(mediaType, "video")) {
        m_videoSources.ensure(pad, [&gstSource] {
            return RealtimeIncomingVideoSourceGStreamer::create(gstSource.get());
        });
    }

    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(gstSink.get(), "sink"));
    gst_pad_link(pad, sinkPad.get());

    if (!m_audioSources.isEmpty() && !m_videoSources.isEmpty())
        start();
}

void GStreamerMediaEndpoint::addRemoteStream(GstPad* pad)
{
    if (RuntimeEnabledFeatures::sharedFeatures().webRTCLegacyAPIEnabled())
        sourceFromGstPad(pad);

    // TODO(philn): what about the non-legacy API... Track-based.
    GRefPtr<GstCaps> caps = adoptGRef(gst_pad_query_caps(pad, nullptr));
    g_printerr("caps: %s\n", gst_caps_to_string(caps.get()));
    GstStructure* capsStructure = gst_caps_get_structure(caps.get(), 0);
    const gchar* mediaType = gst_structure_get_string(capsStructure, "media");
    if (!g_strcmp0(mediaType, "audio")) {
        auto audioReceiver = m_peerConnectionBackend.audioReceiver(trackId(*rtcTrack));
    } else if (!g_strcmp0(mediaType, "video")) {
        auto videoReceiver = m_peerConnectionBackend.videoReceiver(trackId(*rtcTrack));
    }

    m_peerConnectionBackend.connection().fireEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false, WTFMove(receiver), track, WTFMove(streams), nullptr));
}


void GStreamerMediaEndpoint::removeRemoteStream(GstPad*)
{
    notImplemented();
    // auto* mediaStream = m_streams.take(pad);
    // if (mediaStream)
    //     m_peerConnectionBackend.removeRemoteStream(mediaStream);
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

bool GStreamerMediaEndpoint::addIceCandidate(RTCIceCandidate& candidate)
{
    int sdpMLineIndex = candidate.sdpMLineIndex() ? candidate.sdpMLineIndex().value() : 0;
    g_signal_emit_by_name(m_webrtcBin.get(), "add-ice-candidate", sdpMLineIndex, candidate.candidate().utf8().data());
    return true;
}

void GStreamerMediaEndpoint::stop()
{
    stopLoggingStats();
}

void GStreamerMediaEndpoint::OnNegotiationNeeded()
{
    GST_DEBUG("Negotiation needed!");
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.markAsNeedingNegotiation();
    });
}

static inline RTCIceConnectionState toRTCIceConnectionState(GstWebRTCICEConnectionState state)
{
    switch (state) {
    case GST_WEBRTC_ICE_CONNECTION_STATE_NEW:
        return RTCIceConnectionState::New;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING:
        return RTCIceConnectionState::Checking;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED:
        return RTCIceConnectionState::Connected;
    case GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED:
        return RTCIceConnectionState::Completed;
    case GST_WEBRTC_ICE_CONNECTION_STATE_FAILED:
        return RTCIceConnectionState::Failed;
    case GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED:
        return RTCIceConnectionState::Disconnected;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED:
        return RTCIceConnectionState::Closed;
    }

    ASSERT_NOT_REACHED();
    return RTCIceConnectionState::New;
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
    GST_DEBUG("ICE candidate: %s", candidate);

    callOnMainThread([protectedThis = makeRef(*this), mid = WTFMove(candidateMid), sdp = WTFMove(candidateSDP), sdpMLineIndex] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.newICECandidate(String(sdp), String(mid), sdpMLineIndex);
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


RTCRtpParameters GStreamerMediaEndpoint::getRTCRtpSenderParameters(RTCRtpSender& sender)
{
    notImplemented();
    return { };
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
