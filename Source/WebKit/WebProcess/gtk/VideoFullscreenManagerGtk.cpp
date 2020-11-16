/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2010 Motorola Mobility, Inc.  All rights reserved.
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

#include "VideoFullscreenManager.h"

#include "LayerHostingContext.h"
#include "LayerTreeHost.h"
#include "Logging.h"
#include "PlaybackSessionManager.h"
#include "VideoFullscreenManagerMessages.h"
#include "VideoFullscreenManagerProxyMessages.h"
#include "WebCoreArgumentCoders.h"
#include "WebPage.h"
#include "WebProcess.h"
#include <WebCore/FrameView.h>
#include <WebCore/HTMLVideoElement.h>
#include <WebCore/NotImplemented.h>
#include <WebCore/PictureInPictureSupport.h>

#undef LOG
#define LOG(foo, ...) WTFLogAlways(__VA_ARGS__)

namespace WebKit {
using namespace WebCore;

VideoFullscreenManager::ModelInterfaceTuple VideoFullscreenManager::createModelAndInterface(PlaybackSessionContextIdentifier contextId)
{
    auto model = VideoFullscreenModelVideoElement::create();
    auto interface = VideoFullscreenInterfaceContext::create(*this, contextId);
    m_playbackSessionManager->addClientForContext(contextId);

    interface->setLayerHostingContext(LayerHostingContext::createForExternalHostingProcess());
    model->addClient(interface.get());

    return std::make_tuple(WTFMove(model), WTFMove(interface));
}

bool VideoFullscreenManager::supportsVideoFullscreen(WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode) const
{
    return mode == HTMLMediaElementEnums::VideoFullscreenModeStandard || (mode == HTMLMediaElementEnums::VideoFullscreenModePictureInPicture && supportsPictureInPicture());
}

bool VideoFullscreenManager::supportsVideoFullscreenStandby() const
{
    return false;
}

void VideoFullscreenManager::enterVideoFullscreenForVideoElement(HTMLVideoElement& videoElement, HTMLMediaElementEnums::VideoFullscreenMode mode, bool standby)
{
    ASSERT(m_page);
    ASSERT(standby || mode != HTMLMediaElementEnums::VideoFullscreenModeNone);
    LOG(Fullscreen, "VideoFullscreenManager::enterVideoFullscreenForVideoElement(%p)", this);

    auto contextId = m_playbackSessionManager->contextIdForMediaElement(videoElement);
    auto addResult = m_videoElements.add(&videoElement, contextId);
    UNUSED_VARIABLE(addResult);
    ASSERT(addResult.iterator->value == contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);
    HTMLMediaElementEnums::VideoFullscreenMode oldMode = interface->fullscreenMode();

    addClientForContext(contextId);

    auto videoRect = inlineVideoFrame(videoElement);
    auto videoLayerFrame = FloatRect(0, 0, videoRect.width(), videoRect.height());

    interface->setTargetIsFullscreen(mode != HTMLMediaElementEnums::VideoFullscreenModeNone);
    interface->setFullscreenMode(mode);
    interface->setFullscreenStandby(standby);
    model->setVideoElement(&videoElement);
    if (oldMode == HTMLMediaElementEnums::VideoFullscreenModeNone && mode != HTMLMediaElementEnums::VideoFullscreenModeNone)
        model->setVideoLayerFrame(videoLayerFrame);

    if (interface->animationState() != VideoFullscreenInterfaceContext::AnimationType::None)
        return;
    interface->setAnimationState(VideoFullscreenInterfaceContext::AnimationType::IntoFullscreen);

    bool allowsPictureInPicture = videoElement.webkitSupportsPresentationMode(HTMLVideoElement::VideoPresentationMode::PictureInPicture);

    auto* layerHostingContext = interface->layerHostingContext();
    if (!layerHostingContext->rootLayer()) {
        auto videoLayer = model->createVideoFullscreenLayer();
        // [videoLayer setDelegate:[WebActionDisablingCALayerDelegate shared]];
        // [videoLayer setName:@"Web Video Fullscreen Layer"];
        // [videoLayer setPosition:CGPointMake(0, 0)];
        // [videoLayer setBackgroundColor:cachedCGColor(WebCore::Color::transparentBlack)];

        // Set a scale factor here to make convertRect:toLayer:nil take scale factor into account. <rdar://problem/18316542>.
        // This scale factor is inverted in the hosting process.
        // float hostingScaleFactor = m_page->deviceScaleFactor();
        // [videoLayer setTransform:CATransform3DMakeScale(hostingScaleFactor, hostingScaleFactor, 1)];

        WTFLogAlways("->> id: %" PRIu64, videoLayer->id());
        //layerHostingContext->setContextID(videoLayer->id());
        layerHostingContext->setRootLayer(videoLayer.get());
        layerHostingContext->setContextID(m_layerTreeHost.layerTreeContext().contextID);
    }

    m_page->send(Messages::VideoFullscreenManagerProxy::SetupFullscreenWithID(contextId, layerHostingContext->contextID(), videoRect, FloatSize(videoElement.videoWidth(), videoElement.videoHeight()), m_page->deviceScaleFactor(), interface->fullscreenMode(), allowsPictureInPicture, standby));
}

void VideoFullscreenManager::requestVideoContentLayer(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::requestVideoContentLayer(%p, %x)", this, contextId);
    auto [model, interface] = ensureModelAndInterface(contextId);

    notImplemented();
}

void VideoFullscreenManager::returnVideoContentLayer(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::returnVideoContentLayer(%p, %x)", this, contextId);
    RefPtr<VideoFullscreenModelVideoElement> model;
    RefPtr<VideoFullscreenInterfaceContext> interface;
    std::tie(model, interface) = ensureModelAndInterface(contextId);

    model->waitForPreparedForInlineThen([protectedThis = makeRefPtr(this), this, contextId, model] () mutable { // need this for return video layer
        notImplemented();
    });
}

void VideoFullscreenManager::didSetupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::didSetupFullscreen(%p, %x)", this, contextId);

