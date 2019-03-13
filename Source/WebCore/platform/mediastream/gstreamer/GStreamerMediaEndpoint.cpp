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

#include "Document.h"
#include "GStreamerCommon.h"
#include "GStreamerDataChannelHandler.h"
#include "GStreamerRtpReceiverBackend.h"
#include "GStreamerRtpTransceiverBackend.h"
#include "GStreamerStatsCollector.h"
#include "GStreamerWebRTCUtils.h"
#include "JSDOMPromiseDeferred.h"
#include "JSRTCStatsReport.h"
#include "Logging.h"
#include "MediaEndpointConfiguration.h"
#include "NotImplemented.h"
#include "RTCDataChannel.h"
#include "RTCDataChannelEvent.h"
#include "RTCIceCandidate.h"
#include "RTCOfferOptions.h"
#include "RTCPeerConnection.h"
#include "RTCSessionDescription.h"
#include "RTCStatsReport.h"
#include "RTCTrackEvent.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include "RealtimeOutgoingVideoSourceGStreamer.h"
#include "RuntimeEnabledFeatures.h"

#include <gst/sdp/sdp.h>
#include <wtf/MainThread.h>
#include <wtf/glib/RunLoopSourcePriority.h>

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

GStreamerMediaEndpoint::GStreamerMediaEndpoint(GStreamerPeerConnectionBackend& peerConnection)
    : m_peerConnectionBackend(peerConnection)
    , m_statsCollector(GStreamerStatsCollector::create())
#if !RELEASE_LOG_DISABLED
    , m_statsLogTimer(*this, &GStreamerMediaEndpoint::gatherStatsForLogging)
    , m_logger(peerConnection.logger())
    , m_logIdentifier(peerConnection.logIdentifier())
#endif
{
    initializeGStreamer();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_endpoint_debug, "webkitwebrtcendpoint", 0, "WebKit WebRTC end-point");
    });
}

void GStreamerMediaEndpoint::initializePipeline()
{
    static int nPipeline = 0;
    GUniquePtr<char> pipeName(g_strdup_printf("MediaEndPoint%d", nPipeline++));
    m_pipeline = gst_pipeline_new(pipeName.get());

    GRefPtr<GstClock> clock = adoptGRef(gst_system_clock_obtain());
    gst_pipeline_use_clock(GST_PIPELINE(m_pipeline.get()), clock.get());
    gst_element_set_base_time(m_pipeline.get(), getSharedBaseTime());
    gst_element_set_start_time(m_pipeline.get(), GST_CLOCK_TIME_NONE);

    connectSimpleBusMessageCallback(m_pipeline.get());

    m_webrtcBin = gst_element_factory_make("webrtcbin", "webkit-webrtcbin");
    m_statsCollector->setElement(m_webrtcBin.get());
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::signaling-state", G_CALLBACK(+[](GStreamerMediaEndpoint* endPoint) {
        endPoint->onSignalingStateChange();
    }), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::ice-connection-state", G_CALLBACK(+[](GStreamerMediaEndpoint* endPoint) {
        endPoint->onIceConnectionChange();
    }), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "notify::ice-gathering-state", G_CALLBACK(+[](GStreamerMediaEndpoint* endPoint) {
        endPoint->onIceGatheringChange();
    }), this);
    g_signal_connect_swapped(m_webrtcBin.get(), "on-negotiation-needed", G_CALLBACK(+[](GStreamerMediaEndpoint* endPoint) {
        endPoint->onNegotiationNeeded();
    }), this);
    g_signal_connect(m_webrtcBin.get(), "on-ice-candidate", G_CALLBACK(+[](GstElement*, guint sdpMLineIndex, gchararray candidate, GStreamerMediaEndpoint* endPoint) {
        endPoint->onIceCandidate(sdpMLineIndex, candidate);
    }), this);
    g_signal_connect(m_webrtcBin.get(), "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, GStreamerMediaEndpoint* endPoint) {
        // Ignore outgoing pad notifications.
        if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
            return;

        callOnMainThreadAndWait([protectedThis = makeRef(*endPoint), pad] {
            if (protectedThis->isStopped())
                return;

            protectedThis->addRemoteStream(pad);
        });
    }), this);
    g_signal_connect(m_webrtcBin.get(), "pad-removed", G_CALLBACK(+[](GstElement*, GstPad* pad, GStreamerMediaEndpoint* endPoint) {
        // Ignore outgoing pad notifications.
        if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
            return;

        if (endPoint->isStopped())
            return;
        endPoint->removeRemoteStream(pad);
    }), this);

    g_signal_connect(m_webrtcBin.get(), "on-data-channel", G_CALLBACK(+[](GstElement*, GstWebRTCDataChannel* channel, GStreamerMediaEndpoint* endPoint) {
        endPoint->onDataChannel(channel);
    }), this);

    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), m_webrtcBin.get());
}

