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

#include "ImageBuffer.h"
#include "RealtimeMediaSourceGStreamer.h"
#include <wtf/RunLoop.h>
#include <gst/gst.h>

namespace WebCore {

class GStreamerRealtimeAudioSource : public GStreamerRealtimeMediaSource {
public:

    static CaptureSourceOrError create(const CaptureDevice&, const MediaConstraints*);

    static AudioCaptureFactory& factory();
    virtual GstCaps* caps() const final { return m_caps.get(); }

protected:
    GStreamerRealtimeAudioSource(const CaptureDevice&);

    virtual void render(double) { }

    double elapsedTime();
    static Seconds renderInterval() { return 60_ms; }

private:

    /* bool applyVolume(double) override { return true; } */
    /* bool applySampleRate(int) override { return true; } */
    /* bool applySampleSize(int) override { return true; } */
    /* bool applyEchoCancellation(bool) override { return true; } */

    /* void updateSettings(RealtimeMediaSourceSettings&) override; */
    /* void initializeCapabilities(RealtimeMediaSourceCapabilities&) override; */
    /* void initializeSupportedConstraints(RealtimeMediaSourceSupportedConstraints&) override; */

    void tick();

    bool isCaptureSource() const final { return true; }

    /* void delaySamples(float) final; */

    /* RunLoop::Timer<GStreamerRealtimeAudioSource> m_timer; */
    double m_startTime { NAN };
    double m_lastRenderTime { NAN };
    double m_elapsedTime { 0 };
    double m_delayUntil { 0 };
    GRefPtr<GstCaps> m_caps;

};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
