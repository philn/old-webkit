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

#include "config.h"
#include "VideoFullscreenManager.h"

#if ENABLE(VIDEO_PRESENTATION_MODE)

#include "Attachment.h"
#include "LayerHostingContext.h"
#include "Logging.h"
#include "PlaybackSessionManager.h"
#include "VideoFullscreenManagerMessages.h"
#include "VideoFullscreenManagerProxyMessages.h"
#include "WebCoreArgumentCoders.h"
#include "WebPage.h"
#include "WebProcess.h"
#include <WebCore/Color.h>
#include <WebCore/DeprecatedGlobalSettings.h>
#include <WebCore/Event.h>
#include <WebCore/EventNames.h>
#include <WebCore/FrameView.h>
#include <WebCore/HTMLVideoElement.h>
#include <WebCore/PictureInPictureSupport.h>
#include <WebCore/RenderLayer.h>
#include <WebCore/RenderLayerBacking.h>
#include <WebCore/RenderVideo.h>
#include <WebCore/RenderView.h>
#include <WebCore/Settings.h>
#include <WebCore/TimeRanges.h>

namespace WebKit {
using namespace WebCore;

#pragma mark - VideoFullscreenInterfaceContext

VideoFullscreenInterfaceContext::VideoFullscreenInterfaceContext(VideoFullscreenManager& manager, PlaybackSessionContextIdentifier contextId)
    : m_manager(&manager)
    , m_contextId(contextId)
{
}

VideoFullscreenInterfaceContext::~VideoFullscreenInterfaceContext()
{
}

void VideoFullscreenInterfaceContext::setLayerHostingContext(std::unique_ptr<LayerHostingContext>&& context)
{
    m_layerHostingContext = WTFMove(context);
}

void VideoFullscreenInterfaceContext::hasVideoChanged(bool hasVideo)
{
    if (m_manager)
        m_manager->hasVideoChanged(m_contextId, hasVideo);
}

void VideoFullscreenInterfaceContext::videoDimensionsChanged(const FloatSize& videoDimensions)
{
    if (m_manager)
        m_manager->videoDimensionsChanged(m_contextId, videoDimensions);
}

#pragma mark - VideoFullscreenManager

Ref<VideoFullscreenManager> VideoFullscreenManager::create(WebPage& page, PlaybackSessionManager& playbackSessionManager)
{
    return adoptRef(*new VideoFullscreenManager(page, playbackSessionManager));
}

VideoFullscreenManager::VideoFullscreenManager(WebPage& page, PlaybackSessionManager& playbackSessionManager)
    : m_page(&page)
    , m_playbackSessionManager(playbackSessionManager)
#if PLATFORM(GTK)
    , m_layerTreeHost(*m_page)
#endif
{
    WebProcess::singleton().addMessageReceiver(Messages::VideoFullscreenManager::messageReceiverName(), page.identifier(), *this);
}

VideoFullscreenManager::~VideoFullscreenManager()
{
    for (auto& [model, interface] : m_contextMap.values()) {
        model->setVideoElement(nullptr);
        model->removeClient(*interface);

        interface->invalidate();
    }

    m_contextMap.clear();
    m_videoElements.clear();
    m_clientCounts.clear();
    
    if (m_page)
        WebProcess::singleton().removeMessageReceiver(Messages::VideoFullscreenManager::messageReceiverName(), m_page->identifier());
}

void VideoFullscreenManager::invalidate()
{
    ASSERT(m_page);
    WebProcess::singleton().removeMessageReceiver(Messages::VideoFullscreenManager::messageReceiverName(), m_page->identifier());
    m_page = nullptr;
}

IntRect VideoFullscreenManager::inlineVideoFrame(HTMLVideoElement& element)
{
    auto& document = element.document();
    if (!document.hasLivingRenderTree() || document.activeDOMObjectsAreStopped())
        return { };

    document.updateLayoutIgnorePendingStylesheets();
    auto* renderer = element.renderer();
    if (!renderer)
        return { };

    if (renderer->hasLayer() && renderer->enclosingLayer()->isComposited()) {
        FloatQuad contentsBox = static_cast<FloatRect>(renderer->enclosingLayer()->backing()->contentsBox());
        contentsBox = renderer->localToAbsoluteQuad(contentsBox);
        return element.document().view()->contentsToRootView(contentsBox.enclosingBoundingBox());
    }

    auto rect = renderer->videoBox();
    rect.moveBy(renderer->absoluteBoundingBoxRect().location());
    return element.document().view()->contentsToRootView(rect);
}


VideoFullscreenManager::ModelInterfaceTuple& VideoFullscreenManager::ensureModelAndInterface(PlaybackSessionContextIdentifier contextId)
{
    auto addResult = m_contextMap.add(contextId, ModelInterfaceTuple());
    if (addResult.isNewEntry)
        addResult.iterator->value = createModelAndInterface(contextId);
    return addResult.iterator->value;
}

WebCore::VideoFullscreenModelVideoElement& VideoFullscreenManager::ensureModel(PlaybackSessionContextIdentifier contextId)
{
    return *std::get<0>(ensureModelAndInterface(contextId));
}

VideoFullscreenInterfaceContext& VideoFullscreenManager::ensureInterface(PlaybackSessionContextIdentifier contextId)
{
    return *std::get<1>(ensureModelAndInterface(contextId));
}

void VideoFullscreenManager::removeContext(PlaybackSessionContextIdentifier contextId)
{
    auto [model, interface] = ensureModelAndInterface(contextId);

    m_playbackSessionManager->removeClientForContext(contextId);

    RefPtr<HTMLVideoElement> videoElement = model->videoElement();
    model->setVideoElement(nullptr);
    model->removeClient(*interface);
    interface->invalidate();
    m_videoElements.remove(videoElement.get());
    m_contextMap.remove(contextId);
}

void VideoFullscreenManager::addClientForContext(PlaybackSessionContextIdentifier contextId)
{
    auto addResult = m_clientCounts.add(contextId, 1);
    if (!addResult.isNewEntry)
        addResult.iterator->value++;
}

void VideoFullscreenManager::removeClientForContext(PlaybackSessionContextIdentifier contextId)
{
    ASSERT(m_clientCounts.contains(contextId));

    int clientCount = m_clientCounts.get(contextId);
    ASSERT(clientCount > 0);
    clientCount--;

    if (clientCount <= 0) {
        m_clientCounts.remove(contextId);
        removeContext(contextId);
        return;
    }

    m_clientCounts.set(contextId, clientCount);
}

#pragma mark Interface to ChromeClient:

void VideoFullscreenManager::exitVideoFullscreenForVideoElement(WebCore::HTMLVideoElement& videoElement, CompletionHandler<void(bool)>&& completionHandler)
{
    LOG(Fullscreen, "VideoFullscreenManager::exitVideoFullscreenForVideoElement(%p)", this);
    ASSERT(m_page);
    ASSERT(m_videoElements.contains(&videoElement));

    auto contextId = m_videoElements.get(&videoElement);
    auto& interface = ensureInterface(contextId);
    if (interface.animationState() != VideoFullscreenInterfaceContext::AnimationType::None) {
        completionHandler(false);
        return;
    }

    m_page->sendWithAsyncReply(Messages::VideoFullscreenManagerProxy::ExitFullscreen(contextId, inlineVideoFrame(videoElement)), [protectedThis = makeRefPtr(this), this, contextId, completionHandler = WTFMove(completionHandler)](auto success) mutable {
        if (!success) {
            completionHandler(false);
            return;
        }

        auto& interface = ensureInterface(contextId);
        interface.setTargetIsFullscreen(false);
        interface.setAnimationState(VideoFullscreenInterfaceContext::AnimationType::FromFullscreen);
        completionHandler(true);
    });
}

void VideoFullscreenManager::exitVideoFullscreenToModeWithoutAnimation(WebCore::HTMLVideoElement& videoElement, WebCore::HTMLMediaElementEnums::VideoFullscreenMode targetMode)
{
    LOG(Fullscreen, "VideoFullscreenManager::exitVideoFullscreenToModeWithoutAnimation(%p)", this);

    ASSERT(m_page);
    ASSERT(m_videoElements.contains(&videoElement));

    auto contextId = m_videoElements.get(&videoElement);
    auto& interface = ensureInterface(contextId);

    interface.setTargetIsFullscreen(false);

    m_page->send(Messages::VideoFullscreenManagerProxy::ExitFullscreenWithoutAnimationToMode(contextId, targetMode));
}

#pragma mark Interface to VideoFullscreenInterfaceContext:

void VideoFullscreenManager::hasVideoChanged(PlaybackSessionContextIdentifier contextId, bool hasVideo)
{
    if (m_page)
        m_page->send(Messages::VideoFullscreenManagerProxy::SetHasVideo(contextId, hasVideo));
}

void VideoFullscreenManager::videoDimensionsChanged(PlaybackSessionContextIdentifier contextId, const FloatSize& videoDimensions)
{
    if (m_page)
        m_page->send(Messages::VideoFullscreenManagerProxy::SetVideoDimensions(contextId, videoDimensions));
}

#pragma mark Messages from VideoFullscreenManagerProxy:

void VideoFullscreenManager::requestFullscreenMode(PlaybackSessionContextIdentifier contextId, WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode, bool finishedWithMedia)
{
    ensureModel(contextId).requestFullscreenMode(mode, finishedWithMedia);
}

void VideoFullscreenManager::fullscreenModeChanged(PlaybackSessionContextIdentifier contextId, WebCore::HTMLMediaElementEnums::VideoFullscreenMode videoFullscreenMode)
{
    auto [model, interface] = ensureModelAndInterface(contextId);
    model->fullscreenModeChanged(videoFullscreenMode);
    interface->setFullscreenMode(videoFullscreenMode);
}

void VideoFullscreenManager::requestUpdateInlineRect(PlaybackSessionContextIdentifier contextId)
{
    if (!m_page)
        return;

    auto& model = ensureModel(contextId);
    IntRect inlineRect = inlineVideoFrame(*model.videoElement());
    m_page->send(Messages::VideoFullscreenManagerProxy::SetInlineRect(contextId, inlineRect, inlineRect != IntRect(0, 0, 0, 0)));
}

void VideoFullscreenManager::setVideoLayerGravityEnum(PlaybackSessionContextIdentifier contextId, unsigned gravity)
{
    ensureModel(contextId).setVideoLayerGravity((MediaPlayerEnums::VideoGravity)gravity);
}

void VideoFullscreenManager::fullscreenMayReturnToInline(PlaybackSessionContextIdentifier contextId, bool isPageVisible)
{
    if (!m_page)
        return;

    auto& model = ensureModel(contextId);

    if (!isPageVisible)
        model.videoElement()->scrollIntoViewIfNotVisible(false);
    m_page->send(Messages::VideoFullscreenManagerProxy::PreparedToReturnToInline(contextId, true, inlineVideoFrame(*model.videoElement())));
}

} // namespace WebKit

#endif // ENABLE(VIDEO_PRESENTATION_MODE)
