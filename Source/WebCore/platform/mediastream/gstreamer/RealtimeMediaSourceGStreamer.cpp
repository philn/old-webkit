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
#include "RealtimeMediaSourceGStreamer.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "CaptureDevice.h"
#include "GStreamerCommon.h"
#include "GStreamerWebRTCUtils.h"
#include "MediaConstraints.h"
#include "RealtimeMediaSourceSettings.h"

namespace WebCore {

static GstPadProbeReturn dropReconfigureEvent(GstPad*, GstPadProbeInfo* info, gpointer)
{
    if (GST_EVENT_TYPE(info->data) == GST_EVENT_RECONFIGURE)
        return GST_PAD_PROBE_DROP;
    return GST_PAD_PROBE_OK;
}

GStreamerRealtimeMediaSource::GStreamerRealtimeMediaSource(RealtimeMediaSource::Type type, String&& name)
    : RealtimeMediaSource(type, name)
{
    m_pipeline = adoptGRef(gst_pipeline_new(nullptr));
    gst_pipeline_use_clock(GST_PIPELINE(m_pipeline.get()), gst_system_clock_obtain());
    gst_element_set_base_time(m_pipeline.get(), getWebRTCBaseTime());
    gst_element_set_start_time(m_pipeline.get(), GST_CLOCK_TIME_NONE);

    m_capsFilter = gst_element_factory_make("capsfilter", nullptr);
    GstElement* queue = gst_element_factory_make("queue", nullptr);
    m_tee = gst_element_factory_make("tee", nullptr);
    g_object_set(m_tee.get(), "allow-not-linked", TRUE, nullptr);

    // GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(m_tee.get(), "sink"));
    // gst_pad_add_probe(sinkPad.get(), GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, dropReconfigureEvent, nullptr, nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_capsFilter.get(), queue, m_tee.get(), nullptr);
    gst_element_link_many(m_capsFilter.get(), queue, m_tee.get(), nullptr);
}

GStreamerRealtimeMediaSource::~GStreamerRealtimeMediaSource()
{
    gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
}

void GStreamerRealtimeMediaSource::setGstSourceElement(GstElement* element)
{
    if (m_element) {
        gst_element_unlink(m_element.get(), m_capsFilter.get());
        gst_bin_remove(GST_BIN_CAST(m_pipeline.get()), m_element.get());
    }
    m_element = element;
    gst_bin_add(GST_BIN_CAST(m_pipeline.get()), m_element.get());
    if (gst_element_link(m_element.get(), m_capsFilter.get()))
        gst_element_sync_state_with_parent(m_element.get());
}

// void GStreamerRealtimeMediaSource::initializeCapabilities()
// {
//     m_capabilities = std::make_unique<RealtimeMediaSourceCapabilities>(supportedConstraints());
//     m_capabilities->setDeviceId(id());
//     initializeCapabilities(*m_capabilities.get());
// }

// const RealtimeMediaSourceCapabilities& GStreamerRealtimeMediaSource::capabilities()
// {
//     if (!m_capabilities)
//         const_cast<GStreamerRealtimeMediaSource&>(*this).initializeCapabilities();
//     return *m_capabilities;
// }

// void GStreamerRealtimeMediaSource::initializeSettings()
// {
//     if (m_currentSettings.deviceId().isEmpty()) {
//         m_currentSettings.setSupportedConstraints(supportedConstraints());
//         m_currentSettings.setDeviceId(id());
//     }

//     updateSettings(m_currentSettings);
// }

// const RealtimeMediaSourceSettings& GStreamerRealtimeMediaSource::settings()
// {
//     const_cast<GStreamerRealtimeMediaSource&>(*this).initializeSettings();
//     return m_currentSettings;
// }

// RealtimeMediaSourceSupportedConstraints& GStreamerRealtimeMediaSource::supportedConstraints()
// {
//     if (m_supportedConstraints.supportsDeviceId())
//         return m_supportedConstraints;

//     m_supportedConstraints.setSupportsDeviceId(true);
//     initializeSupportedConstraints(m_supportedConstraints);

//     return m_supportedConstraints;
// }

void GStreamerRealtimeMediaSource::startProducingData()
{
    g_printerr("Start\n");
    g_object_set(m_capsFilter.get(), "caps", caps(), nullptr);
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(m_pipeline.get()));

    // if (size().isEmpty()) {
    //     setWidth(640);
    //     setHeight(480);
    // }
}

void GStreamerRealtimeMediaSource::stopProducingData()
{
    gst_element_set_state(m_pipeline.get(), GST_STATE_READY);
}

GstElement* GStreamerRealtimeMediaSource::proxySink()
{
    GstElement* queue = gst_element_factory_make("queue", nullptr);
    GstElement* sink = gst_element_factory_make("proxysink", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), queue, sink, nullptr);
    GRefPtr<GstPad> srcPad = adoptGRef(gst_element_get_request_pad(m_tee.get(), "src_%u"));
    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(queue, "sink"));
    gst_element_link(queue, sink);
    gst_pad_link(srcPad.get(), sinkPad.get());
    gst_element_sync_state_with_parent(queue);
    gst_element_sync_state_with_parent(sink);
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(m_pipeline.get()));

    return sink;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
