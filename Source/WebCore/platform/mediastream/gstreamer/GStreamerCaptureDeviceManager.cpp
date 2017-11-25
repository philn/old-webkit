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
#include "GStreamerCaptureDeviceManager.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GStreamerUtilities.h"

namespace WebCore {

GStreamerAudioCaptureDeviceManager& GStreamerAudioCaptureDeviceManager::singleton()
{
    static NeverDestroyed<GStreamerAudioCaptureDeviceManager> manager;
    return manager;
}

GStreamerVideoCaptureDeviceManager& GStreamerVideoCaptureDeviceManager::singleton()
{
    static NeverDestroyed<GStreamerVideoCaptureDeviceManager> manager;
    return manager;
}

std::optional<CaptureDevice> GStreamerCaptureDeviceManager::captureDeviceWithPersistentID(CaptureDevice::DeviceType, const String& deviceID)
{
    initializeGStreamer();
    if (m_gstreamerDevices.isEmpty())
        refreshCaptureDevices();

    for (auto& device : m_devices) {
        if (device.persistentId() == deviceID)
            return device;
    }
    return std::nullopt;
}

std::optional<GStreamerCaptureDevice> GStreamerCaptureDeviceManager::gstreamerDeviceWithUID(const String& deviceID)
{
    initializeGStreamer();
    if (m_gstreamerDevices.isEmpty())
        refreshCaptureDevices();

    for (auto& device : m_gstreamerDevices) {
        if (device.persistentId() == deviceID)
            return device;
    }
    return std::nullopt;
}

const Vector<CaptureDevice>& GStreamerCaptureDeviceManager::captureDevices()
{
    initializeGStreamer();
    if (m_devices.isEmpty())
        refreshCaptureDevices();

    return m_devices;
}

void GStreamerCaptureDeviceManager::deviceAdded(GstDevice* device)
{
    GstStructure* properties = gst_device_get_properties(device);
    const gchar* klass = gst_structure_get_string(properties, "device.class");

    if (klass && !g_strcmp0(klass, "monitor"))
        return;

    CaptureDevice::DeviceType type = deviceType();
    String deviceClass = gst_device_get_device_class(device);
    if (type == CaptureDevice::DeviceType::Microphone && !deviceClass.startsWith("Audio"))
        return;
    if (type == CaptureDevice::DeviceType::Camera && !deviceClass.startsWith("Video"))
        return;

    // FIXME: This isn't uniform across platforms.
    const gchar* identifier = gst_structure_get_string(properties, "device.serial");
    if (!identifier)
        identifier = gst_structure_get_string(properties, "device.product.name");

    String name = gst_device_get_display_name(device);
    auto gstCaptureDevice = GStreamerCaptureDevice(device, identifier, type, name);
    gstCaptureDevice.setEnabled(true);
    m_gstreamerDevices.append(WTFMove(gstCaptureDevice));
    auto captureDevice = CaptureDevice(identifier, type, name);
    captureDevice.setEnabled(true);
    m_devices.append(WTFMove(captureDevice));
}

void GStreamerCaptureDeviceManager::refreshCaptureDevices()
{
    if (!m_deviceMonitor) {
        m_deviceMonitor = adoptGRef(gst_device_monitor_new());

        CaptureDevice::DeviceType type = deviceType();
        if (type == CaptureDevice::DeviceType::Camera) {
            GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("video/x-raw"));
            gst_device_monitor_add_filter(m_deviceMonitor.get(), "Video/Source", caps.get());
            GRefPtr<GstCaps> compressedCaps = adoptGRef(gst_caps_new_empty_simple("video/x-h264"));
            gst_device_monitor_add_filter(m_deviceMonitor.get(), "Video/Source", compressedCaps.get());
        } else if (type == CaptureDevice::DeviceType::Microphone) {
            GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("audio/x-raw"));
            gst_device_monitor_add_filter(m_deviceMonitor.get(), "Audio/Source", caps.get());
        }

        // TODO: Monitor for added/removed messages on the bus.
    }

    gst_device_monitor_start(m_deviceMonitor.get());

    GList* devices = gst_device_monitor_get_devices(m_deviceMonitor.get());
    if (devices != NULL) {
        while (devices != NULL) {
            GstDevice* device = GST_DEVICE_CAST(devices->data);
            deviceAdded(device);
            gst_object_unref(device);
            devices = g_list_delete_link(devices, devices);
        }
    }

    gst_device_monitor_stop(m_deviceMonitor.get());
}


class GStreamerRealtimeAudioSourceFactory : public RealtimeMediaSource::AudioCaptureFactory
{
public:
    CaptureSourceOrError createAudioCaptureSource(const CaptureDevice& device, const MediaConstraints* constraints) final {
        auto gstDevice = GStreamerAudioCaptureDeviceManager::singleton().gstreamerDeviceWithUID(device.persistentId());
        if (!gstDevice)
            return { };
        return GStreamerRealtimeAudioSource::create(device, constraints);
    }
};

class GStreamerRealtimeVideoSourceFactory : public RealtimeMediaSource::VideoCaptureFactory
{
public:
    CaptureSourceOrError createVideoCaptureSource(const CaptureDevice& device, const MediaConstraints* constraints) final {

        auto gstDevice = GStreamerVideoCaptureDeviceManager::singleton().gstreamerDeviceWithUID(device.persistentId());
        if (!gstDevice)
            return { };
        return GStreamerRealtimeVideoSource::create(device, constraints);
    }
};

RealtimeMediaSource::AudioCaptureFactory& GStreamerAudioCaptureDeviceManager::audioFactory()
{
    static NeverDestroyed<GStreamerRealtimeAudioSourceFactory> factory;
    return factory.get();
}

RealtimeMediaSource::VideoCaptureFactory& GStreamerVideoCaptureDeviceManager::videoFactory()
{
    static NeverDestroyed<GStreamerRealtimeVideoSourceFactory> factory;
    return factory.get();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
