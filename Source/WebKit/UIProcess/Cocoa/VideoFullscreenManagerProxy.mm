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
#import "VideoFullscreenManagerProxy.h"

#if ENABLE(VIDEO_PRESENTATION_MODE)

#import "APIUIClient.h"
#import "DrawingAreaProxy.h"
#import "PlaybackSessionManagerProxy.h"
#import "WebPageProxy.h"
#import "WebProcessProxy.h"
#import <QuartzCore/CoreAnimation.h>
#import <WebCore/MediaPlayerEnums.h>
#import <WebCore/TimeRanges.h>
#import <pal/spi/cocoa/QuartzCoreSPI.h>
#import <wtf/MachSendRight.h>
#import <wtf/WeakObjCPtr.h>

#if PLATFORM(IOS_FAMILY)
#import "RemoteLayerTreeDrawingAreaProxy.h"
#import "UIKitSPI.h"
#import <pal/spi/cocoa/AVKitSPI.h>
#endif

@interface WKLayerHostView : PlatformView
@property (nonatomic, assign) uint32_t contextID;
@end

@implementation WKLayerHostView

#if PLATFORM(IOS_FAMILY)
+ (Class)layerClass {
    return [CALayerHost class];
}
#else
- (CALayer *)makeBackingLayer
{
    return [[[CALayerHost alloc] init] autorelease];
}
#endif

- (uint32_t)contextID {
    return [[self layerHost] contextId];
}

- (void)setContextID:(uint32_t)contextID {
    [[self layerHost] setContextId:contextID];
}

- (CALayerHost *)layerHost {
    return (CALayerHost *)[self layer];
}

@end

#if PLATFORM(IOS_FAMILY)
@interface WKVideoFullScreenViewController : UIViewController
- (instancetype)initWithAVPlayerViewController:(AVPlayerViewController *)viewController NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder *)aDecoder NS_UNAVAILABLE;
- (instancetype)init NS_UNAVAILABLE;
@end

@implementation WKVideoFullScreenViewController {
    WeakObjCPtr<AVPlayerViewController> _avPlayerViewController;
}

