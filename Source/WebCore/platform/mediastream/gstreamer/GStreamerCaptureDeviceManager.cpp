/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro <alex@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "GStreamerCaptureDeviceManager.h"

#include "GStreamerCommon.h"

namespace WebCore {

static gint sortDevices(gconstpointer a, gconstpointer b)
{
    GstDevice* adev = GST_DEVICE(a), *bdev = GST_DEVICE(b);
    GUniquePtr<GstStructure> aprops(gst_device_get_properties(adev));
    GUniquePtr<GstStructure> bprops(gst_device_get_properties(bdev));
    gboolean aIsDefault = FALSE, bIsDefault = FALSE;

    gst_structure_get_boolean(aprops.get(), "is-default", &aIsDefault);
    gst_structure_get_boolean(bprops.get(), "is-default", &bIsDefault);

    if (aIsDefault == bIsDefault) {
        GUniquePtr<char> aName(gst_device_get_display_name(adev));
        GUniquePtr<char> bName(gst_device_get_display_name(bdev));

        return g_strcmp0(aName.get(), bName.get());
    }

    return aIsDefault > bIsDefault ? -1 : 1;
}

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

GStreamerDisplayCaptureDeviceManager& GStreamerDisplayCaptureDeviceManager::singleton()
{
    static NeverDestroyed<GStreamerDisplayCaptureDeviceManager> manager;
    return manager;
}

Optional<GStreamerCaptureDevice> GStreamerCaptureDeviceManager::gstreamerDeviceWithUID(const String& deviceID)
{
    captureDevices();

    for (auto& device : m_gstreamerDevices) {
        if (device.persistentId() == deviceID)
            return device;
    }
    return WTF::nullopt;
}

const Vector<CaptureDevice>& GStreamerCaptureDeviceManager::captureDevices()
{
    ensureGStreamerInitialized();
    if (m_devices.isEmpty())
        refreshCaptureDevices();

    return m_devices;
}

void GStreamerCaptureDeviceManager::addDevice(GRefPtr<GstDevice>&& device)
{
    GUniquePtr<GstStructure> properties(gst_device_get_properties(device.get()));
    const char* klass = gst_structure_get_string(properties.get(), "device.class");

    if (klass && !g_strcmp0(klass, "monitor"))
        return;

    CaptureDevice::DeviceType type = deviceType();
    GUniquePtr<char> deviceClassChar(gst_device_get_device_class(device.get()));
    String deviceClass(String(deviceClassChar.get()));
    if (type == CaptureDevice::DeviceType::Microphone && !deviceClass.startsWith("Audio"))
        return;
    if (type == CaptureDevice::DeviceType::Camera && !deviceClass.startsWith("Video"))
        return;

    // This isn't really a UID but should be good enough (libwebrtc
    // itself does that at least for pulseaudio devices).
    GUniquePtr<char> deviceName(gst_device_get_display_name(device.get()));
    gboolean isDefault = FALSE;
    gst_structure_get_boolean(properties.get(), "is-default", &isDefault);

    String identifier = makeString(isDefault ? "default: " : "", deviceName.get());

    auto gstCaptureDevice = GStreamerCaptureDevice(WTFMove(device), identifier, type, identifier);
    gstCaptureDevice.setEnabled(true);
    m_gstreamerDevices.append(WTFMove(gstCaptureDevice));
    // FIXME: We need a CaptureDevice copy in other vector just for captureDevices API.
    auto captureDevice = CaptureDevice(identifier, type, identifier);
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
        } else if (type == CaptureDevice::DeviceType::Microphone) {
            GRefPtr<GstCaps> caps = adoptGRef(gst_caps_new_empty_simple("audio/x-raw"));
            gst_device_monitor_add_filter(m_deviceMonitor.get(), "Audio/Source", caps.get());
        } else
            return;
    }

    // FIXME: Add monitor for added/removed messages on the bus.
    if (!gst_device_monitor_start(m_deviceMonitor.get())) {
        GST_WARNING_OBJECT(m_deviceMonitor.get(), "Could not start device monitor");
        m_deviceMonitor = nullptr;

        return;
    }

    GList* devices = g_list_sort(gst_device_monitor_get_devices(m_deviceMonitor.get()), sortDevices);
    while (devices) {
        GRefPtr<GstDevice> device = adoptGRef(GST_DEVICE_CAST(devices->data));

        addDevice(WTFMove(device));
        devices = g_list_delete_link(devices, devices);
    }

    gst_device_monitor_stop(m_deviceMonitor.get());
}

