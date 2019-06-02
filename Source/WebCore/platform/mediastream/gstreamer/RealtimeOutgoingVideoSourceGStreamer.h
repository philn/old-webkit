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

#include "RealtimeOutgoingMediaSourceGStreamer.h"
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class RealtimeOutgoingVideoSourceGStreamer final : public ThreadSafeRefCounted<RealtimeOutgoingVideoSourceGStreamer>, public RealtimeOutgoingMediaSourceGStreamer {
public:
    static Ref<RealtimeOutgoingVideoSourceGStreamer> create(Ref<MediaStreamTrackPrivate>&& source, GstElement* pipeline) { return adoptRef(*new RealtimeOutgoingVideoSourceGStreamer(WTFMove(source), pipeline)); }

    void setApplyRotation(bool shouldApplyRotation) { m_shouldApplyRotation = shouldApplyRotation; }

    virtual void initialize() override;
    void setPayloadType(int) final;
    void synchronizeStates() final;

protected:
    explicit RealtimeOutgoingVideoSourceGStreamer(Ref<MediaStreamTrackPrivate>&&, GstElement*);

    uint32_t m_width { 0 };
    uint32_t m_height { 0 };
    bool m_shouldApplyRotation { false };

private:
    // MediaStreamTrackPrivate::Observer API
    void sampleBufferUpdated(MediaStreamTrackPrivate&, MediaSample&) final;

    GRefPtr<GstElement> m_videoConvert;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
