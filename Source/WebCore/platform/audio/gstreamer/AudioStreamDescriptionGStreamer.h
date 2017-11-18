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

#pragma once

#include "AudioStreamDescription.h"
#include "NotImplemented.h"
#include "GRefPtrGStreamer.h"
#include <gst/gst.h>

namespace WebCore {

class AudioStreamDescriptionGStreamer final : public AudioStreamDescription {
public:

    WEBCORE_EXPORT AudioStreamDescriptionGStreamer();
    WEBCORE_EXPORT AudioStreamDescriptionGStreamer(const GstCaps*);
    WEBCORE_EXPORT AudioStreamDescriptionGStreamer(double, uint32_t, PCMFormat, bool);
    WEBCORE_EXPORT ~AudioStreamDescriptionGStreamer();

    const PlatformDescription& platformDescription() const final;

    WEBCORE_EXPORT PCMFormat format() const final;

    double sampleRate() const final { return m_sampleRate; }
    bool isPCM() const final { return m_isPCM; }
    bool isInterleaved() const final { return m_isInterleaved; }
    bool isSignedInteger() const final { return m_isSignedInteger; }
    bool isFloat() const final { return m_isFloat; }
    bool isNativeEndian() const final { return m_isNativeEndian; }

    uint32_t numberOfInterleavedChannels() const final { return isInterleaved() ? m_channels : 1; }
    uint32_t numberOfChannelStreams() const final { return isInterleaved() ? 1 : m_channels; }
    uint32_t numberOfChannels() const final { return m_channels; }
    uint32_t sampleWordSize() const final { notImplemented(); return 0; }

private:
    GRefPtr<GstCaps> m_caps;
    GstStructure* m_structure;
    mutable PlatformDescription m_platformDescription;
    mutable PCMFormat m_format { None };
    double m_sampleRate;
    uint32_t m_channels;
    bool m_isPCM;
    bool m_isInterleaved;
    bool m_isSignedInteger;
    bool m_isFloat;
    bool m_isNativeEndian;
};

}
