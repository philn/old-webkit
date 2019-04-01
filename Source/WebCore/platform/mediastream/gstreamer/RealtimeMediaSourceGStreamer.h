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

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "RealtimeMediaSource.h"

#include <gst/gst.h>

namespace WebCore {

class GStreamerRealtimeMediaSource : public RealtimeMediaSource {
public:
    void setGstSourceElement(GstElement* element);
    GstElement* gstSourceElement() const { return m_element.get(); }

    virtual GstCaps* caps() const = 0;
    GstElement* proxySink();

protected:
    GStreamerRealtimeMediaSource(const String& id, Type, const String& name);
    ~GStreamerRealtimeMediaSource();

    virtual void updateSettings(RealtimeMediaSourceSettings&) = 0;
    virtual void initializeCapabilities(RealtimeMediaSourceCapabilities&) = 0;
    virtual void initializeSupportedConstraints(RealtimeMediaSourceSupportedConstraints&) = 0;

    const RealtimeMediaSourceCapabilities& capabilities() override;
    const RealtimeMediaSourceSettings& settings() override;

    RealtimeMediaSourceSupportedConstraints& supportedConstraints();

    unsigned deviceIndex() { return m_deviceIndex; }

    GstElement* pipeline() const { return m_pipeline.get(); }
    GstElement* capsFilter() const { return m_capsFilter.get(); }

private:
    void initializeCapabilities();
    void initializeSettings();

    void startProducingData() final;
    void stopProducingData() final;

    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_capsFilter;
    GRefPtr<GstElement> m_element;
    GRefPtr<GstElement> m_tee;
    RealtimeMediaSourceSettings m_currentSettings;
    RealtimeMediaSourceSupportedConstraints m_supportedConstraints;
    std::unique_ptr<RealtimeMediaSourceCapabilities> m_capabilities;
    unsigned m_deviceIndex { 0 };
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
