/*
 * Copyright (C) 2020 Igalia, S.L.
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

#include "config.h"

#if USE(GSTREAMER)

#include "GStreamerTest.h"
#include "Test.h"
#include <WebCore/GStreamerCommon.h>

using namespace WebCore;

namespace TestWebKitAPI {

TEST_F(GStreamerTest, structureJSONSerializing)
{
    GUniquePtr<GstStructure> structure(gst_structure_new("foo", "int-val", G_TYPE_INT, 5, "str-val", G_TYPE_STRING, "foo", nullptr));
    auto jsonString = structureToJSONString(structure.get());
    ASSERT_EQ(jsonString, "{\"int-val\":5,\"str-val\":\"foo\"}");

    GUniquePtr<GstStructure> inner(gst_structure_new("bar", "boo", G_TYPE_BOOLEAN, FALSE, "double-val", G_TYPE_DOUBLE, 2.42, nullptr));
    gst_structure_set(structure.get(), "inner", GST_TYPE_STRUCTURE, inner.get(), nullptr);
    jsonString = structureToJSONString(structure.get());
    ASSERT_EQ(jsonString, "{\"int-val\":5,\"str-val\":\"foo\",\"inner\":{\"boo\":0,\"double-val\":2.42}}");

}

} // namespace TestWebKitAPI

#endif // USE(GSTREAMER)
