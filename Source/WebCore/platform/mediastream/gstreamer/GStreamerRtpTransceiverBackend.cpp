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
// #include "GStreamerUtils.h"

namespace WebCore {

std::unique_ptr<GStreamerRtpReceiverBackend> GStreamerRtpTransceiverBackend::createReceiverBackend()
{
    return std::make_unique<GStreamerRtpReceiverBackend>(m_rtcTransceiver->receiver());
}

std::unique_ptr<GStreamerRtpSenderBackend> GStreamerRtpTransceiverBackend::createSenderBackend(GStreamerPeerConnectionBackend& backend, GStreamerRtpSenderBackend::Source&& source)
{
    return std::make_unique<GStreamerRtpSenderBackend>(backend, m_rtcTransceiver->sender(), WTFMove(source));
}

RTCRtpTransceiverDirection GStreamerRtpTransceiverBackend::direction() const
{
    return toRTCRtpTransceiverDirection(m_rtcTransceiver->direction());
}

Optional<RTCRtpTransceiverDirection> GStreamerRtpTransceiverBackend::currentDirection() const
{
    auto value = m_rtcTransceiver->current_direction();
    if (!value)
        return WTF::nullopt;
    return toRTCRtpTransceiverDirection(*value);
}

void GStreamerRtpTransceiverBackend::setDirection(RTCRtpTransceiverDirection direction)
{
    m_rtcTransceiver->SetDirection(fromRTCRtpTransceiverDirection(direction));
}

String GStreamerRtpTransceiverBackend::mid()
{
    if (auto mid = m_rtcTransceiver->mid())
        return fromStdString(*mid);
    return String { };
}

void GStreamerRtpTransceiverBackend::stop()
{
    m_rtcTransceiver->Stop();
}

bool GStreamerRtpTransceiverBackend::stopped() const
{
    return m_rtcTransceiver->stopped();
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
