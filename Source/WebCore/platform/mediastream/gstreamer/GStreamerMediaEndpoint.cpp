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

#include <gst/sdp/sdp.h>
#include <wtf/MainThread.h>
#include <wtf/glib/RunLoopSourcePriority.h>
#include <wtf/glib/GLibUtilities.h>
#include <wtf/Scope.h>

GST_DEBUG_CATEGORY(webkit_webrtc_endpoint_debug);
#define GST_CAT_DEFAULT webkit_webrtc_endpoint_debug

namespace WebCore {

GStreamerMediaEndpoint::GStreamerMediaEndpoint(GStreamerPeerConnectionBackend& peerConnection)
    : m_peerConnectionBackend(peerConnection)
    , m_statsCollector(GStreamerStatsCollector::create())
#if !RELEASE_LOG_DISABLED
    , m_statsLogTimer(*this, &GStreamerMediaEndpoint::gatherStatsForLogging)
    , m_logger(peerConnection.logger())
    , m_logIdentifier(peerConnection.logIdentifier())
#endif
{
    ensureGStreamerInitialized();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_endpoint_debug, "webkitwebrtcendpoint", 0, "WebKit WebRTC end-point");
    });
}

gboolean messageCallback(GstBus*, GstMessage* message, GStreamerMediaEndpoint* endPoint)
{
    return endPoint->handleMessage(message);
}

void GStreamerMediaEndpoint::initializePipeline()
{
    static int nPipeline = 0;
    auto pipelineName = makeString("MediaEndPoint-", nPipeline);
    m_pipeline = gst_pipeline_new(pipelineName.ascii().data());

    auto bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    ASSERT(bus);
    gst_bus_add_signal_watch_full(bus.get(), RunLoopSourcePriority::RunLoopDispatcher);
    g_signal_connect(bus.get(), "message", G_CALLBACK(messageCallback), this);

    auto binName = makeString("webkit-webrtcbin-", nPipeline++);
    m_webrtcBin = gst_element_factory_make("webrtcbin", binName.ascii().data());
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

    auto bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    ASSERT(bus);
    g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(messageCallback), this);
    gst_bus_remove_signal_watch(bus.get());

    gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    m_pendingSources.clear();
    m_sources.clear();
    m_remoteMLineInfos.clear();
    m_pendingRemoteMLineInfos.clear();
    m_remoteStreamsById.clear();
    m_webrtcBin = nullptr;
    m_pipeline = nullptr;
}

bool GStreamerMediaEndpoint::handleMessage(GstMessage* message)
{
    GUniqueOutPtr<GError> error;
    GUniqueOutPtr<gchar> debug;
    bool handled = false;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_WARNING:
        gst_message_parse_warning(message, &error.outPtr(), &debug.outPtr());
        g_warning("Warning: %d, %s. Debug output: %s", error->code,  error->message, debug.get());
        handled = true;
        break;
    case GST_MESSAGE_ERROR: {
        WTF::String dotFileName = makeString(GST_OBJECT_NAME(m_pipeline.get()), "_error");
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.utf8().data());
        gst_message_parse_error(message, &error.outPtr(), &debug.outPtr());
        g_warning("Error: %d, %s. Debug output: %s", error->code, error->message, debug.get());
        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
        handled = true;
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline.get())) {
            GstState oldState, newState, pending;
            gst_message_parse_state_changed(message, &oldState, &newState, &pending);

            GST_INFO_OBJECT(m_pipeline.get(), "State changed (old: %s, new: %s, pending: %s)",
                gst_element_state_get_name(oldState), gst_element_state_get_name(newState), gst_element_state_get_name(pending));

            WTF::String dotFileName = makeString(GST_OBJECT_NAME(m_pipeline.get()), '_',
                gst_element_state_get_name(oldState), '_', gst_element_state_get_name(newState));

            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.utf8().data());
        }
        handled = true;
        break;
    case GST_MESSAGE_EOS:
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "eos");
        break;
    case GST_MESSAGE_ELEMENT: {
        const GstStructure* data = gst_message_get_structure(message);
        if (!g_strcmp0(gst_structure_get_name(data), "GstBinForwarded")) {
            GRefPtr<GstMessage> subMessage;
            gst_structure_get(data, "message", GST_TYPE_MESSAGE, &subMessage.outPtr(), nullptr);
            if (GST_MESSAGE_TYPE(subMessage.get()) == GST_MESSAGE_EOS) {
                disposeElementChain(GST_ELEMENT_CAST(GST_MESSAGE_SRC(subMessage.get())));
                handled = true;
                break;
            } else
                FALLTHROUGH;
        }
    }

    default:
        GST_DEBUG_OBJECT(m_pipeline.get(), "Unhandled %" GST_PTR_FORMAT, message);
        break;
    }
    return handled;
}

