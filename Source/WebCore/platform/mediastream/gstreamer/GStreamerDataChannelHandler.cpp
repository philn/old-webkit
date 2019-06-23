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
#include "GStreamerDataChannelHandler.h"

#if USE(GSTREAMER_WEBRTC)

#include "EventNames.h"
#include "RTCDataChannel.h"
#include "RTCDataChannelEvent.h"

#include <gst/gst.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API
#include <wtf/MainThread.h>

namespace WebCore {

GUniquePtr<GstStructure> GStreamerDataChannelHandler::fromRTCDataChannelInit(const RTCDataChannelInit& options)
{
    GUniquePtr<GstStructure> init(gst_structure_new("options", "protocol", G_TYPE_STRING, options.protocol.utf8().data(), nullptr));

    if (options.ordered)
        gst_structure_set(init.get(), "ordered", G_TYPE_BOOLEAN, *options.ordered, nullptr);
    if (options.maxPacketLifeTime)
        gst_structure_set(init.get(), "max-packet-lifetime", G_TYPE_INT, *options.maxPacketLifeTime, nullptr);
    if (options.maxRetransmits)
        gst_structure_set(init.get(), "max-retransmits", G_TYPE_INT, *options.maxRetransmits, nullptr);
    if (options.negotiated)
        gst_structure_set(init.get(), "negotiated", G_TYPE_BOOLEAN, *options.negotiated, nullptr);
    if (options.id)
        gst_structure_set(init.get(), "id", G_TYPE_INT, *options.id, nullptr);

    return init;
}

Ref<RTCDataChannelEvent> GStreamerDataChannelHandler::channelEvent(ScriptExecutionContext& context, GRefPtr<GstWebRTCDataChannel>&& dataChannel)
{
    GUniqueOutPtr<char> label;
    GUniqueOutPtr<char> protocol;
    gboolean ordered, negotiated;
    gint maxPacketLifeTime, maxRetransmits, id;
    g_object_get(dataChannel.get(), "ordered", &ordered, "label", &label.outPtr(),
        "max-packet-lifetime", &maxPacketLifeTime, "max-retransmits", &maxRetransmits,
        "protocol", &protocol.outPtr(), "negotiated", &negotiated, "id", &id, nullptr);

    RTCDataChannelInit init;
    init.ordered = ordered;
    init.maxPacketLifeTime = maxPacketLifeTime;
    init.maxRetransmits = maxRetransmits;
    init.protocol = String(protocol.get());
    init.negotiated = negotiated;
    init.id = id;

    auto handler = std::make_unique<GStreamerDataChannelHandler>(WTFMove(dataChannel));
    auto channel = RTCDataChannel::create(context, WTFMove(handler), String(label.get()), WTFMove(init));
    return RTCDataChannelEvent::create(eventNames().datachannelEvent, Event::CanBubble::No, Event::IsCancelable::No, WTFMove(channel));
}

GStreamerDataChannelHandler::~GStreamerDataChannelHandler()
{
    if (m_client)
        disconnectSignalHandlers();
}

void GStreamerDataChannelHandler::setClient(RTCDataChannelHandlerClient& client)
{
    ASSERT(!m_client);
    m_client = &client;

    g_signal_connect_swapped(m_channel.get(), "notify::ready-state", G_CALLBACK(+[](GStreamerDataChannelHandler* handler) {
        handler->readyStateChanged();
    }), this);
    g_signal_connect(m_channel.get(), "on-message-data", G_CALLBACK(+[](GstWebRTCDataChannel*, GBytes* bytes, GStreamerDataChannelHandler* handler) {
        handler->onMessageData(bytes);
    }), this);
    g_signal_connect(m_channel.get(), "on-message-string", G_CALLBACK(+[](GstWebRTCDataChannel*, char* str, GStreamerDataChannelHandler* handler) {
        handler->onMessageString(str);
    }), this);
    g_signal_connect(m_channel.get(), "on-buffered-amount-low", G_CALLBACK(+[](GstWebRTCDataChannel*, GStreamerDataChannelHandler* handler) {
        handler->onBufferedAmountLow();
    }), this);

    checkState();
}

void GStreamerDataChannelHandler::disconnectSignalHandlers()
{
    g_signal_handlers_disconnect_by_data(m_channel.get(), this);
}

bool GStreamerDataChannelHandler::sendStringData(const String& text)
{
    g_signal_emit_by_name(m_channel.get(), "send-string", text.utf8().data());
    return true;
}

bool GStreamerDataChannelHandler::sendRawData(const char* data, size_t length)
{
    GRefPtr<GBytes> bytes = adoptGRef(g_bytes_new(data, length));
    g_signal_emit_by_name(m_channel.get(), "send-data", bytes.get());
    return true;
}

void GStreamerDataChannelHandler::close()
{
    if (m_client) {
        disconnectSignalHandlers();
        m_client = nullptr;
    }
    g_signal_emit_by_name(m_channel.get(), "close");
}

size_t GStreamerDataChannelHandler::bufferedAmount() const
{
    uint64_t bufferedAmount = 0;
    g_object_get(m_channel.get(), "buffered-amount", &bufferedAmount, nullptr);
    // FIXME: This might wrap around.
    return static_cast<size_t>(bufferedAmount);
}

void GStreamerDataChannelHandler::checkState()
{
    GstWebRTCDataChannelState channelState;
    g_object_get(m_channel.get(), "ready-state", &channelState, nullptr);
    RTCDataChannelState state;
    switch (channelState) {
    case GST_WEBRTC_DATA_CHANNEL_STATE_NEW:
        break;
    case GST_WEBRTC_DATA_CHANNEL_STATE_CONNECTING:
        state = RTCDataChannelState::Connecting;
        break;
    case GST_WEBRTC_DATA_CHANNEL_STATE_OPEN:
        state = RTCDataChannelState::Open;
        break;
    case GST_WEBRTC_DATA_CHANNEL_STATE_CLOSING:
        state = RTCDataChannelState::Closing;
        break;
    case GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED:
        state = RTCDataChannelState::Closed;
        break;
    }
    callOnMainThread([protectedClient = makeRef(*m_client), state] {
        protectedClient->didChangeReadyState(state);
    });
}

void GStreamerDataChannelHandler::readyStateChanged()
{
    if (!m_client)
        return;

    checkState();
}

void GStreamerDataChannelHandler::onMessageData(GBytes* bytes)
{
    if (!m_client)
        return;

    gsize size = 0;
    const char* data = reinterpret_cast<const char*>(g_bytes_get_data(bytes, &size));
    callOnMainThread([protectedClient = makeRef(*m_client), data, size] {
        protectedClient->didReceiveRawData(data, size);
    });
}

void GStreamerDataChannelHandler::onMessageString(char* string)
{
    if (!m_client)
        return;

    CString buffer(string, strlen(string));
    callOnMainThread([protectedClient = makeRef(*m_client), buffer = WTFMove(buffer)] {
        protectedClient->didReceiveStringData(String::fromUTF8(buffer));
    });
}

void GStreamerDataChannelHandler::onBufferedAmountLow()
{
    if (!m_client)
        return;

    uint64_t bufferedAmount = 0;
    g_object_get(m_channel.get(), "buffered-amount", &bufferedAmount, nullptr);
    // if (previousAmount <= m_channel->buffered_amount())
    //     return;

    callOnMainThread([protectedClient = makeRef(*m_client), bufferedAmount] {
        // FIXME: This might wrap around.
        protectedClient->bufferedAmountIsDecreasing(static_cast<size_t>(bufferedAmount));
    });
}

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
