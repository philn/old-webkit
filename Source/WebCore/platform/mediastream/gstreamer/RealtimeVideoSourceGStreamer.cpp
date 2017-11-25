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
#include "RealtimeVideoSourceGStreamer.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "CaptureDevice.h"
#include "GraphicsContext.h"
#include "GStreamerCaptureDeviceManager.h"
#include "MediaConstraints.h"
#include "RealtimeMediaSourceSettings.h"

namespace WebCore {

class GStreamerRealtimeVideoSourceFactory : public RealtimeMediaSource::VideoCaptureFactory
{
public:
    CaptureSourceOrError createVideoCaptureSource(const CaptureDevice& device, const MediaConstraints* constraints) final {
        return GStreamerRealtimeVideoSource::create(device, constraints);
    }
};

CaptureSourceOrError GStreamerRealtimeVideoSource::create(const CaptureDevice& device, const MediaConstraints* constraints)
{
    auto source = adoptRef(*new GStreamerRealtimeVideoSource(device));
    if (constraints && source->applyConstraints(*constraints))
        return { };

    return CaptureSourceOrError(WTFMove(source));
}

static GStreamerRealtimeVideoSourceFactory& gstreamerVideoCaptureSourceFactory()
{
    static NeverDestroyed<GStreamerRealtimeVideoSourceFactory> factory;
    return factory.get();
}

RealtimeMediaSource::VideoCaptureFactory& GStreamerRealtimeVideoSource::factory()
{
    return gstreamerVideoCaptureSourceFactory();
}

GStreamerRealtimeVideoSource::GStreamerRealtimeVideoSource(const CaptureDevice& device)
    : GStreamerRealtimeMediaSource(device.persistentId(), RealtimeMediaSource::Type::Video, device.label())
{
    auto gstDevice = GStreamerVideoCaptureDeviceManager::singleton().gstreamerDeviceWithUID(device.persistentId());
    String videoFormat("video/x-raw");
    m_format = GStreamerRealtimeVideoSource::VideoFormat::Raw;
    if (gstDevice) {
        setGstSourceElement(gstDevice->gstSourceElement());
        setName(device.label());

        // FIXME: This isn't working very well yet wrt outgoing streaming.
        if (!g_getenv("WEBKIT_WEBRTC_ENABLE_COMPRESSED_SOURCES")) {
            m_caps = adoptGRef(gst_caps_new_empty_simple(videoFormat.utf8().data()));
            return;
        }

        GRefPtr<GstCaps> caps = adoptGRef(gstDevice->caps());
        if (caps) {
            unsigned size = gst_caps_get_size(caps.get());
            for(unsigned i = 0; i < size; i++) {
                GstStructure* structure = gst_caps_get_structure(caps.get(), i);
                if (!g_strcmp0(gst_structure_get_name(structure), "video/x-h264")) {
                    videoFormat = "video/x-h264";
                    m_format = GStreamerRealtimeVideoSource::VideoFormat::H264;
                    break;
                }
            }
        }
    }

    m_caps = adoptGRef(gst_caps_new_empty_simple(videoFormat.utf8().data()));
}

void GStreamerRealtimeVideoSource::updateSettings(RealtimeMediaSourceSettings& settings)
{
    settings.setFacingMode(facingMode());
    settings.setFrameRate(frameRate());
    IntSize size = this->size();
    settings.setWidth(size.width());
    settings.setHeight(size.height());
    if (aspectRatio())
        settings.setAspectRatio(aspectRatio());
}

void GStreamerRealtimeVideoSource::initializeCapabilities(RealtimeMediaSourceCapabilities& capabilities)
{
    // FIXME: Get these from the gst device provider.
    capabilities.setWidth(CapabilityValueOrRange(320, 1920));
    capabilities.setHeight(CapabilityValueOrRange(240, 1080));
    capabilities.setFrameRate(CapabilityValueOrRange(15.0, 60.0));
    capabilities.setAspectRatio(CapabilityValueOrRange(4 / 3.0, 16 / 9.0));
}

void GStreamerRealtimeVideoSource::initializeSupportedConstraints(RealtimeMediaSourceSupportedConstraints& supportedConstraints)
{
    supportedConstraints.setSupportsWidth(true);
    supportedConstraints.setSupportsHeight(true);
    supportedConstraints.setSupportsAspectRatio(true);
    supportedConstraints.setSupportsFrameRate(true);
    supportedConstraints.setSupportsFacingMode(true);
}

bool GStreamerRealtimeVideoSource::applyFrameRate(double rate)
{
    int numerator = 0;
    int denumerator = 0;
    gst_util_double_to_fraction(rate, &numerator, &denumerator);
    if (!numerator || !denumerator)
        return true;

    GstStructure* structure = gst_caps_get_structure(m_caps.get(), 0);
    gst_structure_set(structure, "framerate", GST_TYPE_FRACTION, numerator, denumerator, nullptr);
    return true;
}

bool GStreamerRealtimeVideoSource::applySize(const IntSize& size)
{
    GstStructure* structure = gst_caps_get_structure(m_caps.get(), 0);

    if (size.width() > 0)
        gst_structure_set(structure, "width", G_TYPE_INT, size.width(), nullptr);
    if (size.height() > 0)
        gst_structure_set(structure, "height", G_TYPE_INT, size.height(), nullptr);
    return true;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
