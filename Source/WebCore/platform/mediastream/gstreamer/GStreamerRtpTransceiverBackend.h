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

#pragma once

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerRtpSenderBackend.h"
#include "RTCRtpTransceiverBackend.h"
#include "GRefPtrGStreamer.h"

namespace WebCore {

class GStreamerRtpReceiverBackend;

class GStreamerRtpTransceiverBackend final : public RTCRtpTransceiverBackend {
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit GStreamerRtpTransceiverBackend(GRefPtr<GstWebRTCRTPTransceiver> rtcTransceiver)
        : m_rtcTransceiver(rtcTransceiver)
    {
    }

    std::unique_ptr<GStreamerRtpReceiverBackend> createReceiverBackend();
    std::unique_ptr<GStreamerRtpSenderBackend> createSenderBackend(GStreamerPeerConnectionBackend&, GStreamerRtpSenderBackend::Source&&);

    GstWebRTCRTPTransceiver* rtcTransceiver() { return m_rtcTransceiver.get(); }

private:
    RTCRtpTransceiverDirection direction() const final;
    Optional<RTCRtpTransceiverDirection> currentDirection() const final;
    void setDirection(RTCRtpTransceiverDirection) final;
    String mid() final;
    void stop() final;
    bool stopped() const final;

    GRefPtr<GstWebRTCRTPTransceiver> m_rtcTransceiver;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