GStreamerMediaEndpoint::~GStreamerMediaEndpoint()
{
    if (m_pipeline)
        teardownPipeline();
}

void GStreamerMediaEndpoint::teardownPipeline()
{
#if !RELEASE_LOG_DISABLED
    stopLoggingStats();
#endif
    m_statsCollector->setElement(nullptr);
    disconnectSimpleBusMessageCallback(m_pipeline.get());
    gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    m_pendingSources.clear();
    m_sources.clear();
    m_remoteMLineInfos.clear();
    m_pendingRemoteMLineInfos.clear();
    m_remoteStreamsById.clear();
    m_webrtcBin = nullptr;
    m_pipeline = nullptr;
}

bool GStreamerMediaEndpoint::setConfiguration(MediaEndpointConfiguration& configuration)
{
    if (m_pipeline)
        teardownPipeline();

    initializePipeline();

    auto bundlePolicy = bundlePolicyFromConfiguration(configuration);
    auto iceTransportPolicy = iceTransportPolicyFromConfiguration(configuration);
    g_object_set(m_webrtcBin.get(), "bundle-policy", bundlePolicy, "ice-transport-policy", iceTransportPolicy, nullptr);

    for (auto& server : configuration.iceServers) {
        bool stunSet = false;
        for (auto& url : server.urls) {
            if (url.protocol().startsWith("turn")) {
                auto valid = url.string().isolatedCopy().replace("turn:", "turn://").replace("turns:", "turns://");
                URL validUrl(URL(), valid);
                // FIXME: libnice currently doesn't seem to handle IPv6 addresses very well.
                if (validUrl.host().startsWith('['))
                    continue;
                validUrl.setUser(server.username);
                validUrl.setPassword(server.credential);
                g_signal_emit_by_name(m_webrtcBin.get(), "add-turn-server", validUrl.string().utf8().data());
            }
            if (!stunSet && url.protocol().startsWith("stun")) {
                auto valid = url.string().isolatedCopy().replace("stun:", "stun://");
                URL validUrl(URL(), valid);
                // FIXME: libnice currently doesn't seem to handle IPv6 addresses very well.
                if (validUrl.host().startsWith('['))
                    continue;
                validUrl.setUser(server.username);
                validUrl.setPassword(server.credential);
                g_object_set(m_webrtcBin.get(), "stun-server", validUrl.string().utf8().data(), nullptr);
                stunSet = true;
            }
        }
    }

    // FIXME: webrtcbin doesn't support setting custom certificates yet.

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG_OBJECT(m_pipeline.get(), "End-point ready");
    return true;
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::fetchDescription(const char* name) const
{
    if (!m_webrtcBin)
        return nullptr;

    GstWebRTCSessionDescription* description = nullptr;
    g_object_get(m_webrtcBin.get(), makeString(name, "-description").utf8().data(), &description, nullptr);
    if (!description)
        return nullptr;

    String sdpString = gst_sdp_message_as_text(description->sdp);
    GST_TRACE_OBJECT(m_pipeline.get(), "%s-description SDP: %s", name, sdpString.utf8().data());
    return RTCSessionDescription::create(fromSessionDescriptionType(description), WTFMove(sdpString));
}

void GStreamerMediaEndpoint::restartIce()
{
    notImplemented();
}

// FIXME: We might want to create a new object only if the session actually changed for all description getters.
RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::currentLocalDescription() const
{
    return fetchDescription("current-local");
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::currentRemoteDescription() const
{
    return fetchDescription("current-remote");
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::pendingLocalDescription() const
{
    return fetchDescription("pending-local");
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::pendingRemoteDescription() const
{
    return fetchDescription("pending-remote");
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::localDescription() const
{
    return fetchDescription("local");
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::remoteDescription() const
{
    return fetchDescription("remote");
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
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setLocalDescriptionSucceeded();
    });
}

void GStreamerMediaEndpoint::emitSetLocalDescriptionFailed()
{
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setLocalDescriptionFailed(Exception { OperationError, "Unable to apply session local description" });
    });
}

void GStreamerMediaEndpoint::doSetLocalDescription(const RTCSessionDescription* description)
{
    GstSDPMessage* message;
    gst_sdp_message_new(&message);
    gst_sdp_message_parse_buffer((guint8*)description->sdp().utf8().data(), description->sdp().length(), message);
    GstWebRTCSDPType type = toSessionDescriptionType(description->type());
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
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->connectOutgoingSources();
        protectedThis->m_peerConnectionBackend.setRemoteDescriptionSucceeded();
    });
}