void GStreamerMediaEndpoint::disposeElementChain(GstElement* element)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Got element EOS message from %" GST_PTR_FORMAT, element);

    auto pad = adoptGRef(gst_element_get_static_pad(element, "sink"));
    auto peer = adoptGRef(gst_pad_get_peer(pad.get()));
    auto* webrtc = GST_ELEMENT_CAST(gst_pad_get_parent(peer.get()));

    gst_bin_remove(GST_BIN_CAST(m_pipeline.get()), element);
    gst_pad_unlink(peer.get(), pad.get());
    gst_element_release_request_pad(webrtc, peer.get());
    gst_element_set_state(element, GST_STATE_NULL);
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
                bool result = false;
                g_signal_emit_by_name(m_webrtcBin.get(), "add-turn-server", validUrl.string().utf8().data(), &result);
                if (!result)
                    GST_WARNING("Unable to use TURN server: %s", validUrl.string().utf8().data());
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
    // WIP: https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/merge_requests/302
    gst_printerrln(">>>>>>>>>>>>>>>>>> %zu certs", configuration.certificates.size());

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG_OBJECT(m_pipeline.get(), "End-point ready");
    return true;
}

RefPtr<RTCSessionDescription> GStreamerMediaEndpoint::fetchDescription(const char* name) const
{
    if (!m_webrtcBin)
        return nullptr;

    GUniqueOutPtr<GstWebRTCSessionDescription> description;
    g_object_get(m_webrtcBin.get(), makeString(name, "-description").utf8().data(), &description.outPtr(), nullptr);
    if (!description)
        return nullptr;

    String sdpString = gst_sdp_message_as_text(description->sdp);
    GST_TRACE_OBJECT(m_pipeline.get(), "%s-description SDP: %s", name, sdpString.utf8().data());
    return RTCSessionDescription::create(fromSessionDescriptionType(description.get()), WTFMove(sdpString));
}

void GStreamerMediaEndpoint::restartIce()
{
    // WIP in https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/merge_requests/1877
    notImplemented();
}

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

void GStreamerMediaEndpoint::doSetLocalDescription(const RTCSessionDescription* description)
{
    setDescription(*description, true, [protectedThis = makeRef(*this)] {
        protectedThis->m_peerConnectionBackend.setLocalDescriptionSucceeded();
    }, [protectedThis = makeRef(*this)] {
        protectedThis->m_peerConnectionBackend.setLocalDescriptionFailed(Exception { OperationError, "Unable to apply session local description" });
    });
}

void GStreamerMediaEndpoint::doSetRemoteDescription(const RTCSessionDescription& description)
{
    setDescription(description, false, [protectedThis = makeRef(*this)] {
        protectedThis->connectOutgoingSources();
        protectedThis->m_peerConnectionBackend.setRemoteDescriptionSucceeded();
    }, [protectedThis = makeRef(*this)] {
        protectedThis->m_peerConnectionBackend.setRemoteDescriptionFailed(Exception { OperationError, "Unable to apply session remote description" });
    });
#if !RELEASE_LOG_DISABLED
    startLoggingStats();
#endif
}

