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
#include "Logging.h"
#include "RealtimeVideoSourceGStreamer.h"
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
    printf("Stop\n");
}

void RealtimeOutgoingVideoSourceGStreamer::sourceMutedChanged()
{
    ASSERT(m_muted != m_videoSource->muted());

    m_muted = m_videoSource->muted();

    printf("sourceMutedChanged to %d\n", m_muted);
}

void RealtimeOutgoingVideoSourceGStreamer::sourceEnabledChanged()
{
    ASSERT(m_enabled != m_videoSource->enabled());

    m_enabled = m_videoSource->enabled();

    printf("sourceEnabledChanged to %d\n", m_enabled);
}

void RealtimeOutgoingVideoSourceGStreamer::initializeFromSource()
{
    const auto& settings = m_videoSource->source().settings();
    m_width = settings.width();
    m_height = settings.height();

    m_muted = m_videoSource->muted();
    m_enabled = m_videoSource->enabled();

    printf("initializeFromSource muted: %d, enabled: %d\n", m_muted, m_enabled);
    // TODO: White-list of audio/video formats

    auto& videoSource = reinterpret_cast<GStreamerRealtimeVideoSource&>(m_videoSource->source());

    GRefPtr<GstElement> webrtcBin = gst_bin_get_by_name(GST_BIN_CAST(m_pipeline.get()), "webkit-webrtcbin");
    GstElement* videoSourceElement = gst_element_factory_make("proxysrc", nullptr);
    g_object_set(videoSourceElement, "proxysink", videoSource.proxySink(), nullptr);

    // FIXME: properly configure output-selector, somehow!
    m_outputSelector = gst_element_factory_make("identity", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), videoSourceElement, m_outputSelector.get(), nullptr);
    gst_element_link(videoSourceElement, m_outputSelector.get());

#if 1
    // VP8 encoding
    GstElement* videoconvert1 = gst_element_factory_make("videoconvert", nullptr);
    GstElement* venc1 = gst_element_factory_make("vp8enc", nullptr);
    GstElement* rtpvpay1 = gst_element_factory_make("rtpvp8pay", nullptr);
    GstElement* q1 = gst_element_factory_make("queue", nullptr);
    GstElement* vcapsfilter1 = gst_element_factory_make("capsfilter", nullptr);
    GRefPtr<GstCaps> vcaps1 = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "video",
                                                            "payload", G_TYPE_INT, 96,
                                                            "encoding-name", G_TYPE_STRING, "VP8", nullptr));
    g_object_set(vcapsfilter1, "caps", vcaps1.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), videoconvert1, venc1, rtpvpay1, q1, vcapsfilter1, nullptr);
    gst_element_link(m_outputSelector.get(), videoconvert1);
    gst_element_link_many(videoconvert1, venc1, rtpvpay1, q1, vcapsfilter1, nullptr);

    GRefPtr<GstPad> vpad1 = adoptGRef(gst_element_get_static_pad(vcapsfilter1, "src"));
    GRefPtr<GstPad> vsink1 = adoptGRef(gst_element_get_request_pad(webrtcBin.get(), "sink_%u"));
    gst_pad_link(vpad1.get(), vsink1.get());
#endif
#if 0
    // H264 encoding
    GstElement* rtpvpay2 = gst_element_factory_make("rtph264pay", nullptr);
    g_object_set(rtpvpay2, "pt", 98, nullptr);
    GstElement* q2 = gst_element_factory_make("queue", nullptr);
    GstElement* vcapsfilter2 = gst_element_factory_make("capsfilter", nullptr);
    GRefPtr<GstCaps> vcaps2 = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "video",
                                                            "payload", G_TYPE_INT, 98,
                                                            "encoding-name", G_TYPE_STRING, "H264", nullptr));
    g_object_set(vcapsfilter2, "caps", vcaps2.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), rtpvpay2, q2, vcapsfilter2, nullptr);

    if (videoSource.videoFormat() == GStreamerRealtimeVideoSource::VideoFormat::Raw) {
        // FIXME: Make this more... generic.
        GstElement* videoconvert2 = gst_element_factory_make("videoconvert", nullptr);
        GstElement* encoder = gst_element_factory_make("openh264enc", nullptr);
        g_object_set(encoder, "gop-size", 0, NULL);
        gst_util_set_object_arg(G_OBJECT(encoder), "rate-control", "bitrate");
        gst_util_set_object_arg(G_OBJECT(encoder), "complexity", "low");
        gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), videoconvert2, encoder, nullptr);
        gst_element_link(m_outputSelector.get(), videoconvert2);
        gst_element_link_many(videoconvert2, encoder, rtpvpay2, nullptr);
        gst_element_sync_state_with_parent(videoconvert2);
        gst_element_sync_state_with_parent(encoder);
    } else if (videoSource.videoFormat() == GStreamerRealtimeVideoSource::VideoFormat::H264) {
        GstElement* parser = gst_element_factory_make("h264parse", nullptr);
        GstElement* parserCapsFilter = gst_element_factory_make("capsfilter", nullptr);
        g_object_set(parserCapsFilter, "caps", gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr), nullptr);
        g_object_set(parser, "disable-passthrough", TRUE, nullptr);
        g_object_set(parser, "config-interval", 1, nullptr);
        gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), parser, parserCapsFilter, nullptr);
        gst_element_link(m_outputSelector.get(), parser);
        gst_element_link_many(parser, parserCapsFilter, rtpvpay2, nullptr);
        gst_element_sync_state_with_parent(parser);
        gst_element_sync_state_with_parent(parserCapsFilter);
    } else
        ASSERT_NOT_REACHED();
    gst_element_link_many(rtpvpay2, q2, vcapsfilter2, nullptr);

    GRefPtr<GstPad> vpad2 = adoptGRef(gst_element_get_static_pad(vcapsfilter2, "src"));
    GRefPtr<GstPad> vsink2 = adoptGRef(gst_element_get_request_pad(webrtcBin.get(), "sink_%u"));
    gst_pad_link(vpad2.get(), vsink2.get());
#endif

    gst_element_sync_state_with_parent(videoSourceElement);
    gst_element_sync_state_with_parent(m_outputSelector.get());
#if 1
    gst_element_sync_state_with_parent(videoconvert1);
    gst_element_sync_state_with_parent(venc1);
    gst_element_sync_state_with_parent(rtpvpay1);
    gst_element_sync_state_with_parent(q1);
    gst_element_sync_state_with_parent(vcapsfilter1);
#endif
#if 0
    gst_element_sync_state_with_parent(rtpvpay2);
    gst_element_sync_state_with_parent(q2);
    gst_element_sync_state_with_parent(vcapsfilter2);
#endif
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-video");
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
