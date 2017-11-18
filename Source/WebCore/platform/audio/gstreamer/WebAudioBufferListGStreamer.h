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

#include "PlatformAudioData.h"
#include "GRefPtrGStreamer.h"
#include <gst/gst.h>

namespace WebCore {

class WebAudioBufferListGStreamer : public PlatformAudioData {
public:
    WebAudioBufferListGStreamer(GstBuffer* buffer)
        : m_buffer(gst_buffer_ref(buffer))
        { }
    GstBuffer* buffer() const { return m_buffer.get(); }
private:
    Kind kind() const { return Kind::WebAudioBufferList; }
    GRefPtr<GstBuffer> m_buffer;
};

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::WebAudioBufferListGStreamer)
static bool isType(const WebCore::PlatformAudioData& data) { return data.kind() == WebCore::PlatformAudioData::Kind::WebAudioBufferList; }
SPECIALIZE_TYPE_TRAITS_END()