struct SetDescriptionCallData {
    WTF_MAKE_STRUCT_FAST_ALLOCATED;
    SetDescriptionCallData(GStreamerMediaEndpoint& endPoint, Function<void()>&& successCallback, Function<void()>&& failureCallback)
        : successCallback(WTFMove(successCallback))
        , failureCallback(WTFMove(failureCallback))
        , endPoint(endPoint) { }

    Function<void()> successCallback;
    Function<void()> failureCallback;
    GStreamerMediaEndpoint& endPoint;
};

void GStreamerMediaEndpoint::setDescription(const RTCSessionDescription& description, bool isLocal, Function<void()>&& successCallback, Function<void()>&& failureCallback)
{
    GstSDPMessage* message;
    gst_sdp_message_new(&message);
    gst_sdp_message_parse_buffer(description.sdp().characters8(), description.sdp().length(), message);
    if (!isLocal)
        storeRemoteMLineInfo(message);

    auto type = toSessionDescriptionType(description.type());
#ifndef GST_DISABLE_GST_DEBUG
    GUniquePtr<gchar> sdp(gst_sdp_message_as_text(message));
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating %s session for SDP %s: %s", isLocal ? "local" : "remote", gst_webrtc_sdp_type_to_string(type), sdp.get());
#endif
    GUniquePtr<GstWebRTCSessionDescription> sessionDescription(gst_webrtc_session_description_new(type, message));
    auto signalName = makeString("set-", isLocal ? "local" : "remote", "-description");
    std::unique_ptr<SetDescriptionCallData> data = makeUnique<SetDescriptionCallData>(*this, WTFMove(successCallback), WTFMove(failureCallback));
    g_signal_emit_by_name(m_webrtcBin.get(), signalName.ascii().data(), sessionDescription.get(), gst_promise_new_with_change_func([](GstPromise* promise, gpointer userData) {
        std::unique_ptr<SetDescriptionCallData> data(reinterpret_cast<SetDescriptionCallData*>(userData));
        auto result = gst_promise_wait(promise);
        auto cleanup = makeScopeExit([&] {
            gst_promise_unref(promise);
        });

        const auto* reply = gst_promise_get_reply(promise);
        if (result != GST_PROMISE_RESULT_REPLIED || (reply && gst_structure_has_field(reply, "error"))) {
            callOnMainThread([protectedThis = makeRef(data->endPoint), failureCallback = WTFMove(data->failureCallback)] {
                if (protectedThis->isStopped())
                    return;
                failureCallback();
            });
            return;
        }
        callOnMainThread([protectedThis = makeRef(data->endPoint), successCallback = WTFMove(data->successCallback)] {
            if (protectedThis->isStopped())
                return;
            successCallback();
        });
    }, data.release(), nullptr));
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

#ifndef GST_DISABLE_GST_DEBUG
        GUniquePtr<char> mediaRepresentation(gst_sdp_media_as_text(media));
        GST_LOG_OBJECT(m_pipeline.get(), "Processing media:\n%s", mediaRepresentation.get());
#endif
        const char* mid = gst_sdp_media_get_attribute_val(media, "mid");
        if (!mid)
            continue;

        m_mediaForMid.set(String(mid), g_str_equal(typ, "audio") ? RealtimeMediaSource::Type::Audio : RealtimeMediaSource::Type::Video);

        // https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/merge_requests/1907
        if (sdpMediaHasAttributeKey(media, "ice-lite")) {
            GRefPtr<GObject> ice;
            g_object_get(m_webrtcBin.get(), "ice-agent", &ice.outPtr(), nullptr);

            // gboolean iceTCP;
            // g_object_get(ice, "ice-tcp", &iceTCP, nullptr);
            // gst_printerrln("ice-tcp: %d", iceTCP);
            g_object_set(ice.get(), "ice-lite", TRUE, nullptr);
        }

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

            // Relay SDP attributes to the caps, this is specially useful so that elements in
            // webrtcbin will be able to enable RTP header extensions.
            gst_sdp_media_attributes_to_caps(media, formatCaps.get());

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

    source.setPayloadType(found->second);
    found->first.isUsed = true;

    auto padId = makeString("sink_", m_requestPadCounter);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Linking outgoing source to new request-pad %s", padId.utf8().data());
    GstPadTemplate* padTemplate = gst_element_get_pad_template(m_webrtcBin.get(), "sink_%u");
    auto sinkPad = adoptGRef(gst_element_request_pad(m_webrtcBin.get(), padTemplate, padId.utf8().data(), nullptr));
    if (!sinkPad)
        return;
    m_requestPadCounter++;
    RELEASE_ASSERT(!gst_pad_is_linked(sinkPad.get()));
    auto sourceBin = source.bin();
    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), sourceBin.get());
    source.linkTo(sinkPad.get());

    WTF::String dotFileName = makeString(GST_OBJECT_NAME(m_pipeline.get()), ".outgoing-", padId);
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.utf8().data());
}

