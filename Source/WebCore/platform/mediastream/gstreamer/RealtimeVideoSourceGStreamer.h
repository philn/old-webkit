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

#include "RealtimeMediaSourceGStreamer.h"
#include <gst/gst.h>

namespace WebCore {

class GStreamerRealtimeVideoSource : public GStreamerRealtimeMediaSource {
public:
    enum VideoFormat {
        Raw,
        H264
    };

    static CaptureSourceOrError create(const CaptureDevice&, const MediaConstraints*);
    virtual GstCaps* caps() const final { return m_caps.get(); }

    VideoFormat videoFormat() const { return m_format; }

    static VideoCaptureFactory& factory();

protected:
    GStreamerRealtimeVideoSource(const CaptureDevice&);
    bool applySize(const IntSize&) override;

private:
    void updateSettings(RealtimeMediaSourceSettings&) override;
    void initializeCapabilities(RealtimeMediaSourceCapabilities&) override;
    void initializeSupportedConstraints(RealtimeMediaSourceSupportedConstraints&) override;

    bool applyFrameRate(double) override;
    bool applyFacingMode(RealtimeMediaSourceSettings::VideoFacingMode) override { return true; }
    bool applyAspectRatio(double) override { return true; }
    bool isCaptureSource() const final { return true; }

    VideoFormat m_format { VideoFormat::Raw };
    GRefPtr<GstCaps> m_caps;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)  && USE(GSTREAMER)
