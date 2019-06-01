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

#include "MediaStreamTrackPrivate.h"
#include <Timer.h>
#include <wtf/Optional.h>
#include <wtf/ThreadSafeRefCounted.h>
#include "GRefPtrGStreamer.h"
#include <gst/gst.h>

namespace WebCore {

class RealtimeOutgoingVideoSourceGStreamer : public ThreadSafeRefCounted<RealtimeOutgoingVideoSourceGStreamer>, private MediaStreamTrackPrivate::Observer {
public:
    static Ref<RealtimeOutgoingVideoSourceGStreamer> create(Ref<MediaStreamTrackPrivate>&& videoSource, GstElement* pipeline) { return adoptRef(*new RealtimeOutgoingVideoSourceGStreamer(WTFMove(videoSource), pipeline)); }
    ~RealtimeOutgoingVideoSourceGStreamer() { stop(); }

    void stop();
    bool setSource(Ref<MediaStreamTrackPrivate>&&);
    MediaStreamTrackPrivate& source() const { return m_videoSource.get(); }

    void setApplyRotation(bool shouldApplyRotation) { m_shouldApplyRotation = shouldApplyRotation; }

    GRefPtr<GstWebRTCRTPSender> sender() const { return m_sender; }

protected:
    explicit RealtimeOutgoingVideoSourceGStreamer(Ref<MediaStreamTrackPrivate>&&, GstElement*);

    bool m_enabled { true };
    bool m_muted { false };
    uint32_t m_width { 0 };
    uint32_t m_height { 0 };
    bool m_shouldApplyRotation { false };

private:
    void initializeFromSource();

    void sourceMutedChanged();
    void sourceEnabledChanged();

    // MediaStreamTrackPrivate::Observer API
    void trackMutedChanged(MediaStreamTrackPrivate&) final { sourceMutedChanged(); }
    void trackEnabledChanged(MediaStreamTrackPrivate&) final { sourceEnabledChanged(); }
    void trackSettingsChanged(MediaStreamTrackPrivate&) final { initializeFromSource(); }
    void sampleBufferUpdated(MediaStreamTrackPrivate&, MediaSample&) final;
    void trackEnded(MediaStreamTrackPrivate&) final { }

    Ref<MediaStreamTrackPrivate> m_videoSource;
    WTF::Optional<RealtimeMediaSourceSettings> m_initialSettings;
    bool m_isStopped { false };
    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_outgoingVideoSource;
    GRefPtr<GstWebRTCRTPSender> m_sender;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
