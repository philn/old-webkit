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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlaybackSessionInterfaceGTK.h"

#if PLATFORM(GTK) && ENABLE(VIDEO_PRESENTATION_MODE)

#include "IntRect.h"
#include "MediaSelectionOption.h"
#include "PlaybackSessionModel.h"
#include "TimeRanges.h"

namespace WebCore {

Ref<PlaybackSessionInterfaceGTK> PlaybackSessionInterfaceGTK::create(PlaybackSessionModel& model)
{
    auto interface = adoptRef(*new PlaybackSessionInterfaceGTK(model));
    model.addClient(interface);
    return interface;
}

PlaybackSessionInterfaceGTK::PlaybackSessionInterfaceGTK(PlaybackSessionModel& model)
    : m_playbackSessionModel(makeWeakPtr(model))
{
}

PlaybackSessionInterfaceGTK::~PlaybackSessionInterfaceGTK()
{
    invalidate();
}

PlaybackSessionModel* PlaybackSessionInterfaceGTK::playbackSessionModel() const
{
    return m_playbackSessionModel.get();
}

void PlaybackSessionInterfaceGTK::durationChanged(double duration)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    WebPlaybackControlsManager* controlsManager = playBackControlsManager();

    controlsManager.contentDuration = duration;

    // FIXME: We take this as an indication that playback is ready, but that is not necessarily true.
    controlsManager.hasEnabledAudio = YES;
    controlsManager.hasEnabledVideo = YES;
#else
    UNUSED_PARAM(duration);
#endif
}

void PlaybackSessionInterfaceGTK::currentTimeChanged(double currentTime, double anchorTime)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    WebPlaybackControlsManager* controlsManager = playBackControlsManager();
    updatePlaybackControlsManagerTiming(currentTime, anchorTime, controlsManager.rate, controlsManager.playing);
#else
    UNUSED_PARAM(currentTime);
    UNUSED_PARAM(anchorTime);
#endif
}

void PlaybackSessionInterfaceGTK::rateChanged(bool isPlaying, float playbackRate)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    WebPlaybackControlsManager* controlsManager = playBackControlsManager();
    [controlsManager setRate:isPlaying ? playbackRate : 0.];
    [controlsManager setPlaying:isPlaying];
    updatePlaybackControlsManagerTiming(m_playbackSessionModel ? m_playbackSessionModel->currentTime() : 0, [[NSProcessInfo processInfo] systemUptime], playbackRate, isPlaying);
#else
    UNUSED_PARAM(isPlaying);
    UNUSED_PARAM(playbackRate);
#endif
}

void PlaybackSessionInterfaceGTK::beginScrubbing()
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    updatePlaybackControlsManagerTiming(m_playbackSessionModel ? m_playbackSessionModel->currentTime() : 0, [[NSProcessInfo processInfo] systemUptime], 0, false);
#endif
    if (auto* model = playbackSessionModel())
        model->beginScrubbing();
}

void PlaybackSessionInterfaceGTK::endScrubbing()
{
    if (auto* model = playbackSessionModel())
        model->endScrubbing();
}

#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
static RetainPtr<NSMutableArray> timeRangesToArray(const TimeRanges& timeRanges)
{
    RetainPtr<NSMutableArray> rangeArray = adoptNS([[NSMutableArray alloc] init]);

    for (unsigned i = 0; i < timeRanges.length(); i++) {
        const PlatformTimeRanges& ranges = timeRanges.ranges();
        CMTimeRange range = PAL::CMTimeRangeMake(PAL::toCMTime(ranges.start(i)), PAL::toCMTime(ranges.end(i)));
        [rangeArray addObject:[NSValue valueWithCMTimeRange:range]];
    }

    return rangeArray;
}
#endif

void PlaybackSessionInterfaceGTK::seekableRangesChanged(const TimeRanges& timeRanges, double, double)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    [playBackControlsManager() setSeekableTimeRanges:timeRangesToArray(timeRanges).get()];
#else
    UNUSED_PARAM(timeRanges);
#endif
}

void PlaybackSessionInterfaceGTK::audioMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& options, uint64_t selectedIndex)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    [playBackControlsManager() setAudioMediaSelectionOptions:options withSelectedIndex:static_cast<NSUInteger>(selectedIndex)];
#else
    UNUSED_PARAM(options);
    UNUSED_PARAM(selectedIndex);
#endif
}

void PlaybackSessionInterfaceGTK::legibleMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& options, uint64_t selectedIndex)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    [playBackControlsManager() setLegibleMediaSelectionOptions:options withSelectedIndex:static_cast<NSUInteger>(selectedIndex)];
#else
    UNUSED_PARAM(options);
    UNUSED_PARAM(selectedIndex);
