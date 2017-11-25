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
#include <wtf/HashMap.h>

namespace WebCore {

class GStreamerMediaEndpoint;
class RTCRtpReceiver;
class RTCSessionDescription;
class RTCStatsReport;
class RealtimeIncomingAudioSourceGStreamer;
class RealtimeIncomingVideoSourceGStreamer;
class RealtimeOutgoingAudioSourceGStreamer;
class RealtimeOutgoingVideoSourceGStreamer;

class GStreamerPeerConnectionBackend final : public PeerConnectionBackend {
public:
    explicit GStreamerPeerConnectionBackend(RTCPeerConnection&);
    ~GStreamerPeerConnectionBackend();

    bool hasAudioSources() const { return m_audioSources.size(); }
    bool hasVideoSources() const { return m_videoSources.size(); }

private:
    void doCreateOffer(RTCOfferOptions&&) final;
    void doCreateAnswer(RTCAnswerOptions&&) final;
    void doSetLocalDescription(RTCSessionDescription&) final;
    void doSetRemoteDescription(RTCSessionDescription&) final;
    void doAddIceCandidate(RTCIceCandidate&) final;
    void doStop() final;
    std::unique_ptr<RTCDataChannelHandler> createDataChannelHandler(const String&, const RTCDataChannelInit&) final;
    bool setConfiguration(MediaEndpointConfiguration&&) final;
    void getStats(MediaStreamTrack*, Ref<DeferredPromise>&&) final;
    Ref<RTCRtpReceiver> createReceiver(const String& transceiverMid, const String& trackKind, const String& trackId) final;

    RefPtr<RTCSessionDescription> localDescription() const final;
    RefPtr<RTCSessionDescription> currentLocalDescription() const final;
    RefPtr<RTCSessionDescription> pendingLocalDescription() const final;

    RefPtr<RTCSessionDescription> remoteDescription() const final;
    RefPtr<RTCSessionDescription> currentRemoteDescription() const final;
    RefPtr<RTCSessionDescription> pendingRemoteDescription() const final;

    void replaceTrack(RTCRtpSender&, Ref<MediaStreamTrack>&&, DOMPromiseDeferred<void>&&) final;
    RTCRtpParameters getParameters(RTCRtpSender&) const final;

    void emulatePlatformEvent(const String&) final { }
    void applyRotationForOutgoingVideoSources() final;

    friend GStreamerMediaEndpoint;
    RTCPeerConnection& connection() { return m_peerConnection; }
    void addAudioSource(Ref<RealtimeOutgoingAudioSourceGStreamer>&&);
    void addVideoSource(Ref<RealtimeOutgoingVideoSourceGStreamer>&&);

    void getStatsSucceeded(const DeferredPromise&, Ref<RTCStatsReport>&&);
    void getStatsFailed(const DeferredPromise&, Exception&&);

    Vector<RefPtr<MediaStream>> getRemoteStreams() const final { return m_remoteStreams; }
    void removeRemoteStream(MediaStream*);
    void addRemoteStream(Ref<MediaStream>&&);

    void notifyAddedTrack(RTCRtpSender&) final;
    void notifyRemovedTrack(RTCRtpSender&) final;

    struct VideoReceiver {
        Ref<RTCRtpReceiver> receiver;
        Ref<RealtimeIncomingVideoSourceGStreamer> source;
    };
    struct AudioReceiver {
        Ref<RTCRtpReceiver> receiver;
        Ref<RealtimeIncomingAudioSourceGStreamer> source;
    };
    VideoReceiver videoReceiver(String&& trackId);
    AudioReceiver audioReceiver(String&& trackId);

private:
    Ref<GStreamerMediaEndpoint> m_endpoint;
    bool m_isLocalDescriptionSet { false };
    bool m_isRemoteDescriptionSet { false };

    // FIXME: Make m_remoteStreams a Vector of Ref.
    Vector<RefPtr<MediaStream>> m_remoteStreams;
    Vector<Ref<RealtimeOutgoingAudioSourceGStreamer>> m_audioSources;
    Vector<Ref<RealtimeOutgoingVideoSourceGStreamer>> m_videoSources;
    HashMap<const DeferredPromise*, Ref<DeferredPromise>> m_statsPromises;
    Vector<Ref<RTCRtpReceiver>> m_pendingReceivers;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
