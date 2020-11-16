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
#include "VideoRenderer.h"

#include "AcceleratedBackingStore.h"
#include "LayerTreeContext.h"
#include "DrawingAreaProxyCoordinatedGraphics.h"
#include "WebPageProxy.h"

#include <wtf/glib/WTFGType.h>

using namespace WebKit;
using namespace WebCore;

struct _VideoRendererPrivate {
    _VideoRendererPrivate()
        // : updateActivityStateTimer(RunLoop::main(), this, &_VideoRendererPrivate::updateActivityStateTimerFired)
    {
    }

    void updateActivityStateTimerFired()
    {
        // if (!pageProxy)
        //     return;
        // pageProxy->activityStateDidChange(activityStateFlagsToUpdate);
        // activityStateFlagsToUpdate = { };
    }

    // std::unique_ptr<PageClientImpl> pageClient;
    RefPtr<WebPageProxy> pageProxy;

    GtkWindow* toplevelOnScreenWindow { nullptr };

    std::unique_ptr<AcceleratedBackingStore> acceleratedBackingStore;
    LayerTreeContext videoTreeContext;
};

WEBKIT_DEFINE_TYPE(VideoRenderer, video_renderer, GTK_TYPE_WIDGET)

static void videoRendererRealize(GtkWidget* widget)
{
    VideoRenderer* renderer = WEBKIT_VIDEO_RENDERER(widget);
    VideoRendererPrivate* priv = renderer->priv;

    gtk_widget_set_realized(widget, TRUE);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    GdkWindowAttr attributes;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK
        | GDK_EXPOSURE_MASK
        | GDK_BUTTON_PRESS_MASK
        | GDK_BUTTON_RELEASE_MASK
        | GDK_SCROLL_MASK
        | GDK_SMOOTH_SCROLL_MASK
        | GDK_POINTER_MOTION_MASK
        | GDK_ENTER_NOTIFY_MASK
        | GDK_LEAVE_NOTIFY_MASK
        | GDK_KEY_PRESS_MASK
        | GDK_KEY_RELEASE_MASK
        | GDK_BUTTON_MOTION_MASK
        | GDK_BUTTON1_MOTION_MASK
        | GDK_BUTTON2_MOTION_MASK
        | GDK_BUTTON3_MOTION_MASK
        | GDK_TOUCH_MASK;
    attributes.event_mask |= GDK_TOUCHPAD_GESTURE_MASK;

    gint attributesMask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

    GdkWindow* window = gdk_window_new(gtk_widget_get_parent_window(widget), &attributes, attributesMask);
    gtk_widget_set_window(widget, window);
    gdk_window_set_user_data(window, widget);

    if (priv->acceleratedBackingStore)
        priv->acceleratedBackingStore->realize();
}

static void videoRendererUnrealize(GtkWidget* widget)
{
    VideoRenderer* renderer = WEBKIT_VIDEO_RENDERER(widget);

    if (renderer->priv->acceleratedBackingStore)
        renderer->priv->acceleratedBackingStore->unrealize();

    GTK_WIDGET_CLASS(video_renderer_parent_class)->unrealize(widget);
}

static void videoRendererDispose(GObject* gobject)
{
    VideoRenderer* renderer = WEBKIT_VIDEO_RENDERER(gobject);
    // renderer->priv->pageProxy->close();
    renderer->priv->acceleratedBackingStore = nullptr;
    G_OBJECT_CLASS(video_renderer_parent_class)->dispose(gobject);
}

static gboolean videoRendererDraw(GtkWidget* widget, cairo_t* cr)
{
    VideoRenderer* renderer = WEBKIT_VIDEO_RENDERER(widget);
    WTFLogAlways("draw 0");
    auto* drawingArea = static_cast<DrawingAreaProxyCoordinatedGraphics*>(renderer->priv->pageProxy->drawingArea());
    if (!drawingArea)
        return FALSE;

    WTFLogAlways("draw 1");
    GdkRectangle clipRect;
    if (!gdk_cairo_get_clip_rectangle(cr, &clipRect))
        return FALSE;

    WTFLogAlways("draw 2");
    if (drawingArea->isInAcceleratedCompositingMode()) {
        WTFLogAlways("draw 3");
        ASSERT(renderer->priv->acceleratedBackingStore);
        renderer->priv->acceleratedBackingStore->paint(cr, clipRect);
    } else {
        WTFLogAlways("draw 4");
        WebCore::Region unpaintedRegion; // This is simply unused.
        drawingArea->paint(cr, clipRect, unpaintedRegion);
    }

    WTFLogAlways("draw done");
    //GTK_WIDGET_CLASS(video_renderer_parent_class)->draw(widget, cr);
    return FALSE;
}

static void videoRendererGetPreferredWidth(GtkWidget* widget, gint* min, gint* natural)
{
    gint video_width = 320;

    if (min)
        *min = 1;
    if (natural)
        *natural = video_width;
}

static void videoRendererGetPreferredHeight(GtkWidget* widget, gint* min, gint* natural)
{
    gint video_height = 240;

    if (min)
        *min = 1;
    if (natural)
        *natural = video_height;
}

static void video_renderer_class_init(VideoRendererClass* videoRendererClass)
{
    GtkWidgetClass* widgetClass = GTK_WIDGET_CLASS(videoRendererClass);
    widgetClass->realize = videoRendererRealize;
    widgetClass->unrealize = videoRendererUnrealize;
    widgetClass->draw = videoRendererDraw;
    widgetClass->get_preferred_width = videoRendererGetPreferredWidth;
    widgetClass->get_preferred_height = videoRendererGetPreferredHeight;

    GObjectClass* gobjectClass = G_OBJECT_CLASS(videoRendererClass);
    // gobjectClass->constructed = videoRendererConstructed;
    gobjectClass->dispose = videoRendererDispose;

// #if !USE(GTK4)
//     GtkContainerClass* containerClass = GTK_CONTAINER_CLASS(videoRendererClass);
//     containerClass->add = videoRendererContainerAdd;
//     containerClass->remove = videoRendererContainerRemove;
//     containerClass->forall = videoRendererContainerForall;
// #endif

    // gtk_widget_class_set_css_name(widgetClass, "webkitwebview");
}

VideoRenderer* videoRendererCreate(WebPageProxy* page, WebKit::LayerHostingContextID videoLayerID)
{
    auto* renderer = WEBKIT_VIDEO_RENDERER(g_object_new(WEBKIT_TYPE_VIDEO_RENDERER, nullptr));
    auto* priv = renderer->priv;
    priv->pageProxy = page;
    priv->acceleratedBackingStore = AcceleratedBackingStore::create(*priv->pageProxy);
    priv->videoTreeContext.contextID = videoLayerID;
    priv->acceleratedBackingStore->update(priv->videoTreeContext);
    // priv->pageProxy->initializeWebPage();

    return renderer;
}
