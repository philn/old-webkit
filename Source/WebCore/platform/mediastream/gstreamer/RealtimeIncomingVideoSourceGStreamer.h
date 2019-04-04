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

#if USE(GSTREAMER_WEBRTC)

#include "RealtimeVideoSourceGStreamer.h"
#include "DecoderSourceGStreamer.h"

namespace WebCore {

class RealtimeIncomingVideoSourceGStreamer : public RealtimeMediaSource, public DecoderSourceGStreamer {
public:
    static Ref<RealtimeIncomingVideoSourceGStreamer> create(String&& videoTrackId) { return adoptRef(*new RealtimeIncomingVideoSourceGStreamer(WTFMove(videoTrackId))); }
    static Ref<RealtimeIncomingVideoSourceGStreamer> create(GstElement* sourceElement) { return adoptRef(*new RealtimeIncomingVideoSourceGStreamer(sourceElement)); }
    ~RealtimeIncomingVideoSourceGStreamer() = default; //{ stopProducingData(); }

    /* void padExposed(GstPad*) final; */

protected:
    RealtimeIncomingVideoSourceGStreamer(String&&);
    RealtimeIncomingVideoSourceGStreamer(GstElement*);

private:
    // RealtimeMediaSource API
    void startProducingData() final;
    void stopProducingData()  final;

    const RealtimeMediaSourceCapabilities& capabilities() final;
    const RealtimeMediaSourceSettings& settings() final;

    RealtimeMediaSourceSettings m_currentSettings;

    /* bool applySize(const IntSize&) final { return true; } */
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::RealtimeIncomingVideoSourceGStreamer)
    static bool isType(const WebCore::RealtimeMediaSource& source) { return source.isIncomingVideoSource(); }
SPECIALIZE_TYPE_TRAITS_END()

#endif // USE(GSTREAMER_WEBRTC)