void GStreamerMediaEndpoint::emitSetRemoteDescriptionFailed()
{
    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.setRemoteDescriptionFailed(Exception { OperationError, "Unable to apply session remote description" });
    });
}

void GStreamerMediaEndpoint::doSetRemoteDescription(const RTCSessionDescription& description)
{
    GstSDPMessage* message;
    gst_sdp_message_new(&message);
    gst_sdp_message_parse_buffer((guint8*)description.sdp().utf8().data(), description.sdp().length(), message);
    storeRemoteMLineInfo(message);

    GstWebRTCSDPType type = toSessionDescriptionType(description.type());
    GUniquePtr<gchar> sdp(gst_sdp_message_as_text(message));
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating session for SDP %s: %s", gst_webrtc_sdp_type_to_string(type), sdp.get());
    GstWebRTCSessionDescription* sessionDescription = gst_webrtc_session_description_new(type, message);
    GstPromise* promise = gst_promise_new_with_change_func(setRemoteDescriptionPromiseChanged, this, nullptr);
    g_signal_emit_by_name(m_webrtcBin.get(), "set-remote-description", sessionDescription, promise);
    gst_webrtc_session_description_free(sessionDescription);

#if !RELEASE_LOG_DISABLED
    startLoggingStats();
#endif
}

void GStreamerMediaEndpoint::storeRemoteMLineInfo(GstSDPMessage* message)
{
    m_pendingRemoteMLineInfos.clear();
    unsigned totalMedias = gst_sdp_message_medias_len(message);
    m_pendingRemoteMLineInfos.reserveCapacity(totalMedias);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Storing %u remote pending mlines", totalMedias);
    for (unsigned i = 0; i < totalMedias; i++) {
        const GstSDPMedia* media = gst_sdp_message_get_media(message, i);
        const char* typ = gst_sdp_media_get_media(media);
        if (!g_str_equal(typ, "audio") && !g_str_equal(typ, "video"))
            continue;

        const char* mid = gst_sdp_media_get_attribute_val(media, "mid");
        m_mediaForMid.set(String(mid), g_str_equal(typ, "audio") ? RealtimeMediaSource::Type::Audio : RealtimeMediaSource::Type::Video);

        auto caps = adoptGRef(gst_caps_new_empty());
        Vector<int> payloadTypes;
        unsigned totalFormats = gst_sdp_media_formats_len(media);
        GST_DEBUG_OBJECT(m_pipeline.get(), "Media %s has %u formats", typ, totalFormats);
        for (unsigned j = 0; j < totalFormats; j++) {
            auto format = String(gst_sdp_media_get_format(media, j));
            bool ok = false;
            int payloadType = format.toInt(&ok);
            if (!ok) {
                GST_WARNING_OBJECT(m_pipeline.get(), "Invalid payload type: %s", format.utf8().data());
                continue;
            }
            auto formatCaps = adoptGRef(gst_sdp_media_get_caps_from_media(media, payloadType));
            if (!formatCaps) {
                GST_WARNING_OBJECT(m_pipeline.get(), "No caps found for payload type %d", payloadType);
                continue;
            }
            gst_caps_append(caps.get(), formatCaps.leakRef());
            m_ptCounter = MAX(m_ptCounter, payloadType + 1);
            payloadTypes.append(payloadType);
        }

        unsigned totalCaps = gst_caps_get_size(caps.get());
        if (totalCaps) {
            for (unsigned j = 0; j < totalCaps; j++) {
                GstStructure* structure = gst_caps_get_structure(caps.get(), j);
                gst_structure_set_name(structure, "application/x-rtp");
            }
            GST_DEBUG_OBJECT(m_pipeline.get(), "Caching payload caps: %" GST_PTR_FORMAT, caps.get());
            m_pendingRemoteMLineInfos.append({ WTFMove(caps), false, WTFMove(payloadTypes) });
        }
    }
}

void GStreamerMediaEndpoint::connectOutgoingSources()
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Connecting outgoing media sources");
    m_pendingRemoteMLineInfos.swap(m_remoteMLineInfos);
    flushPendingSources();
}

void GStreamerMediaEndpoint::flushPendingSources()
{
    auto sources = WTFMove(m_pendingSources);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Activating %zu pending outgoing sources", sources.size());
    for (auto& source : sources) {
        configureAndLinkSource(WTFMove(source));
    }
}

