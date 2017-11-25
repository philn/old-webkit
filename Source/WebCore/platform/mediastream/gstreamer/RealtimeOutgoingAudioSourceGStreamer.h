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

#include "GRefPtrGStreamer.h"
#include <gst/gst.h>
#include "MediaStreamTrackPrivate.h"
#include "Timer.h"
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class RealtimeOutgoingAudioSourceGStreamer : public ThreadSafeRefCounted<RealtimeOutgoingAudioSourceGStreamer>, private MediaStreamTrackPrivate::Observer {
public:
    static Ref<RealtimeOutgoingAudioSourceGStreamer> create(Ref<MediaStreamTrackPrivate>&& audioSource, GstElement* pipeline) { return adoptRef(*new RealtimeOutgoingAudioSourceGStreamer(WTFMove(audioSource), pipeline)); }

    ~RealtimeOutgoingAudioSourceGStreamer() { stop(); }

    void stop();

    bool setSource(Ref<MediaStreamTrackPrivate>&&);
    MediaStreamTrackPrivate& source() const { return m_audioSource.get(); }

protected:
    explicit RealtimeOutgoingAudioSourceGStreamer(Ref<MediaStreamTrackPrivate>&&, GstElement*);

    virtual void handleMutedIfNeeded();
    virtual void sendSilence() { };
    virtual void pullAudioData() { };

    bool m_muted { false };
    bool m_enabled { true };

private:
    void sourceMutedChanged();
    void sourceEnabledChanged();
    /* virtual void audioSamplesAvailable(const MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t); */

    virtual bool isReachingBufferedAudioDataHighLimit() { return false; };
    virtual bool isReachingBufferedAudioDataLowLimit() { return false; };
    virtual bool hasBufferedEnoughData() { return false; };

    // MediaStreamTrackPrivate::Observer API
    void trackMutedChanged(MediaStreamTrackPrivate&) final { sourceMutedChanged(); }
    void trackEnabledChanged(MediaStreamTrackPrivate&) final { sourceEnabledChanged(); }
    void audioSamplesAvailable(MediaStreamTrackPrivate&, const MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t sampleCount) final { }
    void trackEnded(MediaStreamTrackPrivate&) final { }
    void trackSettingsChanged(MediaStreamTrackPrivate&) final { }

    void initializeConverter();

    Ref<MediaStreamTrackPrivate> m_audioSource;

    Timer m_silenceAudioTimer;
    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_outputSelector;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
