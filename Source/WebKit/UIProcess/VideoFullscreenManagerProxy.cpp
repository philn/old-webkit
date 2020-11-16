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
#include "VideoFullscreenManagerProxy.h"

#include "APIUIClient.h"
#include "PlaybackSessionManagerProxy.h"
#include "VideoFullscreenManagerMessages.h"
#include "VideoFullscreenManagerProxyMessages.h"
#include "WebPageProxy.h"
#include "WebProcessProxy.h"

namespace WebKit {
using namespace WebCore;

VideoFullscreenModelContext::VideoFullscreenModelContext(VideoFullscreenManagerProxy& manager, PlaybackSessionModelContext& playbackSessionModel, PlaybackSessionContextIdentifier contextId)
    : m_manager(&manager)
    , m_playbackSessionModel(playbackSessionModel)
    , m_contextId(contextId)
{
}

VideoFullscreenModelContext::~VideoFullscreenModelContext()
{
}

void VideoFullscreenModelContext::addClient(VideoFullscreenModelClient& client)
{
    ASSERT(!m_clients.contains(&client));
    m_clients.add(&client);
}

void VideoFullscreenModelContext::removeClient(VideoFullscreenModelClient& client)
{
    ASSERT(m_clients.contains(&client));
    m_clients.remove(&client);
}

void VideoFullscreenModelContext::requestFullscreenMode(HTMLMediaElementEnums::VideoFullscreenMode mode, bool finishedWithMedia)
{
    if (m_manager)
        m_manager->requestFullscreenMode(m_contextId, mode, finishedWithMedia);
}

void VideoFullscreenModelContext::setVideoLayerFrame(WebCore::FloatRect frame)
{
    if (m_manager)
        m_manager->setVideoLayerFrame(m_contextId, frame);
}

void VideoFullscreenModelContext::setVideoLayerGravity(WebCore::MediaPlayerEnums::VideoGravity gravity)
{
    if (m_manager)
        m_manager->setVideoLayerGravity(m_contextId, gravity);
}

void VideoFullscreenModelContext::fullscreenModeChanged(WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode)
{
    if (m_manager)
        m_manager->fullscreenModeChanged(m_contextId, mode);
}

void VideoFullscreenModelContext::requestUpdateInlineRect()
{
    if (m_manager)
        m_manager->requestUpdateInlineRect(m_contextId);
}

void VideoFullscreenModelContext::requestVideoContentLayer()
{
    if (m_manager)
        m_manager->requestVideoContentLayer(m_contextId);
}

void VideoFullscreenModelContext::returnVideoContentLayer()
{
    if (m_manager)
        m_manager->returnVideoContentLayer(m_contextId);
}

void VideoFullscreenModelContext::didSetupFullscreen()
{
    if (m_manager)
        m_manager->didSetupFullscreen(m_contextId);
}

void VideoFullscreenModelContext::didEnterFullscreen(const WebCore::FloatSize& size)
{
    if (m_manager)
        m_manager->didEnterFullscreen(m_contextId, size);
}

void VideoFullscreenModelContext::willExitFullscreen()
{
    if (m_manager)
        m_manager->willExitFullscreen(m_contextId);
}

void VideoFullscreenModelContext::didExitFullscreen()
{
    if (m_manager)
        m_manager->didExitFullscreen(m_contextId);
}

void VideoFullscreenModelContext::didCleanupFullscreen()
{
    if (m_manager)
        m_manager->didCleanupFullscreen(m_contextId);
}

void VideoFullscreenModelContext::fullscreenMayReturnToInline()
{
    if (m_manager)
        m_manager->fullscreenMayReturnToInline(m_contextId);
}

void VideoFullscreenModelContext::didEnterPictureInPicture()
{
    if (m_manager)
        m_manager->hasVideoInPictureInPictureDidChange(true);
}

void VideoFullscreenModelContext::didExitPictureInPicture()
{
    if (m_manager)
        m_manager->hasVideoInPictureInPictureDidChange(false);
}

void VideoFullscreenModelContext::willEnterPictureInPicture()
{
    for (auto& client : copyToVector(m_clients))
        client->willEnterPictureInPicture();
}

void VideoFullscreenModelContext::failedToEnterPictureInPicture()
{
    for (auto& client : copyToVector(m_clients))
        client->failedToEnterPictureInPicture();
}

void VideoFullscreenModelContext::willExitPictureInPicture()
{
    for (auto& client : copyToVector(m_clients))
        client->willExitPictureInPicture();
}

#define MESSAGE_CHECK(assertion) MESSAGE_CHECK_BASE(assertion, m_page->process().connection())

#pragma mark - VideoFullscreenManagerProxy

Ref<VideoFullscreenManagerProxy> VideoFullscreenManagerProxy::create(WebPageProxy& page, PlaybackSessionManagerProxy& playbackSessionManagerProxy)
{
    return adoptRef(*new VideoFullscreenManagerProxy(page, playbackSessionManagerProxy));
}

VideoFullscreenManagerProxy::VideoFullscreenManagerProxy(WebPageProxy& page, PlaybackSessionManagerProxy& playbackSessionManagerProxy)
    : m_page(&page)
    , m_playbackSessionManagerProxy(playbackSessionManagerProxy)
{
    m_page->process().addMessageReceiver(Messages::VideoFullscreenManagerProxy::messageReceiverName(), m_page->webPageID(), *this);
}

VideoFullscreenManagerProxy::~VideoFullscreenManagerProxy()
{
    if (!m_page)
        return;
    invalidate();
}

void VideoFullscreenManagerProxy::invalidate()
{
    m_page->process().removeMessageReceiver(Messages::VideoFullscreenManagerProxy::messageReceiverName(), m_page->webPageID());
    m_page = nullptr;

    platformInvalidate();
}

void VideoFullscreenManagerProxy::requestHideAndExitFullscreen()
{
    for (auto& [model, interface] : m_contextMap.values())
        interface->requestHideAndExitFullscreen();
}

bool VideoFullscreenManagerProxy::hasMode(HTMLMediaElementEnums::VideoFullscreenMode mode) const
{
    for (auto& [model, interface] : m_contextMap.values()) {
        if (interface->hasMode(mode))
            return true;
    }
    return false;
}

bool VideoFullscreenManagerProxy::mayAutomaticallyShowVideoPictureInPicture() const
{
    for (auto& [model, interface] : m_contextMap.values()) {
        if (interface->mayAutomaticallyShowVideoPictureInPicture())
            return true;
    }
    return false;
}

#if ENABLE(VIDEO_PRESENTATION_MODE)
bool VideoFullscreenManagerProxy::isPlayingVideoInEnhancedFullscreen() const
{
    for (auto& [model, interface] : m_contextMap.values()) {
        if (interface->isPlayingVideoInEnhancedFullscreen())
            return true;
    }
    return false;
}
#endif

PlatformVideoFullscreenInterface* VideoFullscreenManagerProxy::controlsManagerInterface()
{
    if (auto contextId = m_playbackSessionManagerProxy->controlsManagerContextId())
        return &ensureInterface(contextId);
    return nullptr;
}

void VideoFullscreenManagerProxy::applicationDidBecomeActive()
{
    for (auto& [model, interface] : m_contextMap.values())
        interface->applicationDidBecomeActive();
}

VideoFullscreenManagerProxy::ModelInterfaceTuple VideoFullscreenManagerProxy::createModelAndInterface(PlaybackSessionContextIdentifier contextId)
{
    auto& playbackSessionModel = m_playbackSessionManagerProxy->ensureModel(contextId);
    Ref<VideoFullscreenModelContext> model = VideoFullscreenModelContext::create(*this, playbackSessionModel, contextId);
    auto& playbackSessionInterface = m_playbackSessionManagerProxy->ensureInterface(contextId);
    Ref<PlatformVideoFullscreenInterface> interface = PlatformVideoFullscreenInterface::create(playbackSessionInterface);
    m_playbackSessionManagerProxy->addClientForContext(contextId);

    interface->setVideoFullscreenModel(&model.get());
    interface->setVideoFullscreenChangeObserver(&model.get());

    return std::make_tuple(WTFMove(model), WTFMove(interface));
}

VideoFullscreenManagerProxy::ModelInterfaceTuple& VideoFullscreenManagerProxy::ensureModelAndInterface(PlaybackSessionContextIdentifier contextId)
{
    auto addResult = m_contextMap.add(contextId, ModelInterfaceTuple());
    if (addResult.isNewEntry)
        addResult.iterator->value = createModelAndInterface(contextId);
    return addResult.iterator->value;
}

VideoFullscreenModelContext& VideoFullscreenManagerProxy::ensureModel(PlaybackSessionContextIdentifier contextId)
{
    return *std::get<0>(ensureModelAndInterface(contextId));
}

PlatformVideoFullscreenInterface& VideoFullscreenManagerProxy::ensureInterface(PlaybackSessionContextIdentifier contextId)
{
    return *std::get<1>(ensureModelAndInterface(contextId));
}

PlatformVideoFullscreenInterface* VideoFullscreenManagerProxy::findInterface(PlaybackSessionContextIdentifier contextId)
{
    auto it = m_contextMap.find(contextId);
    if (it == m_contextMap.end())
        return nullptr;

    return std::get<1>(it->value).get();
}

void VideoFullscreenManagerProxy::ensureClientForContext(PlaybackSessionContextIdentifier contextId)
{
    m_clientCounts.add(contextId, 1);
}

void VideoFullscreenManagerProxy::addClientForContext(PlaybackSessionContextIdentifier contextId)
{
    auto addResult = m_clientCounts.add(contextId, 1);
    if (!addResult.isNewEntry)
        addResult.iterator->value++;
}

void VideoFullscreenManagerProxy::removeClientForContext(PlaybackSessionContextIdentifier contextId)
{
    if (!m_clientCounts.contains(contextId))
        return;

    int clientCount = m_clientCounts.get(contextId);
    ASSERT(clientCount > 0);
    clientCount--;

    if (clientCount <= 0) {
        ensureInterface(contextId).setVideoFullscreenModel(nullptr);
        m_playbackSessionManagerProxy->removeClientForContext(contextId);
        m_clientCounts.remove(contextId);
        m_contextMap.remove(contextId);
        return;
    }

    m_clientCounts.set(contextId, clientCount);
}

void VideoFullscreenManagerProxy::forEachSession(Function<void(VideoFullscreenModel&, PlatformVideoFullscreenInterface&)>&& callback)
{
    if (m_contextMap.isEmpty())
        return;

    Vector<ModelInterfaceTuple> values;
    values.reserveInitialCapacity(m_contextMap.size());
    for (auto& value : m_contextMap.values())
        values.uncheckedAppend(value);

    for (auto& value : values) {
        RefPtr<VideoFullscreenModelContext> model;
        RefPtr<PlatformVideoFullscreenInterface> interface;
        std::tie(model, interface) = value;

        ASSERT(model);
        ASSERT(interface);
        if (!model || !interface)
            continue;

        callback(*model, *interface);
    }
}

void VideoFullscreenManagerProxy::hasVideoInPictureInPictureDidChange(bool value)
{
    m_page->uiClient().hasVideoInPictureInPictureDidChange(m_page, value);
    if (m_client)
        m_client->hasVideoInPictureInPictureDidChange(value);
}

#pragma mark Messages from VideoFullscreenManager

void VideoFullscreenManagerProxy::setHasVideo(PlaybackSessionContextIdentifier contextId, bool hasVideo)
{
    if (m_mockVideoPresentationModeEnabled)
        return;

    if (auto* interface = findInterface(contextId))
        interface->hasVideoChanged(hasVideo);
}

void VideoFullscreenManagerProxy::setVideoDimensions(PlaybackSessionContextIdentifier contextId, const FloatSize& videoDimensions)
{
    auto* interface = findInterface(contextId);
    if (!interface)
        return;

    if (m_mockVideoPresentationModeEnabled) {
        if (videoDimensions.isEmpty())
            return;

        m_mockPictureInPictureWindowSize.setHeight(DefaultMockPictureInPictureWindowWidth / videoDimensions.aspectRatio());
        return;
    }

    interface->videoDimensionsChanged(videoDimensions);
}

void VideoFullscreenManagerProxy::enterFullscreen(PlaybackSessionContextIdentifier contextId)
{
    if (m_mockVideoPresentationModeEnabled) {
        didEnterFullscreen(contextId, m_mockPictureInPictureWindowSize);
        return;
    }

    auto& interface = ensureInterface(contextId);
    interface.enterFullscreen();

    // Only one context can be in a given full screen mode at a time:
    for (auto& contextPair : m_contextMap) {
        auto& otherContextId = contextPair.key;
        if (contextId == otherContextId)
            continue;

        auto& otherInterface = std::get<1>(contextPair.value);
        if (otherInterface->hasMode(interface.mode()))
            otherInterface->requestHideAndExitFullscreen();
    }
}

#if !PLATFORM(IOS_FAMILY)

NO_RETURN_DUE_TO_ASSERT void VideoFullscreenManagerProxy::setInlineRect(PlaybackSessionContextIdentifier, const WebCore::IntRect&, bool)
{
    ASSERT_NOT_REACHED();
}

NO_RETURN_DUE_TO_ASSERT void VideoFullscreenManagerProxy::setHasVideoContentLayer(PlaybackSessionContextIdentifier, bool)
{
    ASSERT_NOT_REACHED();
}

#endif

void VideoFullscreenManagerProxy::cleanupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    if (m_mockVideoPresentationModeEnabled) {
        didCleanupFullscreen(contextId);
        return;
    }

