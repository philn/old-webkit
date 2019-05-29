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
#include "RealtimeOutgoingVideoSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include "GStreamerVideoEncoder.h"
#include "Logging.h"
//#include "RealtimeVideoSourceGStreamer.h"
#include <gst/app/gstappsrc.h>
#include <wtf/MainThread.h>

namespace WebCore {

    RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(Ref<MediaStreamTrackPrivate>&& videoSource, GstElement* pipeline)
    : m_videoSource(WTFMove(videoSource))
    , m_pipeline(pipeline)
{
    m_videoSource->addObserver(*this);
    initializeFromSource();
}

bool RealtimeOutgoingVideoSourceGStreamer::setSource(Ref<MediaStreamTrackPrivate>&& newSource)
{
    if (!m_initialSettings)
        m_initialSettings = m_videoSource->source().settings();

    m_videoSource->removeObserver(*this);
    m_videoSource = WTFMove(newSource);
    m_videoSource->addObserver(*this);

    initializeFromSource();
    return true;
}

void RealtimeOutgoingVideoSourceGStreamer::stop()
{
    m_videoSource->removeObserver(*this);
    m_isStopped = true;
    g_printerr("Stop\n");
}

void RealtimeOutgoingVideoSourceGStreamer::sourceMutedChanged()
{
    ASSERT(m_muted != m_videoSource->muted());

    m_muted = m_videoSource->muted();

    g_printerr("sourceMutedChanged to %d\n", m_muted);
}

void RealtimeOutgoingVideoSourceGStreamer::sourceEnabledChanged()
{
    ASSERT(m_enabled != m_videoSource->enabled());

    m_enabled = m_videoSource->enabled();

    g_printerr("sourceEnabledChanged to %d\n", m_enabled);
}

void RealtimeOutgoingVideoSourceGStreamer::initializeFromSource()
{
    const auto& settings = m_videoSource->source().settings();
    m_width = settings.width();
    m_height = settings.height();

    m_muted = m_videoSource->muted();
    m_enabled = m_videoSource->enabled();

    g_printerr("initializeFromSource muted: %d, enabled: %d\n", m_muted, m_enabled);
    // auto& videoSource = reinterpret_cast<GStreamerRealtimeVideoSource&>(m_videoSource->source());

    GRefPtr<GstElement> webrtcBin = gst_bin_get_by_name(GST_BIN_CAST(m_pipeline.get()), "webkit-webrtcbin");

    m_outgoingVideoSource = gst_element_factory_make("appsrc", nullptr);
    // gst_app_src_set_stream_type(GST_APP_SRC(m_outgoingVideoSource.get()), GST_APP_STREAM_TYPE_STREAM);
    g_object_set(m_outgoingVideoSource.get(), "is-live", true, "format", GST_FORMAT_TIME, nullptr);

    // FIXME: properly configure output-selector, somehow!
    m_outputSelector = gst_element_factory_make("identity", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_outgoingVideoSource.get(), m_outputSelector.get(), nullptr);
    gst_element_link(m_outgoingVideoSource.get(), m_outputSelector.get());

// #define _WEBRTC_VP8
#ifdef _WEBRTC_VP8
    GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("video/x-vp8"));
    GstElement* rtpvpay = gst_element_factory_make("rtpvp8pay", nullptr);
    GRefPtr<GstCaps> vcaps2 = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "video",
                                                            "payload", G_TYPE_INT, 96,
                                                            "encoding-name", G_TYPE_STRING, "VP8", nullptr));
#else
    GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("video/x-h264"));
    GstElement* rtpvpay = gst_element_factory_make("rtph264pay", nullptr);
    GRefPtr<GstCaps> vcaps2 = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "video",
                                                            "payload", G_TYPE_INT, 98,
                                                            "encoding-name", G_TYPE_STRING, "H264", nullptr));
    g_object_set(rtpvpay, "pt", 98, "config-interval", 1, nullptr);
#endif

    GstElement* videoconvert = gst_element_factory_make("videoconvert", nullptr);
    GstElement* encoder = gst_element_factory_make("webrtcvideoencoder", nullptr);
    g_object_set(encoder, "format", caps.get(), nullptr);

    GstElement* queue = gst_element_factory_make("queue", nullptr);
    GstElement* vcapsfilter = gst_element_factory_make("capsfilter", nullptr);
    g_object_set(vcapsfilter, "caps", vcaps2.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), videoconvert, encoder, rtpvpay, queue, vcapsfilter, nullptr);

    gst_element_link_many(m_outputSelector.get(), videoconvert, encoder, rtpvpay, queue, vcapsfilter, nullptr);
    gst_element_link(vcapsfilter, webrtcBin.get());

    gst_element_sync_state_with_parent(m_outgoingVideoSource.get());
    gst_element_sync_state_with_parent(m_outputSelector.get());
    gst_element_sync_state_with_parent(videoconvert);
    gst_element_sync_state_with_parent(encoder);
    gst_element_sync_state_with_parent(rtpvpay);
    gst_element_sync_state_with_parent(queue);
    gst_element_sync_state_with_parent(vcapsfilter);
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-video");
}

void RealtimeOutgoingVideoSourceGStreamer::sampleBufferUpdated(MediaStreamTrackPrivate&, MediaSample& mediaSample)
{
    ASSERT(mediaSample.platformSample().type == PlatformSample::GStreamerSampleType);
    GRefPtr<GstSample> gstSample = mediaSample.platformSample().sample.gstSample;

    gst_app_src_push_sample(GST_APP_SRC(m_outgoingVideoSource.get()), gstSample.get());
}


} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
