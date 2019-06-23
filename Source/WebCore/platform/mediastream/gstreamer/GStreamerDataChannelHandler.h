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

#if USE(GSTREAMER_WEBRTC)

#include "GRefPtrGStreamer.h"
#include "GUniquePtrGStreamer.h"
#include "RTCDataChannelHandler.h"

namespace WebCore {

class RTCDataChannelEvent;
class RTCDataChannelHandlerClient;
struct RTCDataChannelInit;
class ScriptExecutionContext;

class GStreamerDataChannelHandler final : public RTCDataChannelHandler {
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit GStreamerDataChannelHandler(GRefPtr<GstWebRTCDataChannel>&& channel) {
        m_channel = channel;
        ASSERT(m_channel);
    }
    ~GStreamerDataChannelHandler();

    static GUniquePtr<GstStructure> fromRTCDataChannelInit(const RTCDataChannelInit&);
    static Ref<RTCDataChannelEvent> channelEvent(ScriptExecutionContext&, GRefPtr<GstWebRTCDataChannel>&&);

protected:
    void onMessageData(GBytes*);
    void onMessageString(char*);
    void onBufferedAmountLow();
    void readyStateChanged();

private:
    // RTCDataChannelHandler API
    void setClient(RTCDataChannelHandlerClient&) final;
    void checkState();
    bool sendStringData(const String&) final;
    bool sendRawData(const char*, size_t) final;
    void close() final;
    size_t bufferedAmount() const final;

    void disconnectSignalHandlers();

    GRefPtr<GstWebRTCDataChannel> m_channel;
    RTCDataChannelHandlerClient* m_client { nullptr };
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
