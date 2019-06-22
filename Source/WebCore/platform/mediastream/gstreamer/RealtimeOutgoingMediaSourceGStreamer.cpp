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
#include "Logging.h"
#include "MediaSampleGStreamer.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API

namespace WebCore {

RealtimeOutgoingMediaSourceGStreamer::RealtimeOutgoingMediaSourceGStreamer(Ref<MediaStreamTrackPrivate>&& track, GstElement* pipeline)
    : m_track(WTFMove(track))
    , m_pipeline(pipeline)
{
}

bool RealtimeOutgoingMediaSourceGStreamer::setTrack(Ref<MediaStreamTrackPrivate>&& newTrack)
{
    if (!m_initialSettings)
        m_initialSettings = m_track->source().settings();

    m_track->removeObserver(*this);
    m_track = WTFMove(newTrack);
    m_track->addObserver(*this);
    initializeFromTrack();
    return true;
}

void RealtimeOutgoingMediaSourceGStreamer::stop()
{
    m_track->removeObserver(*this);
    m_isStopped = true;
    g_printerr("Stop\n");
}

void RealtimeOutgoingMediaSourceGStreamer::sourceMutedChanged()
{
    ASSERT(m_muted != m_track->muted());
    m_muted = m_track->muted();
    g_printerr("sourceMutedChanged to %d\n", m_muted);
}

void RealtimeOutgoingMediaSourceGStreamer::sourceEnabledChanged()
{
    ASSERT(m_enabled != m_track->enabled());
    m_enabled = m_track->enabled();
    g_printerr("sourceEnabledChanged to %d\n", m_enabled);
    if (m_valve)
        g_object_set(m_valve.get(), "drop", !m_enabled, nullptr);
}

void RealtimeOutgoingMediaSourceGStreamer::linkToWebRTCBinPad(GstPad* sinkPad)
{
    m_pad = sinkPad;

    gst_element_link(m_postEncoderQueue.get(), m_capsFilter.get());

    GRefPtr<GstPad> srcPad = adoptGRef(gst_element_get_static_pad(m_capsFilter.get(), "src"));
    gst_pad_link(srcPad.get(), sinkPad);

    GstWebRTCRTPTransceiver* transceiver = nullptr;
    g_object_get(sinkPad, "transceiver", &transceiver, nullptr);
    g_object_get(transceiver, "sender", &m_sender.outPtr(), nullptr);

    synchronizeStates();
}

void RealtimeOutgoingMediaSourceGStreamer::initializeFromTrack()
{
    m_muted = m_track->muted();
    m_enabled = m_track->enabled();
    g_printerr("initializeFromTrack muted: %d, enabled: %d\n", m_muted, m_enabled);

    m_valve = gst_element_factory_make("valve", nullptr);
    m_preEncoderQueue = gst_element_factory_make("queue", nullptr);
    m_postEncoderQueue = gst_element_factory_make("queue", nullptr);
    m_capsFilter = gst_element_factory_make("capsfilter", nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_valve.get(), m_preEncoderQueue.get(),
        m_postEncoderQueue.get(), m_capsFilter.get(), nullptr);

    initialize();
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
