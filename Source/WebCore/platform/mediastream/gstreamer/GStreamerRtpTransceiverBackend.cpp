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
#include "JSDOMPromiseDeferred.h"
#include "NotImplemented.h"
#include "RTCRtpCodecCapability.h"
#include <wtf/glib/GUniquePtr.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_webrtc_endpoint_debug);
#define GST_CAT_DEFAULT webkit_webrtc_endpoint_debug

namespace WebCore {

std::unique_ptr<GStreamerRtpReceiverBackend> GStreamerRtpTransceiverBackend::createReceiverBackend()
{
    GRefPtr<GstWebRTCRTPReceiver> receiver;
    g_object_get(m_rtcTransceiver.get(), "receiver", &receiver.outPtr(), nullptr);
    return std::make_unique<GStreamerRtpReceiverBackend>(WTFMove(receiver));
}

std::unique_ptr<GStreamerRtpSenderBackend> GStreamerRtpTransceiverBackend::createSenderBackend(GStreamerPeerConnectionBackend& backend, GStreamerRtpSenderBackend::Source&& source)
{
    GRefPtr<GstWebRTCRTPSender> sender;
    g_object_get(m_rtcTransceiver.get(), "sender", &sender.outPtr(), nullptr);
    return std::make_unique<GStreamerRtpSenderBackend>(backend, WTFMove(sender), WTFMove(source));
}

RTCRtpTransceiverDirection GStreamerRtpTransceiverBackend::direction() const
{
    GstWebRTCRTPTransceiverDirection gstDirection;
    g_object_get(m_rtcTransceiver.get(), "direction", &gstDirection, nullptr);
    return toRTCRtpTransceiverDirection(gstDirection);
}

std::optional<RTCRtpTransceiverDirection> GStreamerRtpTransceiverBackend::currentDirection() const
{
    GstWebRTCRTPTransceiverDirection gstDirection;
    g_object_get(m_rtcTransceiver.get(), "current-direction", &gstDirection, nullptr);
    if (!gstDirection)
        return std::nullopt;
    return toRTCRtpTransceiverDirection(gstDirection);
}

void GStreamerRtpTransceiverBackend::setDirection(RTCRtpTransceiverDirection direction)
{
    g_object_set(m_rtcTransceiver.get(), "direction", fromRTCRtpTransceiverDirection(direction), nullptr);
}

String GStreamerRtpTransceiverBackend::mid()
{
    GUniqueOutPtr<char> mid;
    g_object_get(m_rtcTransceiver.get(), "mid", &mid.outPtr(), nullptr);
    return String { mid.get() };
}

void GStreamerRtpTransceiverBackend::stop()
{
    notImplemented();
}

bool GStreamerRtpTransceiverBackend::stopped() const
{
    // gboolean isStopped;
    // g_object_get(m_rtcTransceiver.get(), "stopped", &isStopped, nullptr);
    // return isStopped;
    // FIXME: webrtcbin transceivers can't be stopped yet.
    return false;
}

static inline ExceptionOr<GstCaps*> toRtpCodecCapability(const RTCRtpCodecCapability& codec)
{
    if (!codec.mimeType.startsWith("video/") && !codec.mimeType.startsWith("audio/"))
        return Exception { InvalidModificationError, "RTCRtpCodecCapability bad mimeType" };

    GstCaps* caps = gst_caps_new_empty_simple(codec.mimeType.ascii().data());
    gst_caps_set_simple(caps, "clock-rate", G_TYPE_INT, codec.clockRate, nullptr);
    if (codec.channels)
        gst_caps_set_simple(caps, "channels", G_TYPE_INT, *codec.channels, nullptr);

    GST_FIXME("Unprocessed SDP FmtpLine: %s", codec.sdpFmtpLine.utf8().data());
    GST_DEBUG("Codec capability: %" GST_PTR_FORMAT, caps);
    return caps;
}

ExceptionOr<void> GStreamerRtpTransceiverBackend::setCodecPreferences(const Vector<RTCRtpCodecCapability>& codecs)
{
    auto gstCodecs = adoptGRef(gst_caps_new_empty());
    for (auto& codec : codecs) {
        auto result = toRtpCodecCapability(codec);
        if (result.hasException())
            return result.releaseException();
        gst_caps_append(gstCodecs.get(), result.releaseReturnValue());
    }
    g_object_set(m_rtcTransceiver.get(), "codec-preferences", gstCodecs.get(), nullptr);
    return { };
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
