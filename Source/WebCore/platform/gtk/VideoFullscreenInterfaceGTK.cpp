/*
 * Copyright (C) 2016-2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "VideoFullscreenInterfaceGTK.h"

#if PLATFORM(GTK) && ENABLE(VIDEO_PRESENTATION_MODE)

#include "IntRect.h"
#include "Logging.h"
#include "PictureInPictureSupport.h"
#include "PlaybackSessionInterfaceGTK.h"
#include "TimeRanges.h"
#include "VideoFullscreenChangeObserver.h"
#include "VideoFullscreenModel.h"

using WebCore::VideoFullscreenModel;
using WebCore::HTMLMediaElementEnums;
using WebCore::MediaPlayerEnums;
using WebCore::VideoFullscreenInterfaceGTK;
using WebCore::VideoFullscreenChangeObserver;
using WebCore::PlaybackSessionModel;

enum class PIPState {
    NotInPIP,
    EnteringPIP,
    InPIP,
    ExitingPIP
};

#undef LOG
#define LOG(foo, ...) WTFLogAlways(__VA_ARGS__)

namespace WebCore {

VideoFullscreenInterfaceGTK::VideoFullscreenInterfaceGTK(PlaybackSessionInterfaceGTK& playbackSessionInterface)
    : m_playbackSessionInterface(playbackSessionInterface)
{
    ASSERT(m_playbackSessionInterface->playbackSessionModel());
    auto model = m_playbackSessionInterface->playbackSessionModel();
    model->addClient(*this);
    // [videoFullscreenInterfaceObjC() updateIsPlaying:model->isPlaying() newPlaybackRate:model->playbackRate()];
}

VideoFullscreenInterfaceGTK::~VideoFullscreenInterfaceGTK()
{
    if (auto* model = m_playbackSessionInterface->playbackSessionModel())
        model->removeClient(*this);
    if (m_videoFullscreenModel)
        m_videoFullscreenModel->removeClient(*this);
}

void VideoFullscreenInterfaceGTK::setVideoFullscreenModel(VideoFullscreenModel* model)
{
    if (m_videoFullscreenModel)
        m_videoFullscreenModel->removeClient(*this);
    m_videoFullscreenModel = makeWeakPtr(model);
    if (m_videoFullscreenModel)
        m_videoFullscreenModel->addClient(*this);
}

void VideoFullscreenInterfaceGTK::setVideoFullscreenChangeObserver(VideoFullscreenChangeObserver* observer)
{
    m_fullscreenChangeObserver = makeWeakPtr(observer);
}

void VideoFullscreenInterfaceGTK::setMode(HTMLMediaElementEnums::VideoFullscreenMode mode, bool)
{
    HTMLMediaElementEnums::VideoFullscreenMode newMode = m_mode | mode;
    if (m_mode == newMode)
        return;

    m_mode = newMode;
    if (m_videoFullscreenModel)
        m_videoFullscreenModel->fullscreenModeChanged(m_mode);
}

void VideoFullscreenInterfaceGTK::clearMode(HTMLMediaElementEnums::VideoFullscreenMode mode)
{
    HTMLMediaElementEnums::VideoFullscreenMode newMode = m_mode & ~mode;
    if (m_mode == newMode)
        return;

    m_mode = newMode;
    if (m_videoFullscreenModel)
        m_videoFullscreenModel->fullscreenModeChanged(m_mode);
}

void VideoFullscreenInterfaceGTK::rateChanged(bool isPlaying, float playbackRate)
{
    // [videoFullscreenInterfaceObjC() updateIsPlaying:isPlaying newPlaybackRate:playbackRate];
}

// void VideoFullscreenInterfaceGTK::ensureControlsManager()
// {
//     m_playbackSessionInterface->ensureControlsManager();
// }

void VideoFullscreenInterfaceGTK::setupFullscreen(PlatformView& layerHostedView, PlatformWindow* parentWindow, HTMLMediaElementEnums::VideoFullscreenMode mode, bool allowsPictureInPicturePlayback)
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::setupFullscreen(%p), parentWindow:%p, mode:%d", this, parentWindow, mode);

    UNUSED_PARAM(allowsPictureInPicturePlayback);
    ASSERT(mode == HTMLMediaElementEnums::VideoFullscreenModePictureInPicture);

    m_mode = mode;

    // [videoFullscreenInterfaceObjC() setUpPIPForVideoView:&layerHostedView withFrame:(NSRect)initialRect inWindow:parentWindow];

    // dispatch_async(dispatch_get_main_queue(), [protectedThis = makeRefPtr(this), this] {
    if (m_fullscreenChangeObserver)
        m_fullscreenChangeObserver->didSetupFullscreen();
    // });
}

void VideoFullscreenInterfaceGTK::enterFullscreen()
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::enterFullscreen(%p)", this);

    RELEASE_ASSERT(m_videoFullscreenModel);
//     if (mode() == HTMLMediaElementEnums::VideoFullscreenModePictureInPicture) {
//         m_videoFullscreenModel->willEnterPictureInPicture();
//         [m_webVideoFullscreenInterfaceObjC enterPIP];

// #if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
//         [m_playbackSessionInterface->playBackControlsManager() setPictureInPictureActive:YES];
// #endif
//     }
}

bool VideoFullscreenInterfaceGTK::exitFullscreen(PlatformWindow* parentWindow)
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::exitFullscreen(%p), parentWindow:%p", this, parentWindow);

// #if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
//     [m_playbackSessionInterface->playBackControlsManager() setPictureInPictureActive:NO];
// #endif

//     if (finalRect.isEmpty())
//         [m_webVideoFullscreenInterfaceObjC exitPIP];
//     else
//         [m_webVideoFullscreenInterfaceObjC exitPIPAnimatingToRect:finalRect inWindow:parentWindow];

    return true;
}

void VideoFullscreenInterfaceGTK::exitFullscreenWithoutAnimationToMode(HTMLMediaElementEnums::VideoFullscreenMode mode)
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::exitFullscreenWithoutAnimationToMode(%p), mode:%d", this, mode);

#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    [m_playbackSessionInterface->playBackControlsManager() setPictureInPictureActive:NO];
#endif

    bool isExitingToStandardFullscreen = mode == HTMLMediaElementEnums::VideoFullscreenModeStandard;
    // On Mac, standard fullscreen is handled by the Fullscreen API and not by VideoFullscreenManager.
    // Just update m_mode directly to HTMLMediaElementEnums::VideoFullscreenModeStandard in that case to keep
    // m_mode in sync with the fullscreen mode in HTMLMediaElement.
    if (isExitingToStandardFullscreen)
        m_mode = HTMLMediaElementEnums::VideoFullscreenModeStandard;

    // [m_webVideoFullscreenInterfaceObjC setExitingToStandardFullscreen:isExitingToStandardFullscreen];
    // [m_webVideoFullscreenInterfaceObjC exitPIP];
}

void VideoFullscreenInterfaceGTK::cleanupFullscreen()
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::cleanupFullscreen(%p)", this);

    // [m_webVideoFullscreenInterfaceObjC exitPIP];
    // [m_webVideoFullscreenInterfaceObjC invalidateFullscreenState];

    if (m_fullscreenChangeObserver)
        m_fullscreenChangeObserver->didCleanupFullscreen();
}

void VideoFullscreenInterfaceGTK::invalidate()
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::invalidate(%p)", this);

    m_videoFullscreenModel = nullptr;
    m_fullscreenChangeObserver = nullptr;

    cleanupFullscreen();

    // [m_webVideoFullscreenInterfaceObjC invalidate];
    // m_webVideoFullscreenInterfaceObjC = nil;
}

void VideoFullscreenInterfaceGTK::requestHideAndExitPiP()
{
    if (!m_videoFullscreenModel)
        return;

    m_videoFullscreenModel->requestFullscreenMode(HTMLMediaElementEnums::VideoFullscreenModeNone);
    m_videoFullscreenModel->willExitPictureInPicture();
}

void VideoFullscreenInterfaceGTK::preparedToReturnToInline(bool visible, PlatformWindow *parentWindow)
{
    UNUSED_PARAM(visible);
    UNUSED_PARAM(parentWindow);
}

#if 0
void VideoFullscreenInterfaceGTK::externalPlaybackChanged(bool enabled, PlaybackSessionModel::ExternalPlaybackTargetType, const String&)
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::externalPlaybackChanged(%p), enabled:%s", this, boolForPrinting(enabled));

    if (enabled && m_mode == HTMLMediaElementEnums::VideoFullscreenModePictureInPicture)
        exitFullscreen(IntRect(), nil);
}

void VideoFullscreenInterfaceGTK::hasVideoChanged(bool hasVideo)
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::hasVideoChanged(%p):%s", this, boolForPrinting(hasVideo));

    if (!hasVideo)
        exitFullscreenWithoutAnimationToMode(HTMLMediaElementEnums::VideoFullscreenModeNone);
}

void VideoFullscreenInterfaceGTK::videoDimensionsChanged(const FloatSize& videoDimensions)
{
    LOG(Fullscreen, "VideoFullscreenInterfaceGTK::videoDimensionsChanged(%p), width:%.0f, height:%.0f", this, videoDimensions.width(), videoDimensions.height());

    // Width and height can be zero when we are transitioning from one video to another. Ignore zero values.
    // if (!videoDimensions.isZero())
    //     [m_webVideoFullscreenInterfaceObjC setVideoDimensions:videoDimensions];
}
#endif

bool VideoFullscreenInterfaceGTK::isPlayingVideoInEnhancedFullscreen() const
{
    //return hasMode(WebCore::HTMLMediaElementEnums::VideoFullscreenModePictureInPicture) && [m_webVideoFullscreenInterfaceObjC isPlaying];
    return false;
}

#if 0
bool supportsPictureInPicture()
{
    return PIPLibrary() && getPIPViewControllerClass();
}
#endif

}

#endif