void GStreamerMediaEndpoint::configureAndLinkSource(RealtimeOutgoingMediaSourceGStreamer& source)
{
    auto caps = source.allowedCaps();
    WTF::Optional<std::pair<PendingMLineInfo, GRefPtr<GstCaps>>> found;
    for (auto& mLineInfo : m_remoteMLineInfos) {
        if (mLineInfo.isUsed)
            continue;

        for (unsigned i = 0; i < gst_caps_get_size(mLineInfo.caps.get()); i++) {
            auto* structure = gst_caps_get_structure(mLineInfo.caps.get(), i);
            auto caps2 = adoptGRef(gst_caps_new_full(gst_structure_copy(structure), nullptr));
            auto intersected = adoptGRef(gst_caps_intersect(caps.get(), caps2.get()));
            if (!gst_caps_is_empty(intersected.get())) {
                found = std::make_pair(mLineInfo, WTFMove(intersected));
                break;
            }
        }
        if (found.hasValue())
            break;
    }
    RELEASE_ASSERT(found.hasValue());

    GST_DEBUG_OBJECT(m_pipeline.get(), "Unused and compatible caps found for %" GST_PTR_FORMAT "... %" GST_PTR_FORMAT, caps.get(), found->second.get());

    source.setPayloadType(WTFMove(found->second));
    found->first.isUsed = true;

    auto padId = makeString("sink_", m_requestPadCounter).utf8().data();
    GST_DEBUG_OBJECT(m_pipeline.get(), "Linking outgoing source to new request-pad %s", padId);
    GstPadTemplate* padTemplate = gst_element_get_pad_template(m_webrtcBin.get(), "sink_%u");
    auto sinkPad = adoptGRef(gst_element_request_pad(m_webrtcBin.get(), padTemplate, padId, nullptr));
    m_requestPadCounter++;
    RELEASE_ASSERT(!gst_pad_is_linked(sinkPad.get()));
    source.linkToWebRTCBinPad(sinkPad.get());
}

bool GStreamerMediaEndpoint::addTrack(GStreamerRtpSenderBackend& sender, MediaStreamTrack& track, const Vector<String>& mediaStreamIds)
{
    GStreamerRtpSenderBackend::Source source;
    GRefPtr<GstWebRTCRTPSender> rtcSender;

    switch (track.privateTrack().type()) {
    case RealtimeMediaSource::Type::Audio: {
        GST_DEBUG_OBJECT(m_pipeline.get(), "Adding outgoing audio source");
        auto audioSource = RealtimeOutgoingAudioSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
        if (m_delayedNegotiation) {
            // TODO(phil): Move this to RealtimeOutgoingMediaSourceGStreamer ?
            GstWebRTCRTPTransceiver* transceiver;
            g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV, audioSource->allowedCaps().get(), &transceiver);
            g_object_get(transceiver, "sender", &rtcSender.outPtr(), nullptr);
            transceiver->mline = m_mlineIndex;
            m_mlineIndex++;
            // g_object_set(transceiver, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "fec-percentage", 100, nullptr);
            // g_object_set(transceiver, "do-nack", true, nullptr);

            m_pendingSources.append(audioSource.copyRef());
        } else {
            configureAndLinkSource(audioSource);
            rtcSender = audioSource->sender();
        }
        source = WTFMove(audioSource);
        break;
    }
    case RealtimeMediaSource::Type::Video: {
        GST_DEBUG_OBJECT(m_pipeline.get(), "Adding outgoing video source");
        auto videoSource = RealtimeOutgoingVideoSourceGStreamer::create(track.privateTrack(), m_pipeline.get());
        if (m_delayedNegotiation) {
            // TODO(phil): Move this to RealtimeOutgoingMediaSourceGStreamer ?
            GstWebRTCRTPTransceiver* transceiver;
            g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV, videoSource->allowedCaps().get(), &transceiver);
            g_object_get(transceiver, "sender", &rtcSender.outPtr(), nullptr);
            transceiver->mline = m_mlineIndex;
            m_mlineIndex++;

            // g_object_set(transceiver, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "fec-percentage", 100, nullptr);
            // g_object_set(transceiver, "do-nack", true, nullptr);

            m_pendingSources.append(videoSource.copyRef());
        } else {
            configureAndLinkSource(videoSource);
            rtcSender = videoSource->sender();
        }
        source = WTFMove(videoSource);
        break;
    }
    case RealtimeMediaSource::Type::None:
        ASSERT_NOT_REACHED();
        return false;
    }

    sender.setSource(WTFMove(source));

    if (auto rtpSender = sender.rtcSender()) {
        // FIXME: gst_webrtc_rtp_sender_set_transport or gst_webrtc_rtp_sender_set_rtcp_transport?
        // rtpSender->SetTrack(rtcTrack.get());
        return true;
    }

    // std::vector<std::string> ids;
    // for (auto& id : mediaStreamIds)
    //     g_printerr(">>> mediaStreamId %s\n", id.utf8().data());
    //     ids.push_back();

    // auto newRTPSender = m_backend->AddTrack(rtcTrack.get(), WTFMove(ids));
    // if (!newRTPSender.ok())
    //     return false;
    // sender.setRTCSender(newRTPSender.MoveValue());
    sender.setRTCSender(WTFMove(rtcSender));

    if (m_delayedNegotiation && (m_sources.size() > 1 || m_pendingSources.size() > 1)) {
        m_delayedNegotiation = false;
        m_mlineIndex = 0;
        onNegotiationNeeded();
    }

    return true;
}

