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
#include "DecoderSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

namespace WebCore {

static void onDecodeBinPadAddedCallback(GstElement*, GstPad* pad, gpointer userData)
{
    auto source = reinterpret_cast<DecoderSourceGStreamer*>(userData);
    GRefPtr<GstPad> srcPad = gst_ghost_pad_new(nullptr, pad);
    gst_pad_set_active(srcPad.get(), TRUE);
    gst_element_add_pad(source->sourceBin(), srcPad.get());
    //source->padExposed(srcPad.get());
}

static void onDecodeBinReady(GstElement* decodebin, gpointer)
{
    gst_element_sync_state_with_parent(decodebin);
}

DecoderSourceGStreamer::DecoderSourceGStreamer(GstElement* sourceElement)
{
    if (!sourceElement)
        return;

    m_sourceBin = gst_bin_new(nullptr);
    m_decoder = gst_element_factory_make("decodebin", nullptr);

    g_signal_connect(m_decoder.get(), "pad-added", G_CALLBACK(onDecodeBinPadAddedCallback), static_cast<gpointer>(this));
    g_signal_connect(m_decoder.get(), "no-more-pads", G_CALLBACK(onDecodeBinReady), nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_sourceBin.get()), sourceElement, m_decoder.get(), nullptr);
    gst_element_link(sourceElement, m_decoder.get());
}

void DecoderSourceGStreamer::preroll()
{
    if (m_decoder)
        gst_element_set_state(m_decoder.get(), GST_STATE_READY);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