- (instancetype)initWithAVPlayerViewController:(AVPlayerViewController *)controller
{
    if (!(self = [super initWithNibName:nil bundle:nil]))
        return nil;

    _avPlayerViewController = controller;
    self.modalPresentationCapturesStatusBarAppearance = YES;
    self.modalPresentationStyle = UIModalPresentationOverFullScreen;

    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    self.view.frame = UIScreen.mainScreen.bounds;
    self.view.backgroundColor = [UIColor blackColor];
    [_avPlayerViewController view].autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

@end

UIViewController *VideoFullscreenModelContext::presentingViewController()
{
    if (m_manager)
        return m_manager->m_page->uiClient().presentingViewController();

    return nullptr;
}

UIViewController *VideoFullscreenModelContext::createVideoFullscreenViewController(AVPlayerViewController *avPlayerViewController)
{
    return [[WKVideoFullScreenViewController alloc] initWithAVPlayerViewController:avPlayerViewController];
}

#endif // PLATFORM(IOS_FAMILY)

#if PLATFORM(IOS_FAMILY) || PLATFORM(MAC)
void VideoFullscreenModelContext::requestRouteSharingPolicyAndContextUID(CompletionHandler<void(WebCore::RouteSharingPolicy, String)>&& completionHandler)
{
    if (m_manager)
        m_manager->requestRouteSharingPolicyAndContextUID(m_contextId, WTFMove(completionHandler));
    else
        completionHandler(WebCore::RouteSharingPolicy::Default, emptyString());
}

void VideoFullscreenManagerProxy::requestRouteSharingPolicyAndContextUID(PlaybackSessionContextIdentifier contextId, CompletionHandler<void(WebCore::RouteSharingPolicy, String)>&& callback)
{
    m_page->sendWithAsyncReply(Messages::VideoFullscreenManager::RequestRouteSharingPolicyAndContextUID(contextId), WTFMove(callback));
}
#endif

#if PLATFORM(IOS_FAMILY) && !HAVE(AVKIT)
RefPtr<VideoFullscreenManagerProxy> VideoFullscreenManagerProxy::create(WebPageProxy&)
{
    return nullptr;
}

void VideoFullscreenManagerProxy::invalidate()
{
}

bool VideoFullscreenManagerProxy::hasMode(HTMLMediaElementEnums::VideoFullscreenMode) const
{
    return false;
}

bool VideoFullscreenManagerProxy::mayAutomaticallyShowVideoPictureInPicture() const
{
    return false;
}

void VideoFullscreenManagerProxy::requestHideAndExitFullscreen()
{
}

void VideoFullscreenManagerProxy::applicationDidBecomeActive()
{
}
#endif

void VideoFullscreenManagerProxy::platformInvalidate()
{
    auto contextMap = WTFMove(m_contextMap);
    m_clientCounts.clear();

    for (auto& [model, interface] : contextMap.values()) {
        interface->invalidate();
        [model->layerHostView() removeFromSuperview];
        model->setLayerHostView(nullptr);
    }
}

#if PLATFORM(IOS_FAMILY)

void VideoFullscreenManagerProxy::setInlineRect(PlaybackSessionContextIdentifier contextId, const WebCore::IntRect& inlineRect, bool visible)
{
    if (m_mockVideoPresentationModeEnabled)
        return;

    ensureInterface(contextId).setInlineRect(inlineRect, visible);
}

void VideoFullscreenManagerProxy::setHasVideoContentLayer(PlaybackSessionContextIdentifier contextId, bool value)
{
    if (m_mockVideoPresentationModeEnabled) {
        if (value)
            didSetupFullscreen(contextId);
        else
            didExitFullscreen(contextId);

        return;
    }

    ensureInterface(contextId).setHasVideoContentLayer(value);
}

#endif

#define MESSAGE_CHECK(assertion) MESSAGE_CHECK_BASE(assertion, m_page->process().connection())

void VideoFullscreenManagerProxy::setupFullscreenWithID(PlaybackSessionContextIdentifier contextId, WebKit::LayerHostingContextID videoLayerID, const WebCore::IntRect& initialRect, const WebCore::FloatSize& videoDimensions, float hostingDeviceScaleFactor, HTMLMediaElementEnums::VideoFullscreenMode videoFullscreenMode, bool allowsPictureInPicture, bool standby)
{
    MESSAGE_CHECK(videoLayerID);

    auto& [model, interface] = ensureModelAndInterface(contextId);
    ensureClientForContext(contextId);

    if (m_mockVideoPresentationModeEnabled) {
        if (!videoDimensions.isEmpty())
            m_mockPictureInPictureWindowSize.setHeight(DefaultMockPictureInPictureWindowWidth /  videoDimensions.aspectRatio());
#if PLATFORM(IOS_FAMILY)
        requestVideoContentLayer(contextId);
#else
        didSetupFullscreen(contextId);
#endif
        return;
    }

    RetainPtr<WKLayerHostView> view = static_cast<WKLayerHostView*>(model->layerHostView());
    if (!view) {
        view = adoptNS([[WKLayerHostView alloc] init]);
#if PLATFORM(MAC)
        [view setWantsLayer:YES];
#endif
        model->setLayerHostView(view);
    }
    [view setContextID:videoLayerID];
    if (hostingDeviceScaleFactor != 1) {
        // Invert the scale transform added in the WebProcess to fix <rdar://problem/18316542>.
        float inverseScale = 1 / hostingDeviceScaleFactor;
        [[view layer] setSublayerTransform:CATransform3DMakeScale(inverseScale, inverseScale, 1)];
    }

#if PLATFORM(IOS_FAMILY)
    auto* rootNode = downcast<RemoteLayerTreeDrawingAreaProxy>(*m_page->drawingArea()).remoteLayerTreeHost().rootNode();
    UIView *parentView = rootNode ? rootNode->uiView() : nil;
    interface->setupFullscreen(*model->layerHostView(), initialRect, videoDimensions, parentView, videoFullscreenMode, allowsPictureInPicture, standby);
#else
    UNUSED_PARAM(videoDimensions);
    IntRect initialWindowRect;
    m_page->rootViewToWindow(initialRect, initialWindowRect);
    interface->setupFullscreen(*model->layerHostView(), initialWindowRect, m_page->platformWindow(), videoFullscreenMode, allowsPictureInPicture);
#endif
}

void VideoFullscreenManagerProxy::exitFullscreen(PlaybackSessionContextIdentifier contextId, WebCore::IntRect finalRect, CompletionHandler<void(bool)>&& completionHandler)
{
    ASSERT(m_contextMap.contains(contextId));
    if (!m_contextMap.contains(contextId)) {
        completionHandler(false);
        return;
    }

#if PLATFORM(MAC)
    IntRect finalWindowRect;
    m_page->rootViewToWindow(finalRect, finalWindowRect);
#endif

    if (m_mockVideoPresentationModeEnabled) {
#if PLATFORM(IOS_FAMILY)
        returnVideoContentLayer(contextId);
#else
        didExitFullscreen(contextId);
#endif
        completionHandler(true);
        return;
    }

#if PLATFORM(IOS_FAMILY)
    completionHandler(ensureInterface(contextId).exitFullscreen(finalRect));
#else
    completionHandler(ensureInterface(contextId).exitFullscreen(finalWindowRect, m_page->platformWindow()));
#endif
}

void VideoFullscreenManagerProxy::exitFullscreenWithoutAnimationToMode(PlaybackSessionContextIdentifier contextId, WebCore::HTMLMediaElementEnums::VideoFullscreenMode targetMode)
{
    if (m_mockVideoPresentationModeEnabled)
        return;

#if PLATFORM(MAC)
    ensureInterface(contextId).exitFullscreenWithoutAnimationToMode(targetMode);
#else
    auto& [model, interface] = ensureModelAndInterface(contextId);
    interface->invalidate();
    [model->layerHostView() removeFromSuperview];
    model->setLayerHostView(nullptr);
    removeClientForContext(contextId);
#endif

    hasVideoInPictureInPictureDidChange(targetMode & MediaPlayerEnums::VideoFullscreenModePictureInPicture);
}

void VideoFullscreenManagerProxy::preparedToReturnToInline(PlaybackSessionContextIdentifier contextId, bool visible, WebCore::IntRect inlineRect)
{
    m_page->fullscreenMayReturnToInline();

#if PLATFORM(MAC)
    IntRect inlineWindowRect;
    m_page->rootViewToWindow(inlineRect, inlineWindowRect);
#endif

    if (m_mockVideoPresentationModeEnabled)
        return;

#if PLATFORM(IOS_FAMILY)
    ensureInterface(contextId).preparedToReturnToInline(visible, inlineRect);
#else
    ensureInterface(contextId).preparedToReturnToInline(visible, inlineWindowRect, m_page->platformWindow());
#endif
}

void VideoFullscreenManagerProxy::didSetupFullscreen(PlaybackSessionContextIdentifier contextId)
{
#if PLATFORM(IOS_FAMILY)
    enterFullscreen(contextId);
#else
    m_page->send(Messages::VideoFullscreenManager::DidSetupFullscreen(contextId));
#endif
}

void VideoFullscreenManagerProxy::didCleanupFullscreen(PlaybackSessionContextIdentifier contextId)
{
    auto& [model, interface] = ensureModelAndInterface(contextId);

    [CATransaction flush];
    [model->layerHostView() removeFromSuperview];
    model->setLayerHostView(nullptr);
    m_page->send(Messages::VideoFullscreenManager::DidCleanupFullscreen(contextId));

    interface->setMode(HTMLMediaElementEnums::VideoFullscreenModeNone, false);
    removeClientForContext(contextId);
}

void VideoFullscreenManagerProxy::setVideoLayerFrame(PlaybackSessionContextIdentifier contextId, WebCore::FloatRect frame)
{
#if PLATFORM(IOS_FAMILY)
    auto fenceSendRight = MachSendRight::adopt([UIWindow _synchronizeDrawingAcrossProcesses]);
#else
    MachSendRight fenceSendRight;
    if (DrawingAreaProxy* drawingArea = m_page->drawingArea())
        fenceSendRight = drawingArea->createFence();
#endif

    m_page->send(Messages::VideoFullscreenManager::SetVideoLayerFrameFenced(contextId, frame, fenceSendRight));
}

#undef MESSAGE_CHECK

#endif // ENABLE(VIDEO_PRESENTATION_MODE)
