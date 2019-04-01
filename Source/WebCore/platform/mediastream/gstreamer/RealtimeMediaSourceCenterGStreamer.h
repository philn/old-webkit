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

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER) && !USE(LIBWEBRTC)

#include "GStreamerCaptureDeviceManager.h"
#include "RealtimeAudioSourceGStreamer.h"
#include "RealtimeVideoSourceGStreamer.h"
#include "RealtimeMediaSourceCenter.h"

namespace WebCore {

class RealtimeMediaSourceCenterGStreamer final : public RealtimeMediaSourceCenter {
public:
    WEBCORE_EXPORT static RealtimeMediaSourceCenterGStreamer& singleton();

    static RealtimeMediaSource::VideoCaptureFactory& videoCaptureSourceFactory();
    static RealtimeMediaSource::AudioCaptureFactory& audioCaptureSourceFactory();

private:
    RealtimeMediaSourceCenterGStreamer() = default;
    friend NeverDestroyed<RealtimeMediaSourceCenterGStreamer>;

    RealtimeMediaSource::AudioCaptureFactory& audioFactory() final;
    RealtimeMediaSource::VideoCaptureFactory& videoFactory() final;

    CaptureDeviceManager& audioCaptureDeviceManager() final;
    CaptureDeviceManager& videoCaptureDeviceManager() final;
};

}

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER) && !USE(LIBWEBRTC)
