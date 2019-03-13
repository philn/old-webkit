/*
 *  Copyright (C) 2019 Igalia S.L. All rights reserved.
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
#include "GStreamerRtpReceiverBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerRtpReceiverTransformBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "NotImplemented.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

RTCRtpParameters GStreamerRtpReceiverBackend::getParameters()
{
    notImplemented();
    return RTCRtpParameters { };
}

Vector<RTCRtpContributingSource> GStreamerRtpReceiverBackend::getContributingSources() const
{
    Vector<RTCRtpContributingSource> sources;
    notImplemented();
    return sources;
}

Vector<RTCRtpSynchronizationSource> GStreamerRtpReceiverBackend::getSynchronizationSources() const
{
    Vector<RTCRtpSynchronizationSource> sources;
    notImplemented();
    return sources;
}

Ref<RealtimeMediaSource> GStreamerRtpReceiverBackend::createSource(const String& trackKind)
{
    auto* transport = m_rtcReceiver->transport;
    GUniqueOutPtr<char> connectionId;

    if (transport && transport->dtlssrtpenc)
        g_object_get(transport->dtlssrtpenc, "connection-id", &connectionId.outPtr(), nullptr);

    if (trackKind == "video"_s)
        return RealtimeIncomingVideoSourceGStreamer::create(connectionId.get());

    RELEASE_ASSERT(trackKind == "audio"_s);
    return RealtimeIncomingAudioSourceGStreamer::create(connectionId.get());
}

Ref<RTCRtpTransformBackend> GStreamerRtpReceiverBackend::createRTCRtpTransformBackend()
{
    return GStreamerRtpReceiverTransformBackend::create(m_rtcReceiver);
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
