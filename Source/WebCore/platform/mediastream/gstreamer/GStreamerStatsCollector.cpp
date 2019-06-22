/*
 *  Copyright (C) 2019 Igalia S.L. All rights reserved.
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
#include "GStreamerStatsCollector.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GUniquePtrGStreamer.h"
#include "JSRTCStatsReport.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API
#include <wtf/MainThread.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_webrtc_endpoint_debug);
#define GST_CAT_DEFAULT webkit_webrtc_endpoint_debug

namespace WebCore {

static void statsPromiseChanged(GstPromise* promise, gpointer userData)
{
    GstPromiseResult result = gst_promise_wait(promise);
    if (result != GST_PROMISE_RESULT_REPLIED) {
        gst_promise_unref(promise);
        return;
    }

    GStreamerStatsCollector* collector = reinterpret_cast<GStreamerStatsCollector*>(userData);
    collector->processStats(gst_promise_get_reply(promise));
    gst_promise_unref(promise);
}

GStreamerStatsCollector::~GStreamerStatsCollector()
{
    if (!m_callback)
        return;

    callOnMainThread([callback = WTFMove(m_callback)]() mutable {
        callback();
    });
}

void GStreamerStatsCollector::getStats(GstPad* pad, Ref<DeferredPromise>&& promise)
{
    m_callback = [promise = WTFMove(promise)]() mutable -> RefPtr<RTCStatsReport> {
        ASSERT(isMainThread());

        auto report = RTCStatsReport::create();

        promise->resolve<IDLInterface<RTCStatsReport>>(report.copyRef());

        // The promise resolution might fail in which case no backing map will be created.
        if (!report->backingMap())
            return nullptr;
        return report;
    };

    GstPromise* gstPromise = gst_promise_new_with_change_func(statsPromiseChanged, this, nullptr);
    g_signal_emit_by_name(m_webrtcBin.get(), "get-stats", pad, gstPromise);
}

static inline void fillRTCStats(RTCStatsReport::Stats& stats, const GstStructure* structure)
{
    double timestamp;
    if (gst_structure_get_double(structure, "timestamp", &timestamp))
        stats.timestamp = timestamp;
    stats.id = gst_structure_get_string(structure, "id");
}

static inline void fillRTCRTPStreamStats(RTCStatsReport::RTCRTPStreamStats& stats, const GstStructure* structure)
{
    fillRTCStats(stats, structure);

    stats.transportId = gst_structure_get_string(structure, "transport-id");
    stats.codecId = gst_structure_get_string(structure, "codec-id");

    unsigned value;
    if (gst_structure_get_uint(structure, "ssrc", &value))
        stats.ssrc = value;

    if (gst_structure_get_uint(structure, "fir-count", &value))
        stats.firCount = value;

    if (gst_structure_get_uint(structure, "pli-count", &value))
        stats.pliCount = value;

    if (gst_structure_get_uint(structure, "nack-count", &value))
        stats.nackCount = value;

    // FIXME:
    // stats.associateStatsId =
    // stats.isRemote =
    // stats.mediaType =
    // stats.trackId =
    // stats.sliCount =
    // stats.qpSum =
    stats.qpSum = 0;
}

static inline void fillRTCCodecStats(RTCStatsReport::CodecStats& stats, const GstStructure* structure)
{
    fillRTCStats(stats, structure);

    unsigned value;
    if (gst_structure_get_uint(structure, "payload-type", &value))
        stats.payloadType = value;
    if (gst_structure_get_uint(structure, "clock-rate", &value))
        stats.clockRate = value;

    // FIXME:
    // stats.mimeType =
    // stats.channels =
    // stats.sdpFmtpLine =
    // stats.implementation =
}

static inline void fillInboundRTPStreamStats(RTCStatsReport::InboundRTPStreamStats& stats, const GstStructure* structure)
{
    fillRTCRTPStreamStats(stats, structure);

    uint64_t value;
    if (gst_structure_get_uint64(structure, "packets-received", &value))
        stats.packetsReceived = value;
    if (gst_structure_get_uint64(structure, "bytes-received", &value))
        stats.bytesReceived = value;

    unsigned packetsLost;
    if (gst_structure_get_uint(structure, "packets-lost", &packetsLost))
        stats.packetsLost = packetsLost;

    double jitter;
    if (gst_structure_get_double(structure, "jitter", &jitter))
        stats.jitter = jitter;

    // FIXME:
    // stats.fractionLost =
    // stats.packetsDiscarded =
    // stats.packetsRepaired =
    // stats.burstPacketsLost =
    // stats.burstPacketsDiscarded =
    // stats.burstLossCount =
    // stats.burstDiscardCount =
    // stats.burstLossRate =
    // stats.burstDiscardRate =
    // stats.gapLossRate =
    // stats.gapDiscardRate =
    // stats.framesDecoded =
}

static inline void fillOutboundRTPStreamStats(RTCStatsReport::OutboundRTPStreamStats& stats, const GstStructure* structure)
{
    fillRTCRTPStreamStats(stats, structure);

    uint64_t value;
    if (gst_structure_get_uint64(structure, "packets-sent", &value))
        stats.packetsSent = value;
    if (gst_structure_get_uint64(structure, "bytes-sent", &value))
        stats.bytesSent = value;

    // FIXME
    // stats.targetBitrate =
    // stats.framesEncoded =
}

static inline void fillRTCPeerConnectionStats(RTCStatsReport::PeerConnectionStats& stats, const GstStructure* structure)
{
    fillRTCStats(stats, structure);

    int value;
    if (gst_structure_get_int(structure, "data-channels-opened", &value))
        stats.dataChannelsOpened = value;
    if (gst_structure_get_int(structure, "data-channels-closed", &value))
        stats.dataChannelsClosed = value;
}

static inline void fillRTCTransportStats(RTCStatsReport::TransportStats& stats, const GstStructure* structure)
{
    fillRTCStats(stats, structure);

    // FIXME
}

static gboolean fillReportCallback(GQuark, const GValue* value, gpointer userData)
{
    if (!GST_VALUE_HOLDS_STRUCTURE(value))
        return TRUE;

    const GstStructure* structure = gst_value_get_structure(value);
    GstWebRTCStatsType statsType;
    if (!gst_structure_get(structure, "type", GST_TYPE_WEBRTC_STATS_TYPE, &statsType, nullptr))
        return TRUE;

    RTCStatsReport* report = reinterpret_cast<RTCStatsReport*>(userData);

    switch (statsType) {
    case GST_WEBRTC_STATS_CODEC: {
        RTCStatsReport::CodecStats stats;
        fillRTCCodecStats(stats, structure);
        report->addStats<IDLDictionary<RTCStatsReport::CodecStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_INBOUND_RTP: {
        RTCStatsReport::InboundRTPStreamStats stats;
        fillInboundRTPStreamStats(stats, structure);
        report->addStats<IDLDictionary<RTCStatsReport::InboundRTPStreamStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_OUTBOUND_RTP: {
        RTCStatsReport::OutboundRTPStreamStats stats;
        fillOutboundRTPStreamStats(stats, structure);
        report->addStats<IDLDictionary<RTCStatsReport::OutboundRTPStreamStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_REMOTE_INBOUND_RTP: {
        RTCStatsReport::InboundRTPStreamStats stats;
        fillInboundRTPStreamStats(stats, structure);
        stats.isRemote = true;
        report->addStats<IDLDictionary<RTCStatsReport::InboundRTPStreamStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP: {
        RTCStatsReport::OutboundRTPStreamStats stats;
        fillOutboundRTPStreamStats(stats, structure);
        stats.isRemote = true;
        report->addStats<IDLDictionary<RTCStatsReport::OutboundRTPStreamStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_CSRC:
        GST_FIXME("Deprecated stats: csrc");
        break;
    case GST_WEBRTC_STATS_PEER_CONNECTION: {
        RTCStatsReport::PeerConnectionStats stats;
        fillRTCPeerConnectionStats(stats, structure);
        report->addStats<IDLDictionary<RTCStatsReport::PeerConnectionStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_TRANSPORT: {
        RTCStatsReport::TransportStats stats;
        fillRTCTransportStats(stats, structure);
        report->addStats<IDLDictionary<RTCStatsReport::TransportStats>>(WTFMove(stats));
        break;
    }
    case GST_WEBRTC_STATS_STREAM:
        GST_FIXME("Deprecated stats: stream");
        break;
    case GST_WEBRTC_STATS_DATA_CHANNEL:
        GST_FIXME("Missing data-channel stats support");
        break;
    case GST_WEBRTC_STATS_CANDIDATE_PAIR:
    case GST_WEBRTC_STATS_LOCAL_CANDIDATE:
    case GST_WEBRTC_STATS_REMOTE_CANDIDATE:
        GST_FIXME("Missing candidate stats support");
        break;
    case GST_WEBRTC_STATS_CERTIFICATE:
        GST_FIXME("Missing certificate stats support");
        break;
    }

    return TRUE;
}

void GStreamerStatsCollector::processStats(const GstStructure* stats)
{
    GUniquePtr<GstStructure> statsCopy(gst_structure_copy(stats));
    callOnMainThread([stats = statsCopy.release(), protectedThis = makeRef(*this)] {
        auto report = protectedThis->m_callback();
        if (!report)
            return;

        ASSERT(report->backingMap());
        gst_structure_foreach(stats, fillReportCallback, report.get());
    });
}

}; // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
