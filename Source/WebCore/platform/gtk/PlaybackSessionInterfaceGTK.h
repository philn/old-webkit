/*
 *  Copyright (C) 2020 Igalia S.L
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

#if PLATFORM(GTK) && ENABLE(VIDEO_PRESENTATION_MODE)

#include "HTMLMediaElementEnums.h"
#include "PlaybackSessionModel.h"
#include <wtf/RefCounted.h>
/* #include <wtf/RetainPtr.h> */
/* #include <wtf/WeakObjCPtr.h> */
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

/* OBJC_CLASS WebPlaybackControlsManager; */

namespace WebCore {
class IntRect;
class PlaybackSessionModel;

class WEBCORE_EXPORT PlaybackSessionInterfaceGTK final
    : public PlaybackSessionModelClient
    , public RefCounted<PlaybackSessionInterfaceGTK> {
public:
    static Ref<PlaybackSessionInterfaceGTK> create(PlaybackSessionModel&);
    virtual ~PlaybackSessionInterfaceGTK();
    PlaybackSessionModel* playbackSessionModel() const;

    // PlaybackSessionModelClient
    void durationChanged(double) final;
    void currentTimeChanged(double /*currentTime*/, double /*anchorTime*/) final;
    void rateChanged(bool /*isPlaying*/, float /*playbackRate*/) final;
    void seekableRangesChanged(const TimeRanges&, double /*lastModifiedTime*/, double /*liveUpdateInterval*/) final;
    void audioMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& /*options*/, uint64_t /*selectedIndex*/) final;
    void legibleMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& /*options*/, uint64_t /*selectedIndex*/) final;
    void audioMediaSelectionIndexChanged(uint64_t) final;
    void legibleMediaSelectionIndexChanged(uint64_t) final;
    void externalPlaybackChanged(bool /* enabled */, PlaybackSessionModel::ExternalPlaybackTargetType, const String& /* localizedDeviceName */) final;
    void isPictureInPictureSupportedChanged(bool) final;
    void ensureControlsManager() final;

#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    void setPlayBackControlsManager(WebPlaybackControlsManager *);
    WebPlaybackControlsManager *playBackControlsManager();

    void updatePlaybackControlsManagerCanTogglePictureInPicture();
#endif
    void beginScrubbing();
    void endScrubbing();

    void invalidate();

private:
    PlaybackSessionInterfaceGTK(PlaybackSessionModel&);
    WeakPtr<PlaybackSessionModel> m_playbackSessionModel;
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    WeakObjCPtr<WebPlaybackControlsManager> m_playbackControlsManager;

    void updatePlaybackControlsManagerTiming(double currentTime, double anchorTime, double playbackRate, bool isPlaying);
#endif
};

}

#endif // PLATFORM(GTK) && ENABLE(VIDEO_PRESENTATION_MODE)
