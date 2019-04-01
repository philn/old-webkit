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
#include "GStreamerPeerConnectionBackend.h"

#if USE(GSTREAMER_WEBRTC)

#include "IceCandidate.h"
#include "JSRTCStatsReport.h"
#include "GStreamerMediaEndpoint.h"
#include "MediaEndpointConfiguration.h"
#include "NotImplemented.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include "RealtimeOutgoingVideoSourceGStreamer.h"
#include "RTCIceCandidate.h"
#include "RTCPeerConnection.h"
#include "RTCRtpReceiver.h"
#include "RTCSessionDescription.h"

namespace WebCore {

static std::unique_ptr<PeerConnectionBackend> createGStreamerPeerConnectionBackend(RTCPeerConnection& peerConnection)
{
    return std::make_unique<GStreamerPeerConnectionBackend>(peerConnection);
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createGStreamerPeerConnectionBackend;

GStreamerPeerConnectionBackend::GStreamerPeerConnectionBackend(RTCPeerConnection& peerConnection)
    : PeerConnectionBackend(peerConnection)
    , m_endpoint(GStreamerMediaEndpoint::create(*this))
{
}

GStreamerPeerConnectionBackend::~GStreamerPeerConnectionBackend() = default;

bool GStreamerPeerConnectionBackend::setConfiguration(MediaEndpointConfiguration&& configuration)
{
    return m_endpoint->setConfiguration(configuration);
}

void GStreamerPeerConnectionBackend::getStats(MediaStreamTrack* track, Ref<DeferredPromise>&& promise)
{
    if (m_endpoint->isStopped())
        return;

    auto& statsPromise = promise.get();
    m_statsPromises.add(&statsPromise, WTFMove(promise));
    m_endpoint->getStats(track, statsPromise);
}

void GStreamerPeerConnectionBackend::getStatsSucceeded(const DeferredPromise& promise, Ref<RTCStatsReport>&& report)
{
    auto statsPromise = m_statsPromises.take(&promise);
    ASSERT(statsPromise);
    statsPromise.value()->resolve<IDLInterface<RTCStatsReport>>(WTFMove(report));
}

void GStreamerPeerConnectionBackend::getStatsFailed(const DeferredPromise& promise, Exception&& exception)
{
    auto statsPromise = m_statsPromises.take(&promise);
    ASSERT(statsPromise);
    statsPromise.value()->reject(WTFMove(exception));
}

void GStreamerPeerConnectionBackend::doSetLocalDescription(RTCSessionDescription& description)
{
    m_endpoint->doSetLocalDescription(description);
    if (!m_isLocalDescriptionSet)
        m_isLocalDescriptionSet = true;
}

void GStreamerPeerConnectionBackend::doSetRemoteDescription(RTCSessionDescription& description)
{
    m_endpoint->doSetRemoteDescription(description);
    if (!m_isRemoteDescriptionSet)
        m_isRemoteDescriptionSet = true;
}

void GStreamerPeerConnectionBackend::doCreateOffer(RTCOfferOptions&& options)
{
    m_endpoint->doCreateOffer(options);
}

void GStreamerPeerConnectionBackend::doCreateAnswer(RTCAnswerOptions&&)
{
    if (!m_isRemoteDescriptionSet) {
        createAnswerFailed(Exception { InvalidStateError, "No remote description set" });
        return;
    }
    m_endpoint->doCreateAnswer();
}

void GStreamerPeerConnectionBackend::doStop()
{
    for (auto& source : m_audioSources)
        source->stop();
    for (auto& source : m_videoSources)
        source->stop();

    m_endpoint->stop();

    m_remoteStreams.clear();
    m_pendingReceivers.clear();
}

void GStreamerPeerConnectionBackend::doAddIceCandidate(RTCIceCandidate& candidate)
{
    if (m_endpoint->addIceCandidate(candidate))
        addIceCandidateSucceeded();
}

void GStreamerPeerConnectionBackend::addAudioSource(Ref<RealtimeOutgoingAudioSourceGStreamer>&& source)
{
    m_audioSources.append(WTFMove(source));
}

void GStreamerPeerConnectionBackend::addVideoSource(Ref<RealtimeOutgoingVideoSourceGStreamer>&& source)
{
    m_videoSources.append(WTFMove(source));
}

static inline Ref<RTCRtpReceiver> createReceiverForSource(ScriptExecutionContext& context, Ref<RealtimeMediaSource>&& source)
{
    String id = source->id();
    auto remoteTrackPrivate = MediaStreamTrackPrivate::create(WTFMove(source), WTFMove(id));
    auto remoteTrack = MediaStreamTrack::create(context, WTFMove(remoteTrackPrivate));

    return RTCRtpReceiver::create(WTFMove(remoteTrack));
}

static inline Ref<RealtimeMediaSource> createEmptySource(const String& trackKind, String&& trackId)
{
    // FIXME: trackKind should be an enumeration
    if (trackKind == "audio")
        return RealtimeIncomingAudioSourceGStreamer::create(WTFMove(trackId));
    ASSERT(trackKind == "video");
    return RealtimeIncomingVideoSourceGStreamer::create(WTFMove(trackId));
}

Ref<RTCRtpReceiver> GStreamerPeerConnectionBackend::createReceiver(const String&, const String& trackKind, const String& trackId)
{
    auto receiver = createReceiverForSource(*m_peerConnection.scriptExecutionContext(), createEmptySource(trackKind, String(trackId)));
    m_pendingReceivers.append(receiver.copyRef());
    return receiver;
}

GStreamerPeerConnectionBackend::VideoReceiver GStreamerPeerConnectionBackend::videoReceiver(String&& trackId)
{
    // FIXME: Add to Vector a utility routine for that take-or-create pattern.
    // FIXME: We should be selecting the receiver based on track id.
    for (size_t cptr = 0; cptr < m_pendingReceivers.size(); ++cptr) {
        if (m_pendingReceivers[cptr]->track()->source().type() == RealtimeMediaSource::Type::Video) {
            Ref<RTCRtpReceiver> receiver = m_pendingReceivers[cptr].copyRef();
            m_pendingReceivers.remove(cptr);
            Ref<RealtimeIncomingVideoSourceGStreamer> source = static_cast<RealtimeIncomingVideoSourceGStreamer&>(receiver->track()->source());
            return { WTFMove(receiver), WTFMove(source) };
        }
    }
    auto source = RealtimeIncomingVideoSourceGStreamer::create(WTFMove(trackId));
    auto receiver = createReceiverForSource(*m_peerConnection.scriptExecutionContext(), source.copyRef());

    printf("receiver params: %s\n", receiver->getParameters().codecs[0].mimeType.utf8().data());
    auto transceiver = RTCRtpTransceiver::create(RTCRtpSender::create("video", { }, m_peerConnection), receiver.copyRef());
    //transceiver->disableSendingDirection();
    m_peerConnection.addTransceiver(WTFMove(transceiver));

    return { WTFMove(receiver), WTFMove(source) };
}

GStreamerPeerConnectionBackend::AudioReceiver GStreamerPeerConnectionBackend::audioReceiver(String&& trackId)
{
    // FIXME: Add to Vector a utility routine for that take-or-create pattern.
    // FIXME: We should be selecting the receiver based on track id.
    for (size_t cptr = 0; cptr < m_pendingReceivers.size(); ++cptr) {
        if (m_pendingReceivers[cptr]->track()->source().type() == RealtimeMediaSource::Type::Audio) {
            Ref<RTCRtpReceiver> receiver = m_pendingReceivers[cptr].copyRef();
            m_pendingReceivers.remove(cptr);
            Ref<RealtimeIncomingAudioSourceGStreamer> source = static_cast<RealtimeIncomingAudioSourceGStreamer&>(receiver->track()->source());
            return { WTFMove(receiver), WTFMove(source) };
        }
    }
    auto source = RealtimeIncomingAudioSourceGStreamer::create(WTFMove(trackId));
    auto receiver = createReceiverForSource(*m_peerConnection.scriptExecutionContext(), source.copyRef());

    auto transceiver = RTCRtpTransceiver::create(RTCRtpSender::create("audio", { }, m_peerConnection), receiver.copyRef());
    //transceiver->disableSendingDirection();
    m_peerConnection.addTransceiver(WTFMove(transceiver));

    return { WTFMove(receiver), WTFMove(source) };
}

std::unique_ptr<RTCDataChannelHandler> GStreamerPeerConnectionBackend::createDataChannelHandler(const String& label, const RTCDataChannelInit& options)
{
    notImplemented();
    return nullptr;
}

RefPtr<RTCSessionDescription> GStreamerPeerConnectionBackend::currentLocalDescription() const
{
    auto description = m_endpoint->currentLocalDescription();
    if (description)
        description->setSdp(filterSDP(String(description->sdp())));
    return description;
}

RefPtr<RTCSessionDescription> GStreamerPeerConnectionBackend::currentRemoteDescription() const
{
    return m_endpoint->currentRemoteDescription();
}

RefPtr<RTCSessionDescription> GStreamerPeerConnectionBackend::pendingLocalDescription() const
{
    auto description = m_endpoint->pendingLocalDescription();
    if (description)
        description->setSdp(filterSDP(String(description->sdp())));
    return description;
}

RefPtr<RTCSessionDescription> GStreamerPeerConnectionBackend::pendingRemoteDescription() const
{
    return m_endpoint->pendingRemoteDescription();
}

RefPtr<RTCSessionDescription> GStreamerPeerConnectionBackend::localDescription() const
{
    auto description = m_endpoint->localDescription();
    if (description)
        description->setSdp(filterSDP(String(description->sdp())));
    return description;
}

RefPtr<RTCSessionDescription> GStreamerPeerConnectionBackend::remoteDescription() const
{
    return m_endpoint->remoteDescription();
}

void GStreamerPeerConnectionBackend::notifyAddedTrack(RTCRtpSender& sender)
{
    ASSERT(sender.track());
    m_endpoint->addTrack(sender, *sender.track(), sender.mediaStreamIds());
}

void GStreamerPeerConnectionBackend::notifyRemovedTrack(RTCRtpSender& sender)
{
    m_endpoint->removeTrack(sender);
}

void GStreamerPeerConnectionBackend::removeRemoteStream(MediaStream* mediaStream)
{
    m_remoteStreams.removeFirstMatching([mediaStream](const auto& item) {
        return item.get() == mediaStream;
    });
}

void GStreamerPeerConnectionBackend::addRemoteStream(Ref<MediaStream>&& mediaStream)
{
    m_remoteStreams.append(WTFMove(mediaStream));
}

void GStreamerPeerConnectionBackend::replaceTrack(RTCRtpSender& sender, Ref<MediaStreamTrack>&& track, DOMPromiseDeferred<void>&& promise)
{
    ASSERT(sender.track());
    auto* currentTrack = sender.track();

    ASSERT(currentTrack->source().type() == track->source().type());
    switch (currentTrack->source().type()) {
    case RealtimeMediaSource::Type::None:
        ASSERT_NOT_REACHED();
        promise.reject(InvalidModificationError);
        break;
    case RealtimeMediaSource::Type::Audio: {
        for (auto& audioSource : m_audioSources) {
            if (&audioSource->source() == &currentTrack->privateTrack()) {
                if (!audioSource->setSource(track->privateTrack())) {
                    promise.reject(InvalidModificationError);
                    return;
                }
                connection().enqueueReplaceTrackTask(sender, WTFMove(track), WTFMove(promise));
                return;
            }
        }
        promise.reject(InvalidModificationError);
        break;
    }
    case RealtimeMediaSource::Type::Video: {
        for (auto& videoSource : m_videoSources) {
            if (&videoSource->source() == &currentTrack->privateTrack()) {
                if (!videoSource->setSource(track->privateTrack())) {
                    promise.reject(InvalidModificationError);
                    return;
                }
                connection().enqueueReplaceTrackTask(sender, WTFMove(track), WTFMove(promise));
                return;
            }
        }
        promise.reject(InvalidModificationError);
        break;
    }
    }
}

RTCRtpParameters GStreamerPeerConnectionBackend::getParameters(RTCRtpSender& sender) const
{
    return m_endpoint->getRTCRtpSenderParameters(sender);
}

void GStreamerPeerConnectionBackend::applyRotationForOutgoingVideoSources()
{
    for (auto& source : m_videoSources)
        source->setApplyRotation(true);
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
