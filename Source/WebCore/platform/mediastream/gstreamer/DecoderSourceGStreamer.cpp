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

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_queue1.get(), m_sink.get(), nullptr);

    gst_element_link(m_queue1.get(), m_sink.get());

    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_queue1.get(), "sink"));
    gst_pad_link(incomingSrcPad, sinkPad.get());

    gst_element_set_state(m_queue1.get(), GST_STATE_PLAYING);
    gst_element_set_state(m_sink.get(), GST_STATE_PLAYING);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
