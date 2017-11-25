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
#include "MockRealtimeVideoSourceGStreamer.h"
#include "GStreamerUtilities.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

namespace WebCore {

CaptureSourceOrError MockRealtimeVideoSource::create(const String& deviceID, const String& name, const MediaConstraints* constraints)
{
    initializeGStreamer();
    auto source = adoptRef(*new MockRealtimeVideoSourceGStreamer(deviceID));
    if (constraints && source->applyConstraints(*constraints))
        return { };

    return CaptureSourceOrError(WTFMove(source));
}

MockRealtimeVideoSourceGStreamer::MockRealtimeVideoSourceGStreamer(const String& deviceID)
    : GStreamerRealtimeVideoSource({})
{
    GstElement* element = gst_element_factory_make("videotestsrc", deviceID.utf8().data());
    g_object_set(element, "is-live", TRUE, nullptr);
    setGstSourceElement(element);
}

}

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