void GStreamerMediaEndpoint::removeTrack(GStreamerRtpSenderBackend& sender)
{
    notImplemented();
    sender.clearSource();
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
    if (result != GST_PROMISE_RESULT_REPLIED) {
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
    //flushPendingSources(true);

    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating offer");
    // TODO(philn): fill options (currently unused in webrtcbin anyway).
    GstPromise* promise = gst_promise_new_with_change_func(offerPromiseChanged, this, nullptr);
    GstStructure* offerOptions = nullptr;
    g_signal_emit_by_name(m_webrtcBin.get(), "create-offer", offerOptions, promise);
}

void GStreamerMediaEndpoint::doCreateAnswer()
{
    m_isInitiator = false;

    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating answer");
    // TODO(philn): fill options (currently unused in webrtcbin anyway).
    GstPromise* promise = gst_promise_new_with_change_func(answerPromiseChanged, this, nullptr);
    GstStructure* options = nullptr;
    g_signal_emit_by_name(m_webrtcBin.get(), "create-answer", options, promise);
}

void GStreamerMediaEndpoint::getStats(GstPad* pad, Ref<DeferredPromise>&& promise)
{
    m_statsCollector->getStats([promise = WTFMove(promise), protectedThis = makeRef(*this)](auto&& report) mutable {
        ASSERT(isMainThread());
        if (protectedThis->isStopped() || !report)
            return;

        promise->resolve<IDLInterface<RTCStatsReport>>(report.releaseNonNull());
    }, pad);
}

void GStreamerMediaEndpoint::onSignalingStateChange()
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

MediaStream& GStreamerMediaEndpoint::mediaStreamFromRTCStream(GstPad* pad)
{
    GUniquePtr<gchar> name(gst_pad_get_name(pad));
    String label(name.get());
    auto mediaStream = m_remoteStreamsById.ensure(label, [label, this]() mutable {
        auto& document = downcast<Document>(*m_peerConnectionBackend.connection().scriptExecutionContext());
        return MediaStream::create(document, MediaStreamPrivate::create(document.logger(), { }, WTFMove(label)));
    });
    return *mediaStream.iterator->value;
}

void GStreamerMediaEndpoint::addRemoteStream(GstPad* pad)
{
    m_pendingIncomingStreams++;
    GRefPtr<GstCaps> caps = adoptGRef(gst_pad_query_caps(pad, nullptr));
    GstStructure* capsStructure = gst_caps_get_structure(caps.get(), 0);
    const gchar* mediaType = gst_structure_get_string(capsStructure, "media");
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding remote %s stream to %" GST_PTR_FORMAT, mediaType, pad);

    Vector<RefPtr<MediaStream>> streams;
    auto& mediaStream = mediaStreamFromRTCStream(pad);
    streams.append(&mediaStream);

    if (!g_strcmp0(mediaType, "audio")) {
        auto audioReceiver = m_peerConnectionBackend.audioReceiver(m_pipeline.get(), pad);
        auto& track = audioReceiver.receiver->track();
        mediaStream.addTrackFromPlatform(track);
        m_peerConnectionBackend.addPendingTrackEvent({WTFMove(audioReceiver.receiver), makeRef(track), WTFMove(streams), WTFMove(audioReceiver.transceiver)});
    } else if (!g_strcmp0(mediaType, "video")) {
        auto videoReceiver = m_peerConnectionBackend.videoReceiver(m_pipeline.get(), pad);
        auto& track = videoReceiver.receiver->track();
        mediaStream.addTrackFromPlatform(track);
        m_peerConnectionBackend.addPendingTrackEvent({WTFMove(videoReceiver.receiver), makeRef(track), WTFMove(streams), WTFMove(videoReceiver.transceiver)});
    }

    if (m_pendingIncomingStreams > 1) {
        m_pendingIncomingStreams = 0;
        callOnMainThread([protectedThis = makeRef(*this)] {
            protectedThis->m_peerConnectionBackend.firePendingTrackEvents();
        });
    }
    WTF::String dotFileName = makeString(GST_OBJECT_NAME(m_pipeline.get()), ".incoming-", mediaType, '-', GST_OBJECT_NAME(pad));
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.utf8().data());
}

