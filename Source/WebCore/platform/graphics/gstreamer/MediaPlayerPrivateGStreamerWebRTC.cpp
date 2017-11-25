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

#include "MediaPlayerPrivateGStreamerWebRTC.h"

#if ENABLE(VIDEO) && ENABLE(MEDIA_STREAM) && USE(GSTREAMER_WEBRTC)

#include "GStreamerUtilities.h"
#include "MediaPlayer.h"
#include "MediaStreamPrivate.h"
#include "NotImplemented.h"
#include "RealtimeAudioSourceGStreamer.h"
#include "RealtimeVideoSourceGStreamer.h"
#include "URL.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY(webkit_webrtc_debug);
#define GST_CAT_DEFAULT webkit_webrtc_debug

namespace WebCore {

MediaPlayerPrivateGStreamerWebRTC::MediaPlayerPrivateGStreamerWebRTC(MediaPlayer* player)
    : MediaPlayerPrivateGStreamerBase(player)
{
    initializeGStreamerAndGStreamerDebugging();
}

MediaPlayerPrivateGStreamerWebRTC::~MediaPlayerPrivateGStreamerWebRTC()
{
    GST_TRACE("Destroying");

    if (hasAudio())
        m_audioTrack->removeObserver(*this);
    if (hasVideo())
        m_videoTrack->removeObserver(*this);

    m_audioTrackMap.clear();
    m_videoTrackMap.clear();

    stop();
}

void MediaPlayerPrivateGStreamerWebRTC::play()
{
    GST_DEBUG("Play");

    if (!m_streamPrivate || !m_streamPrivate->active()) {
        m_readyState = MediaPlayer::HaveNothing;
        loadingFailed(MediaPlayer::Empty);
        return;
    }

    m_ended = false;
    m_paused = false;

    GST_DEBUG("Connecting to live stream, descriptor: %p", m_streamPrivate.get());

    if (m_videoTrack)
        maybeHandleChangeMutedState(*m_videoTrack.get());

    if (m_audioTrack)
        maybeHandleChangeMutedState(*m_audioTrack.get());

    gst_element_set_state(pipeline(), GST_STATE_PLAYING);
}

void MediaPlayerPrivateGStreamerWebRTC::pause()
{
    GST_DEBUG("Pause");
    m_paused = true;
    disableMediaTracks();
}

bool MediaPlayerPrivateGStreamerWebRTC::hasVideo() const
{
    return m_videoTrack;
}

bool MediaPlayerPrivateGStreamerWebRTC::hasAudio() const
{
    return m_audioTrack;
}

void MediaPlayerPrivateGStreamerWebRTC::setVolume(float volume)
{
    if (!m_audioTrack)
        return;

    GST_DEBUG("Setting volume: %f", volume);
    notImplemented();
}

void MediaPlayerPrivateGStreamerWebRTC::setMuted(bool muted)
{
    if (!m_audioTrack)
        return;

    GST_DEBUG("Setting mute: %s", muted ? "on":"off");
    notImplemented();
}

float MediaPlayerPrivateGStreamerWebRTC::currentTime() const
{
    gint64 position = GST_CLOCK_TIME_NONE;
    GstQuery* query = gst_query_new_position(GST_FORMAT_TIME);

    if (m_videoTrack && gst_element_query(m_videoSink.get(), query))
        gst_query_parse_position(query, 0, &position);
    else if (m_audioTrack && gst_element_query(m_audioSink.get(), query))
        gst_query_parse_position(query, 0, &position);

    float result = 0;
    if (static_cast<GstClockTime>(position) != GST_CLOCK_TIME_NONE)
        result = static_cast<double>(position) / GST_SECOND;

    GST_LOG("Position %" GST_TIME_FORMAT, GST_TIME_ARGS(position));
    gst_query_unref(query);

    return result;
}

void MediaPlayerPrivateGStreamerWebRTC::load(const String &)
{
    // Properly fail so the global MediaPlayer tries to fallback to the next MediaPlayerPrivate.
    m_networkState = MediaPlayer::FormatError;
    m_player->networkStateChanged();
}

#if ENABLE(MEDIA_SOURCE)
void MediaPlayerPrivateGStreamerWebRTC::load(const String&, MediaSourcePrivateClient*)
{
    // Properly fail so the global MediaPlayer tries to fallback to the next MediaPlayerPrivate.
    m_networkState = MediaPlayer::FormatError;
    m_player->networkStateChanged();
}
#endif

static void onDecodeBinPadAddedCallback(GstElement*, GstPad* pad, gpointer userData)
{
    auto sinkPad = GST_PAD_CAST(userData);
    ASSERT(!gst_pad_is_linked(sinkPad));
    if (gst_pad_is_linked(sinkPad))
        return;

    gst_pad_link(pad, sinkPad);
}

static void onDecodeBinReady(GstElement* decodebin, gpointer)
{
    gst_element_sync_state_with_parent(decodebin);
}

void MediaPlayerPrivateGStreamerWebRTC::load(MediaStreamPrivate& streamPrivate)
{
    if (!initializeGStreamer())
        return;

    m_streamPrivate = &streamPrivate;
    if (!m_streamPrivate->active()) {
        loadingFailed(MediaPlayer::NetworkError);
        return;
    }

    GstElement* videoSink = nullptr;
    if (streamPrivate.hasVideo() && !m_videoSink)
        videoSink = createVideoSink();

    GstElement* audioSink = nullptr;
    if (streamPrivate.hasAudio() && !m_audioSink) {
        createGSTAudioSinkBin();
        audioSink = m_audioSink.get();
    }

    GST_DEBUG("Loading MediaStreamPrivate %p video: %s, audio: %s", &streamPrivate, streamPrivate.hasVideo() ? "yes":"no", streamPrivate.hasAudio() ? "yes":"no");

    m_readyState = MediaPlayer::HaveNothing;
    m_networkState = MediaPlayer::Loading;
    m_player->networkStateChanged();
    m_player->readyStateChanged();

    for (auto track : m_streamPrivate->tracks()) {
        if (!track->enabled()) {
            GST_DEBUG("Track %s disabled", track->label().ascii().data());
            continue;
        }

        GST_DEBUG("Processing track %s", track->label().ascii().data());

        bool observeTrack = false;

        // TODO: Support for multiple tracks of the same type.

        switch (track->type()) {
        case RealtimeMediaSource::Type::Audio:
            if (!m_audioTrack) {
                String preSelectedDevice = getenv("WEBKIT_AUDIO_DEVICE");
                if (!preSelectedDevice || (preSelectedDevice == track->label())) {
                    m_audioTrack = track;
                    auto audioTrack = AudioTrackPrivateMediaStream::create(*m_audioTrack.get());
                    m_player->addAudioTrack(*audioTrack);
                    m_audioTrackMap.add(track->id(), audioTrack);
                    observeTrack = true;
                }
            }
            break;
        case RealtimeMediaSource::Type::Video:
            if (!m_videoTrack) {
                String preSelectedDevice = getenv("WEBKIT_VIDEO_DEVICE");
                if (!preSelectedDevice || (preSelectedDevice == track->label())) {
                    m_videoTrack = track;
                    auto videoTrack = VideoTrackPrivateMediaStream::create(*m_videoTrack.get());
                    m_player->addVideoTrack(*videoTrack);
                    videoTrack->setSelected(true);
                    m_videoTrackMap.add(track->id(), videoTrack);
                    observeTrack = true;
                }
            }
            break;
        case RealtimeMediaSource::Type::None:
            GST_WARNING("Loading a track with None type");
        }

        if (observeTrack)
            track->addObserver(*this);
    }

    setPipeline(gst_pipeline_new(nullptr));
    gst_pipeline_use_clock(GST_PIPELINE(m_pipeline.get()), gst_system_clock_obtain());
    gst_element_set_base_time(m_pipeline.get(), getWebRTCBaseTime());
    gst_element_set_start_time(m_pipeline.get(), GST_CLOCK_TIME_NONE);

    if (videoSink) {
        auto& realTimeVideoSource = static_cast<GStreamerRealtimeVideoSource&>(m_videoTrack->source());
        m_videoSrc = gst_element_factory_make("proxysrc", nullptr);
        g_object_set(m_videoSrc.get(), "proxysink", realTimeVideoSource.proxySink(), nullptr);

        GstElement* videoconvert = gst_element_factory_make("videoconvert", nullptr);
        gst_bin_add_many(GST_BIN_CAST(pipeline()), m_videoSrc.get(), videoconvert, videoSink, nullptr);
        gst_element_link(videoconvert, videoSink);
        if (realTimeVideoSource.videoFormat() == GStreamerRealtimeVideoSource::VideoFormat::H264) {
            GST_DEBUG("Compressed source detected. Adding a video decoder to the pipeline.");
            GstElement* decoder = gst_element_factory_make("decodebin", nullptr);
            GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(videoconvert, "sink"));
            g_signal_connect(decoder, "pad-added", G_CALLBACK(onDecodeBinPadAddedCallback), static_cast<gpointer>(pad.get()));
            g_signal_connect(decoder, "no-more-pads", G_CALLBACK(onDecodeBinReady), nullptr);
            gst_bin_add(GST_BIN_CAST(pipeline()), decoder);
            gst_element_link(m_videoSrc.get(), decoder);
        } else
            gst_element_link(m_videoSrc.get(), videoconvert);
    }

