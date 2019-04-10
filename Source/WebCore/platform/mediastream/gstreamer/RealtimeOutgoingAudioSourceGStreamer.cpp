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
#include "RealtimeOutgoingAudioSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "RealtimeAudioSourceGStreamer.h"

namespace WebCore {

RealtimeOutgoingAudioSourceGStreamer::RealtimeOutgoingAudioSourceGStreamer(Ref<MediaStreamTrackPrivate>&& audioSource, GstElement* pipeline)
    : m_audioSource(WTFMove(audioSource))
    , m_silenceAudioTimer(*this, &RealtimeOutgoingAudioSourceGStreamer::sendSilence)
    , m_pipeline(pipeline)
{
    m_audioSource->addObserver(*this);
    initializeConverter();
}

bool RealtimeOutgoingAudioSourceGStreamer::setSource(Ref<MediaStreamTrackPrivate>&& newSource)
{
    m_audioSource->removeObserver(*this);
    m_audioSource = WTFMove(newSource);
    m_audioSource->addObserver(*this);

    initializeConverter();
    return true;
}

void RealtimeOutgoingAudioSourceGStreamer::initializeConverter()
{
    m_muted = m_audioSource->muted();
    m_enabled = m_audioSource->enabled();
    handleMutedIfNeeded();

    // TODO: White-list of audio/video formats

    auto& audioSource = reinterpret_cast<GStreamerRealtimeAudioSource&>(m_audioSource->source());
    GRefPtr<GstElement> webrtcBin = gst_bin_get_by_name(GST_BIN_CAST(m_pipeline.get()), "webkit-webrtcbin");

    GstElement* audioSourceElement = gst_element_factory_make("proxysrc", nullptr);
    // g_object_set(audioSourceElement, "proxysink", audioSource.proxySink(), nullptr);

    GstElement* audioconvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement* audioresample = gst_element_factory_make("audioresample", nullptr);
    GstElement* aenc = gst_element_factory_make("opusenc", nullptr);
    GstElement* rtpapay = gst_element_factory_make("rtpopuspay", nullptr);
    GstElement* audioqueue = gst_element_factory_make("queue", nullptr);
    GstElement* acapsfilter = gst_element_factory_make("capsfilter", nullptr);
    GRefPtr<GstCaps> acaps = adoptGRef(gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING, "audio",
                                                           "payload", G_TYPE_INT, 111,
                                                           "encoding-name", G_TYPE_STRING, "OPUS", nullptr));
    g_object_set(acapsfilter, "caps", acaps.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), audioSourceElement, audioconvert, audioresample, aenc, rtpapay, audioqueue, acapsfilter, nullptr);
    gst_element_link_many(audioSourceElement, audioconvert, audioresample, aenc, rtpapay, audioqueue, acapsfilter, nullptr);

    GRefPtr<GstPad> apad = adoptGRef(gst_element_get_static_pad(acapsfilter, "src"));
    GRefPtr<GstPad> asink = adoptGRef(gst_element_get_request_pad(webrtcBin.get(), "sink_%u"));
    // g_printerr("webrtcBin: %p, pad: %p\n", webrtcBin.get(), asink.get());
    // GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-audio");
    // gst_pad_link(apad.get(), asink.get());

    // gst_element_sync_state_with_parent(audioSourceElement);
    // gst_element_sync_state_with_parent(audioconvert);
    // gst_element_sync_state_with_parent(audioresample);
    // gst_element_sync_state_with_parent(aenc);
    // gst_element_sync_state_with_parent(rtpapay);
    // gst_element_sync_state_with_parent(audioqueue);
    // gst_element_sync_state_with_parent(acapsfilter);
    // GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-rtc-outgoing-audio");
}

void RealtimeOutgoingAudioSourceGStreamer::stop()
{
    m_silenceAudioTimer.stop();
    m_audioSource->removeObserver(*this);
}

void RealtimeOutgoingAudioSourceGStreamer::sourceMutedChanged()
{
    m_muted = m_audioSource->muted();
    handleMutedIfNeeded();
}

void RealtimeOutgoingAudioSourceGStreamer::sourceEnabledChanged()
{
    m_enabled = m_audioSource->enabled();
    handleMutedIfNeeded();
}

void RealtimeOutgoingAudioSourceGStreamer::handleMutedIfNeeded()
{
    bool isSilenced = m_muted || !m_enabled;
    if (isSilenced && !m_silenceAudioTimer.isActive())
        m_silenceAudioTimer.startRepeating(1_s);
    if (!isSilenced && m_silenceAudioTimer.isActive())
        m_silenceAudioTimer.stop();
}

void RealtimeOutgoingAudioSourceGStreamer::audioSamplesAvailable(const MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t)
{
    g_printerr("yo\n");
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
