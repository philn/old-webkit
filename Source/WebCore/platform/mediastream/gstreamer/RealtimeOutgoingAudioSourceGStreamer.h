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
#include "Timer.h"

namespace WebCore {

class RealtimeOutgoingAudioSourceGStreamer final : public RealtimeOutgoingMediaSourceGStreamer {
public:
    static Ref<RealtimeOutgoingAudioSourceGStreamer> create(Ref<MediaStreamTrackPrivate>&& audioSource, GstElement* pipeline) { return adoptRef(*new RealtimeOutgoingAudioSourceGStreamer(WTFMove(audioSource), pipeline)); }

    void setPayloadType(int) final;

protected:
    explicit RealtimeOutgoingAudioSourceGStreamer(Ref<MediaStreamTrackPrivate>&&, GstElement*);

    void initialize() final;
    void synchronizeStates() final;

private:
    GRefPtr<GstElement> m_audioconvert;
    GRefPtr<GstElement> m_audioresample;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
