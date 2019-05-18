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

#include "Document.h"
#include "IceCandidate.h"
#include "JSRTCStatsReport.h"
#include "GStreamerMediaEndpoint.h"
#include "GStreamerRtpReceiverBackend.h"
#include "GStreamerRtpSenderBackend.h"
#include "GStreamerRtpTransceiverBackend.h"
#include "MediaEndpointConfiguration.h"
#include "NotImplemented.h"
#include "Page.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include "RealtimeOutgoingVideoSourceGStreamer.h"
#include "RTCIceCandidate.h"
#include "RTCPeerConnection.h"
#include "RTCRtpCapabilities.h"
#include "RTCRtpReceiver.h"
#include "RTCSessionDescription.h"
#include "RuntimeEnabledFeatures.h"

namespace WebCore {

static std::unique_ptr<PeerConnectionBackend> createGStreamerPeerConnectionBackend(RTCPeerConnection& peerConnection)
{
    return std::make_unique<GStreamerPeerConnectionBackend>(peerConnection);
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createGStreamerPeerConnectionBackend;


Optional<RTCRtpCapabilities> PeerConnectionBackend::receiverCapabilities(ScriptExecutionContext&, const String& kind)
{
    g_printerr("receiverCapabilities for %s\n", kind.utf8().data());
    notImplemented();
    return { };
    // return page->libWebRTCProvider().receiverCapabilities(kind);
}

Optional<RTCRtpCapabilities> PeerConnectionBackend::senderCapabilities(ScriptExecutionContext&, const String& kind)
{
    g_printerr("senderCapabilities for %s\n", kind.utf8().data());
    notImplemented();
    return { };
    // return page->libWebRTCProvider().senderCapabilities(kind);
}

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

void GStreamerPeerConnectionBackend::getStats(Ref<DeferredPromise>&& promise)
{
    m_endpoint->getStats(WTFMove(promise));
}

static inline GStreamerRtpSenderBackend& backendFromRTPSender(RTCRtpSender& sender)
{
    ASSERT(!sender.isStopped());
    return static_cast<GStreamerRtpSenderBackend&>(*sender.backend());
}

void GStreamerPeerConnectionBackend::getStats(RTCRtpSender& sender, Ref<DeferredPromise>&& promise)
{
    GstWebRTCRTPSender* rtcSender = sender.backend() ? backendFromRTPSender(sender).rtcSender() : nullptr;

    if (!rtcSender) {
        m_endpoint->getStats(WTFMove(promise));
        return;
    }
    m_endpoint->getStats(rtcSender, WTFMove(promise));
}

void GStreamerPeerConnectionBackend::getStats(RTCRtpReceiver& receiver, Ref<DeferredPromise>&& promise)
{
    GstWebRTCRTPReceiver* rtcReceiver = receiver.backend() ? static_cast<GStreamerRtpReceiverBackend*>(receiver.backend())->rtcReceiver() : nullptr;

    if (!rtcReceiver) {
        m_endpoint->getStats(WTFMove(promise));
        return;
    }
    m_endpoint->getStats(rtcReceiver, WTFMove(promise));
}

void GStreamerPeerConnectionBackend::doSetLocalDescription(RTCSessionDescription& description)
{
    m_endpoint->doSetLocalDescription(description);
    if (!m_isLocalDescriptionSet) {
        if (m_isRemoteDescriptionSet) {
            for (auto& candidate : m_pendingCandidates)
                m_endpoint->addIceCandidate(*candidate);
            m_pendingCandidates.clear();
        }
        m_isLocalDescriptionSet = true;
    }
}

void GStreamerPeerConnectionBackend::doSetRemoteDescription(RTCSessionDescription& description)
{
    m_endpoint->doSetRemoteDescription(description);
    if (!m_isRemoteDescriptionSet) {
        if (m_isLocalDescriptionSet) {
            for (auto& candidate : m_pendingCandidates)
                m_endpoint->addIceCandidate(*candidate);
        }
        m_isRemoteDescriptionSet = true;
    }
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
    // for (auto& source : m_audioSources)
    //     source->stop();
    // for (auto& source : m_videoSources)
    //     source->stop();

    m_endpoint->stop();

    // m_remoteStreams.clear();
    m_pendingReceivers.clear();
}

void GStreamerPeerConnectionBackend::doAddIceCandidate(RTCIceCandidate& candidate)
{
    unsigned sdpMLineIndex = candidate.sdpMLineIndex() ? candidate.sdpMLineIndex().value() : 0;
    std::unique_ptr<GStreamerIceCandidate> rtcCandidate = std::make_unique<GStreamerIceCandidate>(*new GStreamerIceCandidate { sdpMLineIndex, candidate.candidate() });
    // libwebrtc does not like that ice candidates are set before the description.
    if (!m_isLocalDescriptionSet || !m_isRemoteDescriptionSet)
        m_pendingCandidates.append(rtcCandidate.release());
    else if (!m_endpoint->addIceCandidate(*rtcCandidate)) {
        ASSERT_NOT_REACHED();
        addIceCandidateFailed(Exception { OperationError, "Failed to apply the received candidate"_s });
        return;
    }
    addIceCandidateSucceeded();
}

// void GStreamerPeerConnectionBackend::addAudioSource(Ref<RealtimeOutgoingAudioSourceGStreamer>&& source)
// {
//     m_audioSources.append(WTFMove(source));
// }

// void GStreamerPeerConnectionBackend::addVideoSource(Ref<RealtimeOutgoingVideoSourceGStreamer>&& source)
// {
//     m_videoSources.append(WTFMove(source));
// }

Ref<RTCRtpReceiver> GStreamerPeerConnectionBackend::createReceiverForSource(Ref<RealtimeMediaSource>&& source, std::unique_ptr<RTCRtpReceiverBackend>&& backend)
{
    String trackID = source->persistentID();
    auto remoteTrackPrivate = MediaStreamTrackPrivate::create(WTFMove(source), WTFMove(trackID));
    auto remoteTrack = MediaStreamTrack::create(*m_peerConnection.scriptExecutionContext(), WTFMove(remoteTrackPrivate));

    return RTCRtpReceiver::create(*this, WTFMove(remoteTrack), WTFMove(backend));
}

static inline Ref<RealtimeMediaSource> createEmptySource(const String& trackKind, String&& trackId)
{
    // FIXME: trackKind should be an enumeration
    if (trackKind == "audio")
        return RealtimeIncomingAudioSourceGStreamer::create(WTFMove(trackId));
    ASSERT(trackKind == "video");
    return RealtimeIncomingVideoSourceGStreamer::create(WTFMove(trackId));
}

Ref<RTCRtpReceiver> GStreamerPeerConnectionBackend::createReceiver(const String& trackKind, const String& trackId)
{
    auto receiver = createReceiverForSource(createEmptySource(trackKind, String(trackId)), nullptr);
    m_pendingReceivers.append(receiver.copyRef());
    return receiver;
}

GStreamerPeerConnectionBackend::VideoReceiver GStreamerPeerConnectionBackend::videoReceiver(GstElement* pipeline, GstPad* pad)
{
    // FIXME: Add to Vector a utility routine for that take-or-create pattern.
    // FIXME: We should be selecting the receiver based on track id.
    // for (size_t cptr = 0; cptr < m_pendingReceivers.size(); ++cptr) {
    //     if (m_pendingReceivers[cptr]->track().source().type() == RealtimeMediaSource::Type::Video) {
    //         Ref<RTCRtpReceiver> receiver = m_pendingReceivers[cptr].copyRef();
    //         m_pendingReceivers.remove(cptr);
    //         Ref<RealtimeIncomingVideoSourceGStreamer> source = static_cast<RealtimeIncomingVideoSourceGStreamer&>(receiver->track().source());
    //         return { WTFMove(receiver), WTFMove(source) };
    //     }
    // }
    auto source = RealtimeIncomingVideoSourceGStreamer::create(pipeline, pad);
    auto receiver = createReceiverForSource(source.copyRef(), nullptr);

    auto senderBackend = std::make_unique<GStreamerRtpSenderBackend>(*this, nullptr);
    auto transceiver = RTCRtpTransceiver::create(RTCRtpSender::create(*this, "video"_s, { }, WTFMove(senderBackend)), receiver.copyRef(), nullptr);
    transceiver->disableSendingDirection();
    m_peerConnection.addTransceiver(WTFMove(transceiver));

    return { WTFMove(receiver), WTFMove(source), WTFMove(transceiver) };
}

GStreamerPeerConnectionBackend::AudioReceiver GStreamerPeerConnectionBackend::audioReceiver(GstElement* pipeline, GstPad* pad)
{
    // FIXME: Add to Vector a utility routine for that take-or-create pattern.
    // FIXME: We should be selecting the receiver based on track id.
    // for (size_t cptr = 0; cptr < m_pendingReceivers.size(); ++cptr) {
    //     if (m_pendingReceivers[cptr]->track().source().type() == RealtimeMediaSource::Type::Audio) {
    //         Ref<RTCRtpReceiver> receiver = m_pendingReceivers[cptr].copyRef();
    //         m_pendingReceivers.remove(cptr);
    //         Ref<RealtimeIncomingAudioSourceGStreamer> source = static_cast<RealtimeIncomingAudioSourceGStreamer&>(receiver->track().source());
    //         return { WTFMove(receiver), WTFMove(source) };
    //     }
    // }
    auto source = RealtimeIncomingAudioSourceGStreamer::create(pipeline, pad);
    auto receiver = createReceiverForSource(source.copyRef(), nullptr);

    auto senderBackend = std::make_unique<GStreamerRtpSenderBackend>(*this, nullptr);
    auto transceiver = RTCRtpTransceiver::create(RTCRtpSender::create(*this, "audio"_s, { }, WTFMove(senderBackend)), receiver.copyRef(), nullptr);
    transceiver->disableSendingDirection();
    m_peerConnection.addTransceiver(WTFMove(transceiver));

    return { WTFMove(receiver), WTFMove(source), WTFMove(transceiver) };
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

// void GStreamerPeerConnectionBackend::notifyAddedTrack(RTCRtpSender& sender)
// {
//     ASSERT(sender.track());
//     m_endpoint->addTrack(sender, *sender.track(), sender.mediaStreamIds());
// }

// void GStreamerPeerConnectionBackend::notifyRemovedTrack(RTCRtpSender& sender)
// {
//     m_endpoint->removeTrack(sender);
// }

// void GStreamerPeerConnectionBackend::removeRemoteStream(MediaStream* mediaStream)
// {
//     m_remoteStreams.removeFirstMatching([mediaStream](const auto& item) {
//         return item.get() == mediaStream;
//     });
// }

// void GStreamerPeerConnectionBackend::addRemoteStream(Ref<MediaStream>&& mediaStream)
// {
//     m_remoteStreams.append(WTFMove(mediaStream));
// }

static inline RefPtr<RTCRtpSender> findExistingSender(const Vector<RefPtr<RTCRtpTransceiver>>& transceivers, GStreamerRtpSenderBackend& senderBackend)
{
    ASSERT(senderBackend.rtcSender());
    for (auto& transceiver : transceivers) {
        auto& sender = transceiver->sender();
        if (!sender.isStopped() && senderBackend.rtcSender() == backendFromRTPSender(sender).rtcSender())
            return makeRef(sender);
    }
    return nullptr;
}

ExceptionOr<Ref<RTCRtpSender>> GStreamerPeerConnectionBackend::addTrack(MediaStreamTrack& track, Vector<String>&& mediaStreamIds)
{
    if (RuntimeEnabledFeatures::sharedFeatures().webRTCUnifiedPlanEnabled()) {
        auto senderBackend = std::make_unique<GStreamerRtpSenderBackend>(*this, nullptr);
        if (!m_endpoint->addTrack(*senderBackend, track, mediaStreamIds))
            return Exception { TypeError, "Unable to add track"_s };

        // if (auto sender = findExistingSender(m_peerConnection.currentTransceivers(), *senderBackend)) {
        //     backendFromRTPSender(*sender).takeSource(*senderBackend);
        //     sender->setTrack(makeRef(track));
        //     sender->setMediaStreamIds(WTFMove(mediaStreamIds));
        //     return sender.releaseNonNull();
        // }

        auto transceiverBackend = m_endpoint->transceiverBackendFromSender(*senderBackend);

        auto sender = RTCRtpSender::create(*this, makeRef(track), WTFMove(mediaStreamIds), WTFMove(senderBackend));
        auto receiver = createReceiverForSource(createEmptySource(track.kind(), createCanonicalUUIDString()), transceiverBackend->createReceiverBackend());
        auto transceiver = RTCRtpTransceiver::create(sender.copyRef(), WTFMove(receiver), WTFMove(transceiverBackend));
        m_peerConnection.addInternalTransceiver(WTFMove(transceiver));
        return sender;
    }

    RTCRtpSender* sender = nullptr;
    // Reuse an existing sender with the same track kind if it has never been used to send before.
    for (auto& transceiver : m_peerConnection.currentTransceivers()) {
        auto& existingSender = transceiver->sender();
        if (!existingSender.isStopped() && existingSender.trackKind() == track.kind() && existingSender.trackId().isNull() && !transceiver->hasSendingDirection()) {
            existingSender.setTrack(makeRef(track));
            existingSender.setMediaStreamIds(WTFMove(mediaStreamIds));
            transceiver->enableSendingDirection();
            sender = &existingSender;

            break;
        }
    }

    if (!sender) {
        const String& trackKind = track.kind();
        String trackId = createCanonicalUUIDString();

        auto senderBackend = std::make_unique<GStreamerRtpSenderBackend>(*this, nullptr);
        auto newSender = RTCRtpSender::create(*this, makeRef(track), Vector<String> { mediaStreamIds }, WTFMove(senderBackend));
        auto receiver = createReceiver(trackKind, trackId);
        auto transceiver = RTCRtpTransceiver::create(WTFMove(newSender), WTFMove(receiver), nullptr);

        sender = &transceiver->sender();
        m_peerConnection.addInternalTransceiver(WTFMove(transceiver));
    }

    if (!m_endpoint->addTrack(backendFromRTPSender(*sender), track, mediaStreamIds))
        return Exception { TypeError, "Unable to add track"_s };

    return makeRef(*sender);
}


#if 0
// phil
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
#endif


template<typename T>
ExceptionOr<Ref<RTCRtpTransceiver>> GStreamerPeerConnectionBackend::addUnifiedPlanTransceiver(T&& trackOrKind, const RTCRtpTransceiverInit& init)
{
    auto backends = m_endpoint->addTransceiver(trackOrKind, init);
    if (!backends)
        return Exception { InvalidAccessError, "Unable to add transceiver"_s };

    auto sender = RTCRtpSender::create(*this, WTFMove(trackOrKind), Vector<String> { }, WTFMove(backends->senderBackend));
    auto receiver = createReceiverForSource(createEmptySource(sender->trackKind(), createCanonicalUUIDString()), WTFMove(backends->receiverBackend));
    auto transceiver = RTCRtpTransceiver::create(WTFMove(sender), WTFMove(receiver), WTFMove(backends->transceiverBackend));
    m_peerConnection.addInternalTransceiver(transceiver.copyRef());
    return transceiver;
}

ExceptionOr<Ref<RTCRtpTransceiver>> GStreamerPeerConnectionBackend::addTransceiver(const String& trackKind, const RTCRtpTransceiverInit& init)
{
    if (RuntimeEnabledFeatures::sharedFeatures().webRTCUnifiedPlanEnabled())
        return addUnifiedPlanTransceiver(String { trackKind }, init);

    auto senderBackend = std::make_unique<GStreamerRtpSenderBackend>(*this, nullptr);
    auto newSender = RTCRtpSender::create(*this, String(trackKind), Vector<String>(), WTFMove(senderBackend));
    return completeAddTransceiver(WTFMove(newSender), init, createCanonicalUUIDString(), trackKind);
}

ExceptionOr<Ref<RTCRtpTransceiver>> GStreamerPeerConnectionBackend::addTransceiver(Ref<MediaStreamTrack>&& track, const RTCRtpTransceiverInit& init)
{
    if (RuntimeEnabledFeatures::sharedFeatures().webRTCUnifiedPlanEnabled())
        return addUnifiedPlanTransceiver(WTFMove(track), init);

    auto senderBackend = std::make_unique<GStreamerRtpSenderBackend>(*this, nullptr);
    auto& backend = *senderBackend;
    auto sender = RTCRtpSender::create(*this, track.copyRef(), Vector<String>(), WTFMove(senderBackend));
    if (!m_endpoint->addTrack(backend, track, Vector<String> { }))
        return Exception { InvalidAccessError, "Unable to add track"_s };

    return completeAddTransceiver(WTFMove(sender), init, track->id(), track->kind());
}

void GStreamerPeerConnectionBackend::setSenderSourceFromTrack(GStreamerRtpSenderBackend& sender, MediaStreamTrack& track)
{
    m_endpoint->setSenderSourceFromTrack(sender, track);
}

static inline GStreamerRtpTransceiverBackend& backendFromRTPTransceiver(RTCRtpTransceiver& transceiver)
{
    return static_cast<GStreamerRtpTransceiverBackend&>(*transceiver.backend());
}

RTCRtpTransceiver* GStreamerPeerConnectionBackend::existingTransceiver(WTF::Function<bool(GStreamerRtpTransceiverBackend&)>&& matchingFunction)
{
    for (auto& transceiver : m_peerConnection.currentTransceivers()) {
        if (matchingFunction(backendFromRTPTransceiver(*transceiver)))
            return transceiver.get();
    }
    return nullptr;
}

RTCRtpTransceiver& GStreamerPeerConnectionBackend::newRemoteTransceiver(std::unique_ptr<GStreamerRtpTransceiverBackend>&& transceiverBackend, Ref<RealtimeMediaSource>&& receiverSource)
{
    auto sender = RTCRtpSender::create(*this, receiverSource->type() == RealtimeMediaSource::Type::Audio ? "audio"_s : "video"_s, Vector<String> { }, transceiverBackend->createSenderBackend(*this, nullptr));
    auto receiver = createReceiverForSource(WTFMove(receiverSource), transceiverBackend->createReceiverBackend());
    auto transceiver = RTCRtpTransceiver::create(WTFMove(sender), WTFMove(receiver), WTFMove(transceiverBackend));
    m_peerConnection.addInternalTransceiver(transceiver.copyRef());
    return transceiver.get();
}

Ref<RTCRtpTransceiver> GStreamerPeerConnectionBackend::completeAddTransceiver(Ref<RTCRtpSender>&& sender, const RTCRtpTransceiverInit& init, const String& trackId, const String& trackKind)
{
    auto transceiver = RTCRtpTransceiver::create(WTFMove(sender), createReceiver(trackKind, trackId), nullptr);

    transceiver->setDirection(init.direction);

    m_peerConnection.addInternalTransceiver(transceiver.copyRef());
    return transceiver;
}

void GStreamerPeerConnectionBackend::collectTransceivers()
{
    m_endpoint->collectTransceivers();
}

void GStreamerPeerConnectionBackend::removeTrack(RTCRtpSender& sender)
{
    m_endpoint->removeTrack(backendFromRTPSender(sender));
}

void GStreamerPeerConnectionBackend::applyRotationForOutgoingVideoSources()
{
    for (auto& transceiver : m_peerConnection.currentTransceivers()) {
        if (!transceiver->sender().isStopped()) {
            if (auto* videoSource = backendFromRTPSender(transceiver->sender()).videoSource())
                videoSource->setApplyRotation(true);
        }
    }
}

bool GStreamerPeerConnectionBackend::shouldOfferAllowToReceive(const char* kind) const
{
    ASSERT(!RuntimeEnabledFeatures::sharedFeatures().webRTCUnifiedPlanEnabled());
    for (const auto& transceiver : m_peerConnection.currentTransceivers()) {
        if (transceiver->sender().trackKind() != kind)
            continue;

        if (transceiver->direction() == RTCRtpTransceiverDirection::Recvonly)
            return true;

        if (transceiver->direction() != RTCRtpTransceiverDirection::Sendrecv)
            continue;

        auto* backend = static_cast<GStreamerRtpSenderBackend*>(transceiver->sender().backend());
        if (backend && !backend->rtcSender())
            return true;
    }
    return false;
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