bool GStreamerMediaEndpoint::addTrack(GStreamerRtpSenderBackend& sender, MediaStreamTrack& track, const Vector<String>& mediaStreamIds)
{
    GStreamerRtpSenderBackend::Source source;
    GRefPtr<GstWebRTCRTPSender> rtcSender;

    switch (track.privateTrack().type()) {
    case RealtimeMediaSource::Type::Audio: {
        GST_DEBUG_OBJECT(m_pipeline.get(), "Adding outgoing audio source");
        auto audioSource = RealtimeOutgoingAudioSourceGStreamer::create(track.privateTrack());
        if (m_delayedNegotiation) {
            // TODO(phil): Move this to RealtimeOutgoingMediaSourceGStreamer ?
            GRefPtr<GstWebRTCRTPTransceiver> transceiver;
            g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV, audioSource->allowedCaps().get(), &transceiver.outPtr());
            g_object_get(transceiver.get(), "sender", &rtcSender.outPtr(), nullptr);
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
        auto videoSource = RealtimeOutgoingVideoSourceGStreamer::create(track.privateTrack());
        if (m_delayedNegotiation) {
            // TODO(phil): Move this to RealtimeOutgoingMediaSourceGStreamer ?
            GRefPtr<GstWebRTCRTPTransceiver> transceiver;
            g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV, videoSource->allowedCaps().get(), &transceiver.outPtr());
            g_object_get(transceiver.get(), "sender", &rtcSender.outPtr(), nullptr);
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
    gst_printerrln(">>>>>>>>>>>>>> Remove track");
    auto bin = sender.stopSource();
    if (bin)
        gst_bin_remove(GST_BIN_CAST(m_pipeline.get()), bin.get());
    sender.clearSource();
}

void GStreamerMediaEndpoint::doCreateOffer(const RTCOfferOptions& options)
{
    //flushPendingSources(true);

    // https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/merge_requests/1877
    GUniquePtr<GstStructure> offerOptions(gst_structure_new("webrtcbin-offer-options", "ice-restart", G_TYPE_BOOLEAN, options.iceRestart, nullptr));
    initiate(true, WTFMove(offerOptions));
}

void GStreamerMediaEndpoint::doCreateAnswer()
{
    initiate(false, nullptr);
}

void GStreamerMediaEndpoint::initiate(bool isInitiator, GUniquePtr<GstStructure>&& options)
{
    m_isInitiator = isInitiator;
    const char* type = isInitiator ? "offer" : "answer";
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating %s", type);
    auto signalName = makeString("create-", type);
    g_signal_emit_by_name(m_webrtcBin.get(), signalName.ascii().data(), options.release(), gst_promise_new_with_change_func([](GstPromise* promise, gpointer userData) {
        auto* endpoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
        auto cleanup = makeScopeExit([&] {
            gst_promise_unref(promise);
        });
        auto result = gst_promise_wait(promise);
        if (result != GST_PROMISE_RESULT_REPLIED) {
            endpoint->createSessionDescriptionFailed({});
            return;
        }

        const auto* reply = gst_promise_get_reply(promise);
        RELEASE_ASSERT(reply);
        if (gst_structure_has_field(reply, "error")) {
            GUniqueOutPtr<GError> error;
            gst_structure_get(reply, "error", G_TYPE_ERROR, &error.outPtr(), nullptr);
            endpoint->createSessionDescriptionFailed(WTFMove(error));
            return;
        }

        const char* type = endpoint->m_isInitiator ? "offer" : "answer";
        GUniqueOutPtr<GstWebRTCSessionDescription> description;
        gst_structure_get(reply, type, GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &description.outPtr(), nullptr);

#ifndef GST_DISABLE_GST_DEBUG
        GUniquePtr<gchar> sdp(gst_sdp_message_as_text(description->sdp));
        GST_DEBUG_OBJECT(endpoint->pipeline(), "Created %s: %s", type, sdp.get());
#endif
        endpoint->createSessionDescriptionSucceeded(WTFMove(description));
    }, this, nullptr));
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
    GstWebRTCSignalingState state;
    g_object_get(m_webrtcBin.get(), "signaling-state", &state, nullptr);
#ifndef GST_DISABLE_GST_DEBUG
    auto desc = enumToString(GST_TYPE_WEBRTC_SIGNALING_STATE, state);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Signaling state changed to %s", desc.data());
#endif
    callOnMainThread([protectedThis = makeRef(*this), state = toSignalingState(state)] {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.updateSignalingState(state);
    });
}

MediaStream& GStreamerMediaEndpoint::mediaStreamFromRTCStream(String label)
{
    auto mediaStream = m_remoteStreamsById.ensure(label, [label, this]() mutable {
        auto& document = downcast<Document>(*m_peerConnectionBackend.connection().scriptExecutionContext());
        return MediaStream::create(document, MediaStreamPrivate::create(document.logger(), { }, WTFMove(label)));
    });
    return *mediaStream.iterator->value;
}

void GStreamerMediaEndpoint::addRemoteStream(GstPad* pad)
{
    m_pendingIncomingStreams++;

    auto caps = adoptGRef(gst_pad_query_caps(pad, nullptr));
    const char* mediaType = capsMediaType(caps.get());
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding remote %s stream to %" GST_PTR_FORMAT " %u pending incoming streams, caps: %" GST_PTR_FORMAT, mediaType, pad, m_pendingIncomingStreams, caps.get());

    GRefPtr<GstWebRTCRTPTransceiver> rtcTransceiver;
    g_object_get(pad, "transceiver", &rtcTransceiver.outPtr(), nullptr);

    auto* transceiver = m_peerConnectionBackend.existingTransceiver([&](auto& transceiverBackend) {
        return rtcTransceiver.get() == transceiverBackend.rtcTransceiver();
    });
    if (!transceiver) {
        auto type = doCapsHaveType(caps.get(), "audio") ? RealtimeMediaSource::Type::Audio : RealtimeMediaSource::Type::Video;
        transceiver = &m_peerConnectionBackend.newRemoteTransceiver(makeUnique<GStreamerRtpTransceiverBackend>(WTFMove(rtcTransceiver)), type);
    }

    GUniqueOutPtr<GstWebRTCSessionDescription> description;
    g_object_get(m_webrtcBin.get(), "current-remote-description", &description.outPtr(), nullptr);

    const auto* media = gst_sdp_message_get_media(description->sdp, rtcTransceiver->mline);
    String sdpString = gst_sdp_media_as_text(media);

    auto* structure = gst_caps_get_structure(caps.get(), 0);
    unsigned ssrc = 0;
    gst_structure_get(structure, "ssrc", G_TYPE_UINT, &ssrc, nullptr);

    GUniquePtr<gchar> name(gst_pad_get_name(pad));
    String label(name.get());
    auto key = makeString("a=ssrc:", ssrc, " msid:");
    auto lines = sdpString.split('\n');
    for (auto& line : lines) {
        if (line.startsWith(key)) {
            auto tmp = line.substring(key.length());
            label = tmp.substring(0, tmp.find(' '));
            break;
        }
    }
    GST_DEBUG_OBJECT(m_pipeline.get(), "ssrc %u has msid: %s", ssrc, label.ascii().data());

    GstElement* bin = nullptr;
    auto& track = transceiver->receiver().track();
    auto& source = track.privateTrack().source();
    if (source.isIncomingAudioSource())
        bin = static_cast<RealtimeIncomingAudioSourceGStreamer&>(source).bin();
    else if (source.isIncomingVideoSource())
        bin = static_cast<RealtimeIncomingVideoSourceGStreamer&>(source).bin();
    else
        RELEASE_ASSERT_NOT_REACHED();

    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), bin);
    auto sinkPad = adoptGRef(gst_element_get_static_pad(bin, "sink"));
    gst_pad_link(pad, sinkPad.get());
    gst_element_sync_state_with_parent(bin);

    track.setEnabled(true);
    source.setMuted(false);

    auto& mediaStream = mediaStreamFromRTCStream(label);
    mediaStream.addTrackFromPlatform(track);
    Vector<RefPtr<MediaStream>> streams { &mediaStream };

    m_peerConnectionBackend.addPendingTrackEvent({makeRef(transceiver->receiver()), makeRef(transceiver->receiver().track()), WTFMove(streams), makeRef(*transceiver)});

    // FIXME: This needs to be revised.
    if (m_pendingIncomingStreams == gst_sdp_message_medias_len(description->sdp)) {
        GST_DEBUG_OBJECT(m_pipeline.get(), "Incoming streams gathered, now firing track events");
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

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::createTransceiverBackends(const String& kind, const RTCRtpTransceiverInit& init, GStreamerRtpSenderBackend::Source&& source)
{
    if (!m_webrtcBin)
        return WTF::nullopt;

    GST_DEBUG_OBJECT(m_pipeline.get(), "%zu streams in init data", init.streams.size());

    // FIXME: This needs to be revised.
    const char* encodingName = nullptr;
    int clockRate = 0;
    const char* media = kind.utf8().data();
    if (kind == "video"_s) {
        encodingName = "VP8";
        clockRate = 90000;
    } else {
        encodingName = "OPUS";
        clockRate = 48000;
    }

    auto caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, media, "encoding-name", G_TYPE_STRING, encodingName,
        "payload", G_TYPE_INT, m_ptCounter++, "clock-rate", G_TYPE_INT, clockRate, nullptr));
    auto direction = fromRTCRtpTransceiverDirection(init.direction);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding %s transceiver for payload %" GST_PTR_FORMAT, enumToString(GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION, direction).data(), caps.get());
    GRefPtr<GstWebRTCRTPTransceiver> rtcTransceiver;
    g_signal_emit_by_name(m_webrtcBin.get(), "add-transceiver", direction, caps.get(), &rtcTransceiver.outPtr());
    if (!rtcTransceiver)
        return WTF::nullopt;

    auto transceiver = makeUnique<GStreamerRtpTransceiverBackend>(WTFMove(rtcTransceiver));
    return GStreamerMediaEndpoint::Backends { transceiver->createSenderBackend(m_peerConnectionBackend, WTFMove(source)), transceiver->createReceiverBackend(), WTFMove(transceiver) };
}

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::addTransceiver(const String& trackKind, const RTCRtpTransceiverInit& init)
{
    return createTransceiverBackends(trackKind, init, nullptr);
}

