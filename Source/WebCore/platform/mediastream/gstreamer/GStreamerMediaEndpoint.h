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
#include "GStreamerPeerConnectionBackend.h"
#include "GStreamerRtpSenderBackend.h"
#include "GStreamerStatsCollector.h"
#include "GUniquePtrGStreamer.h"
#include "RTCRtpReceiver.h"

#include <wtf/LoggerHelper.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

class GStreamerRtpReceiverBackend;
class GStreamerRtpTransceiverBackend;
class MediaStreamTrack;
class RTCSessionDescription;
class RealtimeOutgoingMediaSourceGStreamer;

class GStreamerMediaEndpoint : public ThreadSafeRefCounted<GStreamerMediaEndpoint, WTF::DestructionThread::Main>
#if !RELEASE_LOG_DISABLED
    , private LoggerHelper
#endif
{
public:
    static Ref<GStreamerMediaEndpoint> create(GStreamerPeerConnectionBackend& peerConnection) { return adoptRef(*new GStreamerMediaEndpoint(peerConnection)); }
    virtual ~GStreamerMediaEndpoint();

    bool setConfiguration(MediaEndpointConfiguration&);
    void restartIce();

    void doSetLocalDescription(const RTCSessionDescription*);
    void doSetRemoteDescription(const RTCSessionDescription&);
    void doCreateOffer(const RTCOfferOptions&);
    void doCreateAnswer();

    void getStats(GstPad*, Ref<DeferredPromise>&&);

    std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String&, const RTCDataChannelInit&);
    void onDataChannel(GstWebRTCDataChannel*);

    void addIceCandidate(GStreamerIceCandidate&);

    void close();
    void stop();
    bool isStopped() const { return !m_pipeline; }

    void suspend();
    void resume();

    RefPtr<RTCSessionDescription> fetchDescription(const char* name) const;
    RefPtr<RTCSessionDescription> localDescription() const;
    RefPtr<RTCSessionDescription> remoteDescription() const;
    RefPtr<RTCSessionDescription> currentLocalDescription() const;
    RefPtr<RTCSessionDescription> currentRemoteDescription() const;
    RefPtr<RTCSessionDescription> pendingLocalDescription() const;
    RefPtr<RTCSessionDescription> pendingRemoteDescription() const;

    void configureAndLinkSource(RealtimeOutgoingMediaSourceGStreamer&);

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

    void collectTransceivers();

    void createSessionDescriptionSucceeded(GUniqueOutPtr<GstWebRTCSessionDescription>&&);
    void createSessionDescriptionFailed(GUniqueOutPtr<GError>&&);

    void connectOutgoingSources();

    GstElement* pipeline() const { return m_pipeline.get(); }
    bool handleMessage(GstMessage*);

#if !RELEASE_LOG_DISABLED
    void processStats(const GValue*);
#endif

protected:
#if !RELEASE_LOG_DISABLED
    void onStatsDelivered(const GstStructure*);
#endif

private:
    GStreamerMediaEndpoint(GStreamerPeerConnectionBackend&);

    void initializePipeline();
    void teardownPipeline();
    void disposeElementChain(GstElement*);

    void setDescription(const RTCSessionDescription&, bool isLocal, Function<void()>&& successCallback, Function<void()>&& failureCallback);
    void initiate(bool isInitiator, GUniquePtr<GstStructure>&&);

    void onSignalingStateChange();
    void onNegotiationNeeded();
    void onIceConnectionChange();
    void onIceGatheringChange();
    void onIceCandidate(guint sdpMLineIndex, gchararray candidate);

    MediaStream& mediaStreamFromRTCStream(String label);

    void addRemoteStream(GstPad*);
    void removeRemoteStream(GstPad*);

    Optional<Backends> createTransceiverBackends(const String& kind, const RTCRtpTransceiverInit&, GStreamerRtpSenderBackend::Source&&);
    // void newTransceiver(rtc::scoped_refptr<webrtc::RtpTransceiverInterface>&&);
    GStreamerRtpSenderBackend::Source createSourceForTrack(MediaStreamTrack&);

    void storeRemoteMLineInfo(GstSDPMessage*);
    void flushPendingSources();

#if !RELEASE_LOG_DISABLED
    void gatherStatsForLogging();
    void startLoggingStats();
    void stopLoggingStats();

    const Logger& logger() const final { return m_logger.get(); }
    const void* logIdentifier() const final { return m_logIdentifier; }
    const char* logClassName() const final { return "GStreamerMediaEndpoint"; }
    WTFLogChannel& logChannel() const final;

    Seconds statsLogInterval(Seconds) const;
#endif

    HashMap<String, RealtimeMediaSource::Type> m_mediaForMid;
    RealtimeMediaSource::Type mediaTypeForMid(const char* mid);

    GStreamerPeerConnectionBackend& m_peerConnectionBackend;
    GRefPtr<GstElement> m_webrtcBin;
    GRefPtr<GstElement> m_pipeline;

    HashMap<String, RefPtr<MediaStream>> m_remoteStreamsById;

    struct PendingMLineInfo {
        GRefPtr<GstCaps> caps;
        bool isUsed;
        Vector<int> payloadTypes;
    };
    Vector<PendingMLineInfo> m_pendingRemoteMLineInfos;
    Vector<PendingMLineInfo> m_remoteMLineInfos;

    Ref<GStreamerStatsCollector> m_statsCollector;

    unsigned m_requestPadCounter { 0 };
    int m_ptCounter { 96 };

    bool m_delayedNegotiation { false };

    unsigned m_mlineIndex { 0 };

    Vector<Ref<RealtimeOutgoingMediaSourceGStreamer>> m_pendingSources;
    Vector<Ref<RealtimeOutgoingMediaSourceGStreamer>> m_sources;

    unsigned m_pendingIncomingStreams { 0 };

    bool m_isInitiator { false };

#if !RELEASE_LOG_DISABLED
    Timer m_statsLogTimer;
    Seconds m_statsFirstDeliveredTimestamp;
    Ref<const Logger> m_logger;
    const void* m_logIdentifier;
#endif

};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