#endif
}

void PlaybackSessionInterfaceGTK::audioMediaSelectionIndexChanged(uint64_t selectedIndex)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    [playBackControlsManager() setAudioMediaSelectionIndex:selectedIndex];
#else
    UNUSED_PARAM(selectedIndex);
#endif
}

void PlaybackSessionInterfaceGTK::legibleMediaSelectionIndexChanged(uint64_t selectedIndex)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    [playBackControlsManager() setLegibleMediaSelectionIndex:selectedIndex];
#else
    UNUSED_PARAM(selectedIndex);
#endif
}

void PlaybackSessionInterfaceGTK::isPictureInPictureSupportedChanged(bool)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    updatePlaybackControlsManagerCanTogglePictureInPicture();
#endif
}

void PlaybackSessionInterfaceGTK::externalPlaybackChanged(bool, PlaybackSessionModel::ExternalPlaybackTargetType, const String&)
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    updatePlaybackControlsManagerCanTogglePictureInPicture();
#endif
}

void PlaybackSessionInterfaceGTK::invalidate()
{
    if (!m_playbackSessionModel)
        return;

    m_playbackSessionModel->removeClient(*this);
    m_playbackSessionModel = nullptr;
}

void PlaybackSessionInterfaceGTK::ensureControlsManager()
{
#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)
    playBackControlsManager();
#endif
}

#if ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)

WebPlaybackControlsManager *PlaybackSessionInterfaceGTK::playBackControlsManager()
{
    return m_playbackControlsManager.getAutoreleased();
}

void PlaybackSessionInterfaceGTK::setPlayBackControlsManager(WebPlaybackControlsManager *manager)
{
    m_playbackControlsManager = manager;

    if (!manager || !m_playbackSessionModel)
        return;

    NSTimeInterval anchorTimeStamp = ![manager rate] ? NAN : [[NSProcessInfo processInfo] systemUptime];
    manager.timing = [getAVValueTimingClass() valueTimingWithAnchorValue:m_playbackSessionModel->currentTime() anchorTimeStamp:anchorTimeStamp rate:0];
    double duration = m_playbackSessionModel->duration();
    manager.contentDuration = duration;
    manager.hasEnabledAudio = duration > 0;
    manager.hasEnabledVideo = duration > 0;
    manager.rate = m_playbackSessionModel->isPlaying() ? m_playbackSessionModel->playbackRate() : 0.;
    manager.seekableTimeRanges = timeRangesToArray(m_playbackSessionModel->seekableRanges()).get();
    manager.canTogglePlayback = YES;
    manager.playing = m_playbackSessionModel->isPlaying();
    [manager setAudioMediaSelectionOptions:m_playbackSessionModel->audioMediaSelectionOptions() withSelectedIndex:static_cast<NSUInteger>(m_playbackSessionModel->audioMediaSelectedIndex())];
    [manager setLegibleMediaSelectionOptions:m_playbackSessionModel->legibleMediaSelectionOptions() withSelectedIndex:static_cast<NSUInteger>(m_playbackSessionModel->legibleMediaSelectedIndex())];
}

void PlaybackSessionInterfaceGTK::updatePlaybackControlsManagerCanTogglePictureInPicture()
{
    PlaybackSessionModel* model = playbackSessionModel();
    if (!model) {
        [playBackControlsManager() setCanTogglePictureInPicture:NO];
        return;
    }

    [playBackControlsManager() setCanTogglePictureInPicture:model->isPictureInPictureSupported() && !model->externalPlaybackEnabled()];
}

void PlaybackSessionInterfaceGTK::updatePlaybackControlsManagerTiming(double currentTime, double anchorTime, double playbackRate, bool isPlaying)
{
    WebPlaybackControlsManager *manager = playBackControlsManager();
    if (!manager)
        return;

    PlaybackSessionModel *model = playbackSessionModel();
    if (!model)
        return;

    double effectiveAnchorTime = playbackRate ? anchorTime : NAN;
    double effectivePlaybackRate = playbackRate;
    if (!isPlaying
        || model->isScrubbing()
        || (manager.rate > 0 && model->playbackStartedTime() >= currentTime)
        || (manager.rate < 0 && model->playbackStartedTime() <= currentTime))
        effectivePlaybackRate = 0;

    manager.timing = [getAVValueTimingClass() valueTimingWithAnchorValue:currentTime anchorTimeStamp:effectiveAnchorTime rate:effectivePlaybackRate];
}

#endif // ENABLE(WEB_PLAYBACK_CONTROLS_MANAGER)

}

#endif // PLATFORM(MAC) && ENABLE(VIDEO_PRESENTATION_MODE)
