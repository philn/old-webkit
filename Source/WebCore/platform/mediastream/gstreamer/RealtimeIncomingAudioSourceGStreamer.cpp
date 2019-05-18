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
#include "NotImplemented.h"

namespace WebCore {

RealtimeIncomingAudioSourceGStreamer::RealtimeIncomingAudioSourceGStreamer(String&& audioTrackId)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Audio, WTFMove(audioTrackId))
    , DecoderSourceGStreamer(nullptr, nullptr)
{
    notImplemented();
}

RealtimeIncomingAudioSourceGStreamer::RealtimeIncomingAudioSourceGStreamer(GstElement* pipeline, GstPad* pad)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Audio, String { })
    , DecoderSourceGStreamer(pipeline, pad)
{
    // notifyMutedChange(!m_audioTrack);
    // setGstSourceElement(sourceBin());
    preroll();
    start();
}

RealtimeIncomingAudioSourceGStreamer::~RealtimeIncomingAudioSourceGStreamer()
{
    stop();
}

void RealtimeIncomingAudioSourceGStreamer::startProducingData()
{
    notImplemented();
}

void RealtimeIncomingAudioSourceGStreamer::stopProducingData()
{
    notImplemented();
}

const RealtimeMediaSourceCapabilities& RealtimeIncomingAudioSourceGStreamer::capabilities()
{
    return RealtimeMediaSourceCapabilities::emptyCapabilities();
}

const RealtimeMediaSourceSettings& RealtimeIncomingAudioSourceGStreamer::settings()
{
    return m_currentSettings;
}

// void RealtimeIncomingAudioSourceGStreamer::padExposed(GstPad* pad)
// {
//     GRefPtr<GstPad> sinkPad = gst_element_get_static_pad(capsFilter(), "sink");
//     gst_pad_link(pad, sinkPad.get());
// }

void RealtimeIncomingAudioSourceGStreamer::handleDecodedSample(GstSample* sample)
{
    GstAudioInfo info;
    GstCaps* caps = gst_sample_get_caps(sample);
    gst_audio_info_from_caps(&info, caps);

    auto data(std::unique_ptr<GStreamerAudioData>(new GStreamerAudioData(WTFMove(sample), info)));
    size_t numberOfFrames = 1;
    auto mediaTime = MediaTime((m_numberOfFrames * G_USEC_PER_SEC) / info.rate, G_USEC_PER_SEC);
    audioSamplesAvailable(mediaTime, *data.get(), GStreamerAudioStreamDescription(info), numberOfFrames);

    m_numberOfFrames += numberOfFrames;

}

}

#endif // USE(GSTREAMER_WEBRTC)