    if (audioSink) {
        auto& realTimeAudioSource = static_cast<GStreamerRealtimeAudioSource&>(m_audioTrack->source());
        m_audioSrc = gst_element_factory_make("proxysrc", nullptr);
        g_object_set(m_audioSrc.get(), "proxysink", realTimeAudioSource.proxySink(), nullptr);
        gst_bin_add_many(GST_BIN_CAST(pipeline()), m_audioSrc.get(), audioSink, nullptr);
        gst_element_link(m_audioSrc.get(), audioSink);
    }

    m_readyState = MediaPlayer::HaveEnoughData;
    m_player->readyStateChanged();
}

void MediaPlayerPrivateGStreamerWebRTC::loadingFailed(MediaPlayer::NetworkState error)
{
    if (m_networkState != error) {
        GST_WARNING("Loading failed, error: %d", error);
        m_networkState = error;
        m_player->networkStateChanged();
    }
    if (m_readyState != MediaPlayer::HaveNothing) {
        m_readyState = MediaPlayer::HaveNothing;
        m_player->readyStateChanged();
    }
}

bool MediaPlayerPrivateGStreamerWebRTC::didLoadingProgress() const
{
    // FIXME: Implement loading progress support.
    notImplemented();
    return true;
}

void MediaPlayerPrivateGStreamerWebRTC::disableMediaTracks()
{
    notImplemented();
}

