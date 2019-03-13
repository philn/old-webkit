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
#include "RealtimeOutgoingMediaSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include "GStreamerMediaStreamSource.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API

namespace WebCore {

RealtimeOutgoingMediaSourceGStreamer::RealtimeOutgoingMediaSourceGStreamer()
{
    m_bin = gst_bin_new(nullptr);

    m_valve = gst_element_factory_make("valve", nullptr);
    m_preEncoderQueue = gst_element_factory_make("queue", nullptr);
    m_postEncoderQueue = gst_element_factory_make("queue", nullptr);
    m_capsFilter = gst_element_factory_make("capsfilter", nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_bin.get()), m_valve.get(), m_preEncoderQueue.get(),
        m_postEncoderQueue.get(), m_capsFilter.get(), nullptr);

    auto srcPad = adoptGRef(gst_element_get_static_pad(m_capsFilter.get(), "src"));
    gst_element_add_pad(m_bin.get(), gst_ghost_pad_new("src", srcPad.get()));
}

void RealtimeOutgoingMediaSourceGStreamer::setSource(Ref<MediaStreamTrackPrivate>&& newSource)
{
    if (m_source.hasValue() && !m_initialSettings)
        m_initialSettings = m_source.value()->settings();

    if (m_source.hasValue())
        m_source.value()->removeObserver(*this);
    m_source = WTFMove(newSource);
    initializeFromTrack();
}

void RealtimeOutgoingMediaSourceGStreamer::start()
{
    m_source.value()->addObserver(*this);
    m_isStopped = false;
}

void RealtimeOutgoingMediaSourceGStreamer::stop()
{
    if (m_source.hasValue()) {
        m_source.value()->removeObserver(*this);

        auto srcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
        auto peer = adoptGRef(gst_pad_get_peer(srcPad.get()));
        if (!peer)
            return;

        gst_element_send_event(m_outgoingSource.get(), gst_event_new_eos());
        GRefPtr<GstWebRTCRTPTransceiver> transceiver;
        g_object_get(peer.get(), "transceiver", &transceiver.outPtr(), nullptr);
        g_object_set(transceiver.get(), "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE, nullptr);

        gst_element_set_locked_state(m_outgoingSource.get(), true);
        gst_element_set_state(m_outgoingSource.get(), GST_STATE_NULL);
        gst_pad_unlink(srcPad.get(), peer.get());

        auto parent = adoptGRef(gst_pad_get_parent_element(peer.get()));
        gst_element_release_request_pad(parent.get(), peer.get());
    }
    m_isStopped = true;
}

void RealtimeOutgoingMediaSourceGStreamer::sourceMutedChanged()
{
    if (!m_source.hasValue())
        return;
    ASSERT(m_muted != m_source.value()->muted());
    m_muted = m_source.value()->muted();
    g_printerr("sourceMutedChanged to %d\n", m_muted);
}

void RealtimeOutgoingMediaSourceGStreamer::sourceEnabledChanged()
{
    if (!m_source.hasValue())
        return;

    m_enabled = m_source.value()->enabled();
    g_printerr("sourceEnabledChanged to %d\n", m_enabled);
    if (m_valve)
        g_object_set(m_valve.get(), "drop", !m_enabled, nullptr);
}

void RealtimeOutgoingMediaSourceGStreamer::initializeFromTrack()
{
    m_muted = m_source.value()->muted();
    m_enabled = m_source.value()->enabled();
    g_printerr("initializeFromTrack muted: %d, enabled: %d\n", m_muted, m_enabled);

    m_outgoingSource = webkitMediaStreamSrcNew();
    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_outgoingSource.get());
    webkitMediaStreamSrcAddTrack(WEBKIT_MEDIA_STREAM_SRC(m_outgoingSource.get()), m_source->ptr(), true);
}

void RealtimeOutgoingMediaSourceGStreamer::linkTo(GstPad* sinkPad)
{
    gst_element_link(m_postEncoderQueue.get(), m_capsFilter.get());

    auto srcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
    gst_pad_link(srcPad.get(), sinkPad);

    GRefPtr<GstWebRTCRTPTransceiver> transceiver;
    g_object_get(sinkPad, "transceiver", &transceiver.outPtr(), nullptr);
    g_object_get(transceiver.get(), "sender", &m_sender.outPtr(), nullptr);

    gst_element_sync_state_with_parent(m_bin.get());
}

GRefPtr<GstPad>&& RealtimeOutgoingMediaSourceGStreamer::pad() const
{
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
    auto peer = adoptGRef(gst_pad_get_peer(srcPad.get()));
    return WTFMove(peer);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