    ensureInterface(contextId).cleanupFullscreen();
}

void VideoFullscreenManagerProxy::preparedToExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    if (m_mockVideoPresentationModeEnabled)
        return;

    ensureInterface(contextId).preparedToExitFullscreen();
}

#pragma mark Messages to VideoFullscreenManager

void VideoFullscreenManagerProxy::requestFullscreenMode(PlaybackSessionContextIdentifier contextId, WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode, bool finishedWithMedia)
{
    m_page->send(Messages::VideoFullscreenManager::RequestFullscreenMode(contextId, mode, finishedWithMedia));
}

void VideoFullscreenManagerProxy::requestUpdateInlineRect(PlaybackSessionContextIdentifier contextId)
{
    m_page->send(Messages::VideoFullscreenManager::RequestUpdateInlineRect(contextId));
}

void VideoFullscreenManagerProxy::requestVideoContentLayer(PlaybackSessionContextIdentifier contextId)
{
    m_page->send(Messages::VideoFullscreenManager::RequestVideoContentLayer(contextId));
}

void VideoFullscreenManagerProxy::returnVideoContentLayer(PlaybackSessionContextIdentifier contextId)
{
    m_page->send(Messages::VideoFullscreenManager::ReturnVideoContentLayer(contextId));
}

void VideoFullscreenManagerProxy::willExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    m_page->send(Messages::VideoFullscreenManager::WillExitFullscreen(contextId));
}

