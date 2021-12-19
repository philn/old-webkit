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
#include "GStreamerDtlsTransportBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerIceTransportBackend.h"
#include "GStreamerWebRTCUtils.h"
#include <JavaScriptCore/ArrayBuffer.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

GStreamerDtlsTransportBackend::GStreamerDtlsTransportBackend(GRefPtr<GstWebRTCDTLSTransport>&& transport)
    : m_backend(WTFMove(transport))
{
    ASSERT(m_backend);
    ASSERT(isMainThread());
    g_signal_connect_swapped(m_backend.get(), "notify::state", G_CALLBACK(+[](GStreamerDtlsTransportBackend* backend) {
        callOnMainThread([backend] {
            backend->stateChanged();
        });
    }), this);
}

GStreamerDtlsTransportBackend::~GStreamerDtlsTransportBackend()
{
}

UniqueRef<RTCIceTransportBackend> GStreamerDtlsTransportBackend::iceTransportBackend()
{
    GRefPtr<GstWebRTCICETransport> iceTransport;
    g_object_get(m_backend.get(), "transport", &iceTransport.outPtr(), nullptr);
    return makeUniqueRef<GStreamerIceTransportBackend>(WTFMove(iceTransport));
}

void GStreamerDtlsTransportBackend::registerClient(Client& client)
{
    ASSERT(!m_client);
    m_client = client;
}

void GStreamerDtlsTransportBackend::unregisterClient()
{
    ASSERT(m_client);
    m_client.reset();
}

void GStreamerDtlsTransportBackend::stateChanged()
{
    if (!m_client)
        return;

    GUniqueOutPtr<char> remoteCertificate;
    GstWebRTCDTLSTransportState state;
    // FIXME "certificate" too?
    g_object_get(m_backend.get(), "state", &state, "remote-certificate", &remoteCertificate.outPtr(), nullptr);

    Vector<Ref<JSC::ArrayBuffer>> certificates;
    if (remoteCertificate) {
        auto certificateString = makeString(remoteCertificate.get());
        auto certificate = JSC::ArrayBuffer::create(certificateString.characters8(), certificateString.sizeInBytes());
        certificates.append(WTFMove(certificate));
    }
    (*m_client)->onStateChanged(toRTCDtlsTransportState(state), WTFMove(certificates));
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
