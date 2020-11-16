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
#include "PlatformView.h"
#include "PlatformWindow.h"
#include "PlaybackSessionInterfaceGTK.h"
#include "PlaybackSessionModel.h"
#include "VideoFullscreenModel.h"
#include <wtf/RefCounted.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class IntRect;
class FloatSize;
class PlaybackSessionInterfaceGTK;
class VideoFullscreenChangeObserver;

class VideoFullscreenInterfaceGTK
    : public VideoFullscreenModelClient
    , private PlaybackSessionModelClient
    , public RefCounted<VideoFullscreenInterfaceGTK> {

public:
    static Ref<VideoFullscreenInterfaceGTK> create(PlaybackSessionInterfaceGTK& playbackSessionInterface)
    {
        return adoptRef(*new VideoFullscreenInterfaceGTK(playbackSessionInterface));
    }
    virtual ~VideoFullscreenInterfaceGTK();
    PlaybackSessionInterfaceGTK& playbackSessionInterface() const { return m_playbackSessionInterface.get(); }
    VideoFullscreenModel* videoFullscreenModel() const { return m_videoFullscreenModel.get(); }
    PlaybackSessionModel* playbackSessionModel() const { return m_playbackSessionInterface->playbackSessionModel(); }
    WEBCORE_EXPORT void setVideoFullscreenModel(VideoFullscreenModel*);
    VideoFullscreenChangeObserver* videoFullscreenChangeObserver() const { return m_fullscreenChangeObserver.get(); }
    WEBCORE_EXPORT void setVideoFullscreenChangeObserver(VideoFullscreenChangeObserver*);

    // PlaybackSessionModelClient
    WEBCORE_EXPORT void rateChanged(bool isPlaying, float playbackRate) override;
    /* WEBCORE_EXPORT void externalPlaybackChanged(bool  enabled, PlaybackSessionModel::ExternalPlaybackTargetType, const String& localizedDeviceName) override; */
    /* WEBCORE_EXPORT void ensureControlsManager() override; */

    // VideoFullscreenModelClient
    /* WEBCORE_EXPORT void hasVideoChanged(bool) final; */
    /* WEBCORE_EXPORT void videoDimensionsChanged(const FloatSize&) final; */

    WEBCORE_EXPORT void setupFullscreen(PlatformView& layerHostedView, PlatformWindow* parentWindow, HTMLMediaElementEnums::VideoFullscreenMode, bool allowsPictureInPicturePlayback);
    WEBCORE_EXPORT void enterFullscreen();
    WEBCORE_EXPORT bool exitFullscreen(PlatformWindow* parentWindow);
    WEBCORE_EXPORT void exitFullscreenWithoutAnimationToMode(HTMLMediaElementEnums::VideoFullscreenMode);
    WEBCORE_EXPORT void cleanupFullscreen();
    WEBCORE_EXPORT void invalidate();
    void requestHideAndExitFullscreen() { }
    WEBCORE_EXPORT void preparedToReturnToInline(bool visible, PlatformWindow* parentWindow);
    void preparedToExitFullscreen() { }

    HTMLMediaElementEnums::VideoFullscreenMode mode() const { return m_mode; }
    bool hasMode(HTMLMediaElementEnums::VideoFullscreenMode mode) const { return m_mode & mode; }
    bool isMode(HTMLMediaElementEnums::VideoFullscreenMode mode) const { return m_mode == mode; }
    WEBCORE_EXPORT void setMode(HTMLMediaElementEnums::VideoFullscreenMode, bool);
    void clearMode(HTMLMediaElementEnums::VideoFullscreenMode);

    WEBCORE_EXPORT bool isPlayingVideoInEnhancedFullscreen() const;

    bool mayAutomaticallyShowVideoPictureInPicture() const { return false; }
    void applicationDidBecomeActive() { }

    WEBCORE_EXPORT void requestHideAndExitPiP();

private:
    WEBCORE_EXPORT VideoFullscreenInterfaceGTK(PlaybackSessionInterfaceGTK&);
    Ref<PlaybackSessionInterfaceGTK> m_playbackSessionInterface;
    WeakPtr<VideoFullscreenModel> m_videoFullscreenModel;
    WeakPtr<VideoFullscreenChangeObserver> m_fullscreenChangeObserver;
    HTMLMediaElementEnums::VideoFullscreenMode m_mode { HTMLMediaElementEnums::VideoFullscreenModeNone };
};

}

#endif

