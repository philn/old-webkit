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
#include "RTCDataChannelState.h"
#include "SharedBuffer.h"
#include <wtf/Lock.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class Document;
class RTCDataChannelEvent;
class RTCDataChannelHandlerClient;
struct RTCDataChannelInit;

class GStreamerDataChannelHandler final : public RTCDataChannelHandler {
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit GStreamerDataChannelHandler(GRefPtr<GstWebRTCDataChannel>&&);
    ~GStreamerDataChannelHandler();

    static GUniquePtr<GstStructure> fromRTCDataChannelInit(const RTCDataChannelInit&);
    static Ref<RTCDataChannelEvent> channelEvent(Document&, GRefPtr<GstWebRTCDataChannel>&&);

protected:
    void onMessageData(GBytes*);
    void onMessageString(char*);
    void onError(GError*);
    void onBufferedAmountLow();
    void readyStateChanged();
    void bufferedAmountChanged();

private:
    // RTCDataChannelHandler API
    void setClient(RTCDataChannelHandlerClient&, ScriptExecutionContextIdentifier) final;
    void setBufferedAmountLowThreshold(size_t) final;
    bool sendStringData(const CString&) final;
    bool sendRawData(const uint8_t*, size_t) final;
    void close() final;

    void checkState();
    void postTask(Function<void()>&&);

    struct StateChange {
        RTCDataChannelState state;
        std::optional<GError*> error;
    };
    using Message = std::variant<StateChange, String, Ref<FragmentedSharedBuffer>>;
    using PendingMessages = Vector<Message>;

    Lock m_clientLock;
    GRefPtr<GstWebRTCDataChannel> m_channel;
    WeakPtr<RTCDataChannelHandlerClient> m_client WTF_GUARDED_BY_LOCK(m_clientLock) { nullptr };
    bool m_hasClient WTF_GUARDED_BY_LOCK(m_clientLock) { false };
    ScriptExecutionContextIdentifier m_contextIdentifier;
    std::optional<uint64_t> m_previousBufferedAmount;
    PendingMessages m_bufferedMessages WTF_GUARDED_BY_LOCK(m_clientLock);
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