void GStreamerMediaEndpoint::removeRemoteStream(GstPad*)
{
    GST_FIXME_OBJECT(m_pipeline.get(), "removeRemoteStream");
    notImplemented();
}

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::addTransceiver(const String& trackKind, const RTCRtpTransceiverInit& init)
{
    if (!m_webrtcBin)
        return WTF::nullopt;

    GST_DEBUG_OBJECT(m_pipeline.get(), "%zu streams in init data", init.streams.size());
    // FIXME: This needs to be revised.
    const char* encodingName;
    int clockRate;
    if (trackKind == "video") {
        encodingName = "VP9";
        clockRate = 90000;
    } else {
        encodingName = "OPUS";
        clockRate = 48000;
    }
    auto caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, trackKind.utf8().data(), "encoding-name", G_TYPE_STRING, encodingName,
        "payload", G_TYPE_INT, m_ptCounter++, "clock-rate", G_TYPE_INT, clockRate, nullptr));
    auto direction = fromRTCRtpTransceiverDirection(init.direction);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding %s transceiver for payload %" GST_PTR_FORMAT, enumValue(GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION, direction).get(), caps.get());
    GRefPtr<GstWebRTCRTPTransceiver> rtcTransceiver;
    g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", direction, caps.get(), &rtcTransceiver.outPtr());
    if (!rtcTransceiver)
        return WTF::nullopt;

    // g_object_set(rtcTransceiver.get(), "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "fec-percentage", 100, nullptr);
    // g_object_set(rtcTransceiver.get(), "do-nack", true, nullptr);

    auto transceiver = std::make_unique<GStreamerRtpTransceiverBackend>(WTFMove(rtcTransceiver));
    return GStreamerMediaEndpoint::Backends { transceiver->createSenderBackend(m_peerConnectionBackend, nullptr), transceiver->createReceiverBackend(), WTFMove(transceiver) };
}

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::addTransceiver(MediaStreamTrack& track, const RTCRtpTransceiverInit& init)
{
    GRefPtr<GstWebRTCRTPTransceiver> rtcTransceiver;
    auto caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, track.kind().string().utf8().data(), nullptr));
    g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", fromRTCRtpTransceiverDirection(init.direction), caps.get(), &rtcTransceiver.outPtr());
    if (!rtcTransceiver)
        return WTF::nullopt;

    auto transceiver = std::make_unique<GStreamerRtpTransceiverBackend>(WTFMove(rtcTransceiver));
    GStreamerMediaEndpoint::Backends backend { nullptr, nullptr, WTFMove(transceiver) };
    return backend;
}

void GStreamerMediaEndpoint::setSenderSourceFromTrack(GStreamerRtpSenderBackend& sender, MediaStreamTrack& track)
{
    // auto sourceAndTrack = createSourceAndRTCTrack(track);
    // sender.setSource(WTFMove(sourceAndTrack.first));
    // sender.rtcSender()->SetTrack(WTFMove(sourceAndTrack.second));
    GST_FIXME_OBJECT(m_pipeline.get(), "setSenderSourceFromTrack");
    notImplemented();
}