    ASSERT(m_page);
    auto [model, interface] = ensureModelAndInterface(contextId);
    notImplemented();
}

void VideoFullscreenManager::willExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::willExitFullscreen(%p, %x)", this, contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);

    RefPtr<HTMLVideoElement> videoElement = model->videoElement();
    if (!videoElement)
        return;

    notImplemented();
}

void VideoFullscreenManager::didEnterFullscreen(PlaybackSessionContextIdentifier contextId, Optional<WebCore::FloatSize> size)
{
    LOG(Fullscreen, "VideoFullscreenManager::didEnterFullscreen(%p, %x)", this, contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);

    interface->setAnimationState(VideoFullscreenInterfaceContext::AnimationType::None);
    interface->setIsFullscreen(false);

    RefPtr<HTMLVideoElement> videoElement = model->videoElement();
    if (!videoElement)
        return;

    videoElement->didEnterFullscreenOrPictureInPicture(size.valueOr(WebCore::FloatSize()));

    if (interface->targetIsFullscreen() || interface->fullscreenStandby())
        return;

    // exit fullscreen now if it was previously requested during an animation.
    notImplemented();
}

void VideoFullscreenManager::didExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::didExitFullscreen(%p, %x)", this, contextId);

    RefPtr<VideoFullscreenModelVideoElement> model;
    RefPtr<VideoFullscreenInterfaceContext> interface;
    std::tie(model, interface) = ensureModelAndInterface(contextId);

    notImplemented();
}

void VideoFullscreenManager::didCleanupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::didCleanupFullscreen(%p, %x)", this, contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);

    interface->setAnimationState(VideoFullscreenInterfaceContext::AnimationType::None);
    interface->setIsFullscreen(false);
    HTMLMediaElementEnums::VideoFullscreenMode mode = interface->fullscreenMode();
    bool standby = interface->fullscreenStandby();
    bool targetIsFullscreen = interface->targetIsFullscreen();

    notImplemented();

    RefPtr<HTMLVideoElement> videoElement = model->videoElement();
    if (videoElement)
        videoElement->didExitFullscreenOrPictureInPicture();

    interface->setFullscreenMode(HTMLMediaElementEnums::VideoFullscreenModeNone);
    interface->setFullscreenStandby(false);
    removeClientForContext(contextId);

    if (!videoElement || !targetIsFullscreen)
        return;

    notImplemented();
}

} // namespace WebKit
