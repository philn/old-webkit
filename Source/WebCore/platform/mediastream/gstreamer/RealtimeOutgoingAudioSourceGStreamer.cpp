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

GST_DEBUG_CATEGORY_EXTERN(webkit_webrtc_endpoint_debug);
#define GST_CAT_DEFAULT webkit_webrtc_endpoint_debug

namespace WebCore {

RealtimeOutgoingAudioSourceGStreamer::RealtimeOutgoingAudioSourceGStreamer(Ref<MediaStreamTrackPrivate>&& source)
    : RealtimeOutgoingMediaSourceGStreamer()
{
    m_allowedCaps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "audio",
        "encoding-name", G_TYPE_STRING, "OPUS", nullptr));

    m_audioconvert = gst_element_factory_make("audioconvert", nullptr);
    m_audioresample = gst_element_factory_make("audioresample", nullptr);

    m_encoder = gst_element_factory_make("opusenc", nullptr);
    gst_util_set_object_arg(G_OBJECT(m_encoder.get()), "audio-type", "voice");
    // FIXME: Enable dtx too?

    m_payloader = gst_element_factory_make("rtpopuspay", nullptr);

    // FIXME: Re-enable this. Currently triggers caps negotiation error.
    g_object_set(m_payloader.get(), "auto-header-extension", FALSE, nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_bin.get()), m_audioconvert.get(), m_audioresample.get(), m_encoder.get(), m_payloader.get(), nullptr);
    setSource(WTFMove(source));
}

void RealtimeOutgoingAudioSourceGStreamer::setPayloadType(GRefPtr<GstCaps>& caps)
{
    GST_DEBUG_OBJECT(m_bin.get(), "Outgoing audio source payload caps: %" GST_PTR_FORMAT, caps.get());

    int payloadType;
    GUniqueOutPtr<char> encodingName;
    auto* structure = gst_caps_get_structure(caps.get(), 0);
    gst_structure_get(structure, "payload", G_TYPE_INT, &payloadType, "encoding-name", G_TYPE_STRING, &encodingName.outPtr(), nullptr);

    if (strcmp(encodingName.get(), "OPUS")) {
        GST_ERROR_OBJECT(m_bin.get(), "Unsupported outgoing audio encoding: %s", encodingName.get());
        return;
    }

    if (const char* useInbandFec = gst_structure_get_string(structure, "useinbandfec")) {
        if (!strcmp(useInbandFec, "1"))
            g_object_set(G_OBJECT(m_encoder.get()), "inband-fec", true, nullptr);
    }

    if (const char* minPTime = gst_structure_get_string(structure, "minptime")) {
        String time(minPTime);
        g_object_set(m_payloader.get(), "min-ptime", time.toInt64() * GST_MSECOND, nullptr);
    }

    g_object_set(m_payloader.get(), "pt", payloadType, nullptr);
    g_object_set(m_capsFilter.get(), "caps", caps.get(), nullptr);

    gst_element_link_many(m_outgoingSource.get(), m_valve.get(), m_audioconvert.get(), m_audioresample.get(), m_preEncoderQueue.get(), m_encoder.get(), m_payloader.get(), m_postEncoderQueue.get(), nullptr);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
