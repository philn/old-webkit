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

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "InlineLevelBox.h"
#include "InlineLine.h"
#include "InlineRect.h"
#include <wtf/IsoMallocInlines.h>
#include <wtf/UniqueRef.h>
#include <wtf/WeakPtr.h>

namespace WebCore {
namespace Layout {

class BoxGeometry;
class InlineFormattingContext;
class LineBoxBuilder;
struct SimplifiedVerticalAlignment;

//   ____________________________________________________________ Line Box
// |                                    --------------------
// |                                   |                    |
// | ----------------------------------|--------------------|---------- Root Inline Box
// ||   _____    ___      ___          |                    |
// ||  |        /   \    /   \         |  Inline Level Box  |
// ||  |_____  |     |  |     |        |                    |    ascent
// ||  |       |     |  |     |        |                    |
// ||__|________\___/____\___/_________|____________________|_______ alignment_baseline
// ||
// ||                                                      descent
// ||_______________________________________________________________
// |________________________________________________________________
// The resulting rectangular area that contains the boxes that form a single line of inline-level content is called a line box.
// https://www.w3.org/TR/css-inline-3/#model
class LineBox {
    WTF_MAKE_FAST_ALLOCATED;
public:
    LineBox(const Box& rootLayoutBox, const InlineLayoutPoint& logicalTopLeft, InlineLayoutUnit contentLogicalLeft, InlineLayoutUnit lineLogicalWidth, InlineLayoutUnit contentLogicalWidth, size_t nonSpanningInlineLevelBoxCount);

    const InlineRect& logicalRect() const { return m_logicalRect; }
    InlineLayoutUnit logicalWidth() const { return logicalSize().width(); }
    InlineLayoutUnit logicalHeight() const { return logicalSize().height(); }
    InlineLayoutPoint logicalTopLeft() const { return logicalRect().topLeft(); }
    InlineLayoutSize logicalSize() const { return logicalRect().size(); }
    InlineLayoutUnit contentLogicalWidth() const { return m_contentLogicalWidth; }

    // Note that the line can have many inline boxes and be "empty" the same time e.g. <div><span></span><span></span></div>
    bool hasContent() const { return m_hasContent; }
    bool hasInlineBox() const { return m_boxTypes.contains(InlineLevelBox::Type::InlineBox); }
    bool hasNonInlineBox() const { return m_boxTypes.containsAny({ InlineLevelBox::Type::AtomicInlineLevelBox, InlineLevelBox::Type::LineBreakBox, InlineLevelBox::Type::GenericInlineLevelBox }); }
    bool hasAtomicInlineLevelBox() const { return m_boxTypes.contains(InlineLevelBox::Type::AtomicInlineLevelBox); }

    const InlineLevelBox& inlineLevelBoxForLayoutBox(const Box& layoutBox) const { return const_cast<LineBox&>(*this).inlineLevelBoxForLayoutBox(layoutBox); }

    InlineRect logicalRectForTextRun(const Line::Run&) const;
    InlineRect logicalRectForLineBreakBox(const Box&) const;
    InlineRect logicalRectForRootInlineBox() const { return m_rootInlineBox.logicalRect(); }
    InlineRect logicalBorderBoxForAtomicInlineLevelBox(const Box&, const BoxGeometry&) const;
    InlineRect logicalBorderBoxForInlineBox(const Box&, const BoxGeometry&) const;

    const InlineLevelBox& rootInlineBox() const { return m_rootInlineBox; }
    using InlineLevelBoxList = Vector<InlineLevelBox>;
    const InlineLevelBoxList& nonRootInlineLevelBoxes() const { return m_nonRootInlineLevelBoxList; }

    InlineLayoutUnit alignmentBaseline() const { return m_rootInlineBox.logicalTop() + m_rootInlineBox.baseline(); }

private:
    friend class LineBoxBuilder;

    void setLogicalHeight(InlineLayoutUnit logicalHeight) { m_logicalRect.setHeight(logicalHeight); }

    void addInlineLevelBox(InlineLevelBox&&);
    InlineLevelBoxList& nonRootInlineLevelBoxes() { return m_nonRootInlineLevelBoxList; }

    InlineLevelBox& rootInlineBox() { return m_rootInlineBox; }

    InlineLevelBox& inlineLevelBoxForLayoutBox(const Box& layoutBox) { return &layoutBox == &m_rootInlineBox.layoutBox() ? m_rootInlineBox : m_nonRootInlineLevelBoxList[m_nonRootInlineLevelBoxMap.get(&layoutBox)]; }
    InlineRect logicalRectForInlineLevelBox(const Box& layoutBox) const;

    void setHasContent(bool hasContent) { m_hasContent = hasContent; }

private:
    InlineRect m_logicalRect;
    InlineLayoutUnit m_contentLogicalWidth { 0 };
    bool m_hasContent { false };
    OptionSet<InlineLevelBox::Type> m_boxTypes;

    InlineLevelBox m_rootInlineBox;
    InlineLevelBoxList m_nonRootInlineLevelBoxList;

    HashMap<const Box*, size_t> m_nonRootInlineLevelBoxMap;
};

}
}

#endif
