/*
 *  Copyright (C) 2017-2022 Igalia S.L. All rights reserved.
 *  Copyright (C) 2022 Metrological Group B.V.
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

#if USE(GSTREAMER_WEBRTC)
#include "RealtimeIncomingVideoSourceGStreamer.h"

#include "MediaSampleGStreamer.h"
#include "VideoFrameMetadataGStreamer.h"
#include <gst/rtp/rtp.h>

namespace WebCore {

RealtimeIncomingVideoSourceGStreamer::RealtimeIncomingVideoSourceGStreamer(String&& videoTrackId)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Video, WTFMove(videoTrackId))
    , RealtimeIncomingSourceGStreamer()
{
    RealtimeMediaSourceSupportedConstraints constraints;
    constraints.setSupportsWidth(true);
    constraints.setSupportsHeight(true);
    m_currentSettings = RealtimeMediaSourceSettings { };
    m_currentSettings->setSupportedConstraints(WTFMove(constraints));

    auto sinkPad = adoptGRef(gst_element_get_static_pad(m_valve.get(), "sink"));
    gst_pad_add_probe(sinkPad.get(), static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER), [](GstPad*, GstPadProbeInfo* info, gpointer) -> GstPadProbeReturn {
        auto videoSampleMetadata = std::make_optional<VideoSampleMetadata>({ });
        videoSampleMetadata->receiveTime = MonotonicTime::now().secondsSinceEpoch();

        auto* buffer = GST_BUFFER_CAST(GST_PAD_PROBE_INFO_DATA(info));
        GstRTPBuffer rtpBuffer GST_RTP_BUFFER_INIT;
        if (gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtpBuffer)) {
            videoSampleMetadata->rtpTimestamp = gst_rtp_buffer_get_timestamp(&rtpBuffer);
            gst_rtp_buffer_unmap(&rtpBuffer);
        }
        buffer = webkitGstBufferSetVideoSampleMetadata(buffer, WTFMove(videoSampleMetadata));
        GST_PAD_PROBE_INFO_DATA(info) = buffer;
        return GST_PAD_PROBE_OK;
    }, nullptr, nullptr);

    start();
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
        m_currentSettings = std::nullopt;
}

void RealtimeIncomingVideoSourceGStreamer::dispatchSample(GRefPtr<GstSample>&& gstSample)
{
    videoSampleAvailable(MediaSampleGStreamer::createWrappedSample(gstSample), { });
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
