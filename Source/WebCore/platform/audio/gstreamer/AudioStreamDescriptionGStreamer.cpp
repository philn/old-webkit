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
#include "AudioStreamDescriptionGStreamer.h"
#include <gst/audio/audio.h>

namespace WebCore {

AudioStreamDescriptionGStreamer::AudioStreamDescriptionGStreamer()
    : m_caps(adoptGRef(gst_caps_new_empty_simple("audio/x-raw")))
{
    m_structure = gst_caps_get_structure(m_caps.get(), 0);
    m_sampleRate = 0;
    m_isPCM = true;
    m_isInterleaved = false;
    m_isSignedInteger = false;
    m_isFloat = false;
    m_isNativeEndian = true;
    m_channels = 1;
}

AudioStreamDescriptionGStreamer::~AudioStreamDescriptionGStreamer() = default;

AudioStreamDescriptionGStreamer::AudioStreamDescriptionGStreamer(const GstCaps *caps)
    : m_caps(adoptGRef(gst_caps_copy(caps)))
{
    m_structure = gst_caps_get_structure(m_caps.get(), 0);
    ASSERT(m_structure);
    m_sampleRate = 0;
    m_isPCM = true;
    m_isInterleaved = false;
    m_isSignedInteger = false;
    m_isFloat = false;
    m_isNativeEndian = true;
    m_channels = 1;

    if (!m_structure)
        return;

    int sampleRate = 0;
    if (gst_structure_get_int(m_structure, "samplerate", &sampleRate))
        m_sampleRate = sampleRate;
    int channels = 0;
    if (gst_structure_get_int(m_structure, "channels", &channels))
        m_channels = channels;
    const gchar* layout = gst_structure_get_string(m_structure, "layout");
    if (layout)
        m_isInterleaved = !g_strcmp0(layout, "interleaved");

    GstAudioFormat audioFormat;
    if (!gst_structure_get(m_structure, "format", GST_TYPE_AUDIO_FORMAT, &audioFormat, nullptr))
        return;

    const GstAudioFormatInfo* formatInfo = gst_audio_format_get_info(audioFormat);
    m_isSignedInteger = GST_AUDIO_FORMAT_INFO_IS_SIGNED(formatInfo) && GST_AUDIO_FORMAT_INFO_IS_INTEGER(formatInfo);
    m_isFloat = GST_AUDIO_FORMAT_INFO_IS_FLOAT(formatInfo);
    m_isNativeEndian = GST_AUDIO_FORMAT_INFO_ENDIANNESS(formatInfo) == G_BYTE_ORDER;
}

AudioStreamDescriptionGStreamer::AudioStreamDescriptionGStreamer(double sampleRate, uint32_t numChannels, PCMFormat format, bool isInterleaved)
{
    m_sampleRate = sampleRate;
    m_channels = numChannels;
    m_format = format;
    m_isInterleaved = isInterleaved;
    m_isNativeEndian = true;
    m_caps = adoptGRef(gst_caps_new_simple("audio/x-raw", "samplerate", G_TYPE_INT, static_cast<int>(sampleRate), "channels", G_TYPE_INT, numChannels, nullptr));
    m_structure = gst_caps_get_structure(m_caps.get(), 0);
    if (isInterleaved)
        gst_structure_set(m_structure, "layout", G_TYPE_STRING, "interleaved", nullptr);

    GstAudioFormat gstFormat;
    switch (format) {
    case Int16:
        gstFormat = GST_AUDIO_FORMAT_S16;
        m_isSignedInteger = true;
        m_isFloat = false;
        break;
    case Int32:
        gstFormat = GST_AUDIO_FORMAT_S32;
        m_isSignedInteger = true;
        m_isFloat = false;
        break;
    case Float32:
        gstFormat = GST_AUDIO_FORMAT_F32;
        m_isSignedInteger = false;
        m_isFloat = true;
        break;
    case Float64:
        gstFormat = GST_AUDIO_FORMAT_F64;
        m_isSignedInteger = false;
        m_isFloat = true;
        break;
    case None:
        ASSERT_NOT_REACHED();
        break;
    }

    gst_structure_set(m_structure, "format", GST_TYPE_AUDIO_FORMAT, gstFormat, nullptr);
}

const PlatformDescription& AudioStreamDescriptionGStreamer::platformDescription() const
{
    m_platformDescription = { PlatformDescription::GStreamerAudioStreamType, { .caps = m_caps.get() } };
    return m_platformDescription;
}

AudioStreamDescription::PCMFormat AudioStreamDescriptionGStreamer::format() const
{
    if (m_format != None)
        return m_format;

    if (!m_structure)
        return None;

    GstAudioFormat audioFormat;
    if (!gst_structure_get(m_structure, "format", GST_TYPE_AUDIO_FORMAT, &audioFormat, nullptr))
        return None;

    const GstAudioFormatInfo* formatInfo = gst_audio_format_get_info(audioFormat);
    gint width = GST_AUDIO_FORMAT_INFO_WIDTH(formatInfo);
    if (GST_AUDIO_FORMAT_INFO_IS_INTEGER(formatInfo) && GST_AUDIO_FORMAT_INFO_IS_SIGNED(formatInfo)) {
        if (width == 16)
            return m_format = Int16;
        else if (width == 32)
            return m_format = Int32;
    } else if (GST_AUDIO_FORMAT_INFO_IS_FLOAT(formatInfo)) {
        if (width == 32)
            return m_format = Float32;
        else if (width == 64)
            return m_format = Float64;
    }

    return None;
}

}
