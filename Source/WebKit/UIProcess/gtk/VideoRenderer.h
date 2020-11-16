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

#pragma once

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define WEBKIT_TYPE_VIDEO_RENDERER            (video_renderer_get_type())
#define WEBKIT_VIDEO_RENDERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), WEBKIT_TYPE_VIDEO_RENDERER, VideoRenderer))
#define WEBKIT_IS_VIDEO_RENDERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), WEBKIT_TYPE_VIDEO_RENDERER))
#define WEBKIT_VIDEO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  WEBKIT_TYPE_VIDEO_RENDERER, VideoRendererClass))
#define WEBKIT_IS_VIDEO_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  WEBKIT_TYPE_VIDEO_RENDERER))
#define WEBKIT_VIDEO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  WEBKIT_TYPE_VIDEO_RENDERER, VideoRendererClass))

typedef struct _VideoRenderer VideoRenderer;
typedef struct _VideoRendererClass VideoRendererClass;
typedef struct _VideoRendererPrivate VideoRendererPrivate;

struct _VideoRenderer {
    GtkWidget parent;

    VideoRendererPrivate *priv;
};

struct _VideoRendererClass {
    GtkWidgetClass parent;
};

GType video_renderer_get_type (void);

G_END_DECLS

namespace WebKit {
class WebPageProxy;
using LayerHostingContextID = uint32_t;
}

VideoRenderer* videoRendererCreate(WebKit::WebPageProxy*, WebKit::LayerHostingContextID);
