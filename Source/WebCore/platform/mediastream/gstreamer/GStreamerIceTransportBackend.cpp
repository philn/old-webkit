/*
 *  Copyright (C) 2021 Igalia S.L. All rights reserved.
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
#include "GStreamerIceTransportBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerIceTransportBackend.h"
#include "GStreamerWebRTCUtils.h"
#include <JavaScriptCore/ArrayBuffer.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

GStreamerIceTransportBackend::GStreamerIceTransportBackend(const GRefPtr<GstWebRTCICETransport>& transport)
    : m_backend(transport)
{
    ASSERT(m_backend);
    g_signal_connect_swapped(m_backend.get(), "notify::state", G_CALLBACK(+[](GStreamerIceTransportBackend* backend) {
        backend->stateChanged();
    }), this);
    g_signal_connect_swapped(m_backend.get(), "notify::gathering-state", G_CALLBACK(+[](GStreamerIceTransportBackend* backend) {
        backend->gatheringStateChanged();
    }), this);
}

GStreamerIceTransportBackend::~GStreamerIceTransportBackend()
{
}

void GStreamerIceTransportBackend::registerClient(Client& client)
{
    ASSERT(!m_client);
    m_client = client;

    GstWebRTCICEConnectionState transportState;
    GstWebRTCICEGatheringState gatheringState;
    g_object_get(m_backend.get(), "state", &transportState, "gathering-state", &gatheringState, nullptr);

    // We start observing a bit late and might miss the checking state. Synthesize it as needed.
    if (transportState > GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING && transportState != GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED) {
        callOnMainThread([protectedThis = WeakPtr { *this }] {
            if (protectedThis->m_client)
                protectedThis->m_client->onStateChanged(RTCIceTransportState::Checking);
        });
    }
    callOnMainThread([protectedThis = WeakPtr { *this }, transportState, gatheringState] {
        if (!protectedThis->m_client)
            return;
        protectedThis->m_client->onStateChanged(toRTCIceTransportState(transportState));
        protectedThis->m_client->onGatheringStateChanged(toRTCIceGatheringState(gatheringState));
    });
}

void GStreamerIceTransportBackend::unregisterClient()
{
    ASSERT(m_client);
    m_client.clear();
}

void GStreamerIceTransportBackend::stateChanged()
{
    if (!m_client)
        return;

    GstWebRTCICEConnectionState transportState;
    g_object_get(m_backend.get(), "state", &transportState, nullptr);
    callOnMainThread([protectedThis = WeakPtr { *this }, transportState] {
        if (!protectedThis->m_client)
            return;
        protectedThis->m_client->onStateChanged(toRTCIceTransportState(transportState));
    });
}

void GStreamerIceTransportBackend::gatheringStateChanged()
{
    if (!m_client)
        return;

    GstWebRTCICEGatheringState gatheringState;
    g_object_get(m_backend.get(), "gathering-state", &gatheringState, nullptr);
    callOnMainThread([protectedThis = WeakPtr { *this }, gatheringState] {
        if (!protectedThis->m_client)
            return;
        protectedThis->m_client->onGatheringStateChanged(toRTCIceGatheringState(gatheringState));
    });
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
