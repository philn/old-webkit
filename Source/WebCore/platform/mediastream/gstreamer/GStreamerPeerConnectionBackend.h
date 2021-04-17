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

#include "PeerConnectionBackend.h"
#include "RealtimeMediaSource.h"
#include <gst/gst.h>
#include <wtf/HashMap.h>

namespace WebCore {

class GStreamerMediaEndpoint;
class GStreamerRtpReceiverBackend;
class GStreamerRtpSenderBackend;
class GStreamerRtpTransceiverBackend;
class RTCRtpReceiver;
class RTCRtpReceiverBackend;
class RTCSessionDescription;
class RTCStatsReport;
class RealtimeIncomingAudioSourceGStreamer;
class RealtimeIncomingVideoSourceGStreamer;
class RealtimeMediaSourceGStreamer;
class RealtimeOutgoingAudioSourceGStreamer;
class RealtimeOutgoingVideoSourceGStreamer;

struct GStreamerIceCandidate {
    unsigned sdpMLineIndex;
    String candidate;
};

class GStreamerPeerConnectionBackend final : public PeerConnectionBackend {
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit GStreamerPeerConnectionBackend(RTCPeerConnection&);
    ~GStreamerPeerConnectionBackend();

private:
    void close() final;
    void doCreateOffer(RTCOfferOptions&&) final;
    void doCreateAnswer(RTCAnswerOptions&&) final;
    void doSetLocalDescription(const RTCSessionDescription*) final;
    void doSetRemoteDescription(const RTCSessionDescription&) final;
    void doAddIceCandidate(RTCIceCandidate&) final;
    void doStop() final;
    std::unique_ptr<RTCDataChannelHandler> createDataChannelHandler(const String&, const RTCDataChannelInit&) final;
    void restartIce() final;
    bool setConfiguration(MediaEndpointConfiguration&&) final;
    void getStats(Ref<DeferredPromise>&&) final;
    void getStats(RTCRtpSender&, Ref<DeferredPromise>&&) final;
    void getStats(RTCRtpReceiver&, Ref<DeferredPromise>&&) final;

    RefPtr<RTCSessionDescription> localDescription() const final;
    RefPtr<RTCSessionDescription> currentLocalDescription() const final;
    RefPtr<RTCSessionDescription> pendingLocalDescription() const final;

    RefPtr<RTCSessionDescription> remoteDescription() const final;
    RefPtr<RTCSessionDescription> currentRemoteDescription() const final;
    RefPtr<RTCSessionDescription> pendingRemoteDescription() const final;

    void emulatePlatformEvent(const String&) final { }
    void applyRotationForOutgoingVideoSources() final;

    friend class GStreamerMediaEndpoint;
    friend class GStreamerRtpSenderBackend;
    RTCPeerConnection& connection() { return m_peerConnection; }

    void getStatsSucceeded(const DeferredPromise&, Ref<RTCStatsReport>&&);

    ExceptionOr<Ref<RTCRtpSender>> addTrack(MediaStreamTrack&, Vector<String>&&) final;
    void removeTrack(RTCRtpSender&) final;

    ExceptionOr<Ref<RTCRtpTransceiver>> addTransceiver(const String&, const RTCRtpTransceiverInit&) final;
    ExceptionOr<Ref<RTCRtpTransceiver>> addTransceiver(Ref<MediaStreamTrack>&&, const RTCRtpTransceiverInit&) final;
    void setSenderSourceFromTrack(GStreamerRtpSenderBackend&, MediaStreamTrack&);

    RTCRtpTransceiver* existingTransceiver(WTF::Function<bool(GStreamerRtpTransceiverBackend&)>&&);
    RTCRtpTransceiver& newRemoteTransceiver(std::unique_ptr<GStreamerRtpTransceiverBackend>&&, RealtimeMediaSource::Type);

    void collectTransceivers() final;

private:
    bool isLocalDescriptionSet() const final { return m_isLocalDescriptionSet; }

    template<typename T>
    ExceptionOr<Ref<RTCRtpTransceiver>> addTransceiverFromTrackOrKind(T&& trackOrKind, const RTCRtpTransceiverInit&);

    Ref<RTCRtpReceiver> createReceiver(std::unique_ptr<GStreamerRtpReceiverBackend>&&, const String& trackKind);

    void suspend() final;
    void resume() final;

    Ref<GStreamerMediaEndpoint> m_endpoint;
    bool m_isLocalDescriptionSet { false };
    bool m_isRemoteDescriptionSet { false };

    Vector<std::unique_ptr<GStreamerIceCandidate>> m_pendingCandidates;
    Vector<Ref<RTCRtpReceiver>> m_pendingReceivers;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
