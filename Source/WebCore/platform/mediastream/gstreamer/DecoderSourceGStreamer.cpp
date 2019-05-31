/*
 *  Copyright (C) 2017 Igalia S.L. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#if USE(GSTREAMER_WEBRTC)
#include "DecoderSourceGStreamer.h"

#include <gst/app/gstappsink.h>

namespace WebCore {

DecoderSourceGStreamer::DecoderSourceGStreamer(GstElement* pipeline, GstPad* incomingSrcPad)
{
    if (!pipeline)
        return;

    m_pipeline = pipeline;
    m_queue1 = gst_element_factory_make("queue", nullptr);
    m_decoder = gst_element_factory_make("decodebin", nullptr);
    m_queue2 = gst_element_factory_make("queue", nullptr);
    m_sink = gst_element_factory_make("appsink", nullptr);

    g_object_set(m_sink.get(), "sync", false, "enable-last-sample", false, nullptr);
    gst_app_sink_set_emit_signals(GST_APP_SINK(m_sink.get()), true);
    g_signal_connect(m_sink.get(), "new-sample", G_CALLBACK(+[](GstElement* sink, gpointer userData) -> GstFlowReturn {
        GRefPtr<GstSample> sample = adoptGRef(gst_app_sink_pull_sample(GST_APP_SINK(sink)));
        if (sample) {
            auto source = reinterpret_cast<DecoderSourceGStreamer*>(userData);
            source->handleDecodedSample(sample.get());
        }
        return GST_FLOW_OK;
    }), this);

    g_signal_connect(m_decoder.get(), "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer userData) {
        auto source = reinterpret_cast<DecoderSourceGStreamer*>(userData);
        source->linkDecodePad(pad);
    }), static_cast<gpointer>(this));

    g_signal_connect(m_decoder.get(), "no-more-pads", G_CALLBACK(+[](GstElement* decodebin, gpointer) {
        GRefPtr<GstObject> parent = adoptGRef(gst_element_get_parent(decodebin));
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(parent.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-incoming-decoded-stream");
    }), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_queue1.get(), m_decoder.get(), m_queue2.get(), m_sink.get(), nullptr);

    gst_element_link(m_queue1.get(), m_decoder.get());

    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_queue1.get(), "sink"));
    gst_pad_link(incomingSrcPad, sinkPad.get());

    gst_element_set_state(m_queue1.get(), GST_STATE_PLAYING);
    gst_element_set_state(m_decoder.get(), GST_STATE_PLAYING);
}

void DecoderSourceGStreamer::linkDecodePad(GstPad* srcPad)
{
    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_queue2.get(), "sink"));
    RELEASE_ASSERT(!gst_pad_is_linked(sinkPad.get()));

    gst_element_link(m_queue2.get(), m_sink.get());
    gst_element_sync_state_with_parent(m_queue2.get());
    gst_element_sync_state_with_parent(m_sink.get());

    gst_pad_link(srcPad, sinkPad.get());
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
