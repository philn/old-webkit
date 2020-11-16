/*
 * Copyright (C) 2016 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "VideoFullscreenManagerProxy.h"

#include "VideoFullscreenManagerMessages.h"
#include "VideoFullscreenManagerProxyMessages.h"
#include "VideoRenderer.h"
#include "WebPageProxy.h"
#include "WebProcessProxy.h"

#include <WebCore/HTMLMediaElementEnums.h>
#include <WebCore/MediaPlayerEnums.h>
#include <WebCore/NotImplemented.h>

#if ENABLE(VIDEO_PRESENTATION_MODE)

namespace WebKit {
using namespace WebCore;

void VideoFullscreenManagerProxy::platformInvalidate()
{
    WTFLogAlways("platformInvalidate");
    notImplemented();
}

#define MESSAGE_CHECK(assertion) MESSAGE_CHECK_BASE(assertion, m_page->process().connection())

void VideoFullscreenManagerProxy::setupFullscreenWithID(PlaybackSessionContextIdentifier contextId, WebKit::LayerHostingContextID videoLayerID, const WebCore::IntRect& initialRect, const WebCore::FloatSize& videoDimensions, float hostingDeviceScaleFactor, HTMLMediaElementEnums::VideoFullscreenMode videoFullscreenMode, bool allowsPictureInPicture, bool standby)
{
    MESSAGE_CHECK(videoLayerID);

    auto& [model, interface] = ensureModelAndInterface(contextId);
    ensureClientForContext(contextId);

    if (m_mockVideoPresentationModeEnabled) {
        if (!videoDimensions.isEmpty())
            m_mockPictureInPictureWindowSize.setHeight(DefaultMockPictureInPictureWindowWidth /  videoDimensions.aspectRatio());
        didSetupFullscreen(contextId);
        return;
    }

    WTFLogAlways("setupFullscreenWithID");
    m_renderer = adoptGRef(reinterpret_cast<GtkWidget*>(videoRendererCreate(m_page, videoLayerID)));
    auto* view = model->layerHostView();
    if (view) {
        gtk_container_add(GTK_CONTAINER(view), GTK_WIDGET(m_renderer.get()));
        gtk_widget_show(GTK_WIDGET(m_renderer.get()));
    } else {
        auto container = adoptGRef(reinterpret_cast<GtkWindow*>(gtk_window_new(GTK_WINDOW_TOPLEVEL)));
        gtk_container_add(GTK_CONTAINER(container.get()), GTK_WIDGET(m_renderer.get()));
        // GtkWidget* foo = gtk_label_new("Foo");
        // gtk_container_add(GTK_CONTAINER(container.get()), foo);
        gtk_widget_show_all(GTK_WIDGET(container.get()));
        model->setLayerHostView(WTFMove(container));
    }

    interface->setupFullscreen(*model->layerHostView(), m_page->platformWindow(), videoFullscreenMode, allowsPictureInPicture);
}

#undef MESSAGE_CHECK

void VideoFullscreenManagerProxy::exitFullscreen(PlaybackSessionContextIdentifier contextId, WebCore::IntRect finalRect, CompletionHandler<void(bool)>&& completionHandler)
{
    WTFLogAlways("exitFullscreen");
    ASSERT(m_contextMap.contains(contextId));
    if (!m_contextMap.contains(contextId)) {
        completionHandler(false);
        return;
    }

    if (m_mockVideoPresentationModeEnabled) {
        didExitFullscreen(contextId);
        completionHandler(true);
        return;
    }

    notImplemented();
    completionHandler(ensureInterface(contextId).exitFullscreen(m_page->platformWindow()));
}

void VideoFullscreenManagerProxy::exitFullscreenWithoutAnimationToMode(PlaybackSessionContextIdentifier contextId, WebCore::HTMLMediaElementEnums::VideoFullscreenMode targetMode)
{
    if (m_mockVideoPresentationModeEnabled)
        return;

    ensureInterface(contextId).exitFullscreenWithoutAnimationToMode(targetMode);
    notImplemented();
    hasVideoInPictureInPictureDidChange(targetMode & MediaPlayerEnums::VideoFullscreenModePictureInPicture);
}

void VideoFullscreenManagerProxy::preparedToReturnToInline(PlaybackSessionContextIdentifier contextId, bool visible, WebCore::IntRect inlineRect)
{
    m_page->fullscreenMayReturnToInline();

    if (m_mockVideoPresentationModeEnabled)
        return;

    ensureInterface(contextId).preparedToReturnToInline(visible, m_page->platformWindow());
}

void VideoFullscreenManagerProxy::didSetupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    enterFullscreen(contextId);
}

void VideoFullscreenManagerProxy::didCleanupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    auto& [model, interface] = ensureModelAndInterface(contextId);

    notImplemented();
    m_page->send(Messages::VideoFullscreenManager::DidCleanupFullscreen(contextId));

    interface->setMode(HTMLMediaElementEnums::VideoFullscreenModeNone, false);
    removeClientForContext(contextId);
}

void VideoFullscreenManagerProxy::setVideoLayerFrame(PlaybackSessionContextIdentifier contextId, WebCore::FloatRect frame)
{
    notImplemented();
}

} // namespace WebKit

#endif // ENABLE(VIDEO_PRESENTATION_MODE)
