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

#include "Logging.h"
#include "NotImplemented.h"

namespace WebCore {

RealtimeIncomingVideoSourceGStreamer::RealtimeIncomingVideoSourceGStreamer(String&& videoTrackId)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Video, WTFMove(videoTrackId))
    , DecoderSourceGStreamer(nullptr)
{
    notImplemented();
}


RealtimeIncomingVideoSourceGStreamer::RealtimeIncomingVideoSourceGStreamer(GstElement* sourceElement)
    : RealtimeMediaSource(RealtimeMediaSource::Type::Video, String { })
    , DecoderSourceGStreamer(sourceElement)
{
    m_currentSettings.setWidth(640);
    m_currentSettings.setHeight(480);
    // notifyMutedChange(!m_videoTrack);

    // setGstSourceElement(sourceBin());
    preroll();
    // setWidth(640);
    // setHeight(480);
    start();
}

// void RealtimeIncomingVideoSourceGStreamer::padExposed(GstPad* pad)
// {
//     GRefPtr<GstPad> sinkPad = gst_element_get_static_pad(capsFilter(), "sink");
//     gst_pad_link(pad, sinkPad.get());
// }

void RealtimeIncomingVideoSourceGStreamer::startProducingData()
{
    notImplemented();
}

void RealtimeIncomingVideoSourceGStreamer::stopProducingData()
{
    notImplemented();
}

const RealtimeMediaSourceCapabilities& RealtimeIncomingVideoSourceGStreamer::capabilities()
{
    return RealtimeMediaSourceCapabilities::emptyCapabilities();
}

const RealtimeMediaSourceSettings& RealtimeIncomingVideoSourceGStreamer::settings()
{
    return m_currentSettings;
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
