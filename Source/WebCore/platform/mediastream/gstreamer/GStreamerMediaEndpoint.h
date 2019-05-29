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

#include "GRefPtrGStreamer.h"
#include "GStreamerRtpSenderBackend.h"
//#include "PeerConnectionBackend.h"
#include "GStreamerPeerConnectionBackend.h"
#include "RTCRtpReceiver.h"

#include <Timer.h>
#include <gst/gst.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class GStreamerRtpReceiverBackend;
class GStreamerRtpTransceiverBackend;
class MediaStreamTrack;
class RTCSessionDescription;

class GStreamerMediaEndpoint
    : public ThreadSafeRefCounted<GStreamerMediaEndpoint, WTF::DestructionThread::Main>
{
public:
    static Ref<GStreamerMediaEndpoint> create(GStreamerPeerConnectionBackend& peerConnection) { return adoptRef(*new GStreamerMediaEndpoint(peerConnection)); }
    virtual ~GStreamerMediaEndpoint() = default;

    bool setConfiguration(MediaEndpointConfiguration&);

    void emitMediaStream();

    void doSetLocalDescription(RTCSessionDescription&);
    void doSetRemoteDescription(RTCSessionDescription&);
    void doCreateOffer(const RTCOfferOptions&);
    void doCreateAnswer();
    void getStats(Ref<DeferredPromise>&&);
    void getStats(GstWebRTCRTPReceiver*, Ref<DeferredPromise>&&);
    void getStats(GstWebRTCRTPSender*, Ref<DeferredPromise>&&);
    bool addIceCandidate(GStreamerIceCandidate&);
    void stop();
    bool isStopped() const { return false; /* !m_backend; */ }

    RefPtr<RTCSessionDescription> localDescription() const;
    RefPtr<RTCSessionDescription> remoteDescription() const;
    RefPtr<RTCSessionDescription> currentLocalDescription() const;
    RefPtr<RTCSessionDescription> currentRemoteDescription() const;
    RefPtr<RTCSessionDescription> pendingLocalDescription() const;
    RefPtr<RTCSessionDescription> pendingRemoteDescription() const;

    bool addTrack(GStreamerRtpSenderBackend&, MediaStreamTrack&, const Vector<String>&);
    void removeTrack(GStreamerRtpSenderBackend&);

    struct Backends {
        std::unique_ptr<GStreamerRtpSenderBackend> senderBackend;
        std::unique_ptr<GStreamerRtpReceiverBackend> receiverBackend;
        std::unique_ptr<GStreamerRtpTransceiverBackend> transceiverBackend;
    };
    Optional<Backends> addTransceiver(const String& trackKind, const RTCRtpTransceiverInit&);
    Optional<Backends> addTransceiver(MediaStreamTrack&, const RTCRtpTransceiverInit&);
    std::unique_ptr<GStreamerRtpTransceiverBackend> transceiverBackendFromSender(GStreamerRtpSenderBackend&);

    void setSenderSourceFromTrack(GStreamerRtpSenderBackend&, MediaStreamTrack&);

    RefPtr<RealtimeMediaSource> sourceFromNewReceiver(GstWebRTCRTPReceiver*);
    void collectTransceivers();

    void createSessionDescriptionSucceeded(GstWebRTCSessionDescription*);

    void emitSetLocalDescriptionSuceeded();
    void emitSetLocalDescriptionFailed();
    void emitSetRemoteDescriptionSuceeded();
    void emitSetRemoteDescriptionFailed();

    void setRemoteSessionDescriptionSucceeded();
    void createSessionDescriptionFailed(const std::string& errorMessage);

    GstElement* pipeline() const { return m_pipeline.get(); }

    void processSourcePad(GstPad*);

protected:
    static void signalingStateChangedCallback(GStreamerMediaEndpoint*);
    static void iceConnectionStateChangedCallback(GStreamerMediaEndpoint*);
    static void iceGatheringStateChangedCallback(GStreamerMediaEndpoint*);
    static void negotiationNeededCallback(GStreamerMediaEndpoint*);
    static void iceCandidateCallback(GstElement*, guint sdpMLineIndex, gchararray candidate, GStreamerMediaEndpoint*);
    static void webrtcBinPadAddedCallback(GstElement*, GstPad*, GStreamerMediaEndpoint*);
    static void webrtcBinPadRemovedCallback(GstElement*, GstPad*, GStreamerMediaEndpoint*);
    static void webrtcBinReadyCallback(GstElement*, GStreamerMediaEndpoint*);


private:
    GStreamerMediaEndpoint(GStreamerPeerConnectionBackend&);

    void OnSignalingStateChange();
    void OnNegotiationNeeded();
    void OnIceConnectionChange();
    void OnIceGatheringChange();
    void OnIceCandidate(guint sdpMLineIndex, gchararray candidate);
    void OnAddStream(GstPad*);
    void OnRemoveStream(GstPad*);

    void start();

    MediaStream& mediaStreamFromRTCStream(GstPad*);

    void setLocalSessionDescriptionSucceeded();
    void addRemoteStream(GstPad*);
    void removeRemoteStream(GstPad*);

    void gatherStatsForLogging();
    void startLoggingStats();
    void stopLoggingStats();

    void AddRef() const { ref(); }
    int Release() const {
        auto result = refCount() - 1;
        deref();
        return result;
    }

    bool shouldOfferAllowToReceiveAudio() const;
    bool shouldOfferAllowToReceiveVideo() const;

    GStreamerPeerConnectionBackend& m_peerConnectionBackend;
    GRefPtr<GstElement> m_webrtcBin;
    GRefPtr<GstElement> m_pipeline;

    HashMap<String, RefPtr<MediaStream>> m_remoteStreamsById;
    HashMap<MediaStreamTrack*, Vector<String>> m_remoteStreamsFromRemoteTrack;

    HashMap<GstPad*, Ref<RealtimeMediaSource>> m_audioSources;
    HashMap<GstPad*, Ref<RealtimeMediaSource>> m_videoSources;

    //Vector<RefPtr<MediaStream>> m_pendingIncomingStreams;
    unsigned m_pendingIncomingStreams { 0 };

    bool m_isInitiator { false };
    Timer m_statsLogTimer;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
