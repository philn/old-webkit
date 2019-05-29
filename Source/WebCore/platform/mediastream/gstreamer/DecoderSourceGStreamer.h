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

#pragma once

#if USE(GSTREAMER_WEBRTC)

#include <gst/gst.h>
#include "GRefPtrGStreamer.h"

namespace WebCore {

class DecoderSourceGStreamer {
 public:
    void linkDecodePad(GstPad*);
    GstFlowReturn pullDecodedSample();

    virtual void handleDecodedSample(GstSample*) = 0;

 protected:
    DecoderSourceGStreamer(GstElement*, GstPad*);

 private:
    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_queue1;
    GRefPtr<GstElement> m_decoder;
    GRefPtr<GstElement> m_queue2;
    GRefPtr<GstElement> m_sink;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
