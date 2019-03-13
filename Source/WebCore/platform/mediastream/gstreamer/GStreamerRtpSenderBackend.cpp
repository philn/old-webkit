/*
 *  Copyright (C) 2019 Igalia S.L. All rights reserved.
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
#include "GStreamerRtpSenderBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerDTMFSenderBackend.h"
#include "GStreamerPeerConnectionBackend.h"
#include "GStreamerRtpSenderTransformBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "JSDOMPromiseDeferred.h"
#include "NotImplemented.h"
#include "RTCPeerConnection.h"
#include "RTCRtpSender.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include "RealtimeOutgoingVideoSourceGStreamer.h"
#include "RuntimeEnabledFeatures.h"
#include "ScriptExecutionContext.h"

namespace WebCore {

template<typename Source>
static inline bool updateTrackSource(Source& source, MediaStreamTrack* track)
{
    if (!track) {
        source.stop();
        return true;
    }
    return source.setTrack(track->privateTrack());
}

void GStreamerRtpSenderBackend::startSource()
{
    switchOn(m_source, [](Ref<RealtimeOutgoingAudioSourceGStreamer>& source) {
        source->start();
    }, [](Ref<RealtimeOutgoingVideoSourceGStreamer>& source) {
        source->start();
    }, [](std::nullptr_t&) {
    });
}

GRefPtr<GstElement> GStreamerRtpSenderBackend::stopSource()
{
    switchOn(
        m_source, [](Ref<RealtimeOutgoingAudioSourceGStreamer>& source) {
        source->stop();
        gst_printerrln("is audio");
        return GRefPtr<GstElement>(source->bin()); }, [](Ref<RealtimeOutgoingVideoSourceGStreamer>& source) {
        source->stop();
        gst_printerrln("is video");
        return GRefPtr<GstElement>(source->bin()); }, [](std::nullptr_t&) {
        gst_printerrln("is null");
        return GRefPtr<GstElement>(nullptr); });
    //m_source = nullptr;
    return nullptr;
}

bool GStreamerRtpSenderBackend::replaceTrack(RTCRtpSender& sender, MediaStreamTrack* track)
{
    if (!track) {
        stopSource();
        return true;
    }

    if (sender.track()) {
        switchOn(m_source, [&](Ref<RealtimeOutgoingAudioSourceGStreamer>& source) {
            ASSERT(track->source().type() == RealtimeMediaSource::Type::Audio);
            source->stop();
            source->setSource(track->privateTrack());
            source->start();
        }, [&](Ref<RealtimeOutgoingVideoSourceGStreamer>& source) {
            ASSERT(track->source().type() == RealtimeMediaSource::Type::Video);
            source->stop();
            source->setSource(track->privateTrack());
            source->start();
        }, [](std::nullptr_t&) {
            ASSERT_NOT_REACHED();
        });
    }

    m_peerConnectionBackend->setSenderSourceFromTrack(*this, *track);
    return true;
}

RTCRtpSendParameters GStreamerRtpSenderBackend::getParameters() const
{
    if (!m_rtcSender)
        return { };

    notImplemented();
#if 0
    // FIXME: Implement
    m_currentParameters = m_rtcSender->GetParameters();
    return toRTCRtpSendParameters(*m_currentParameters);
#endif
    return { };
}

void GStreamerRtpSenderBackend::setParameters(const RTCRtpSendParameters& parameters, DOMPromiseDeferred<void>&& promise)
{
    if (!m_rtcSender) {
        promise.reject(NotSupportedError);
        return;
    }

    for (auto& codec : parameters.codecs) {
        printf(">> payloadType: %u, mimeType: %s, clockRate: %lu, channels: %hu\n", codec.payloadType, codec.mimeType.utf8().data(), codec.clockRate, codec.channels);
    }

    notImplemented();
#if 0
    // FIXME: Implement
    if (!m_currentParameters) {
        promise.reject(Exception { InvalidStateError, "getParameters must be called before setParameters"_s });
        return;
    }

    auto rtcParameters = WTFMove(*m_currentParameters);
    updateRTCRtpSendParameters(parameters, rtcParameters);
    m_currentParameters = WTF::nullopt;

    auto error = m_rtcSender->SetParameters(rtcParameters);
    if (!error.ok()) {
        promise.reject(Exception { InvalidStateError, error.message() });
        return;
    }
#endif
    promise.resolve();
}

std::unique_ptr<RTCDTMFSenderBackend> GStreamerRtpSenderBackend::createDTMFBackend()
{
    return makeUnique<GStreamerDTMFSenderBackend>();
}

Ref<RTCRtpTransformBackend> GStreamerRtpSenderBackend::createRTCRtpTransformBackend()
{
    return GStreamerRtpSenderTransformBackend::create(m_rtcSender);
}

void GStreamerRtpSenderBackend::setMediaStreamIds(const Vector<String>& streamIds)
{
    for (auto& id : streamIds)
        gst_printerrln(">>>>>>>> set stream %s for this=%p", id.ascii().data(), this);
    notImplemented();
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
