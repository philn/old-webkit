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
#include "RealtimeIncomingVideoSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "MediaSampleGStreamer.h"

namespace WebCore {

RealtimeIncomingVideoSourceGStreamer::RealtimeIncomingVideoSourceGStreamer(String&& videoTrackId)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Video, WTFMove(videoTrackId))
    , DecoderSourceGStreamer(nullptr, nullptr)
{
}

RealtimeIncomingVideoSourceGStreamer::RealtimeIncomingVideoSourceGStreamer(GstElement* pipeline, GstPad* pad)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Video, "remote video"_s)
    , DecoderSourceGStreamer(pipeline, pad)
{
    RealtimeMediaSourceSupportedConstraints constraints;
    constraints.setSupportsWidth(true);
    constraints.setSupportsHeight(true);
    m_currentSettings = RealtimeMediaSourceSettings { };
    m_currentSettings->setSupportedConstraints(WTFMove(constraints));

    // notifyMutedChange(!m_videoTrack);
    start();
}

void RealtimeIncomingVideoSourceGStreamer::handleDecodedSample(GRefPtr<GstSample>&& sample)
{
    auto mediaSample = MediaSampleGStreamer::create(WTFMove(sample), WebCore::FloatSize(), String());
    callOnMainThread([protectedThis = makeRef(*this), mediaSample = WTFMove(mediaSample)] {
        protectedThis->videoSampleAvailable(mediaSample.get());
    });
}

void RealtimeIncomingVideoSourceGStreamer::startProducingData()
{
    releaseValve();
}

void RealtimeIncomingVideoSourceGStreamer::stopProducingData()
{
    lockValve();
}

const RealtimeMediaSourceCapabilities& RealtimeIncomingVideoSourceGStreamer::capabilities()
{
    return RealtimeMediaSourceCapabilities::emptyCapabilities();
}

const RealtimeMediaSourceSettings& RealtimeIncomingVideoSourceGStreamer::settings()
{
    if (m_currentSettings)
        return m_currentSettings.value();

    RealtimeMediaSourceSupportedConstraints constraints;
    constraints.setSupportsWidth(true);
    constraints.setSupportsHeight(true);

    RealtimeMediaSourceSettings settings;
    auto& size = this->size();
    settings.setWidth(size.width());
    settings.setHeight(size.height());
    settings.setSupportedConstraints(constraints);

    m_currentSettings = WTFMove(settings);
    return m_currentSettings.value();
}

void RealtimeIncomingVideoSourceGStreamer::settingsDidChange(OptionSet<RealtimeMediaSourceSettings::Flag> settings)
{
    if (settings.containsAny({ RealtimeMediaSourceSettings::Flag::Width, RealtimeMediaSourceSettings::Flag::Height }))
        m_currentSettings = WTF::nullopt;
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
