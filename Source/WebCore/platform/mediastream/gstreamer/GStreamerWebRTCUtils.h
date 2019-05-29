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

#include "MediaEndpointConfiguration.h"
#include "RTCBundlePolicy.h"
#include "RTCIceConnectionState.h"
#include "RTCIceTransportPolicy.h"
#include "RTCRtpSendParameters.h"
#include "RTCRtpTransceiverDirection.h"
#include "RTCSdpType.h"
#include "RTCSignalingState.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API

namespace WebCore {

inline RTCRtpTransceiverDirection toRTCRtpTransceiverDirection(GstWebRTCRTPTransceiverDirection direction)
{
    switch (direction) {
    case GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE:
        // ¯\_(ツ)_/¯
        return RTCRtpTransceiverDirection::Inactive;
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
    return RTCRtpTransceiverDirection::Inactive;
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

/* inline RTCRtpSendParameters toRTCRtpSendParameters() */
/* { */
/*     // FIXME */
/* } */

static GstClockTime getWebRTCBaseTime()
{
    static GstClockTime webrtcBaseTime = GST_CLOCK_TIME_NONE;
    if(!GST_CLOCK_TIME_IS_VALID(webrtcBaseTime)) {
        GstClock* clock = gst_system_clock_obtain();
        webrtcBaseTime = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }
    return webrtcBaseTime;
}


static inline GstWebRTCSDPType toSessionDescriptionType(RTCSdpType sdpType)
{
    switch (sdpType) {
    case RTCSdpType::Offer:
        return GST_WEBRTC_SDP_TYPE_OFFER;
    case RTCSdpType::Pranswer:
        return GST_WEBRTC_SDP_TYPE_PRANSWER;
    case RTCSdpType::Answer:
        return GST_WEBRTC_SDP_TYPE_ANSWER;
    case RTCSdpType::Rollback:
        return GST_WEBRTC_SDP_TYPE_ROLLBACK;
    }

    ASSERT_NOT_REACHED();
    return GST_WEBRTC_SDP_TYPE_OFFER;
}

static inline RTCSdpType fromSessionDescriptionType(GstWebRTCSessionDescription* description)
{
    auto type = description->type;
    if (type == GST_WEBRTC_SDP_TYPE_OFFER)
        return RTCSdpType::Offer;
    if (type == GST_WEBRTC_SDP_TYPE_ANSWER)
        return RTCSdpType::Answer;
    ASSERT(type == GST_WEBRTC_SDP_TYPE_PRANSWER);
    return RTCSdpType::Pranswer;
}

static RTCSignalingState toSignalingState(GstWebRTCSignalingState state)
{
    switch (state) {
    case GST_WEBRTC_SIGNALING_STATE_STABLE:
        return RTCSignalingState::Stable;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER:
        return RTCSignalingState::HaveLocalOffer;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER:
        return RTCSignalingState::HaveLocalPranswer;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER:
        return RTCSignalingState::HaveRemoteOffer;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER:
        return RTCSignalingState::HaveRemotePranswer;
    case GST_WEBRTC_SIGNALING_STATE_CLOSED:
        // ¯\_(ツ)_/¯
        return RTCSignalingState::Stable;
    }

    ASSERT_NOT_REACHED();
    return RTCSignalingState::Stable;
}

static inline RTCIceConnectionState toRTCIceConnectionState(GstWebRTCICEConnectionState state)
{
    switch (state) {
    case GST_WEBRTC_ICE_CONNECTION_STATE_NEW:
        return RTCIceConnectionState::New;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING:
        return RTCIceConnectionState::Checking;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED:
        return RTCIceConnectionState::Connected;
    case GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED:
        return RTCIceConnectionState::Completed;
    case GST_WEBRTC_ICE_CONNECTION_STATE_FAILED:
        return RTCIceConnectionState::Failed;
    case GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED:
        return RTCIceConnectionState::Disconnected;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED:
        return RTCIceConnectionState::Closed;
    }

    ASSERT_NOT_REACHED();
    return RTCIceConnectionState::New;
}

static inline GstWebRTCBundlePolicy bundlePolicyFromConfiguration(MediaEndpointConfiguration& configuration)
{
    switch (configuration.bundlePolicy) {
    case RTCBundlePolicy::Balanced:
        return GST_WEBRTC_BUNDLE_POLICY_BALANCED;
    case RTCBundlePolicy::MaxCompat:
        return GST_WEBRTC_BUNDLE_POLICY_MAX_COMPAT;
    case RTCBundlePolicy::MaxBundle:
        return GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE;
    }

    ASSERT_NOT_REACHED();
    return GST_WEBRTC_BUNDLE_POLICY_NONE;
}

static inline GstWebRTCICETransportPolicy iceTransportPolicyFromConfiguration(MediaEndpointConfiguration& configuration)
{
    switch (configuration.iceTransportPolicy) {
    case RTCIceTransportPolicy::All:
        return GST_WEBRTC_ICE_TRANSPORT_POLICY_ALL;
    case RTCIceTransportPolicy::Relay:
        return GST_WEBRTC_ICE_TRANSPORT_POLICY_RELAY;
    }

    ASSERT_NOT_REACHED();
    return GST_WEBRTC_ICE_TRANSPORT_POLICY_ALL;
}

}
#endif