const Vector<CaptureDevice>& GStreamerDisplayCaptureDeviceManager::captureDevices()
{
    if (m_devices.isEmpty())
        refreshCaptureDevices();

    return m_devices;
}

#if USE(LIBPORTAL)
static void session_created(GObject* source, GAsyncResult* result, gpointer)
{
    auto* portal = XDP_PORTAL(source);
    GStreamerDisplayCaptureDeviceManager& manager = GStreamerDisplayCaptureDeviceManager::singleton();
    g_autoptr(GError) error = NULL;

    g_printerr("session started\n");
    manager.setSession(xdp_portal_create_screencast_session_finish(portal, result, &error));
}

static void session_started(GObject* source, GAsyncResult* result, gpointer)
{
    auto* session = XDP_SESSION(source);
    g_autoptr(GError) error = NULL;
    guint id;
    g_autoptr(GVariantIter) iter = NULL;
    GVariant* props;
    GStreamerDisplayCaptureDeviceManager& manager = GStreamerDisplayCaptureDeviceManager::singleton();

    g_printerr("hello\n");
    if (!xdp_session_start_finish(session, result, &error)) {
        g_warning("Failed to start screencast session: %s", error->message);
        manager.sessionStarted(WTF::nullopt);
        return;
    }
    iter = g_variant_iter_new(xdp_session_get_streams(session));
    g_variant_iter_next(iter, "(u@a{sv})", &id, &props);
    g_variant_unref(props);
    manager.sessionStarted(id);
}
#endif

void GStreamerDisplayCaptureDeviceManager::refreshCaptureDevices()
{
#if USE(LIBPORTAL)
    g_printerr("refreshCaptureDevices 0\n");
    m_portal = xdp_portal_new();
    g_printerr("refreshCaptureDevices 1\n");
    xdp_portal_create_screencast_session(m_portal, static_cast<XdpOutputType>(XDP_OUTPUT_MONITOR | XDP_OUTPUT_WINDOW), XDP_SCREENCAST_FLAG_NONE, NULL, session_created, nullptr);
    g_printerr("refreshCaptureDevices 2\n");

    CaptureDevice captureDevice("Dummy", CaptureDevice::DeviceType::Screen, makeString("Screen"));
    captureDevice.setEnabled(true);
    m_devices.append(WTFMove(captureDevice));

    // {
    //     LockHolder lock(m_sessionMutex);
    //     m_sessionCondition.wait(m_sessionMutex);
    // }
    g_printerr("refreshCaptureDevices 3\n");
#endif
}

void GStreamerDisplayCaptureDeviceManager::sessionStarted(Optional<guint> id)
{
    LockHolder lock(m_sessionMutex);
    if (id) {
        m_devices.remove(0);
        CaptureDevice captureDevice(String::number(*id), CaptureDevice::DeviceType::Screen, makeString("Screen"));
        captureDevice.setEnabled(true);
        m_devices.append(WTFMove(captureDevice));
    }
    deviceChanged();
    m_sessionCondition.notifyOne();
}

#if USE(LIBPORTAL)
void GStreamerDisplayCaptureDeviceManager::setSession(XdpSession* session)
{
    m_session = session;
    g_printerr("session %p\n", session);
    if (!session) {
        LockHolder lock(m_sessionMutex);
        m_sessionCondition.notifyOne();
        return;
    }

    xdp_session_start(session, nullptr, nullptr, session_started, nullptr);
}
#endif

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
