/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if PLATFORM(COCOA) && ENABLE(MEDIA_STREAM)

#include "GPUProcessConnection.h"
#include "RemoteRealtimeMediaSourceProxy.h"
#include <WebCore/CaptureDevice.h>
#include <WebCore/RealtimeMediaSourceIdentifier.h>
#include <WebCore/RealtimeVideoCaptureSource.h>
#include <wtf/Deque.h>

namespace IPC {
class Connection;
}

namespace WebCore {
class CAAudioStreamDescription;
class ImageTransferSessionVT;
struct MediaConstraints;
class MediaSample;
}

namespace WebKit {

class UserMediaCaptureManager;

class RemoteRealtimeVideoSource final : public WebCore::RealtimeVideoCaptureSource
#if ENABLE(GPU_PROCESS)
    , public GPUProcessConnection::Client
#endif
{
public:
    static Ref<WebCore::RealtimeVideoCaptureSource> create(const WebCore::CaptureDevice&, const WebCore::MediaConstraints*, String&& name, String&& hashSalt, UserMediaCaptureManager&, bool shouldCaptureInGPUProcess);
    ~RemoteRealtimeVideoSource();

    WebCore::RealtimeMediaSourceIdentifier identifier() const { return m_proxy.identifier(); }
    IPC::Connection* connection() { return m_proxy.connection(); }

    void setSettings(WebCore::RealtimeMediaSourceSettings&&);

    void captureStopped();
    void captureFailed() final;

    void videoSampleAvailable(WebCore::MediaSample&, WebCore::IntSize);

private:
    RemoteRealtimeVideoSource(WebCore::RealtimeMediaSourceIdentifier, const WebCore::CaptureDevice&, const WebCore::MediaConstraints*, String&& name, String&& hashSalt, UserMediaCaptureManager&, bool shouldCaptureInGPUProcess);

    // WebCore::RealtimeMediaSource
    void startProducingData() final { m_proxy.startProducingData(); }
    void stopProducingData() final { m_proxy.stopProducingData(); }
    bool isCaptureSource() const final { return true; }
    void beginConfiguration() final { }
    void commitConfiguration() final { }
    bool setShouldApplyRotation(bool /* shouldApplyRotation */) final;
    void hasEnded() final;
    const WebCore::RealtimeMediaSourceSettings& settings() final { return m_settings; }
    const WebCore::RealtimeMediaSourceCapabilities& capabilities() final;
    void whenReady(CompletionHandler<void(String)>&& callback) final { m_proxy.whenReady(WTFMove(callback)); }
    WebCore::CaptureDevice::DeviceType deviceType() const final { return m_proxy.deviceType(); }

    // WebCore::RealtimeVideoCaptureSource
    void generatePresets() final;
    WebCore::MediaSample::VideoRotation sampleRotation() const final { return m_sampleRotation; }
    void setFrameRateWithPreset(double, RefPtr<WebCore::VideoPreset>) final;
    bool prefersPreset(WebCore::VideoPreset&) final;

#if ENABLE(GPU_PROCESS)
    // GPUProcessConnection::Client
    void gpuProcessConnectionDidClose(GPUProcessConnection&) final;
#endif

    void createRemoteMediaSource();
    void didFail(String&& errorMessage);
    void setAsReady();
    void setCapabilities(WebCore::RealtimeMediaSourceCapabilities&&);

    RemoteRealtimeMediaSourceProxy m_proxy;
    UserMediaCaptureManager& m_manager;

    std::optional<WebCore::MediaConstraints> m_sizeConstraints;
    WebCore::RealtimeMediaSourceCapabilities m_capabilities;
    WebCore::RealtimeMediaSourceSettings m_settings;
    WebCore::MediaSample::VideoRotation m_sampleRotation { WebCore::MediaSample::VideoRotation::None };
};

} // namespace WebKit

#endif
