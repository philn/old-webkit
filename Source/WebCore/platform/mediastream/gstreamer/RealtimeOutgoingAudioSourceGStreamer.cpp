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
#include "RealtimeOutgoingAudioSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerAudioData.h"
#include <gst/app/gstappsrc.h>

namespace WebCore {

RealtimeOutgoingAudioSourceGStreamer::RealtimeOutgoingAudioSourceGStreamer(Ref<MediaStreamTrackPrivate>&& audioSource, GstElement* pipeline)
    : RealtimeOutgoingMediaSourceGStreamer(WTFMove(audioSource), pipeline)
{
    m_source->addObserver(*this);
    initializeFromSource();
}

void RealtimeOutgoingAudioSourceGStreamer::setPayloadType(int payloadType)
{
    g_object_set(m_payloader.get(), "pt", payloadType, nullptr);
    m_caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "audio",
                                           "payload", G_TYPE_INT, payloadType,
                                           "encoding-name", G_TYPE_STRING, "OPUS", nullptr));
    g_object_set(m_capsFilter.get(), "caps", m_caps.get(), nullptr);
}

void RealtimeOutgoingAudioSourceGStreamer::initialize()
{
    m_audioconvert = gst_element_factory_make("audioconvert", nullptr);
    m_audioresample = gst_element_factory_make("audioresample", nullptr);

// #define _OPUS
// #ifdef _OPUS
    m_encoder = gst_element_factory_make("opusenc", nullptr);
    m_payloader = gst_element_factory_make("rtpopuspay", nullptr);
    m_caps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "audio",
                                           "encoding-name", G_TYPE_STRING, "OPUS", nullptr));

// #else
//     GstElement* aenc = gst_element_factory_make("alawenc", nullptr);
//     GstElement* rtpapay = gst_element_factory_make("rtppcmapay", nullptr);
//     GRefPtr<GstCaps> acaps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "audio",
//                                                            "payload", G_TYPE_INT, 111,
//                                                            "encoding-name", G_TYPE_STRING, "PCMA", nullptr));
//     g_object_set(rtpapay, "pt", 111, nullptr);
// #endif

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_audioconvert.get(), m_audioresample.get(), m_encoder.get(), m_payloader.get(), nullptr);
    gst_element_link_many(m_outgoingSource.get(), m_audioconvert.get(), m_audioresample.get(), m_preEncoderQueue.get(), m_encoder.get(), m_payloader.get(), m_postEncoderQueue.get(), nullptr);
}

void RealtimeOutgoingAudioSourceGStreamer::synchronizeStates()
{
    gst_element_sync_state_with_parent(m_outgoingSource.get());
    gst_element_sync_state_with_parent(m_audioconvert.get());
    gst_element_sync_state_with_parent(m_audioresample.get());
    gst_element_sync_state_with_parent(m_preEncoderQueue.get());
    gst_element_sync_state_with_parent(m_encoder.get());
    gst_element_sync_state_with_parent(m_payloader.get());
    gst_element_sync_state_with_parent(m_postEncoderQueue.get());
    gst_element_sync_state_with_parent(m_capsFilter.get());
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-audio");
}

void RealtimeOutgoingAudioSourceGStreamer::audioSamplesAvailable(MediaStreamTrackPrivate&, const MediaTime&, const PlatformAudioData& audioData, const AudioStreamDescription&, size_t)
{
    auto gstAudioData = static_cast<const GStreamerAudioData&>(audioData);
    gst_app_src_push_sample(GST_APP_SRC(m_outgoingSource.get()), gstAudioData.getSample());
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
