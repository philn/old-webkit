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
#include "PeerConnectionBackend.h"
#include "RTCRtpReceiver.h"
#include <Timer.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <gst/gst.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API

namespace WebCore {

class GStreamerPeerConnectionBackend;
class MediaStreamTrack;
class RTCSessionDescription;

class GStreamerMediaEndpoint
    : public ThreadSafeRefCounted<GStreamerMediaEndpoint>
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
    void getStats(MediaStreamTrack*, const DeferredPromise&);
    bool addIceCandidate(RTCIceCandidate&);
    void stop();
    bool isStopped() const { return false; /* !m_backend; */ }

    RefPtr<RTCSessionDescription> localDescription() const;
    RefPtr<RTCSessionDescription> remoteDescription() const;
    RefPtr<RTCSessionDescription> currentLocalDescription() const;
    RefPtr<RTCSessionDescription> currentRemoteDescription() const;
    RefPtr<RTCSessionDescription> pendingLocalDescription() const;
    RefPtr<RTCSessionDescription> pendingRemoteDescription() const;

    void addTrack(RTCRtpSender&, MediaStreamTrack&, const Vector<String>&);
    void removeTrack(RTCRtpSender&);
    RTCRtpParameters getRTCRtpSenderParameters(RTCRtpSender&);

    void createSessionDescriptionSucceeded(GstWebRTCSessionDescription*);

    void handleMessage(GstMessage*);

    void emitSetLocalDescriptionSuceeded();
    void emitSetLocalDescriptionFailed();
    void emitSetRemoteDescriptionSuceeded();
    void emitSetRemoteDescriptionFailed();

    void setRemoteSessionDescriptionSucceeded();
    void createSessionDescriptionFailed(const std::string& errorMessage);

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

    void setLocalSessionDescriptionSucceeded();
    void addRemoteStream(GstPad*);
    void removeRemoteStream(GstPad*);

    void gatherStatsForLogging();
    void startLoggingStats();
    void stopLoggingStats();

    void sourceFromGstPad(GstPad*);

    int AddRef() const { ref(); return static_cast<int>(refCount()); }
    int Release() const { deref(); return static_cast<int>(refCount()); }

    bool shouldOfferAllowToReceiveAudio() const;
    bool shouldOfferAllowToReceiveVideo() const;

    GStreamerPeerConnectionBackend& m_peerConnectionBackend;
    GRefPtr<GstElement> m_webrtcBin;
    GRefPtr<GstElement> m_pipeline;

    HashMap<GstPad*, Ref<RealtimeMediaSource>> m_audioSources;
    HashMap<GstPad*, Ref<RealtimeMediaSource>> m_videoSources;

    bool m_isInitiator { false };
    Timer m_statsLogTimer;
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
