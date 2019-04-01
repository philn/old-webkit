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

#include "RTCRtpTransceiverDirection.h"

namespace WebCore {

inline RTCRtpTransceiverDirection toRTCRtpTransceiverDirection(GstWebRTCRTPTransceiverDirection direction)
{
    switch (direction) {
    case GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE:
        // ¯\_(ツ)_/¯
        return 0;
    case GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE:
        return RTCRtpTransceiverDirection::Inactive;
    case GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY:
        return RTCRtpTransceiverDirection::Sendonly;
    case GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY:
        return RTCRtpTransceiverDirection::Recvonly;
    case GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV:
        return RTCRtpTransceiverDirection::Sendrecv;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

inline GstWebRTCRTPTransceiverDirection fromRTCRtpTransceiverDirection(RTCRtpTransceiverDirection direction)
{
    switch (direction) {
    case RTCRtpTransceiverDirection::Inactive:
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE;
    case RTCRtpTransceiverDirection::Sendonly:
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    case RTCRtpTransceiverDirection::Recvonly:
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
    case RTCRtpTransceiverDirection::Sendrecv:
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
    }
    return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
}

inline RTCRtpSendParameters toRTCRtpSendParameters()
{
    // FIXME
}

}
#endif
