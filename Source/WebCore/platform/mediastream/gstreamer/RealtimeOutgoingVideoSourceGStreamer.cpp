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

#include "MediaSampleGStreamer.h"
#include <gst/app/gstappsrc.h>

namespace WebCore {

RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(Ref<MediaStreamTrackPrivate>&& source, GstElement* pipeline)
    : RealtimeOutgoingMediaSourceGStreamer(WTFMove(source), pipeline)
{
    m_source->addObserver(*this);
    initializeFromSource();
}

void RealtimeOutgoingVideoSourceGStreamer::setPayloadType(int payloadType)
{
    g_object_set(m_payloader.get(), "pt", payloadType, nullptr);
    m_caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "video",
        "encoding-name", G_TYPE_STRING, m_encodingName, "payload", G_TYPE_INT, payloadType, nullptr));
    g_object_set(m_capsFilter.get(), "caps", m_caps.get(), nullptr);
}

void RealtimeOutgoingVideoSourceGStreamer::synchronizeStates()
{
    gst_element_sync_state_with_parent(m_outgoingSource.get());
    gst_element_sync_state_with_parent(m_valve.get());
    gst_element_sync_state_with_parent(m_videoConvert.get());
    gst_element_sync_state_with_parent(m_preEncoderQueue.get());
    gst_element_sync_state_with_parent(m_encoder.get());
    gst_element_sync_state_with_parent(m_payloader.get());
    gst_element_sync_state_with_parent(m_postEncoderQueue.get());
    gst_element_sync_state_with_parent(m_capsFilter.get());
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-video");
}

void RealtimeOutgoingVideoSourceGStreamer::initialize()
{
#define USE_VP9 1
#if USE(VP9)
    m_encodingName = "VP9";
    GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("video/x-vp9"));
    m_payloader = gst_element_factory_make("rtpvp9pay", nullptr);
#else
    m_encodingName = "VP8";
    GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("video/x-vp8"));
    m_payloader = gst_element_factory_make("rtpvp8pay", nullptr);
#endif

    m_caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "video",
        "encoding-name", G_TYPE_STRING, m_encodingName, nullptr));
    m_videoConvert = gst_element_factory_make("videoconvert", nullptr);
    m_encoder = gst_element_factory_make("webrtcvideoencoder", nullptr);
    g_object_set(m_encoder.get(), "format", caps.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_videoConvert.get(), m_encoder.get(), m_payloader.get(), nullptr);

    gst_element_link_many(m_outgoingSource.get(), m_valve.get(), m_videoConvert.get(), m_preEncoderQueue.get(),
        m_encoder.get(), m_payloader.get(), m_postEncoderQueue.get(), nullptr);
}

void RealtimeOutgoingVideoSourceGStreamer::sampleBufferUpdated(MediaStreamTrackPrivate&, MediaSample& mediaSample)
{
    ASSERT(mediaSample.platformSample().type == PlatformSample::GStreamerSampleType);
    auto gstSample = static_cast<MediaSampleGStreamer*>(&mediaSample)->platformSample().sample.gstSample;
    gst_app_src_push_sample(GST_APP_SRC(m_outgoingSource.get()), gstSample);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
