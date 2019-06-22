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
#include "GStreamerCommon.h"

namespace WebCore {

DecoderSourceGStreamer::DecoderSourceGStreamer(GstElement* pipeline, GstPad* incomingSrcPad)
{
    if (!pipeline)
        return;

    m_pad = incomingSrcPad;
    m_pipeline = pipeline;
    m_valve = gst_element_factory_make("valve", nullptr);
    m_queue1 = gst_element_factory_make("queue", nullptr);
    m_decoder = gst_element_factory_make("decodebin", nullptr);
    m_queue2 = gst_element_factory_make("queue", nullptr);
    m_tee = gst_element_factory_make("tee", nullptr);
    g_object_set(m_tee.get(), "allow-not-linked", true, nullptr);

    g_signal_connect(m_decoder.get(), "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer userData) {
        auto source = reinterpret_cast<DecoderSourceGStreamer*>(userData);
        source->linkDecodePad(pad);
    }), static_cast<gpointer>(this));

    g_signal_connect(m_decoder.get(), "no-more-pads", G_CALLBACK(+[](GstElement* decodebin, gpointer) {
        GRefPtr<GstObject> parent = adoptGRef(gst_element_get_parent(decodebin));
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(parent.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-incoming-decoded-stream");
    }), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_valve.get(), m_queue1.get(), m_decoder.get(), m_queue2.get(), m_tee.get(), nullptr);

    gst_element_link_many(m_valve.get(), m_queue1.get(), m_decoder.get(), nullptr);

    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_valve.get(), "sink"));
    gst_pad_link(incomingSrcPad, sinkPad.get());

    gst_element_set_state(m_valve.get(), GST_STATE_PLAYING);
    gst_element_set_state(m_queue1.get(), GST_STATE_PLAYING);
    gst_element_set_state(m_decoder.get(), GST_STATE_PLAYING);
}

void DecoderSourceGStreamer::linkDecodePad(GstPad* srcPad)
{
    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_queue2.get(), "sink"));
    RELEASE_ASSERT(!gst_pad_is_linked(sinkPad.get()));

    gst_element_link(m_queue2.get(), m_tee.get());
    gst_element_sync_state_with_parent(m_queue2.get());
    gst_element_sync_state_with_parent(m_tee.get());

    gst_pad_link(srcPad, sinkPad.get());
}

void DecoderSourceGStreamer::lockValve() const
{
    if (m_valve)
        g_object_set(m_valve.get(), "drop", true, nullptr);
}

void DecoderSourceGStreamer::releaseValve() const
{
    if (m_valve)
        g_object_set(m_valve.get(), "drop", false, nullptr);
}

GstElement* DecoderSourceGStreamer::registerClient()
{
    auto queue = gst_element_factory_make("queue", nullptr);
    auto proxy = createProxy();

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), queue, proxy.sink, nullptr);
    gst_element_link_many(m_tee.get(), queue, proxy.sink, nullptr);
    gst_element_sync_state_with_parent(queue);
    gst_element_sync_state_with_parent(proxy.sink);
    return proxy.source;
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