void MediaPlayerPrivateGStreamerWebRTC::stop()
{
    disableMediaTracks();
    if (m_videoTrack) {
        auto videoTrack = m_videoTrackMap.get(m_videoTrack->id());
        if (videoTrack)
            videoTrack->setSelected(false);
    }
}

void MediaPlayerPrivateGStreamerWebRTC::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (initializeGStreamerAndGStreamerDebugging()) {
        registrar([](MediaPlayer* player) {
            return std::make_unique<MediaPlayerPrivateGStreamerWebRTC>(player);
        }, getSupportedTypes, supportsType, nullptr, nullptr, nullptr, nullptr);
    }
}

void MediaPlayerPrivateGStreamerWebRTC::getSupportedTypes(HashSet<String, ASCIICaseInsensitiveHash>& types)
{
    // Not supported in this media player.
    static NeverDestroyed<HashSet<String, ASCIICaseInsensitiveHash>> cache;
    types = cache;
}

MediaPlayer::SupportsType MediaPlayerPrivateGStreamerWebRTC::supportsType(const MediaEngineSupportParameters& parameters)
{
    if (parameters.isMediaStream)
        return MediaPlayer::IsSupported;
    return MediaPlayer::IsNotSupported;
}

bool MediaPlayerPrivateGStreamerWebRTC::initializeGStreamerAndGStreamerDebugging()
{
    if (!initializeGStreamer())
        return false;

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_debug, "webkitwebrtcplayer", 0, "WebKit WebRTC player");
    });

    return true;
}

