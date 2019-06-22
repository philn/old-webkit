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
#include "MediaStreamTrackPrivate.h"
#include <gst/gst.h>
#include <wtf/Optional.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class RealtimeOutgoingMediaSourceGStreamer : public ThreadSafeRefCounted<RealtimeOutgoingMediaSourceGStreamer>, public MediaStreamTrackPrivate::Observer {
public:
    ~RealtimeOutgoingMediaSourceGStreamer() { stop(); }

    void stop();
    bool setTrack(Ref<MediaStreamTrackPrivate>&&);
    MediaStreamTrackPrivate& track() const { return m_track.get(); }

    GRefPtr<GstCaps> caps() const { return m_caps; }
    void linkToWebRTCBinPad(GstPad*);

    GRefPtr<GstWebRTCRTPSender> sender() const { return m_sender; }

    GstPad* pad() const { return m_pad; }
    
    virtual void initialize() = 0;
    virtual void setPayloadType(int) { };
    virtual void synchronizeStates() { };

protected:
    explicit RealtimeOutgoingMediaSourceGStreamer(Ref<MediaStreamTrackPrivate>&&, GstElement*);

    void initializeFromTrack();

    bool m_enabled { true };
    bool m_muted { false };
    bool m_isStopped { false };
    Ref<MediaStreamTrackPrivate> m_track;
    WTF::Optional<RealtimeMediaSourceSettings> m_initialSettings;
    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_outgoingSource;
    GRefPtr<GstElement> m_valve;
    GRefPtr<GstElement> m_preEncoderQueue;
    GRefPtr<GstElement> m_encoder;
    GRefPtr<GstElement> m_payloader;
    GRefPtr<GstElement> m_postEncoderQueue;
    GRefPtr<GstElement> m_capsFilter;
    GRefPtr<GstCaps> m_caps;
    GRefPtr<GstWebRTCRTPSender> m_sender;

private:
    void sourceMutedChanged();
    void sourceEnabledChanged();

    // MediaStreamTrackPrivate::Observer API
    void trackMutedChanged(MediaStreamTrackPrivate&) override { sourceMutedChanged(); }
    void trackEnabledChanged(MediaStreamTrackPrivate&) override { sourceEnabledChanged(); }
    void trackSettingsChanged(MediaStreamTrackPrivate&) override { initializeFromTrack(); }
    void trackEnded(MediaStreamTrackPrivate&) override { }

    GstPad* m_pad;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
