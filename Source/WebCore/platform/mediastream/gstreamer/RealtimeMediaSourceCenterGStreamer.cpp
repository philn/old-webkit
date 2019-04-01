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
#include "RealtimeMediaSourceCenterGStreamer.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER) && !USE(LIBWEBRTC)

#include "CaptureDevice.h"
#include "Logging.h"
#include "MediaStream.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "RealtimeAudioSourceGStreamer.h"
#include "RealtimeMediaSourceGStreamer.h"
#include "RealtimeVideoSourceGStreamer.h"
#include "RealtimeMediaSourceGStreamer.h"
#include "RealtimeMediaSourceCapabilities.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

RealtimeMediaSourceCenterGStreamer& RealtimeMediaSourceCenterGStreamer::singleton()
{
    ASSERT(isMainThread());
    static NeverDestroyed<RealtimeMediaSourceCenterGStreamer> center;
    return center;
}

RealtimeMediaSourceCenter& RealtimeMediaSourceCenter::platformCenter()
{
    return RealtimeMediaSourceCenterGStreamer::singleton();
}

RealtimeMediaSource::AudioCaptureFactory& RealtimeMediaSourceCenterGStreamer::audioFactory()
{
    return GStreamerAudioCaptureDeviceManager::audioFactory();
}

RealtimeMediaSource::VideoCaptureFactory& RealtimeMediaSourceCenterGStreamer::videoFactory()
{
    return GStreamerVideoCaptureDeviceManager::videoFactory();
}

CaptureDeviceManager& RealtimeMediaSourceCenterGStreamer::audioCaptureDeviceManager()
{
    return GStreamerAudioCaptureDeviceManager::singleton();
}

CaptureDeviceManager& RealtimeMediaSourceCenterGStreamer::videoCaptureDeviceManager()
{
    return GStreamerVideoCaptureDeviceManager::singleton();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER) && !USE(LIBWEBRTC)
