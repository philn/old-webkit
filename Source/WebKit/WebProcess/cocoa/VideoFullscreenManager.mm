/*
 * Copyright (C) 2014-2018 Apple Inc. All rights reserved.
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
#import "config.h"
#import "VideoFullscreenManager.h"

#if ENABLE(VIDEO_PRESENTATION_MODE)

#import "Attachment.h"
#import "LayerHostingContext.h"
#import "Logging.h"
#import "PlaybackSessionManager.h"
#import "VideoFullscreenManagerMessages.h"
#import "VideoFullscreenManagerProxyMessages.h"
#import "WebCoreArgumentCoders.h"
#import "WebPage.h"
#import "WebProcess.h"
#import <QuartzCore/CoreAnimation.h>
#import <WebCore/Color.h>
#import <WebCore/DeprecatedGlobalSettings.h>
#import <WebCore/Event.h>
#import <WebCore/EventNames.h>
#import <WebCore/FrameView.h>
#import <WebCore/HTMLVideoElement.h>
#import <WebCore/PictureInPictureSupport.h>
#import <WebCore/PlatformCALayer.h>
#import <WebCore/RenderLayer.h>
#import <WebCore/RenderLayerBacking.h>
#import <WebCore/RenderVideo.h>
#import <WebCore/RenderView.h>
#import <WebCore/Settings.h>
#import <WebCore/TimeRanges.h>
#import <WebCore/WebActionDisablingCALayerDelegate.h>
#import <mach/mach_port.h>
#import <wtf/MachSendRight.h>

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


#pragma mark Interface to ChromeClient:

bool VideoFullscreenManager::supportsVideoFullscreen(WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode) const
{
#if PLATFORM(IOS_FAMILY)
    UNUSED_PARAM(mode);
    return DeprecatedGlobalSettings::avKitEnabled();
#else
    return mode == HTMLMediaElementEnums::VideoFullscreenModeStandard || (mode == HTMLMediaElementEnums::VideoFullscreenModePictureInPicture && supportsPictureInPicture());
#endif
}

bool VideoFullscreenManager::supportsVideoFullscreenStandby() const
{
#if PLATFORM(IOS_FAMILY)
    return true;
#else
    return false;
#endif
}

void VideoFullscreenManager::enterVideoFullscreenForVideoElement(HTMLVideoElement& videoElement, HTMLMediaElementEnums::VideoFullscreenMode mode, bool standby)
{
    ASSERT(m_page);
    ASSERT(standby || mode != HTMLMediaElementEnums::VideoFullscreenModeNone);
    LOG(Fullscreen, "VideoFullscreenManager::enterVideoFullscreenForVideoElement(%p)", this);

    auto contextId = m_playbackSessionManager->contextIdForMediaElement(videoElement);
    auto addResult = m_videoElements.add(&videoElement, contextId);
    UNUSED_PARAM(addResult);
    ASSERT(addResult.iterator->value == contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);
    HTMLMediaElementEnums::VideoFullscreenMode oldMode = interface->fullscreenMode();

    addClientForContext(contextId);
    if (!interface->layerHostingContext())
        interface->setLayerHostingContext(LayerHostingContext::createForExternalHostingProcess());

    auto videoRect = inlineVideoFrame(videoElement);
    FloatRect videoLayerFrame = FloatRect(0, 0, videoRect.width(), videoRect.height());

    if (mode != HTMLMediaElementEnums::VideoFullscreenModeNone)
        interface->setTargetIsFullscreen(true);
    else
        interface->setTargetIsFullscreen(false);

    interface->setFullscreenMode(mode);
    interface->setFullscreenStandby(standby);
    model->setVideoElement(&videoElement);
    if (oldMode == HTMLMediaElementEnums::VideoFullscreenModeNone && mode != HTMLMediaElementEnums::VideoFullscreenModeNone)
        model->setVideoLayerFrame(videoLayerFrame);

    if (interface->animationState() != VideoFullscreenInterfaceContext::AnimationType::None)
        return;
    interface->setAnimationState(VideoFullscreenInterfaceContext::AnimationType::IntoFullscreen);

    bool allowsPictureInPicture = videoElement.webkitSupportsPresentationMode(HTMLVideoElement::VideoPresentationMode::PictureInPicture);

    if (!interface->layerHostingContext()->rootLayer()) {
        auto videoLayer = model->createVideoFullscreenLayer();
        [videoLayer setDelegate:[WebActionDisablingCALayerDelegate shared]];
        [videoLayer setName:@"Web Video Fullscreen Layer"];
        [videoLayer setPosition:CGPointMake(0, 0)];
        [videoLayer setBackgroundColor:cachedCGColor(WebCore::Color::transparentBlack)];

        // Set a scale factor here to make convertRect:toLayer:nil take scale factor into account. <rdar://problem/18316542>.
        // This scale factor is inverted in the hosting process.
        float hostingScaleFactor = m_page->deviceScaleFactor();
        [videoLayer setTransform:CATransform3DMakeScale(hostingScaleFactor, hostingScaleFactor, 1)];

        interface->layerHostingContext()->setRootLayer(videoLayer.get());
    }

    m_page->send(Messages::VideoFullscreenManagerProxy::SetupFullscreenWithID(contextId, interface->layerHostingContext()->contextID(), videoRect, FloatSize(videoElement.videoWidth(), videoElement.videoHeight()), m_page->deviceScaleFactor(), interface->fullscreenMode(), allowsPictureInPicture, standby));
}

#pragma mark Messages from VideoFullscreenManagerProxy:

void VideoFullscreenManager::requestVideoContentLayer(PlaybackSessionContextIdentifier contextId)
{
    auto [model, interface] = ensureModelAndInterface(contextId);

    CALayer* videoLayer = interface->layerHostingContext()->rootLayer();

    model->setVideoFullscreenLayer(videoLayer, [protectedThis = makeRefPtr(this), this, contextId] () mutable {
        dispatch_async(dispatch_get_main_queue(), [protectedThis = WTFMove(protectedThis), this, contextId] {
            if (protectedThis->m_page)
                m_page->send(Messages::VideoFullscreenManagerProxy::SetHasVideoContentLayer(contextId, true));
        });
    });
}

void VideoFullscreenManager::returnVideoContentLayer(PlaybackSessionContextIdentifier contextId)
{
    RefPtr<VideoFullscreenModelVideoElement> model;
    RefPtr<VideoFullscreenInterfaceContext> interface;
    std::tie(model, interface) = ensureModelAndInterface(contextId);

    model->waitForPreparedForInlineThen([protectedThis = makeRefPtr(this), this, contextId, model] () mutable { // need this for return video layer
        dispatch_async(dispatch_get_main_queue(), [protectedThis = WTFMove(protectedThis), this, contextId, model] () mutable {
            model->setVideoFullscreenLayer(nil, [protectedThis = WTFMove(protectedThis), this, contextId] () mutable {
                dispatch_async(dispatch_get_main_queue(), [protectedThis = WTFMove(protectedThis), this, contextId] {
                    if (protectedThis->m_page)
                        m_page->send(Messages::VideoFullscreenManagerProxy::SetHasVideoContentLayer(contextId, false));
                });
            });
        });
    });
}

#if !PLATFORM(IOS_FAMILY)
void VideoFullscreenManager::didSetupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::didSetupFullscreen(%p, %x)", this, contextId);

    ASSERT(m_page);
    auto [model, interface] = ensureModelAndInterface(contextId);
    CALayer* videoLayer = interface->layerHostingContext()->rootLayer();

    model->setVideoFullscreenLayer(videoLayer, [protectedThis = makeRefPtr(this), this, contextId] () mutable {
        dispatch_async(dispatch_get_main_queue(), [protectedThis = WTFMove(protectedThis), this, contextId] {
            if (protectedThis->m_page)
                m_page->send(Messages::VideoFullscreenManagerProxy::EnterFullscreen(contextId));
        });
    });
}
#endif

void VideoFullscreenManager::willExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::willExitFullscreen(%p, %x)", this, contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);

    RefPtr<HTMLVideoElement> videoElement = model->videoElement();
    if (!videoElement)
        return;

    dispatch_async(dispatch_get_main_queue(), [protectedThis = makeRefPtr(this), videoElement = WTFMove(videoElement), contextId] {
        videoElement->willExitFullscreen();
        if (protectedThis->m_page)
            protectedThis->m_page->send(Messages::VideoFullscreenManagerProxy::PreparedToExitFullscreen(contextId));
    });
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
    dispatch_async(dispatch_get_main_queue(), [protectedThis = makeRefPtr(this), videoElement] {
        if (protectedThis->m_page)
            protectedThis->exitVideoFullscreenForVideoElement(*videoElement, [](bool) { });
    });
}

void VideoFullscreenManager::didExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::didExitFullscreen(%p, %x)", this, contextId);

    RefPtr<VideoFullscreenModelVideoElement> model;
    RefPtr<VideoFullscreenInterfaceContext> interface;
    std::tie(model, interface) = ensureModelAndInterface(contextId);

#if PLATFORM(IOS_FAMILY)
    dispatch_async(dispatch_get_main_queue(), [protectedThis = makeRefPtr(this), contextId, interface] {
        if (protectedThis->m_page)
            protectedThis->m_page->send(Messages::VideoFullscreenManagerProxy::CleanupFullscreen(contextId));
    });
#else
    model->waitForPreparedForInlineThen([protectedThis = makeRefPtr(this), contextId, interface, model]() mutable {
        dispatch_async(dispatch_get_main_queue(), [protectedThis = WTFMove(protectedThis), contextId, interface, model] () mutable {
            model->setVideoFullscreenLayer(nil, [protectedThis = WTFMove(protectedThis), contextId, interface] () mutable {
                dispatch_async(dispatch_get_main_queue(), [protectedThis = WTFMove(protectedThis), contextId, interface] {
                    if (interface->layerHostingContext()) {
                        interface->layerHostingContext()->setRootLayer(nullptr);
                        interface->setLayerHostingContext(nullptr);
                    }
                    if (protectedThis->m_page)
                        protectedThis->m_page->send(Messages::VideoFullscreenManagerProxy::CleanupFullscreen(contextId));
                });
            });
        });
    });
#endif
}

void VideoFullscreenManager::didCleanupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    LOG(Fullscreen, "VideoFullscreenManager::didCleanupFullscreen(%p, %x)", this, contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);

    if (interface->layerHostingContext()) {
        interface->layerHostingContext()->setRootLayer(nullptr);
        interface->setLayerHostingContext(nullptr);
    }

    interface->setAnimationState(VideoFullscreenInterfaceContext::AnimationType::None);
    interface->setIsFullscreen(false);
    HTMLMediaElementEnums::VideoFullscreenMode mode = interface->fullscreenMode();
    bool standby = interface->fullscreenStandby();
    bool targetIsFullscreen = interface->targetIsFullscreen();

    model->setVideoFullscreenLayer(nil);
    RefPtr<HTMLVideoElement> videoElement = model->videoElement();
    if (videoElement)
        videoElement->didExitFullscreenOrPictureInPicture();

    interface->setFullscreenMode(HTMLMediaElementEnums::VideoFullscreenModeNone);
    interface->setFullscreenStandby(false);
    removeClientForContext(contextId);

    if (!videoElement || !targetIsFullscreen)
        return;

    dispatch_async(dispatch_get_main_queue(), [protectedThis = makeRefPtr(this), videoElement, mode, standby] {
        if (protectedThis->m_page)
            protectedThis->enterVideoFullscreenForVideoElement(*videoElement, mode, standby);
    });
}

void VideoFullscreenManager::requestRouteSharingPolicyAndContextUID(PlaybackSessionContextIdentifier contextId, Messages::VideoFullscreenManager::RequestRouteSharingPolicyAndContextUID::AsyncReply&& reply)
{
    ensureModel(contextId).requestRouteSharingPolicyAndContextUID(WTFMove(reply));
}
    
void VideoFullscreenManager::setVideoLayerFrameFenced(PlaybackSessionContextIdentifier contextId, WebCore::FloatRect bounds, const WTF::MachSendRight& machSendRight)
{
    LOG(Fullscreen, "VideoFullscreenManager::setVideoLayerFrameFenced(%p, %x)", this, contextId);

    auto [model, interface] = ensureModelAndInterface(contextId);

    if (std::isnan(bounds.x()) || std::isnan(bounds.y()) || std::isnan(bounds.width()) || std::isnan(bounds.height())) {
        auto videoRect = inlineVideoFrame(*model->videoElement());
        bounds = FloatRect(0, 0, videoRect.width(), videoRect.height());
    }
    
    if (auto* context = interface->layerHostingContext())
        context->setFencePort(machSendRight.sendRight());
    model->setVideoLayerFrame(bounds);
}

} // namespace WebKit

#endif // ENABLE(VIDEO_PRESENTATION_MODE)
