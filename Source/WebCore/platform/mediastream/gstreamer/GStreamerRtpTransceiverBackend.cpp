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

#include "config.h"
#include "GStreamerRtpTransceiverBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerRtpReceiverBackend.h"
#include "GStreamerRtpSenderBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "NotImplemented.h"

namespace WebCore {

std::unique_ptr<GStreamerRtpReceiverBackend> GStreamerRtpTransceiverBackend::createReceiverBackend()
{
    GstWebRTCRTPReceiver* receiver;
    g_printerr("this: %p\n", this);
    g_printerr(">>> transceiver %p\n", m_rtcTransceiver.get());
    g_object_get(m_rtcTransceiver.get(), "receiver", &receiver, nullptr);
    return std::make_unique<GStreamerRtpReceiverBackend>(receiver);
}

std::unique_ptr<GStreamerRtpSenderBackend> GStreamerRtpTransceiverBackend::createSenderBackend(GStreamerPeerConnectionBackend& backend, GStreamerRtpSenderBackend::Source&& source)
{
    GstWebRTCRTPSender* sender;
    g_object_get(m_rtcTransceiver.get(), "sender", &sender, nullptr);
    return std::make_unique<GStreamerRtpSenderBackend>(backend, sender, WTFMove(source));
}

RTCRtpTransceiverDirection GStreamerRtpTransceiverBackend::direction() const
{
    // FIXME: Why no direction GObject property in GstWebRTCRTPTransceiver?
    return toRTCRtpTransceiverDirection(m_rtcTransceiver->direction);
}

Optional<RTCRtpTransceiverDirection> GStreamerRtpTransceiverBackend::currentDirection() const
{
    // FIXME: Why no current-direction GObject property in GstWebRTCRTPTransceiver?
    GstWebRTCRTPTransceiverDirection value = m_rtcTransceiver->current_direction;
    if (!value)
        return WTF::nullopt;
    return toRTCRtpTransceiverDirection(value);
}

void GStreamerRtpTransceiverBackend::setDirection(RTCRtpTransceiverDirection direction)
{
    // FIXME: Why no direction GObject property in GstWebRTCRTPTransceiver?
    m_rtcTransceiver->direction = fromRTCRtpTransceiverDirection(direction);
}

String GStreamerRtpTransceiverBackend::mid()
{
    // FIXME: Why no mid GObject property in GstWebRTCRTPTransceiver?
    if (char* mid = m_rtcTransceiver->mid)
        return mid;
    return String { };
}

void GStreamerRtpTransceiverBackend::stop()
{
    notImplemented();
}

bool GStreamerRtpTransceiverBackend::stopped() const
{
    // FIXME: Why no stopped GObject property in GstWebRTCRTPTransceiver?
    return m_rtcTransceiver->stopped;
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
