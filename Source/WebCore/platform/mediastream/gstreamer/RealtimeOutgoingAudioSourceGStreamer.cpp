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

#include "GStreamerAudioCaptureSource.h"
#include "GStreamerMediaStreamSource.h"

GST_DEBUG_CATEGORY_EXTERN(webkit_webrtc_endpoint_debug);

namespace WebCore {

RealtimeOutgoingAudioSourceGStreamer::RealtimeOutgoingAudioSourceGStreamer(Ref<MediaStreamTrackPrivate>&& source, GstElement* pipeline)
    : RealtimeOutgoingMediaSourceGStreamer(pipeline)
{
    m_allowedCaps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "audio",
        "encoding-name", G_TYPE_STRING, "OPUS", nullptr));

    setSource(WTFMove(source));
}

void RealtimeOutgoingAudioSourceGStreamer::setPayloadType(GRefPtr<GstCaps>&& caps)
{
    m_caps = WTFMove(caps);
    GST_DEBUG_OBJECT(m_pipeline.get(), "Outgoing audio source payload caps: %" GST_PTR_FORMAT, m_caps.get());

    int payloadType;
    GUniqueOutPtr<char> encodingName;
    auto* structure = gst_caps_get_structure(m_caps.get(), 0);
    gst_structure_get(structure, "payload", G_TYPE_INT, &payloadType, "encoding-name", G_TYPE_STRING, &encodingName.outPtr(), nullptr);

    if (strcmp(encodingName.get(), "OPUS")) {
        GST_ERROR_OBJECT(m_pipeline.get(), "Unsupported outgoing audio encoding: %s", encodingName.get());
        return;
    }

    g_object_set(m_payloader.get(), "pt", payloadType, nullptr);
    g_object_set(m_capsFilter.get(), "caps", m_caps.get(), nullptr);

    gst_element_link_many(m_outgoingSource.get(), m_valve.get(), m_audioconvert.get(), m_audioresample.get(), m_preEncoderQueue.get(), m_encoder.get(), m_payloader.get(), m_postEncoderQueue.get(), nullptr);
}

void RealtimeOutgoingAudioSourceGStreamer::initialize()
{
    m_outgoingSource = webkitMediaStreamSrcNew();
    webkitMediaStreamSrcAddTrack(WEBKIT_MEDIA_STREAM_SRC(m_outgoingSource.get()), m_source.value().ptr(), true);

    m_audioconvert = gst_element_factory_make("audioconvert", nullptr);
    m_audioresample = gst_element_factory_make("audioresample", nullptr);

    m_encoder = gst_element_factory_make("opusenc", nullptr);
    gst_util_set_object_arg(G_OBJECT(m_encoder.get()), "audio-type", "voice");
    // FIXME: Enable dtx and inband-fec too?

    m_payloader = gst_element_factory_make("rtpopuspay", nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_outgoingSource.get(), m_audioconvert.get(), m_audioresample.get(), m_encoder.get(), m_payloader.get(), nullptr);
}

void RealtimeOutgoingAudioSourceGStreamer::synchronizeStates()
{
    gst_element_sync_state_with_parent(m_outgoingSource.get());
    gst_element_sync_state_with_parent(m_valve.get());
    gst_element_sync_state_with_parent(m_audioconvert.get());
    gst_element_sync_state_with_parent(m_audioresample.get());
    gst_element_sync_state_with_parent(m_preEncoderQueue.get());
    gst_element_sync_state_with_parent(m_encoder.get());
    gst_element_sync_state_with_parent(m_payloader.get());
    gst_element_sync_state_with_parent(m_postEncoderQueue.get());
    gst_element_sync_state_with_parent(m_capsFilter.get());
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-audio");
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