void MediaPlayerPrivateGStreamerWebRTC::createGSTAudioSinkBin()
{
    ASSERT(!m_audioSink);
    GST_DEBUG("Creating audio sink");

    m_audioSink = gst_bin_new(nullptr);

    // FIXME: volume/mute support: https://webkit.org/b/153828.

    // Pre-roll an autoaudiosink so that the platform audio sink is created and
    // can be retrieved from the autoaudiosink bin.
    GRefPtr<GstElement> sink = gst_element_factory_make("autoaudiosink", nullptr);
    GstChildProxy* childProxy = GST_CHILD_PROXY(sink.get());
    gst_element_set_state(sink.get(), GST_STATE_READY);
    GRefPtr<GstElement> platformSink = adoptGRef(GST_ELEMENT(gst_child_proxy_get_child_by_index(childProxy, 0)));
    GstElementFactory* factory = gst_element_get_factory(platformSink.get());

    // Dispose now un-needed autoaudiosink.
    gst_element_set_state(sink.get(), GST_STATE_NULL);

    // Create a fresh new audio sink compatible with the platform.
    GstElement* audioSink = gst_element_factory_create(factory, nullptr);

    GstElement* audioconvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement* audioresample = gst_element_factory_make("audioresample", nullptr);
    GstElement* audiorate = gst_element_factory_make("audiorate", nullptr);
    GstElement* queue = gst_element_factory_make("queue", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_audioSink.get()), audioconvert, audioresample, audiorate, queue, audioSink, nullptr);
    gst_element_link_many(audioconvert, audioresample, audiorate, queue, audioSink, nullptr);
    GRefPtr<GstPad> sinkPad = adoptGRef(gst_element_get_static_pad(audioconvert, "sink"));
    GRefPtr<GstPad> ghostPad = gst_ghost_pad_new(nullptr, sinkPad.get());
    gst_pad_set_active(ghostPad.get(), TRUE);
    gst_element_add_pad(m_audioSink.get(), ghostPad.get());
}

void MediaPlayerPrivateGStreamerWebRTC::trackEnded(MediaStreamTrackPrivate& track)
{
    GST_DEBUG("Track ended");

    if (!m_streamPrivate || !m_streamPrivate->active()) {
        stop();
        return;
    }

    GST_DEBUG("Disabling track %s", track.source().name().utf8().data());

    GstElement* element = nullptr;
    if (&track == m_videoTrack)
        element = m_videoSrc.get();
    else if (&track == m_audioTrack)
        element = m_audioSrc.get();

    if (!element)
        return;

    gst_element_send_event(element, gst_event_new_eos());
    gst_element_send_event(element, gst_event_new_flush_start());
    gst_element_send_event(element, gst_event_new_flush_stop(TRUE));
}

void MediaPlayerPrivateGStreamerWebRTC::readyStateChanged(MediaStreamTrackPrivate& track)
{
    GST_DEBUG("Ready state changed");
}

void MediaPlayerPrivateGStreamerWebRTC::maybeHandleChangeMutedState(MediaStreamTrackPrivate& track)
{
    notImplemented();
}

void MediaPlayerPrivateGStreamerWebRTC::setSize(const IntSize& size)
{
    if (size == m_size)
        return;

    MediaPlayerPrivateGStreamerBase::setSize(size);
    if (!m_videoTrack)
        return;

    // auto& realTimeMediaSource = static_cast<RealtimeMediaSourceGStreamer&>(m_videoTrack->source());
    // realTimeMediaSource.setWidth(size.width());
    // realTimeMediaSource.setHeight(size.height());
}

FloatSize MediaPlayerPrivateGStreamerWebRTC::naturalSize() const
{
    auto size = MediaPlayerPrivateGStreamerBase::naturalSize();

    // In case we are not playing the video we return the size we set to the media source.
    if (m_videoTrack && size.isZero()) {
        auto& realTimeVideoSource = static_cast<GStreamerRealtimeVideoSource&>(m_videoTrack->source());
        return realTimeVideoSource.size();
    }

    return size;
}

} // namespace WebCore

#endif // ENABLE(VIDEO) && ENABLE(MEDIA_STREAM) && USE(GSTREAMER_WEBRTC)
