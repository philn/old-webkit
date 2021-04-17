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
#include "RealtimeIncomingAudioSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerAudioData.h"
#include "GStreamerAudioStreamDescription.h"

namespace WebCore {

RealtimeIncomingAudioSourceGStreamer::RealtimeIncomingAudioSourceGStreamer(String&& audioTrackId)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Audio, WTFMove(audioTrackId))
    , RealtimeIncomingSourceGStreamer()
{
    // notifyMutedChange(false);
    start();
}

RealtimeIncomingAudioSourceGStreamer::~RealtimeIncomingAudioSourceGStreamer()
{
    stop();
}

void RealtimeIncomingAudioSourceGStreamer::startProducingData()
{
    releaseValve();
}

void RealtimeIncomingAudioSourceGStreamer::stopProducingData()
{
    lockValve();
}

const RealtimeMediaSourceCapabilities& RealtimeIncomingAudioSourceGStreamer::capabilities()
{
    return RealtimeMediaSourceCapabilities::emptyCapabilities();
}

const RealtimeMediaSourceSettings& RealtimeIncomingAudioSourceGStreamer::settings()
{
    return m_currentSettings;
}

void RealtimeIncomingAudioSourceGStreamer::dispatchSample(GRefPtr<GstSample>&& gstSample)
{
    auto* buffer = gst_sample_get_buffer(gstSample.get());
    GStreamerAudioStreamDescription description;
    GStreamerAudioData frames(WTFMove(gstSample), description.getInfo());

    audioSamplesAvailable(MediaTime(GST_TIME_AS_USECONDS(GST_BUFFER_PTS(buffer)), G_USEC_PER_SEC), frames, description, 0);
}

}

#endif // USE(GSTREAMER_WEBRTC)