std::unique_ptr<GStreamerRtpTransceiverBackend> GStreamerMediaEndpoint::transceiverBackendFromSender(GStreamerRtpSenderBackend& backend)
{
    // TODO: GArray smart pointer?
    GArray* transceivers;
    g_signal_emit_by_name(m_webrtcBin.get(), "get-transceivers", &transceivers);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Looking for sender %p in %u existing transceivers", backend.rtcSender(), transceivers->len);
    for (unsigned i = 0; i < transceivers->len; i++) {
        GstWebRTCRTPTransceiver* current = g_array_index(transceivers, GstWebRTCRTPTransceiver*, i);
        GstWebRTCRTPSender* sender = nullptr;
        g_object_get(current, "sender", &sender, nullptr);

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

bool GStreamerMediaEndpoint::addIceCandidate(GStreamerIceCandidate& candidate)
{
    if (candidate.candidate.isEmpty()) {
        GST_WARNING_OBJECT(m_pipeline.get(), "Empty ICE candidate...");
        return true;
    }
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding ICE candidate %s", candidate.candidate.utf8().data());
    g_signal_emit_by_name(m_webrtcBin.get(), "add-ice-candidate", candidate.sdpMLineIndex, candidate.candidate.utf8().data());
    return true;
}

std::unique_ptr<RTCDataChannelHandler> GStreamerMediaEndpoint::createDataChannel(const String& label, const RTCDataChannelInit& options)
{
    if (!m_webrtcBin)
        return nullptr;

    GRefPtr<GstWebRTCDataChannel> channel;
    auto init = GStreamerDataChannelHandler::fromRTCDataChannelInit(options);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating data channel");
    g_signal_emit_by_name(m_webrtcBin.get(), "create-data-channel", label.utf8().data(), init.get(), &channel.outPtr());
    return channel ? std::make_unique<GStreamerDataChannelHandler>(WTFMove(channel)) : nullptr;
}

void GStreamerMediaEndpoint::onDataChannel(GstWebRTCDataChannel* dataChannel)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Incoming data channel");
    GRefPtr<GstWebRTCDataChannel> channel = dataChannel;
    callOnMainThread([protectedThis = makeRef(*this), dataChannel = WTFMove(channel)]() mutable {
        if (protectedThis->isStopped())
            return;
        auto& connection = protectedThis->m_peerConnectionBackend.connection();
        connection.dispatchEventWhenFeasible(GStreamerDataChannelHandler::channelEvent(*connection.document(), WTFMove(dataChannel)));
    });
}

void GStreamerMediaEndpoint::stop()
{
#if !RELEASE_LOG_DISABLED
    stopLoggingStats();
#endif

    if (m_pipeline)
        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
}

void GStreamerMediaEndpoint::onNegotiationNeeded()
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Checking negotiation status");
    if (m_pendingSources.isEmpty() && m_sources.isEmpty()) {
        GST_DEBUG_OBJECT(m_pipeline.get(), "Delaying negotiation");
        m_delayedNegotiation = true;
        return;
    }

    callOnMainThread([protectedThis = makeRef(*this)] {
        if (protectedThis->isStopped())
            return;
        GST_DEBUG_OBJECT(protectedThis->m_pipeline.get(), "Negotiation needed!");
        protectedThis->m_peerConnectionBackend.markAsNeedingNegotiation();
    });
}

void GStreamerMediaEndpoint::onIceConnectionChange()
{
    GstWebRTCICEConnectionState state;
    g_object_get(m_webrtcBin.get(), "ice-connection-state", &state, nullptr);
    auto connectionState = toRTCIceConnectionState(state);
    callOnMainThread([protectedThis = makeRef(*this), connectionState] {
        if (protectedThis->isStopped())
            return;
        auto& connection = protectedThis->m_peerConnectionBackend.connection();
        if (connection.iceConnectionState() != connectionState)
            connection.updateIceConnectionState(connectionState);
    });
}

void GStreamerMediaEndpoint::onIceGatheringChange()
{
    GstWebRTCICEGatheringState state;
    g_object_get(m_webrtcBin.get(), "ice-gathering-state", &state, nullptr);
    callOnMainThread([protectedThis = makeRef(*this), state] {
        if (protectedThis->isStopped())
            return;
        auto& connection = protectedThis->m_peerConnectionBackend.connection();
        if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE)
            protectedThis->m_peerConnectionBackend.doneGatheringCandidates();
        else if (state == GST_WEBRTC_ICE_GATHERING_STATE_GATHERING)
            connection.updateIceGatheringState(RTCIceGatheringState::Gathering);
        else if (state == GST_WEBRTC_ICE_GATHERING_STATE_NEW)
            connection.updateIceGatheringState(RTCIceGatheringState::New);
    });
}

