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

#include "GStreamerPeerConnectionBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "NotImplemented.h"
#include "RTCPeerConnection.h"
#include "RTCRtpSender.h"
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
    return source.setSource(track->privateTrack());
}

void GStreamerRtpSenderBackend::replaceTrack(ScriptExecutionContext& context, RTCRtpSender& sender, RefPtr<MediaStreamTrack>&& track, DOMPromiseDeferred<void>&& promise)
{
    if (!m_peerConnectionBackend) {
        promise.reject(Exception { InvalidStateError, "No WebRTC backend"_s });
        return;
    }

    auto* currentTrack = sender.track();

    ASSERT(!track || !currentTrack || currentTrack->source().type() == track->source().type());
    if (currentTrack) {
        switch (currentTrack->source().type()) {
        case RealtimeMediaSource::Type::None:
            ASSERT_NOT_REACHED();
            promise.reject(InvalidModificationError);
            break;
        case RealtimeMediaSource::Type::Audio:
            if (!updateTrackSource(*audioSource(), track.get())) {
                promise.reject(InvalidModificationError);
                return;
            }
            break;
        case RealtimeMediaSource::Type::Video:
            if (!updateTrackSource(*videoSource(), track.get())) {
                promise.reject(InvalidModificationError);
                return;
            }
            break;
        }
    }

    // FIXME: Remove this postTask once this whole function is executed as part of the RTCPeerConnection operation queue.
    context.postTask([protectedSender = makeRef(sender), promise = WTFMove(promise), track = WTFMove(track), this](ScriptExecutionContext&) mutable {
        if (protectedSender->isStopped())
            return;

        if (!track) {
            protectedSender->setTrackToNull();
            promise.resolve();
            return;
        }

        bool hasTrack = protectedSender->track();
        protectedSender->setTrack(track.releaseNonNull());

        if (hasTrack) {
            promise.resolve();
            return;
        }

        if (RuntimeEnabledFeatures::sharedFeatures().webRTCUnifiedPlanEnabled()) {
            m_source = nullptr;
            m_peerConnectionBackend->setSenderSourceFromTrack(*this, *protectedSender->track());
            promise.resolve();
            return;
        }

        auto result = m_peerConnectionBackend->addTrack(*protectedSender->track(), { });
        if (result.hasException()) {
            promise.reject(result.releaseException());
            return;
        }
        promise.resolve();
    });
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

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
