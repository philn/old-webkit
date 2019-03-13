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

#include "GStreamerVideoCaptureSource.h"

GST_DEBUG_CATEGORY_EXTERN(webkit_webrtc_endpoint_debug);
#define GST_CAT_DEFAULT webkit_webrtc_endpoint_debug

namespace WebCore {

RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(Ref<MediaStreamTrackPrivate>&& source)
    : RealtimeOutgoingMediaSourceGStreamer()
{
    m_allowedCaps = adoptGRef(gst_caps_new_empty());
    // FIXME: Add VP9 here, if we manage to make the encoder less CPU hungry.
    auto encodings = { "VP8", "H264" };
    // TODO: Probe for encoder before advertizing as supported.
    for (auto& encoding : encodings)
        gst_caps_append_structure(m_allowedCaps.get(), gst_structure_new("application/x-rtp", "media", G_TYPE_STRING, "video",
            "encoding-name", G_TYPE_STRING, encoding, nullptr));

    m_videoConvert = gst_element_factory_make("videoconvert", nullptr);
    m_encoder = gst_element_factory_make("webrtcvideoencoder", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_bin.get()), m_videoConvert.get(), m_encoder.get(), nullptr);

    setSource(WTFMove(source));
}

void RealtimeOutgoingVideoSourceGStreamer::setPayloadType(GRefPtr<GstCaps>& caps)
{
    GST_DEBUG_OBJECT(m_bin.get(), "Outgoing video source payload caps: %" GST_PTR_FORMAT, caps.get());

    int payloadType;
    GUniqueOutPtr<char> encodingName;
    auto* structure = gst_caps_get_structure(caps.get(), 0);
    gst_structure_get(structure, "payload", G_TYPE_INT, &payloadType, "encoding-name", G_TYPE_STRING, &encodingName.outPtr(), nullptr);

    GRefPtr<GstCaps> encoderCaps;
    if (!strcmp(encodingName.get(), "VP8")) {
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-vp8"));
        m_payloader = gst_element_factory_make("rtpvp8pay", nullptr);
    } else if (!strcmp(encodingName.get(), "VP9")) {
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-vp9"));
        m_payloader = gst_element_factory_make("rtpvp9pay", nullptr);
    } else if (!strcmp(encodingName.get(), "H264")) {
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-h264"));
        m_payloader = gst_element_factory_make("rtph264pay", nullptr);
        gst_util_set_object_arg(G_OBJECT(m_payloader.get()), "aggregate-mode", "zero-latency");
        g_object_set(m_payloader.get(), "config-interval", -1, nullptr);
    } else {
        GST_ERROR_OBJECT(m_bin.get(), "Unsupported outgoing video encoding: %s", encodingName.get());
        return;
    }

    g_object_set(m_payloader.get(), "pt", payloadType, nullptr);
    g_object_set(m_capsFilter.get(), "caps", caps.get(), nullptr);
    g_object_set(m_encoder.get(), "format", encoderCaps.get(), nullptr);

    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_payloader.get());
    gst_element_link_many(m_outgoingSource.get(), m_valve.get(), m_videoConvert.get(), m_preEncoderQueue.get(),
        m_encoder.get(), m_payloader.get(), m_postEncoderQueue.get(), nullptr);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