void VideoFullscreenManagerProxy::didExitFullscreen(PlaybackSessionContextIdentifier contextId)
{
    m_page->send(Messages::VideoFullscreenManager::DidExitFullscreen(contextId));
    m_page->didExitFullscreen();
}

void VideoFullscreenManagerProxy::didEnterFullscreen(PlaybackSessionContextIdentifier contextId, const WebCore::FloatSize& size)
{
    Optional<FloatSize> optionalSize;
    if (!size.isEmpty())
        optionalSize = size;

    m_page->send(Messages::VideoFullscreenManager::DidEnterFullscreen(contextId, optionalSize));
    m_page->didEnterFullscreen();
}

void VideoFullscreenManagerProxy::setVideoLayerGravity(PlaybackSessionContextIdentifier contextId, WebCore::MediaPlayerEnums::VideoGravity gravity)
{
    m_page->send(Messages::VideoFullscreenManager::SetVideoLayerGravityEnum(contextId, (unsigned)gravity));
}

void VideoFullscreenManagerProxy::fullscreenModeChanged(PlaybackSessionContextIdentifier contextId, WebCore::HTMLMediaElementEnums::VideoFullscreenMode mode)
{
    m_page->send(Messages::VideoFullscreenManager::FullscreenModeChanged(contextId, mode));
}

void VideoFullscreenManagerProxy::fullscreenMayReturnToInline(PlaybackSessionContextIdentifier contextId)
{
    m_page->send(Messages::VideoFullscreenManager::FullscreenMayReturnToInline(contextId, m_page->isViewVisible()));
}

} // namespace WebKit

#undef MESSAGE_CHECK
