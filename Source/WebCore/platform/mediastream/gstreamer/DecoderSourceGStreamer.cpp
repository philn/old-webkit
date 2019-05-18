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

static void onDecodeBinPadAddedCallback(GstElement*, GstPad* pad, gpointer userData)
{
    auto source = reinterpret_cast<DecoderSourceGStreamer*>(userData);
    // GRefPtr<GstPad> srcPad = gst_ghost_pad_new(nullptr, pad);
    // gst_pad_set_active(srcPad.get(), TRUE);
    // gst_element_add_pad(source->sourceBin(), srcPad.get());
    //source->padExposed(srcPad.get());
    source->linkDecodePad(pad);
}

static void onDecodeBinReady(GstElement* decodebin, gpointer)
{
    //gst_element_sync_state_with_parent(decodebin);
    gst_element_set_state(decodebin, GST_STATE_PLAYING);

    GRefPtr<GstObject> parent = adoptGRef(gst_element_get_parent(decodebin));
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(parent.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-incoming-decoded-stream");
}

static GstFlowReturn newSampleCallback(GstElement*, gpointer userData)
{
    auto source = reinterpret_cast<DecoderSourceGStreamer*>(userData);
    return source->pullDecodedSample();
}

DecoderSourceGStreamer::DecoderSourceGStreamer(GstElement* pipeline, GstPad* incomingSrcPad)
{
    if (!pipeline)
        return;

    m_pipeline = pipeline;

    m_decoder = gst_element_factory_make("decodebin", nullptr);
    m_sink = gst_element_factory_make("appsink", nullptr);

    gst_app_sink_set_emit_signals(GST_APP_SINK(m_sink.get()), true);
    g_signal_connect(m_sink.get(), "new-sample", G_CALLBACK(newSampleCallback), this);
    g_object_set(m_sink.get(), "sync", false, nullptr);

    g_signal_connect(m_decoder.get(), "pad-added", G_CALLBACK(onDecodeBinPadAddedCallback), static_cast<gpointer>(this));
    g_signal_connect(m_decoder.get(), "no-more-pads", G_CALLBACK(onDecodeBinReady), nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_decoder.get(), m_sink.get(), nullptr);

    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_decoder.get(), "sink"));
    gst_pad_link(incomingSrcPad, sinkPad.get());
    //gst_element_link(sourceElement, m_decoder.get());
    //gst_element_sync_state_with_parent(m_decoder.get());
    // gst_element_set_state(m_decoder.get(), GST_STATE_READY);
}

void DecoderSourceGStreamer::linkDecodePad(GstPad* srcPad)
{
    GRefPtr<GstPad> sinkPad = gst_element_get_static_pad(m_sink.get(), "sink");
    gst_pad_link(srcPad, sinkPad.get());
    gst_element_sync_state_with_parent(m_sink.get());
}

GstFlowReturn DecoderSourceGStreamer::pullDecodedSample()
{
    auto sample = gst_app_sink_pull_sample(GST_APP_SINK(m_sink.get()));
    // auto buffer = gst_sample_get_buffer(sample);
    handleDecodedSample(sample);
    return GST_FLOW_OK;
}

void DecoderSourceGStreamer::preroll()
{
    if (m_decoder)
        gst_element_set_state(m_decoder.get(), GST_STATE_READY);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