void GStreamerMediaEndpoint::onIceCandidate(guint sdpMLineIndex, gchararray candidate)
{
    String candidateSDP(candidate);

    GstWebRTCSessionDescription* description = nullptr;
    g_object_get(m_webrtcBin.get(), "current-local-description", &description, nullptr);
    if (!description)
        return;

    const GstSDPMedia* media = gst_sdp_message_get_media(description->sdp, sdpMLineIndex);
    String candidateMid(gst_sdp_media_get_attribute_val(media, "mid"));

    GST_DEBUG_OBJECT(m_pipeline.get(), "mlineIndex: %u, ICE candidate: %s, mid: %s", sdpMLineIndex, candidate, candidateMid.utf8().data());

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

void GStreamerMediaEndpoint::collectTransceivers()
{
    // TODO: GArray smart pointer?
    GArray* transceivers;
    g_signal_emit_by_name(m_webrtcBin.get(), "get-transceivers", &transceivers);
    for (unsigned i = 0; i < transceivers->len; i++) {
        GstWebRTCRTPTransceiver* current = g_array_index(transceivers, GstWebRTCRTPTransceiver*, i);

        auto* existingTransceiver = m_peerConnectionBackend.existingTransceiver([&](auto& transceiverBackend) {
            return current == transceiverBackend.rtcTransceiver();
        });
        if (existingTransceiver)
            continue;

        if (!current->receiver)
            continue;

        if (!current->mid)
            continue;

        m_peerConnectionBackend.newRemoteTransceiver(std::make_unique<GStreamerRtpTransceiverBackend>(WTFMove(current)), m_mediaForMid.get(current->mid));
    }
    g_array_unref(transceivers);
}

#if !RELEASE_LOG_DISABLED
void GStreamerMediaEndpoint::gatherStatsForLogging()
{
    GstPromise* gstPromise = gst_promise_new_with_change_func([](GstPromise* promise, gpointer userData) {
        GstPromiseResult result = gst_promise_wait(promise);
        if (result != GST_PROMISE_RESULT_REPLIED) {
            gst_promise_unref(promise);
            return;
        }

        auto* endPoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
        endPoint->onStatsDelivered(gst_promise_get_reply(promise));
        gst_promise_unref(promise);
    }, this, nullptr);
    g_signal_emit_by_name(m_webrtcBin.get(), "get-stats", nullptr, gstPromise);
}

class RTCStatsLogger {
public:
    explicit RTCStatsLogger(const GstStructure* stats)
        : m_stats(stats)
    {
    }

    String toJSONString() const {
        return structureToJSONString(m_stats);
    }

private:
    const GstStructure* m_stats;
};

static gboolean processStatsCallback(GQuark, const GValue* value, gpointer userData)
{
    auto* endPoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
    endPoint->processStats(value);
    return TRUE;
}

void GStreamerMediaEndpoint::processStats(const GValue* value)
{
    if (!GST_VALUE_HOLDS_STRUCTURE(value))
        return;

    const GstStructure* structure = gst_value_get_structure(value);
    GstWebRTCStatsType statsType;
    if (!gst_structure_get(structure, "type", GST_TYPE_WEBRTC_STATS_TYPE, &statsType, nullptr))
        return;

    // Just check a single timestamp, inbound RTP for instance.
    if (!m_statsFirstDeliveredTimestamp && statsType == GST_WEBRTC_STATS_INBOUND_RTP) {
        double timestamp;
        if (gst_structure_get_double(structure, "timestamp", &timestamp)) {
            auto ts = Seconds::fromMilliseconds(timestamp);
            m_statsFirstDeliveredTimestamp = ts;

            if (!isStopped() && m_statsLogTimer.repeatInterval() != statsLogInterval(ts)) {
                m_statsLogTimer.stop();
                m_statsLogTimer.startRepeating(statsLogInterval(ts));
            }
        }
    }

    if (logger().willLog(logChannel(), WTFLogLevel::Debug)) {
        // Stats are very verbose, let's only display them in inspector console in verbose mode.
        logger().debug(LogWebRTC,
            Logger::LogSiteIdentifier("GStreamerMediaEndpoint", "onStatsDelivered", logIdentifier()),
            RTCStatsLogger { structure });
    } else {
        logger().logAlways(LogWebRTCStats,
            Logger::LogSiteIdentifier("GStreamerMediaEndpoint", "onStatsDelivered", logIdentifier()),
            RTCStatsLogger { structure });
    }
}


void GStreamerMediaEndpoint::onStatsDelivered(const GstStructure* stats)
{
    GUniquePtr<GstStructure> statsCopy(gst_structure_copy(stats));
    callOnMainThread([protectedThis = makeRef(*this), this, stats = statsCopy.release()] {
        gst_structure_foreach(stats, processStatsCallback, this);
    });
}
#endif

#if !RELEASE_LOG_DISABLED
void GStreamerMediaEndpoint::startLoggingStats()
{
    if (m_statsLogTimer.isActive())
        m_statsLogTimer.stop();
    m_statsLogTimer.startRepeating(statsLogInterval(Seconds::nan()));
}

void GStreamerMediaEndpoint::stopLoggingStats()
{
    m_statsLogTimer.stop();
}

WTFLogChannel& GStreamerMediaEndpoint::logChannel() const
{
    return LogWebRTC;
}

Seconds GStreamerMediaEndpoint::statsLogInterval(Seconds reportTimestamp) const
{
    if (logger().willLog(logChannel(), WTFLogLevel::Info))
        return 2_s;

    if (reportTimestamp - m_statsFirstDeliveredTimestamp > 15_s)
        return 10_s;

    return 4_s;
}
#endif

} // namespace WebCore

#if !RELEASE_LOG_DISABLED
namespace WTF {

template<typename Type>
struct LogArgument;

template <>
struct LogArgument<WebCore::RTCStatsLogger> {
    static String toString(const WebCore::RTCStatsLogger& logger)
    {
        return String(logger.toJSONString());
    }
};

}; // namespace WTF
#endif // !RELEASE_LOG_DISABLED

#endif // USE(GSTREAMER_WEBRTC)
