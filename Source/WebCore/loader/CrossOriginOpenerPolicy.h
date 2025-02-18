/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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

#include "SecurityOrigin.h"
#include <wtf/URL.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class ResourceResponse;
class ScriptExecutionContext;

// https://html.spec.whatwg.org/multipage/origin.html#cross-origin-opener-policy-value
enum class CrossOriginOpenerPolicyValue : uint8_t {
    UnsafeNone,
    SameOrigin,
    SameOriginPlusCOEP,
    SameOriginAllowPopups
};

// https://html.spec.whatwg.org/multipage/origin.html#cross-origin-opener-policy
struct CrossOriginOpenerPolicy {
    CrossOriginOpenerPolicyValue value { CrossOriginOpenerPolicyValue::UnsafeNone };
    String reportingEndpoint;
    CrossOriginOpenerPolicyValue reportOnlyValue { CrossOriginOpenerPolicyValue::UnsafeNone };
    String reportOnlyReportingEndpoint;
};

// https://html.spec.whatwg.org/multipage/origin.html#coop-enforcement-result
struct CrossOriginOpenerPolicyEnforcementResult {
    bool needsBrowsingContextGroupSwitch { false };
    bool needsBrowsingContextGroupSwitchDueToReportOnly { false };
    URL url;
    Ref<SecurityOrigin> currentOrigin;
    CrossOriginOpenerPolicy crossOriginOpenerPolicy;
    bool isCurrentContextNavigationSource { true };
};

CrossOriginOpenerPolicy obtainCrossOriginOpenerPolicy(const ResourceResponse&, const ScriptExecutionContext&);

} // namespace WebCore