GStreamerRtpSenderBackend::Source GStreamerMediaEndpoint::createSourceForTrack(MediaStreamTrack& track)
{
    GStreamerRtpSenderBackend::Source source;
    switch (track.privateTrack().type()) {
    case RealtimeMediaSource::Type::None:
        ASSERT_NOT_REACHED();
        break;
    case RealtimeMediaSource::Type::Audio: {
        auto audioSource = RealtimeOutgoingAudioSourceGStreamer::create(track.privateTrack());
        source = WTFMove(audioSource);
        break;
    }
    case RealtimeMediaSource::Type::Video: {
        auto videoSource = RealtimeOutgoingVideoSourceGStreamer::create(track.privateTrack());
        source = WTFMove(videoSource);
        break;
    }
    }
    return source;
}

Optional<GStreamerMediaEndpoint::Backends> GStreamerMediaEndpoint::addTransceiver(MediaStreamTrack& track, const RTCRtpTransceiverInit& init)
{
    auto source = createSourceForTrack(track);
    String kind(track.kind());
    return createTransceiverBackends(kind, init, WTFMove(source));
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
    GRefPtr<GArray> transceivers;
    g_signal_emit_by_name(m_webrtcBin.get(), "get-transceivers", &transceivers.outPtr());

    GST_DEBUG_OBJECT(m_pipeline.get(), "Looking for sender %p in %u existing transceivers", backend.rtcSender(), transceivers->len);
    for (unsigned i = 0; i < transceivers->len; i++) {
        GstWebRTCRTPTransceiver* current = g_array_index(transceivers.get(), GstWebRTCRTPTransceiver*, i);
        GRefPtr<GstWebRTCRTPSender> sender;
        g_object_get(current, "sender", &sender.outPtr(), nullptr);

        if (!sender)
            continue;
        // if (!backend.rtcSender()) {
        //     // FIXME: GStreamerRtpTransceiverBackend should use a RefPtr for GstWebRTCRTPTransceiver.
        //     auto result = std::make_unique<GStreamerRtpTransceiverBackend>(reinterpret_cast<GstWebRTCRTPTransceiver*>(gst_object_ref(current)));
        //     return result;
        // }
        if (sender.get() == backend.rtcSender())
            return std::make_unique<GStreamerRtpTransceiverBackend>(current);
    }

    return nullptr;
}

