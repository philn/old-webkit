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

#include "CaptureDeviceManager.h"
#include "GRefPtrGStreamer.h"
#include "GStreamerCaptureDevice.h"
#include "RealtimeAudioSourceGStreamer.h"
#include "RealtimeVideoSourceGStreamer.h"
#include "RealtimeMediaSourceCenter.h"

namespace WebCore {

class GStreamerCaptureDeviceManager : public CaptureDeviceManager {
public:
    virtual const Vector<CaptureDevice>& captureDevices() override;
    virtual std::optional<CaptureDevice> captureDeviceWithPersistentID(CaptureDevice::DeviceType, const String&) override;
    std::optional<GStreamerCaptureDevice> gstreamerDeviceWithUID(const String&);

    void refreshCaptureDevices() final;
    virtual CaptureDevice::DeviceType deviceType() = 0;

private:
    void deviceAdded(GstDevice*);

    GRefPtr<GstDeviceMonitor> m_deviceMonitor;
    Vector<GStreamerCaptureDevice> m_gstreamerDevices;
    Vector<CaptureDevice> m_devices;
};

class GStreamerAudioCaptureDeviceManager final : public GStreamerCaptureDeviceManager {
    friend class NeverDestroyed<GStreamerAudioCaptureDeviceManager>;
public:
    static GStreamerAudioCaptureDeviceManager& singleton();
    static RealtimeMediaSource::AudioCaptureFactory& audioFactory();
    CaptureDevice::DeviceType deviceType() final { return CaptureDevice::DeviceType::Microphone; }
private:
    GStreamerAudioCaptureDeviceManager() = default;
    ~GStreamerAudioCaptureDeviceManager() = default;
};

class GStreamerVideoCaptureDeviceManager final : public GStreamerCaptureDeviceManager {
    friend class NeverDestroyed<GStreamerVideoCaptureDeviceManager>;
public:
    static GStreamerVideoCaptureDeviceManager& singleton();
    static RealtimeMediaSource::VideoCaptureFactory& videoFactory();
    CaptureDevice::DeviceType deviceType() final { return CaptureDevice::DeviceType::Camera; }
private:
    GStreamerVideoCaptureDeviceManager() = default;
    ~GStreamerVideoCaptureDeviceManager() = default;
};

}

#endif // ENABLE(MEDIA_STREAM)  && USE(GSTREAMER)
