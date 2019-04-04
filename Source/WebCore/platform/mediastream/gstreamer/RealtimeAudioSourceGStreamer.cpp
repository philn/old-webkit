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
#include "RealtimeAudioSourceGStreamer.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "GStreamerCaptureDeviceManager.h"
#include "CaptureDevice.h"
#include "Logging.h"
#include "MediaConstraints.h"
#include "NotImplemented.h"
#include "RealtimeMediaSourceSettings.h"
#include <wtf/UUID.h>

namespace WebCore {

class GStreamerRealtimeAudioSourceFactory : public RealtimeMediaSource::AudioCaptureFactory
{
public:
    CaptureSourceOrError createAudioCaptureSource(const CaptureDevice& device, const MediaConstraints* constraints) final {
        return GStreamerRealtimeAudioSource::create(device, constraints);
    }
};

    CaptureSourceOrError GStreamerRealtimeAudioSource::create(const CaptureDevice& device, const MediaConstraints* constraints)
{
    auto source = adoptRef(*new GStreamerRealtimeAudioSource(device));
    if (constraints && source->applyConstraints(*constraints))
        return { };

    return CaptureSourceOrError(WTFMove(source));
}

static GStreamerRealtimeAudioSourceFactory& gstreamerAudioCaptureSourceFactory()
{
    static NeverDestroyed<GStreamerRealtimeAudioSourceFactory> factory;
    return factory.get();
}

RealtimeMediaSource::AudioCaptureFactory& GStreamerRealtimeAudioSource::factory()
{
    return gstreamerAudioCaptureSourceFactory();
}

GStreamerRealtimeAudioSource::GStreamerRealtimeAudioSource(const CaptureDevice& device)
    : GStreamerRealtimeMediaSource(RealtimeMediaSource::Type::Audio, device.label())
{
    auto gstDevice = GStreamerAudioCaptureDeviceManager::singleton().gstreamerDeviceWithUID(device.persistentId());
    if (device) {
        setGstSourceElement(gstDevice->gstSourceElement());
        setName(device.label());
    }
    m_caps = adoptGRef(gst_caps_new_empty_simple("audio/x-raw"));
}

// void GStreamerRealtimeAudioSource::updateSettings(RealtimeMediaSourceSettings& settings)
// {
//     settings.setVolume(volume());
//     settings.setEchoCancellation(echoCancellation());
//     settings.setSampleRate(sampleRate());
// }

// void GStreamerRealtimeAudioSource::initializeCapabilities(RealtimeMediaSourceCapabilities& capabilities)
// {
//     capabilities.setVolume(CapabilityValueOrRange(0.0, 1.0));
//     capabilities.setEchoCancellation(RealtimeMediaSourceCapabilities::EchoCancellation::ReadWrite);
//     capabilities.setSampleRate(CapabilityValueOrRange(44100, 48000));
// }

// void GStreamerRealtimeAudioSource::initializeSupportedConstraints(RealtimeMediaSourceSupportedConstraints& supportedConstraints)
// {
//     supportedConstraints.setSupportsVolume(true);
//     supportedConstraints.setSupportsEchoCancellation(true);
//     supportedConstraints.setSupportsSampleRate(true);
// }

double GStreamerRealtimeAudioSource::elapsedTime()
{
    if (std::isnan(m_startTime))
        return m_elapsedTime;

    return m_elapsedTime + (monotonicallyIncreasingTime() - m_startTime);
}

void GStreamerRealtimeAudioSource::tick()
{
    if (std::isnan(m_lastRenderTime))
        m_lastRenderTime = monotonicallyIncreasingTime();

    double now = monotonicallyIncreasingTime();

    if (m_delayUntil) {
        if (m_delayUntil < now)
            return;
        m_delayUntil = 0;
    }

    double delta = now - m_lastRenderTime;
    m_lastRenderTime = now;
    render(delta);
}

void GStreamerRealtimeAudioSource::delaySamples(float delta)
{
    m_delayUntil = monotonicallyIncreasingTime() + delta;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