void GStreamerMediaEndpoint::addIceCandidate(GStreamerIceCandidate& candidate)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Adding ICE candidate %s", candidate.candidate.utf8().data());
    g_signal_emit_by_name(m_webrtcBin.get(), "add-ice-candidate", candidate.sdpMLineIndex, candidate.candidate.utf8().data());
}

std::unique_ptr<RTCDataChannelHandler> GStreamerMediaEndpoint::createDataChannel(const String& label, const RTCDataChannelInit& options)
{
    if (!m_webrtcBin)
        return nullptr;

    auto init = GStreamerDataChannelHandler::fromRTCDataChannelInit(options);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Creating data channel for init options %" GST_PTR_FORMAT, init.get());
    GRefPtr<GstWebRTCDataChannel> channel;
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

void GStreamerMediaEndpoint::close()
{
    // https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/issues/1181
    GST_DEBUG_OBJECT(m_pipeline.get(), "Closing");
    if (m_pipeline)
        gst_element_set_state(m_pipeline.get(), GST_STATE_READY);

#if !RELEASE_LOG_DISABLED
    stopLoggingStats();
#endif
}

void GStreamerMediaEndpoint::stop()
{
#if !RELEASE_LOG_DISABLED
    stopLoggingStats();
#endif

    if (!m_pipeline)
        return;

    GST_DEBUG_OBJECT(m_pipeline.get(), "Stopping");
    teardownPipeline();
}

void GStreamerMediaEndpoint::suspend()
{
    if (!m_pipeline)
        return;

    GST_DEBUG_OBJECT(m_pipeline.get(), "Suspending");
    gst_element_set_state(m_pipeline.get(), GST_STATE_PAUSED);
}

void GStreamerMediaEndpoint::resume()
{
    if (!m_pipeline)
        return;

    GST_DEBUG_OBJECT(m_pipeline.get(), "Resuming");
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
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
    // gst_printerrln("failed : %d", connectionState == RTCIceConnectionState::Failed);
    callOnMainThread([protectedThis = makeRef(*this), connectionState = toRTCIceConnectionState(state)] {
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
    GUniqueOutPtr<GstWebRTCSessionDescription> description;
    g_object_get(m_webrtcBin.get(), "local-description", &description.outPtr(), nullptr);
    if (!description)
        return;

    const GstSDPMedia* media = gst_sdp_message_get_media(description->sdp, sdpMLineIndex);
    String candidateMid(gst_sdp_media_get_attribute_val(media, "mid"));
    String candidateSDP(candidate);
    callOnMainThread([protectedThis = makeRef(*this), mid = WTFMove(candidateMid), sdp = WTFMove(candidateSDP), sdpMLineIndex]() mutable {
        if (protectedThis->isStopped())
            return;
        protectedThis->m_peerConnectionBackend.newICECandidate(WTFMove(sdp), WTFMove(mid), sdpMLineIndex, { });
    });
}

void GStreamerMediaEndpoint::createSessionDescriptionSucceeded(GUniqueOutPtr<GstWebRTCSessionDescription>&& description)
{
    callOnMainThread([protectedThis = makeRef(*this), description = description.release()] {
        if (protectedThis->isStopped())
            return;

        GUniquePtr<char> sdp(gst_sdp_message_as_text(description->sdp));
        if (protectedThis->m_isInitiator)
            protectedThis->m_peerConnectionBackend.createOfferSucceeded(sdp.get());
        else
            protectedThis->m_peerConnectionBackend.createAnswerSucceeded(sdp.get());
    });
}

void GStreamerMediaEndpoint::createSessionDescriptionFailed(GUniqueOutPtr<GError>&& error)
{
    callOnMainThread([protectedThis = makeRef(*this), error = error.release()] {
        if (protectedThis->isStopped())
            return;

        auto exc = Exception { OperationError, error ? error->message : "Unknown Error" };
        if (protectedThis->m_isInitiator)
            protectedThis->m_peerConnectionBackend.createOfferFailed(WTFMove(exc));
        else
            protectedThis->m_peerConnectionBackend.createAnswerFailed(WTFMove(exc));
    });
}

void GStreamerMediaEndpoint::collectTransceivers()
{
    GRefPtr<GArray> transceivers;
    g_signal_emit_by_name(m_webrtcBin.get(), "get-transceivers", &transceivers.outPtr());
    for (unsigned i = 0; i < transceivers->len; i++) {
        GstWebRTCRTPTransceiver* current = g_array_index(transceivers.get(), GstWebRTCRTPTransceiver*, i);

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
}

#if !RELEASE_LOG_DISABLED
void GStreamerMediaEndpoint::gatherStatsForLogging()
{
    g_signal_emit_by_name(m_webrtcBin.get(), "get-stats", nullptr, gst_promise_new_with_change_func([](GstPromise* promise, gpointer userData) {
        auto result = gst_promise_wait(promise);
        auto cleanup = makeScopeExit([&] {
            gst_promise_unref(promise);
        });
        if (result != GST_PROMISE_RESULT_REPLIED)
            return;

        const auto* reply = gst_promise_get_reply(promise);
        RELEASE_ASSERT(reply);
        if (gst_structure_has_field(reply, "error"))
            return;

        auto* endPoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
        endPoint->onStatsDelivered(reply);
    }, this, nullptr));
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
        gst_structure_foreach(stats, static_cast<GstStructureForeachFunc>([](GQuark, const GValue* value, gpointer userData) -> gboolean {
            auto* endPoint = reinterpret_cast<GStreamerMediaEndpoint*>(userData);
            endPoint->processStats(value);
            return TRUE;
        }), this);
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
