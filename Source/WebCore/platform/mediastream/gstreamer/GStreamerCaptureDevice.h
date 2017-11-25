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

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "CaptureDevice.h"
#include "GRefPtrGStreamer.h"

#include <gst/gst.h>

namespace WebCore {

class GStreamerCaptureDevice : public CaptureDevice {
public:
    GStreamerCaptureDevice(GstDevice* device, const String& persistentId, DeviceType type, const String& label, const String& groupId = emptyString())
        : CaptureDevice(persistentId, type, label, groupId)
    {
        m_device = device;
    }

    GstElement* gstSourceElement() const { return gst_device_create_element(m_device.get(), nullptr); }
    GstCaps* caps() const { return gst_device_get_caps(m_device.get()); }

private:
    GRefPtr<GstDevice> m_device;
};

}

#endif // ENABLE(MEDIA_STREAM)  && USE(GSTREAMER)
