/*
 * Copyright (C) 2008-2021 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"

#if ENABLE(JIT)
#include "JIT.h"

#include "CacheableIdentifierInlines.h"
#include "CodeBlock.h"
#include "DirectArguments.h"
#include "JITInlines.h"
#include "JITThunks.h"
#include "JSLexicalEnvironment.h"
#include "LinkBuffer.h"
#include "PrivateFieldPutKind.h"
#include "SlowPathCall.h"
#include "StructureStubInfo.h"
#include "ThunkGenerators.h"
#include <wtf/ScopedLambda.h>
#include <wtf/StringPrintStream.h>

namespace JSC {
#if USE(JSVALUE64)

void JIT::emit_op_get_by_val(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetByVal>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;
    ArrayProfile* profile = &metadata.m_arrayProfile;

    emitGetVirtualRegister(base, regT0);
    emitGetVirtualRegister(property, regT1);

    if (metadata.m_seenIdentifiers.count() > Options::getByValICMaxNumberOfIdentifiers()) {
        auto notCell = branchIfNotCell(regT0);
        emitArrayProfilingSiteWithCell(regT0, regT2, profile);
        notCell.link(this);
        callOperationWithProfile(bytecode.metadata(m_codeBlock), operationGetByVal, dst, TrustedImmPtr(m_codeBlock->globalObject()), regT0, regT1);
    } else {
        emitJumpSlowCaseIfNotJSCell(regT0, base);
        emitArrayProfilingSiteWithCell(regT0, regT2, profile);

        JSValueRegs resultRegs = JSValueRegs(regT0);

        JITGetByValGenerator gen(
            m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), AccessType::GetByVal, RegisterSet::stubUnavailableRegisters(),
            JSValueRegs(regT0), JSValueRegs(regT1), resultRegs, regT2);
        if (isOperandConstantInt(property))
            gen.stubInfo()->propertyIsInt32 = true;
        gen.generateFastPath(*this);
        if (!JITCode::useDataIC(JITType::BaselineJIT))
            addSlowCase(gen.slowPathJump());
        else
            addSlowCase();
        m_getByVals.append(gen);

        emitValueProfilingSite(bytecode.metadata(m_codeBlock), resultRegs);
        emitPutVirtualRegister(dst);
    }

}

void JIT::emitSlow_op_get_by_val(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    if (hasAnySlowCases(iter)) {
        auto bytecode = currentInstruction->as<OpGetByVal>();
        VirtualRegister dst = bytecode.m_dst;
        auto& metadata = bytecode.metadata(m_codeBlock);
        ArrayProfile* profile = &metadata.m_arrayProfile;

        linkAllSlowCases(iter);

        JITGetByValGenerator& gen = m_getByVals[m_getByValIndex++];

        Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
        Call call = callOperationWithProfile(metadata, operationGetByValOptimize, dst, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), profile, regT0, regT1);
#else
        VM& vm = this->vm();
        uint32_t bytecodeOffset = m_bytecodeIndex.offset();
        ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

        constexpr GPRReg bytecodeOffsetGPR = argumentGPR4;
        move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

        constexpr GPRReg stubInfoGPR = argumentGPR3; // arg1 arg1 already used.
        constexpr GPRReg profileGPR = argumentGPR2;
        constexpr GPRReg baseGPR = regT0;
        constexpr GPRReg propertyGPR = regT1;
        static_assert(baseGPR == argumentGPR0 || !isARM64());
        static_assert(propertyGPR == argumentGPR1);

        move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
        move(TrustedImmPtr(profile), profileGPR);
        emitNakedNearCall(vm.getCTIStub(slow_op_get_by_val_prepareCallGenerator).retaggedCode<NoPtrTag>());

        Call call;
        if (JITCode::useDataIC(JITType::BaselineJIT))
            gen.stubInfo()->m_slowOperation = operationGetByValOptimize;
        else
            call = appendCall(operationGetByValOptimize);
        emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

        emitValueProfilingSite(metadata, returnValueGPR);
        emitPutVirtualRegister(dst, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

        gen.reportSlowPathCall(coldPathBegin, call);
    }
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_get_by_val_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR4;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR5;
    constexpr GPRReg stubInfoGPR = argumentGPR3;
    constexpr GPRReg profileGPR = argumentGPR2;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyGPR == argumentGPR1);

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationGetByValOptimize)>(globalObjectGPR, stubInfoGPR, profileGPR, baseGPR, propertyGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_get_by_val_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_get_private_name(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetPrivateName>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;
    GPRReg baseGPR = regT0;
    GPRReg propertyGPR = regT1;
    emitGetVirtualRegister(base, baseGPR);
    emitGetVirtualRegister(property, propertyGPR);

    emitJumpSlowCaseIfNotJSCell(regT0, base);

    JSValueRegs resultRegs = JSValueRegs(regT0);

    JITGetByValGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), AccessType::GetPrivateName,
        RegisterSet::stubUnavailableRegisters(), JSValueRegs(baseGPR), JSValueRegs(propertyGPR), resultRegs, regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_getByVals.append(gen);

    emitValueProfilingSite(bytecode.metadata(m_codeBlock), resultRegs);
    emitPutVirtualRegister(dst);
}

void JIT::emitSlow_op_get_private_name(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    ASSERT(hasAnySlowCases(iter));
    auto bytecode = currentInstruction->as<OpGetPrivateName>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;

    linkAllSlowCases(iter);

    JITGetByValGenerator& gen = m_getByVals[m_getByValIndex++];
    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    Call call = callOperationWithProfile(metadata, operationGetPrivateNameOptimize, dst, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), baseGPR, propertyGPR);
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR2; // arg1 already used.
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_private_name_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationGetPrivateNameOptimize;
    else
        call = appendCall(operationGetPrivateNameOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitValueProfilingSite(metadata, returnValueGPR);
    emitPutVirtualRegister(dst, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_get_private_name_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR5;
    constexpr GPRReg stubInfoGPR = argumentGPR2;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyGPR == argumentGPR1);

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationGetPrivateNameOptimize)>(globalObjectGPR, stubInfoGPR, baseGPR, propertyGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_xxx_private_name_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_set_private_brand(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpSetPrivateBrand>();
    VirtualRegister base = bytecode.m_base;
    VirtualRegister brand = bytecode.m_brand;
    GPRReg baseGPR = regT0;
    GPRReg brandGPR = regT1;
    emitGetVirtualRegister(base, baseGPR);
    emitGetVirtualRegister(brand, brandGPR);

    emitJumpSlowCaseIfNotJSCell(baseGPR, base);

    JITPrivateBrandAccessGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), AccessType::SetPrivateBrand, RegisterSet::stubUnavailableRegisters(),
        JSValueRegs(baseGPR), JSValueRegs(brandGPR), regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_privateBrandAccesses.append(gen);

    // We should emit write-barrier at the end of sequence since write-barrier clobbers registers.
    // IC can write new Structure without write-barrier if a base is cell.
    // FIXME: Use UnconditionalWriteBarrier in Baseline effectively to reduce code size.
    // https://bugs.webkit.org/show_bug.cgi?id=209395
    emitWriteBarrier(base, ShouldFilterBase);
}

void JIT::emitSlow_op_set_private_brand(const Instruction*, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    JITPrivateBrandAccessGenerator& gen = m_privateBrandAccesses[m_privateBrandAccessIndex++];
    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg brandGPR = regT1;
    Call call = callOperation(operationSetPrivateBrandOptimize, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), baseGPR, brandGPR);
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR2; // arg1 already used.
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    static_assert(std::is_same<FunctionTraits<decltype(operationSetPrivateBrandOptimize)>::ArgumentTypes, FunctionTraits<decltype(operationGetPrivateNameOptimize)>::ArgumentTypes>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_private_name_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationSetPrivateBrandOptimize;
    else
        call = appendCall(operationSetPrivateBrandOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
#endif

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emit_op_check_private_brand(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpCheckPrivateBrand>();
    VirtualRegister base = bytecode.m_base;
    VirtualRegister brand = bytecode.m_brand;

    emitGetVirtualRegister(base, regT0);
    emitGetVirtualRegister(brand, regT1);

    emitJumpSlowCaseIfNotJSCell(regT0, base);

    JITPrivateBrandAccessGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), AccessType::CheckPrivateBrand, RegisterSet::stubUnavailableRegisters(),
        JSValueRegs(regT0), JSValueRegs(regT1), regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_privateBrandAccesses.append(gen);
}

void JIT::emitSlow_op_check_private_brand(const Instruction*, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    JITPrivateBrandAccessGenerator& gen = m_privateBrandAccesses[m_privateBrandAccessIndex++];
    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg brandGPR = regT1;
    Call call = callOperation(operationCheckPrivateBrandOptimize, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), baseGPR, brandGPR);
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR2; // arg1 already used.
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    static_assert(std::is_same<FunctionTraits<decltype(operationCheckPrivateBrandOptimize)>::ArgumentTypes, FunctionTraits<decltype(operationGetPrivateNameOptimize)>::ArgumentTypes>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_private_name_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationCheckPrivateBrandOptimize;
    else
        call = appendCall(operationCheckPrivateBrandOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emit_op_put_by_val_direct(const Instruction* currentInstruction)
{
    emit_op_put_by_val<OpPutByValDirect>(currentInstruction);
}

template<typename Op>
void JIT::emit_op_put_by_val(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<Op>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;
    ArrayProfile* profile = &metadata.m_arrayProfile;
    ByValInfo* byValInfo = m_codeBlock->addByValInfo(m_bytecodeIndex);

    emitGetVirtualRegister(base, regT0);
    bool propertyNameIsIntegerConstant = isOperandConstantInt(property);
    if (propertyNameIsIntegerConstant)
        move(Imm32(getOperandConstantInt(property)), regT1);
    else
        emitGetVirtualRegister(property, regT1);

    emitJumpSlowCaseIfNotJSCell(regT0, base);
    PatchableJump notIndex;
    if (!propertyNameIsIntegerConstant) {
        if (JITCode::useDataIC(JITType::BaselineJIT)) {
            auto isInt32 = branchIfInt32(regT1);
            farJump(AbsoluteAddress(&byValInfo->m_notIndexJumpTarget), JITStubRoutinePtrTag);
            isInt32.link(this);
        } else {
            notIndex = emitPatchableJumpIfNotInt(regT1);
            addSlowCase(notIndex);
        }
        // See comment in op_get_by_val.
        zeroExtend32ToWord(regT1, regT1);
    }
    emitArrayProfilingSiteWithCell(regT0, regT2, profile);

    PatchableJump badType;
    JumpList slowCases;

    // FIXME: Maybe we should do this inline?
    addSlowCase(branchTest32(NonZero, regT2, TrustedImm32(CopyOnWrite)));
    and32(TrustedImm32(IndexingShapeMask), regT2);

    JITArrayMode mode = chooseArrayMode(profile);
    switch (mode) {
    case JITInt32:
        slowCases = emitInt32PutByVal(bytecode, badType, byValInfo);
        break;
    case JITDouble:
        slowCases = emitDoublePutByVal(bytecode, badType, byValInfo);
        break;
    case JITContiguous:
        slowCases = emitContiguousPutByVal(bytecode, badType, byValInfo);
        break;
    case JITArrayStorage:
        slowCases = emitArrayStoragePutByVal(bytecode, badType, byValInfo);
        break;
    default:
        CRASH();
        break;
    }
    
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(badType);
    addSlowCase(slowCases);
    
    Label done = label();
    
    m_byValCompilationInfo.append(ByValCompilationInfo(byValInfo, m_bytecodeIndex, notIndex, badType, mode, profile, done, done));
}

template<typename Op>
JIT::JumpList JIT::emitGenericContiguousPutByVal(Op bytecode, PatchableJump& badType, ByValInfo* byValInfo, IndexingType indexingShape)
{
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister value = bytecode.m_value;
    ArrayProfile* profile = &metadata.m_arrayProfile;
    
    JumpList slowCases;

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(Equal, regT2, TrustedImm32(indexingShape));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(NotEqual, regT2, TrustedImm32(indexingShape));
    } else
        badType = patchableBranch32(NotEqual, regT2, TrustedImm32(indexingShape));

    
    loadPtr(Address(regT0, JSObject::butterflyOffset()), regT2);
    Jump outOfBounds = branch32(AboveOrEqual, regT1, Address(regT2, Butterfly::offsetOfPublicLength()));

    Label storeResult = label();
    emitGetVirtualRegister(value, regT3);
    switch (indexingShape) {
    case Int32Shape:
        slowCases.append(branchIfNotInt32(regT3));
        store64(regT3, BaseIndex(regT2, regT1, TimesEight));
        break;
    case DoubleShape: {
        Jump notInt = branchIfNotInt32(regT3);
        convertInt32ToDouble(regT3, fpRegT0);
        Jump ready = jump();
        notInt.link(this);
        add64(numberTagRegister, regT3);
        move64ToDouble(regT3, fpRegT0);
        slowCases.append(branchIfNaN(fpRegT0));
        ready.link(this);
        storeDouble(fpRegT0, BaseIndex(regT2, regT1, TimesEight));
        break;
    }
    case ContiguousShape:
        store64(regT3, BaseIndex(regT2, regT1, TimesEight));
        emitWriteBarrier(bytecode.m_base, value, ShouldFilterValue);
        break;
    default:
        CRASH();
        break;
    }
    
    Jump done = jump();
    outOfBounds.link(this);
    
    slowCases.append(branch32(AboveOrEqual, regT1, Address(regT2, Butterfly::offsetOfVectorLength())));
    
    emitArrayProfileStoreToHoleSpecialCase(profile);
    
    add32(TrustedImm32(1), regT1, regT3);
    store32(regT3, Address(regT2, Butterfly::offsetOfPublicLength()));
    jump().linkTo(storeResult, this);
    
    done.link(this);
    
    return slowCases;
}

template<typename Op>
JIT::JumpList JIT::emitArrayStoragePutByVal(Op bytecode, PatchableJump& badType, ByValInfo* byValInfo)
{
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister value = bytecode.m_value;
    ArrayProfile* profile = &metadata.m_arrayProfile;
    
    JumpList slowCases;
    
    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(Equal, regT2, TrustedImm32(ArrayStorageShape));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(NotEqual, regT2, TrustedImm32(ArrayStorageShape));
    } else
        badType = patchableBranch32(NotEqual, regT2, TrustedImm32(ArrayStorageShape));

    loadPtr(Address(regT0, JSObject::butterflyOffset()), regT2);
    slowCases.append(branch32(AboveOrEqual, regT1, Address(regT2, ArrayStorage::vectorLengthOffset())));

    Jump empty = branchTest64(Zero, BaseIndex(regT2, regT1, TimesEight, ArrayStorage::vectorOffset()));

    Label storeResult(this);
    emitGetVirtualRegister(value, regT3);
    store64(regT3, BaseIndex(regT2, regT1, TimesEight, ArrayStorage::vectorOffset()));
    emitWriteBarrier(bytecode.m_base, value, ShouldFilterValue);
    Jump end = jump();
    
    empty.link(this);
    emitArrayProfileStoreToHoleSpecialCase(profile);
    add32(TrustedImm32(1), Address(regT2, ArrayStorage::numValuesInVectorOffset()));
    branch32(Below, regT1, Address(regT2, ArrayStorage::lengthOffset())).linkTo(storeResult, this);

    add32(TrustedImm32(1), regT1);
    store32(regT1, Address(regT2, ArrayStorage::lengthOffset()));
    sub32(TrustedImm32(1), regT1);
    jump().linkTo(storeResult, this);

    end.link(this);
    
    return slowCases;
}

template<typename Op>
JITPutByIdGenerator JIT::emitPutByValWithCachedId(Op bytecode, PutKind putKind, CacheableIdentifier propertyName, JumpList& doneCases, JumpList& slowCases)
{
    // base: regT0
    // property: regT1
    // scratch: regT2

    VirtualRegister base = bytecode.m_base;
    VirtualRegister value = bytecode.m_value;

    slowCases.append(branchIfNotCell(regT1));
    emitByValIdentifierCheck(regT1, regT1, propertyName, slowCases);

    // Write barrier breaks the registers. So after issuing the write barrier,
    // reload the registers.
    emitGetVirtualRegisters(base, regT0, value, regT1);

    JITPutByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(), propertyName,
        JSValueRegs(regT0), JSValueRegs(regT1), regT3, regT2, ecmaMode(bytecode), putKind);
    gen.generateFastPath(*this);
    // IC can write new Structure without write-barrier if a base is cell.
    // FIXME: Use UnconditionalWriteBarrier in Baseline effectively to reduce code size.
    // https://bugs.webkit.org/show_bug.cgi?id=209395
    emitWriteBarrier(base, ShouldFilterBase);
    doneCases.append(jump());

    Label coldPathBegin = label();
    gen.slowPathJump().link(this);

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        gen.stubInfo()->m_slowOperation = gen.slowPathFunction();
        move(TrustedImmPtr(gen.stubInfo()), GPRInfo::nonArgGPR0);
        callOperation<decltype(gen.slowPathFunction())>(Address(GPRInfo::nonArgGPR0, StructureStubInfo::offsetOfSlowOperation()), TrustedImmPtr(m_codeBlock->globalObject()), GPRInfo::nonArgGPR0, regT1, regT0, propertyName.rawBits());
    } else
        call = callOperation(gen.slowPathFunction(), TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT1, regT0, propertyName.rawBits());
    gen.reportSlowPathCall(coldPathBegin, call);
    doneCases.append(jump());

    return gen;
}

void JIT::emitSlow_op_put_by_val(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    bool isDirect = currentInstruction->opcodeID() == op_put_by_val_direct;
    VirtualRegister base;
    VirtualRegister property;
    VirtualRegister value;
    ECMAMode ecmaMode = ECMAMode::strict();

    auto load = [&](auto bytecode) {
        base = bytecode.m_base;
        property = bytecode.m_property;
        value = bytecode.m_value;
        ecmaMode = bytecode.m_ecmaMode;
    };

    if (isDirect)
        load(currentInstruction->as<OpPutByValDirect>());
    else
        load(currentInstruction->as<OpPutByVal>());

    ByValInfo* byValInfo = m_byValCompilationInfo[m_byValInstructionIndex].byValInfo;

    linkAllSlowCases(iter);
    Label slowPath = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    emitGetVirtualRegister(base, regT0);
    emitGetVirtualRegister(property, regT1);
    emitGetVirtualRegister(value, regT2);
    Call call = callOperation(isDirect ? operationDirectPutByValOptimize : operationPutByValOptimize, TrustedImmPtr(m_codeBlock->globalObject()), regT0, regT1, regT2, byValInfo, TrustedImm32(ecmaMode.value()));
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg baseGPR = argumentGPR1;
    constexpr GPRReg propertyGPR = argumentGPR2;
    constexpr GPRReg valuePR = argumentGPR3;
    constexpr GPRReg byValInfoGPR = argumentGPR4;
    constexpr GPRReg ecmaModeGPR = argumentGPR5;

    emitGetVirtualRegister(base, baseGPR);
    emitGetVirtualRegister(property, propertyGPR);
    emitGetVirtualRegister(value, valuePR);
    move(TrustedImmPtr(byValInfo), byValInfoGPR);
    move(TrustedImm32(ecmaMode.value()), ecmaModeGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_put_by_val_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        byValInfo->m_slowOperation = isDirect ? operationDirectPutByValOptimize : operationPutByValOptimize;
    else
        call = appendCall(isDirect ? operationDirectPutByValOptimize : operationPutByValOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
#endif // ENABLE(EXTRA_CTI_THUNKS)

    m_byValCompilationInfo[m_byValInstructionIndex].slowPathTarget = slowPath;
    m_byValCompilationInfo[m_byValInstructionIndex].returnAddress = call;
    m_byValInstructionIndex++;
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_put_by_val_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg baseGPR = argumentGPR1;
    constexpr GPRReg propertyGPR = argumentGPR2;
    constexpr GPRReg valuePR = argumentGPR3;
    constexpr GPRReg byValInfoGPR = argumentGPR4;
    constexpr GPRReg ecmaModeGPR = argumentGPR5;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationPutByValOptimize)>(globalObjectGPR, baseGPR, propertyGPR, valuePR, byValInfoGPR, ecmaModeGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR4, ByValInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_put_xxx_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_put_private_name(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutPrivateName>();
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;
    ByValInfo* byValInfo = m_codeBlock->addByValInfo(m_bytecodeIndex);

    emitGetVirtualRegister(base, regT0);
    emitGetVirtualRegister(property, regT1);

    emitJumpSlowCaseIfNotJSCell(regT0, base);

    PatchableJump fastPathJmp;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        farJump(AbsoluteAddress(&byValInfo->m_notIndexJumpTarget), JITStubRoutinePtrTag);
    else {
        fastPathJmp = patchableJump();
        addSlowCase(fastPathJmp);
    }
    
    Label done = label();
    
    m_byValCompilationInfo.append(ByValCompilationInfo(byValInfo, m_bytecodeIndex, fastPathJmp, done, done));
}

void JIT::emitSlow_op_put_private_name(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    auto bytecode = currentInstruction->as<OpPutPrivateName>();
    ByValInfo* byValInfo = m_byValCompilationInfo[m_byValInstructionIndex].byValInfo;
    PrivateFieldPutKind putKind = bytecode.m_putKind;

    linkAllSlowCases(iter);
    Label slowPath = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    constexpr GPRReg valueGPR = regT2;

    emitGetVirtualRegister(bytecode.m_base, baseGPR);
    emitGetVirtualRegister(bytecode.m_property, propertyGPR);
    emitGetVirtualRegister(bytecode.m_value, valueGPR);
    Call call = callOperation(operationPutPrivateNameOptimize, TrustedImmPtr(m_codeBlock->globalObject()), baseGPR, propertyGPR, valueGPR, byValInfo, TrustedImm32(putKind.value()));
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg baseGPR = argumentGPR1;
    constexpr GPRReg propertyGPR = argumentGPR2;
    constexpr GPRReg valuePR = argumentGPR3;
    constexpr GPRReg byValInfoGPR = argumentGPR4;
    constexpr GPRReg putKindGPR = argumentGPR5;

    emitGetVirtualRegister(bytecode.m_base, baseGPR);
    emitGetVirtualRegister(bytecode.m_property, propertyGPR);
    emitGetVirtualRegister(bytecode.m_value, valuePR);
    move(TrustedImmPtr(byValInfo), byValInfoGPR);
    move(TrustedImm32(putKind.value()), putKindGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_put_private_name_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        byValInfo->m_slowOperation = operationPutPrivateNameOptimize;
    else
        call = appendCall(operationPutPrivateNameOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
#endif // ENABLE(EXTRA_CTI_THUNKS)

    m_byValCompilationInfo[m_byValInstructionIndex].slowPathTarget = slowPath;
    m_byValCompilationInfo[m_byValInstructionIndex].returnAddress = call;
    m_byValInstructionIndex++;
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_put_private_name_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg baseGPR = argumentGPR1;
    constexpr GPRReg propertyGPR = argumentGPR2;
    constexpr GPRReg valuePR = argumentGPR3;
    constexpr GPRReg byValInfoGPR = argumentGPR4;
    constexpr GPRReg putKindGPR = argumentGPR5;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationPutPrivateNameOptimize)>(globalObjectGPR, baseGPR, propertyGPR, valuePR, byValInfoGPR, putKindGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR4, ByValInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_put_put_private_name_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_put_getter_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutGetterById>();
    emitGetVirtualRegister(bytecode.m_base, regT0);
    int32_t options = bytecode.m_attributes;
    emitGetVirtualRegister(bytecode.m_accessor, regT1);
    callOperation(operationPutGetterById, TrustedImmPtr(m_codeBlock->globalObject()), regT0, m_codeBlock->identifier(bytecode.m_property).impl(), options, regT1);
}

void JIT::emit_op_put_setter_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutSetterById>();
    emitGetVirtualRegister(bytecode.m_base, regT0);
    int32_t options = bytecode.m_attributes;
    emitGetVirtualRegister(bytecode.m_accessor, regT1);
    callOperation(operationPutSetterById, TrustedImmPtr(m_codeBlock->globalObject()), regT0, m_codeBlock->identifier(bytecode.m_property).impl(), options, regT1);
}

void JIT::emit_op_put_getter_setter_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutGetterSetterById>();
    emitGetVirtualRegister(bytecode.m_base, regT0);
    int32_t attribute = bytecode.m_attributes;
    emitGetVirtualRegister(bytecode.m_getter, regT1);
    emitGetVirtualRegister(bytecode.m_setter, regT2);
    callOperation(operationPutGetterSetter, TrustedImmPtr(m_codeBlock->globalObject()), regT0, m_codeBlock->identifier(bytecode.m_property).impl(), attribute, regT1, regT2);
}

void JIT::emit_op_put_getter_by_val(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutGetterByVal>();
    emitGetVirtualRegister(bytecode.m_base, regT0);
    emitGetVirtualRegister(bytecode.m_property, regT1);
    int32_t attributes = bytecode.m_attributes;
    emitGetVirtualRegister(bytecode.m_accessor, regT2);
    callOperation(operationPutGetterByVal, TrustedImmPtr(m_codeBlock->globalObject()), regT0, regT1, attributes, regT2);
}

void JIT::emit_op_put_setter_by_val(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutSetterByVal>();
    emitGetVirtualRegister(bytecode.m_base, regT0);
    emitGetVirtualRegister(bytecode.m_property, regT1);
    int32_t attributes = bytecode.m_attributes;
    emitGetVirtualRegister(bytecode.m_accessor, regT2);
    callOperation(operationPutSetterByVal, TrustedImmPtr(m_codeBlock->globalObject()), regT0, regT1, attributes, regT2);
}

void JIT::emit_op_del_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpDelById>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    emitGetVirtualRegister(base, regT1);
    emitJumpSlowCaseIfNotJSCell(regT1, base);
    JITDelByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident),
        JSValueRegs(regT1), JSValueRegs(regT0), regT3, regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_delByIds.append(gen);

    boxBoolean(regT0, JSValueRegs(regT0));
    emitPutVirtualRegister(dst, JSValueRegs(regT0));

    // IC can write new Structure without write-barrier if a base is cell.
    // We should emit write-barrier at the end of sequence since write-barrier clobbers registers.
    // FIXME: Use UnconditionalWriteBarrier in Baseline effectively to reduce code size.
    // https://bugs.webkit.org/show_bug.cgi?id=209395
    emitWriteBarrier(base, ShouldFilterBase);
}

void JIT::emitSlow_op_del_by_id(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpDelById>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    JITDelByIdGenerator& gen = m_delByIds[m_delByIdIndex++];

    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    emitGetVirtualRegister(base, regT0);
    Call call = callOperation(operationDeleteByIdOptimize, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits(), TrustedImm32(bytecode.m_ecmaMode.value()));
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = argumentGPR2;
    constexpr GPRReg propertyGPR = argumentGPR3;
    constexpr GPRReg ecmaModeGPR = argumentGPR4;

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    emitGetVirtualRegister(base, baseGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    move(TrustedImm32(bytecode.m_ecmaMode.value()), ecmaModeGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_del_by_id_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationDeleteByIdOptimize;
    else
        call = appendCall(operationDeleteByIdOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
    static_assert(returnValueGPR == regT0);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    boxBoolean(regT0, JSValueRegs(regT0));
    emitPutVirtualRegister(dst, JSValueRegs(regT0));
    gen.reportSlowPathCall(coldPathBegin, call);
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_del_by_id_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = argumentGPR2;
    constexpr GPRReg propertyGPR = argumentGPR3;
    constexpr GPRReg ecmaModeGPR = argumentGPR4;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(argumentGPR0, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationDeleteByIdOptimize)>(globalObjectGPR, stubInfoGPR, baseGPR, propertyGPR, ecmaModeGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_del_by_id_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_del_by_val(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpDelByVal>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;

    emitGetVirtualRegister(base, regT1);
    emitJumpSlowCaseIfNotJSCell(regT1, base);
    emitGetVirtualRegister(property, regT0);
    emitJumpSlowCaseIfNotJSCell(regT0, property);
    JITDelByValGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        JSValueRegs(regT1), JSValueRegs(regT0), JSValueRegs(regT0), regT3, regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_delByVals.append(gen);

    boxBoolean(regT0, JSValueRegs(regT0));
    emitPutVirtualRegister(dst, JSValueRegs(regT0));

    // We should emit write-barrier at the end of sequence since write-barrier clobbers registers.
    // IC can write new Structure without write-barrier if a base is cell.
    // FIXME: Use UnconditionalWriteBarrier in Baseline effectively to reduce code size.
    // https://bugs.webkit.org/show_bug.cgi?id=209395
    emitWriteBarrier(base, ShouldFilterBase);
}

void JIT::emitSlow_op_del_by_val(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpDelByVal>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;

    JITDelByValGenerator& gen = m_delByVals[m_delByValIndex++];

    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    emitGetVirtualRegister(base, regT0);
    emitGetVirtualRegister(property, regT1);
    Call call = callOperation(operationDeleteByValOptimize, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, regT1, TrustedImm32(bytecode.m_ecmaMode.value()));
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = argumentGPR2;
    constexpr GPRReg propertyGPR = argumentGPR3;
    constexpr GPRReg ecmaModeGPR = argumentGPR4;

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    emitGetVirtualRegister(base, baseGPR);
    emitGetVirtualRegister(property, propertyGPR);
    move(TrustedImm32(bytecode.m_ecmaMode.value()), ecmaModeGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_del_by_val_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationDeleteByValOptimize;
    else
        call = appendCall(operationDeleteByValOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
    static_assert(returnValueGPR == regT0);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    boxBoolean(regT0, JSValueRegs(regT0));
    emitPutVirtualRegister(dst, JSValueRegs(regT0));
    gen.reportSlowPathCall(coldPathBegin, call);
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_del_by_val_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR0;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = argumentGPR2;
    constexpr GPRReg propertyGPR = argumentGPR3;
    constexpr GPRReg ecmaModeGPR = argumentGPR4;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(argumentGPR0, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationDeleteByValOptimize)>(globalObjectGPR, stubInfoGPR, baseGPR, propertyGPR, ecmaModeGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_del_by_val_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_try_get_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpTryGetById>();
    VirtualRegister resultVReg = bytecode.m_dst;
    VirtualRegister baseVReg = bytecode.m_base;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    emitGetVirtualRegister(baseVReg, regT0);

    emitJumpSlowCaseIfNotJSCell(regT0, baseVReg);

    JSValueRegs resultRegs = JSValueRegs(regT0);

    JITGetByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident), JSValueRegs(regT0), resultRegs, regT1, AccessType::TryGetById);
    gen.generateFastPath(*this);
    addSlowCase(gen.slowPathJump());
    m_getByIds.append(gen);
    
    emitValueProfilingSite(bytecode.metadata(m_codeBlock), resultRegs);
    emitPutVirtualRegister(resultVReg);
}

void JIT::emitSlow_op_try_get_by_id(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpTryGetById>();
    VirtualRegister resultVReg = bytecode.m_dst;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    JITGetByIdGenerator& gen = m_getByIds[m_getByIdIndex++];

    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperation(operationTryGetByIdOptimize, resultVReg, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits());
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    move(JIT::TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR3;
    static_assert(baseGPR == argumentGPR0 || !isARM64());

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    static_assert(std::is_same<decltype(operationTryGetByIdOptimize), decltype(operationGetByIdOptimize)>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_by_id_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationTryGetByIdOptimize;
    else
        call = appendCall(operationTryGetByIdOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitPutVirtualRegister(resultVReg, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emit_op_get_by_id_direct(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetByIdDirect>();
    VirtualRegister resultVReg = bytecode.m_dst;
    VirtualRegister baseVReg = bytecode.m_base;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    emitGetVirtualRegister(baseVReg, regT0);

    emitJumpSlowCaseIfNotJSCell(regT0, baseVReg);

    JSValueRegs resultRegs = JSValueRegs(regT0);

    JITGetByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident), JSValueRegs(regT0), resultRegs, regT1, AccessType::GetByIdDirect);
    gen.generateFastPath(*this);
    addSlowCase(gen.slowPathJump());
    m_getByIds.append(gen);

    emitValueProfilingSite(bytecode.metadata(m_codeBlock), resultRegs);
    emitPutVirtualRegister(resultVReg);
}

void JIT::emitSlow_op_get_by_id_direct(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpGetByIdDirect>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister resultVReg = bytecode.m_dst;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    JITGetByIdGenerator& gen = m_getByIds[m_getByIdIndex++];

    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperationWithProfile(metadata, operationGetByIdDirectOptimize, resultVReg, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits());
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR3;
    static_assert(baseGPR == argumentGPR0 || !isARM64());

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    static_assert(std::is_same<decltype(operationGetByIdDirectOptimize), decltype(operationGetByIdOptimize)>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_by_id_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationGetByIdDirectOptimize;
    else
        call = appendCall(operationGetByIdDirectOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitValueProfilingSite(metadata, returnValueGPR);
    emitPutVirtualRegister(resultVReg, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emit_op_get_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetById>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister resultVReg = bytecode.m_dst;
    VirtualRegister baseVReg = bytecode.m_base;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    emitGetVirtualRegister(baseVReg, regT0);
    
    emitJumpSlowCaseIfNotJSCell(regT0, baseVReg);
    
    if (*ident == m_vm->propertyNames->length && shouldEmitProfiling()) {
        Jump notArrayLengthMode = branch8(NotEqual, AbsoluteAddress(&metadata.m_modeMetadata.mode), TrustedImm32(static_cast<uint8_t>(GetByIdMode::ArrayLength)));
        emitArrayProfilingSiteWithCell(regT0, regT1, &metadata.m_modeMetadata.arrayLengthMode.arrayProfile);
        notArrayLengthMode.link(this);
    }

    JSValueRegs resultRegs = JSValueRegs(regT0);

    JITGetByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident), JSValueRegs(regT0), resultRegs, regT1, AccessType::GetById);
    gen.generateFastPath(*this);
    addSlowCase(gen.slowPathJump());
    m_getByIds.append(gen);

    emitValueProfilingSite(bytecode.metadata(m_codeBlock), resultRegs);
    emitPutVirtualRegister(resultVReg);
}

void JIT::emit_op_get_by_id_with_this(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetByIdWithThis>();
    VirtualRegister resultVReg = bytecode.m_dst;
    VirtualRegister baseVReg = bytecode.m_base;
    VirtualRegister thisVReg = bytecode.m_thisValue;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    emitGetVirtualRegister(baseVReg, regT0);
    emitGetVirtualRegister(thisVReg, regT1);
    emitJumpSlowCaseIfNotJSCell(regT0, baseVReg);
    emitJumpSlowCaseIfNotJSCell(regT1, thisVReg);

    JSValueRegs resultRegs = JSValueRegs(regT0);

    JITGetByIdWithThisGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident), resultRegs, JSValueRegs(regT0), JSValueRegs(regT1), regT2);
    gen.generateFastPath(*this);
    addSlowCase(gen.slowPathJump());
    m_getByIdsWithThis.append(gen);

    emitValueProfilingSite(bytecode.metadata(m_codeBlock), resultRegs);
    emitPutVirtualRegister(resultVReg);
}

void JIT::emitSlow_op_get_by_id(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpGetById>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister resultVReg = bytecode.m_dst;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    JITGetByIdGenerator& gen = m_getByIds[m_getByIdIndex++];
    
    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperationWithProfile(metadata, operationGetByIdOptimize, resultVReg, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits());
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR3;
    static_assert(baseGPR == argumentGPR0 || !isARM64());

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_by_id_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationGetByIdOptimize;
    else
        call = appendCall(operationGetByIdOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitValueProfilingSite(metadata, returnValueGPR);
    emitPutVirtualRegister(resultVReg, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_get_by_id_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR5;
    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR3;
    static_assert(baseGPR == argumentGPR0 || !isARM64());

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationGetByIdOptimize)>(globalObjectGPR, stubInfoGPR, baseGPR, propertyGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_get_by_id_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emitSlow_op_get_by_id_with_this(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpGetByIdWithThis>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister resultVReg = bytecode.m_dst;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    JITGetByIdWithThisGenerator& gen = m_getByIdsWithThis[m_getByIdWithThisIndex++];
    
    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperationWithProfile(metadata, operationGetByIdWithThisOptimize, resultVReg, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, regT1, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits());
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR2; // arg1 already in use.
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg thisGPR = regT1;
    constexpr GPRReg propertyGPR = argumentGPR4;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(thisGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_by_id_with_this_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationGetByIdWithThisOptimize;
    else
        call = appendCall(operationGetByIdWithThisOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitValueProfilingSite(metadata, returnValueGPR);
    emitPutVirtualRegister(resultVReg, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_get_by_id_with_this_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR5;
    constexpr GPRReg stubInfoGPR = argumentGPR2;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg thisGPR = regT1;
    constexpr GPRReg propertyGPR = argumentGPR4;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(thisGPR == argumentGPR1);

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationGetByIdWithThisOptimize)>(globalObjectGPR, stubInfoGPR, baseGPR, thisGPR, propertyGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_get_by_id_with_this_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_put_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutById>();
    VirtualRegister baseVReg = bytecode.m_base;
    VirtualRegister valueVReg = bytecode.m_value;
    bool direct = bytecode.m_flags.isDirect();
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    // In order to be able to patch both the Structure, and the object offset, we store one pointer,
    // to just after the arguments have been loaded into registers 'hotPathBegin', and we generate code
    // such that the Structure & offset are always at the same distance from this.

    emitGetVirtualRegisters(baseVReg, regT0, valueVReg, regT1);

    emitJumpSlowCaseIfNotJSCell(regT0, baseVReg);

    JITPutByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident),
        JSValueRegs(regT0), JSValueRegs(regT1), regT3, regT2, ecmaMode(bytecode),
        direct ? PutKind::Direct : PutKind::NotDirect);
    gen.generateFastPath(*this);
    addSlowCase(gen.slowPathJump());
    m_putByIds.append(gen);
    
    // IC can write new Structure without write-barrier if a base is cell.
    // FIXME: Use UnconditionalWriteBarrier in Baseline effectively to reduce code size.
    // https://bugs.webkit.org/show_bug.cgi?id=209395
    emitWriteBarrier(baseVReg, ShouldFilterBase);
}

void JIT::emitSlow_op_put_by_id(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpPutById>();
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    Label coldPathBegin(this);
    
    JITPutByIdGenerator& gen = m_putByIds[m_putByIdIndex++];

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperation(gen.slowPathFunction(), TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT1, regT0, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits());
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR3; // arg1 already in use.
    constexpr GPRReg valueGPR = regT1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR4;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(valueGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    emitNakedNearCall(vm.getCTIStub(slow_op_put_by_id_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = gen.slowPathFunction();
    else
        call = appendCall(gen.slowPathFunction());
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_put_by_id_prepareCallGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    if (!JITCode::useDataIC(JITType::BaselineJIT))
        jit.tagReturnAddress();

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg globalObjectGPR = argumentGPR5;
    constexpr GPRReg stubInfoGPR = argumentGPR3;
    constexpr GPRReg valueGPR = regT1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR4;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(valueGPR == argumentGPR1);

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), globalObjectGPR);
    jit.loadPtr(Address(globalObjectGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);

    jit.setupArguments<decltype(operationPutByIdStrictOptimize)>(globalObjectGPR, stubInfoGPR, valueGPR, baseGPR, propertyGPR);
    jit.prepareCallOperation(vm);

    if (JITCode::useDataIC(JITType::BaselineJIT))
        jit.farJump(Address(argumentGPR1, StructureStubInfo::offsetOfSlowOperation()), OperationPtrTag);
    else
        jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_put_by_id_prepareCall");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_in_by_id(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpInById>();
    VirtualRegister resultVReg = bytecode.m_dst;
    VirtualRegister baseVReg = bytecode.m_base;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    emitGetVirtualRegister(baseVReg, regT0);

    emitJumpSlowCaseIfNotJSCell(regT0, baseVReg);

    JITInByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(),
        CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident), JSValueRegs(regT0), JSValueRegs(regT0), regT1);
    gen.generateFastPath(*this);
    addSlowCase(gen.slowPathJump());
    m_inByIds.append(gen);

    emitPutVirtualRegister(resultVReg);
}

void JIT::emitSlow_op_in_by_id(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpInById>();
    VirtualRegister resultVReg = bytecode.m_dst;
    const Identifier* ident = &(m_codeBlock->identifier(bytecode.m_property));

    JITInByIdGenerator& gen = m_inByIds[m_inByIdIndex++];

    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperation(operationInByIdOptimize, resultVReg, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits());
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR1;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = argumentGPR3;
    static_assert(baseGPR == argumentGPR0 || !isARM64());

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(CacheableIdentifier::createFromIdentifierOwnedByCodeBlock(m_codeBlock, *ident).rawBits()), propertyGPR);
    // slow_op_get_by_id_prepareCallGenerator will do exactly what we need.
    // So, there's no point in creating a duplicate thunk just to give it a different name.
    static_assert(std::is_same<decltype(operationInByIdOptimize), decltype(operationGetByIdOptimize)>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_by_id_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationInByIdOptimize;
    else
        call = appendCall(operationInByIdOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitPutVirtualRegister(resultVReg, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emit_op_in_by_val(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpInByVal>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    VirtualRegister property = bytecode.m_property;
    auto& metadata = bytecode.metadata(m_codeBlock);
    ArrayProfile* profile = &metadata.m_arrayProfile;

    emitGetVirtualRegister(base, regT0);
    emitJumpSlowCaseIfNotJSCell(regT0, base);
    emitGetVirtualRegister(property, regT1);
    emitArrayProfilingSiteWithCell(regT0, regT2, profile);

    JITInByValGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), AccessType::InByVal, RegisterSet::stubUnavailableRegisters(),
        JSValueRegs(regT0), JSValueRegs(regT1), JSValueRegs(regT0), regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_inByVals.append(gen);

    emitPutVirtualRegister(dst);
}

void JIT::emitSlow_op_in_by_val(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpInByVal>();
    VirtualRegister dst = bytecode.m_dst;
    auto& metadata = bytecode.metadata(m_codeBlock);
    ArrayProfile* profile = &metadata.m_arrayProfile;

    JITInByValGenerator& gen = m_inByVals[m_inByValIndex++];

    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperation(operationInByValOptimize, dst, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), profile, regT0, regT1);
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR4;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR3;
    constexpr GPRReg profileGPR = argumentGPR2;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    move(TrustedImmPtr(profile), profileGPR);
    // slow_op_get_by_val_prepareCallGenerator will do exactly what we need.
    // So, there's no point in creating a duplicate thunk just to give it a different name.
    static_assert(std::is_same<decltype(operationInByValOptimize), decltype(operationGetByValOptimize)>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_by_val_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = operationInByValOptimize;
    else
        call = appendCall(operationInByValOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitPutVirtualRegister(dst, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emitHasPrivate(VirtualRegister dst, VirtualRegister base, VirtualRegister propertyOrBrand, AccessType type)
{
    emitGetVirtualRegister(base, regT0);
    emitJumpSlowCaseIfNotJSCell(regT0, base);
    emitGetVirtualRegister(propertyOrBrand, regT1);

    JITInByValGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), type, RegisterSet::stubUnavailableRegisters(),
        JSValueRegs(regT0), JSValueRegs(regT1), JSValueRegs(regT0), regT2);
    gen.generateFastPath(*this);
    if (!JITCode::useDataIC(JITType::BaselineJIT))
        addSlowCase(gen.slowPathJump());
    else
        addSlowCase();
    m_inByVals.append(gen);

    emitPutVirtualRegister(dst);
}

void JIT::emitHasPrivateSlow(VirtualRegister dst, AccessType type)
{
    ASSERT(type == AccessType::HasPrivateName || type == AccessType::HasPrivateBrand);

    JITInByValGenerator& gen = m_inByVals[m_inByValIndex++];
    Label coldPathBegin = label();

#if !ENABLE(EXTRA_CTI_THUNKS)
    Call call = callOperation(type == AccessType::HasPrivateName ? operationHasPrivateNameOptimize : operationHasPrivateBrandOptimize, dst, TrustedImmPtr(m_codeBlock->globalObject()), gen.stubInfo(), regT0, regT1);
#else
    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR3;
    move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

    constexpr GPRReg stubInfoGPR = argumentGPR2;
    constexpr GPRReg baseGPR = regT0;
    constexpr GPRReg propertyOrBrandGPR = regT1;
    static_assert(baseGPR == argumentGPR0 || !isARM64());
    static_assert(propertyOrBrandGPR == argumentGPR1);

    move(TrustedImmPtr(gen.stubInfo()), stubInfoGPR);
    static_assert(std::is_same<decltype(operationHasPrivateNameOptimize), decltype(operationGetPrivateNameOptimize)>::value);
    static_assert(std::is_same<decltype(operationHasPrivateBrandOptimize), decltype(operationGetPrivateNameOptimize)>::value);
    emitNakedNearCall(vm.getCTIStub(slow_op_get_private_name_prepareCallGenerator).retaggedCode<NoPtrTag>());

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT))
        gen.stubInfo()->m_slowOperation = type == AccessType::HasPrivateName ? operationHasPrivateNameOptimize : operationHasPrivateBrandOptimize;
    else
        call = appendCall(type == AccessType::HasPrivateName ? operationHasPrivateNameOptimize : operationHasPrivateBrandOptimize);
    emitNakedNearCall(vm.getCTIStub(checkExceptionGenerator).retaggedCode<NoPtrTag>());

    emitPutVirtualRegister(dst, returnValueGPR);
#endif // ENABLE(EXTRA_CTI_THUNKS)

    gen.reportSlowPathCall(coldPathBegin, call);
}

void JIT::emit_op_has_private_name(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpHasPrivateName>();
    emitHasPrivate(bytecode.m_dst, bytecode.m_base, bytecode.m_property, AccessType::HasPrivateName);
}

void JIT::emitSlow_op_has_private_name(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpHasPrivateName>();
    emitHasPrivateSlow(bytecode.m_dst, AccessType::HasPrivateName);
}

void JIT::emit_op_has_private_brand(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpHasPrivateBrand>();
    emitHasPrivate(bytecode.m_dst, bytecode.m_base, bytecode.m_brand, AccessType::HasPrivateBrand);
}

void JIT::emitSlow_op_has_private_brand(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpHasPrivateBrand>();
    emitHasPrivateSlow(bytecode.m_dst, AccessType::HasPrivateBrand);
}

void JIT::emitVarInjectionCheck(bool needsVarInjectionChecks)
{
    if (!needsVarInjectionChecks)
        return;
    addSlowCase(branch8(Equal, AbsoluteAddress(m_codeBlock->globalObject()->varInjectionWatchpoint()->addressOfState()), TrustedImm32(IsInvalidated)));
}

void JIT::emitResolveClosure(VirtualRegister dst, VirtualRegister scope, bool needsVarInjectionChecks, unsigned depth)
{
    emitVarInjectionCheck(needsVarInjectionChecks);
    emitGetVirtualRegister(scope, regT0);
    for (unsigned i = 0; i < depth; ++i)
        loadPtr(Address(regT0, JSScope::offsetOfNext()), regT0);
    emitPutVirtualRegister(dst);
}

#if !ENABLE(EXTRA_CTI_THUNKS)
void JIT::emit_op_resolve_scope(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpResolveScope>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister scope = bytecode.m_scope;
    ResolveType resolveType = metadata.m_resolveType;
    unsigned depth = metadata.m_localScopeDepth;

    auto emitCode = [&] (ResolveType resolveType) {
        switch (resolveType) {
        case GlobalProperty:
        case GlobalPropertyWithVarInjectionChecks: {
            JSScope* constantScope = JSScope::constantScopeForCodeBlock(resolveType, m_codeBlock);
            RELEASE_ASSERT(constantScope);
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            load32(&metadata.m_globalLexicalBindingEpoch, regT1);
            addSlowCase(branch32(NotEqual, AbsoluteAddress(m_codeBlock->globalObject()->addressOfGlobalLexicalBindingEpoch()), regT1));
            move(TrustedImmPtr(constantScope), regT0);
            emitPutVirtualRegister(dst);
            break;
        }

        case GlobalVar:
        case GlobalVarWithVarInjectionChecks:
        case GlobalLexicalVar:
        case GlobalLexicalVarWithVarInjectionChecks: {
            JSScope* constantScope = JSScope::constantScopeForCodeBlock(resolveType, m_codeBlock);
            RELEASE_ASSERT(constantScope);
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            move(TrustedImmPtr(constantScope), regT0);
            emitPutVirtualRegister(dst);
            break;
        }
        case ClosureVar:
        case ClosureVarWithVarInjectionChecks:
            emitResolveClosure(dst, scope, needsVarInjectionChecks(resolveType), depth);
            break;
        case ModuleVar:
            move(TrustedImmPtr(metadata.m_lexicalEnvironment.get()), regT0);
            emitPutVirtualRegister(dst);
            break;
        case Dynamic:
            addSlowCase(jump());
            break;
        case ResolvedClosureVar:
        case UnresolvedProperty:
        case UnresolvedPropertyWithVarInjectionChecks:
            RELEASE_ASSERT_NOT_REACHED();
        }
    };

    switch (resolveType) {
    case GlobalProperty:
    case GlobalPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(&metadata.m_resolveType, regT0);

        Jump notGlobalProperty = branch32(NotEqual, regT0, TrustedImm32(resolveType));
        emitCode(resolveType);
        skipToEnd.append(jump());

        notGlobalProperty.link(this);
        emitCode(needsVarInjectionChecks(resolveType) ? GlobalLexicalVarWithVarInjectionChecks : GlobalLexicalVar);

        skipToEnd.link(this);
        break;
    }
    case UnresolvedProperty:
    case UnresolvedPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(&metadata.m_resolveType, regT0);

        Jump notGlobalProperty = branch32(NotEqual, regT0, TrustedImm32(GlobalProperty));
        emitCode(GlobalProperty);
        skipToEnd.append(jump());
        notGlobalProperty.link(this);

        Jump notGlobalPropertyWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalPropertyWithVarInjectionChecks));
        emitCode(GlobalPropertyWithVarInjectionChecks);
        skipToEnd.append(jump());
        notGlobalPropertyWithVarInjections.link(this);

        Jump notGlobalLexicalVar = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVar));
        emitCode(GlobalLexicalVar);
        skipToEnd.append(jump());
        notGlobalLexicalVar.link(this);

        Jump notGlobalLexicalVarWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVarWithVarInjectionChecks));
        emitCode(GlobalLexicalVarWithVarInjectionChecks);
        skipToEnd.append(jump());
        notGlobalLexicalVarWithVarInjections.link(this);

        addSlowCase(jump());
        skipToEnd.link(this);
        break;
    }

    default:
        emitCode(resolveType);
        break;
    }
}
#else // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_resolve_scope(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpResolveScope>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister scope = bytecode.m_scope;
    ResolveType resolveType = metadata.m_resolveType;

    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);
    ASSERT(m_codeBlock->instructionAt(m_bytecodeIndex) == currentInstruction);

    constexpr GPRReg metadataGPR = regT7;
    constexpr GPRReg scopeGPR = regT6;
    constexpr GPRReg bytecodeOffsetGPR = regT5;

    if (resolveType == ModuleVar)
        move(TrustedImmPtr(metadata.m_lexicalEnvironment.get()), regT0);
    else {
        ptrdiff_t metadataOffset = m_codeBlock->offsetInMetadataTable(&metadata);

#define RESOLVE_SCOPE_GENERATOR(resolveType) op_resolve_scope_##resolveType##Generator,
        static const ThunkGenerator generators[] = {
            FOR_EACH_RESOLVE_TYPE(RESOLVE_SCOPE_GENERATOR)
        };
#undef RESOLVE_SCOPE_GENERATOR

        emitGetVirtualRegister(scope, scopeGPR);
        move(TrustedImmPtr(metadataOffset), metadataGPR);
        move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);
        emitNakedNearCall(vm.getCTIStub(generators[resolveType]).retaggedCode<NoPtrTag>());
    }

    emitPutVirtualRegister(dst);
}

MacroAssemblerCodeRef<JITThunkPtrTag> JIT::generateOpResolveScopeThunk(ResolveType resolveType, const char* thunkName)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    using Metadata = OpResolveScope::Metadata;
    constexpr GPRReg metadataGPR = regT7; // incoming
    constexpr GPRReg scopeGPR = regT6; // incoming
    constexpr GPRReg bytecodeOffsetGPR = regT5; // incoming - pass thru to slow path.
    constexpr GPRReg globalObjectGPR = regT4;
    UNUSED_PARAM(bytecodeOffsetGPR);
    RELEASE_ASSERT(thunkIsUsedForOpResolveScope(resolveType));

    tagReturnAddress();

    loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
    loadPtr(Address(regT3, CodeBlock::offsetOfMetadataTable()), regT3);
    addPtr(regT3, metadataGPR);

    JumpList slowCase;

    auto emitVarInjectionCheck = [&] (bool needsVarInjectionChecks, GPRReg globalObjectGPR = InvalidGPRReg) {
        if (!needsVarInjectionChecks)
            return;
        if (globalObjectGPR == InvalidGPRReg) {
            globalObjectGPR = regT4;
            loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
            loadPtr(Address(regT3, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);
        }
        loadPtr(Address(globalObjectGPR, OBJECT_OFFSETOF(JSGlobalObject, m_varInjectionWatchpoint)), regT3);
        slowCase.append(branch8(Equal, Address(regT3, WatchpointSet::offsetOfState()), TrustedImm32(IsInvalidated)));
    };

    auto emitResolveClosure = [&] (bool needsVarInjectionChecks) {
        emitVarInjectionCheck(needsVarInjectionChecks);
        move(scopeGPR, regT0);
        load32(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_localScopeDepth)), regT1);

        Label loop = label();
        Jump done = branchTest32(Zero, regT1);
        {
            loadPtr(Address(regT0, JSScope::offsetOfNext()), regT0);
            sub32(TrustedImm32(1), regT1);
            jump().linkTo(loop, this);
        }
        done.link(this);
    };

    auto emitCode = [&] (ResolveType resolveType) {
        switch (resolveType) {
        case GlobalProperty:
        case GlobalPropertyWithVarInjectionChecks: {
            // JSScope::constantScopeForCodeBlock() loads codeBlock->globalObject().
            loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
            loadPtr(Address(regT3, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType), globalObjectGPR);
            load32(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_globalLexicalBindingEpoch)), regT1);
            slowCase.append(branch32(NotEqual, Address(globalObjectGPR, JSGlobalObject::offsetOfGlobalLexicalBindingEpoch()), regT1));
            move(globalObjectGPR, regT0);
            break;
        }

        case GlobalVar:
        case GlobalVarWithVarInjectionChecks:
        case GlobalLexicalVar:
        case GlobalLexicalVarWithVarInjectionChecks: {
            // JSScope::constantScopeForCodeBlock() loads codeBlock->globalObject() for GlobalVar*,
            // and codeBlock->globalObject()->globalLexicalEnvironment() for GlobalLexicalVar*.
            loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
            loadPtr(Address(regT3, CodeBlock::offsetOfGlobalObject()), regT0);
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType), regT0);
            if (resolveType == GlobalLexicalVar || resolveType == GlobalLexicalVarWithVarInjectionChecks)
                loadPtr(Address(regT0, JSGlobalObject::offsetOfGlobalLexicalEnvironment()), regT0);
            break;
        }
        case ClosureVar:
        case ClosureVarWithVarInjectionChecks:
            emitResolveClosure(needsVarInjectionChecks(resolveType));
            break;
        case Dynamic:
            slowCase.append(jump());
            break;
        case ResolvedClosureVar:
        case ModuleVar:
        case UnresolvedProperty:
        case UnresolvedPropertyWithVarInjectionChecks:
            RELEASE_ASSERT_NOT_REACHED();
        }
    };

    switch (resolveType) {
    case GlobalProperty:
    case GlobalPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_resolveType)), regT0);

        Jump notGlobalProperty = branch32(NotEqual, regT0, TrustedImm32(resolveType));
        emitCode(resolveType);
        skipToEnd.append(jump());

        notGlobalProperty.link(this);
        emitCode(needsVarInjectionChecks(resolveType) ? GlobalLexicalVarWithVarInjectionChecks : GlobalLexicalVar);

        skipToEnd.link(this);
        break;
    }
    case UnresolvedProperty:
    case UnresolvedPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_resolveType)), regT0);

        Jump notGlobalProperty = branch32(NotEqual, regT0, TrustedImm32(GlobalProperty));
        emitCode(GlobalProperty);
        skipToEnd.append(jump());
        notGlobalProperty.link(this);

        Jump notGlobalPropertyWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalPropertyWithVarInjectionChecks));
        emitCode(GlobalPropertyWithVarInjectionChecks);
        skipToEnd.append(jump());
        notGlobalPropertyWithVarInjections.link(this);

        Jump notGlobalLexicalVar = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVar));
        emitCode(GlobalLexicalVar);
        skipToEnd.append(jump());
        notGlobalLexicalVar.link(this);

        Jump notGlobalLexicalVarWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVarWithVarInjectionChecks));
        emitCode(GlobalLexicalVarWithVarInjectionChecks);
        skipToEnd.append(jump());
        notGlobalLexicalVarWithVarInjections.link(this);

        slowCase.append(jump());
        skipToEnd.link(this);
        break;
    }

    default:
        emitCode(resolveType);
        break;
    }

    ret();

    LinkBuffer patchBuffer(*this, GLOBAL_THUNK_ID, LinkBuffer::Profile::Thunk);
    auto slowCaseHandler = vm().getCTIStub(slow_op_resolve_scopeGenerator);
    patchBuffer.link(slowCase, CodeLocationLabel(slowCaseHandler.retaggedCode<NoPtrTag>()));
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, thunkName);
}

#define DEFINE_RESOLVE_SCOPE_GENERATOR(resolveType) \
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::op_resolve_scope_##resolveType##Generator(VM& vm) \
    { \
        if constexpr (!thunkIsUsedForOpResolveScope(resolveType)) \
            return { }; \
        JIT jit(vm); \
        return jit.generateOpResolveScopeThunk(resolveType, "Baseline: op_resolve_scope_" #resolveType); \
    }
FOR_EACH_RESOLVE_TYPE(DEFINE_RESOLVE_SCOPE_GENERATOR)
#undef DEFINE_RESOLVE_SCOPE_GENERATOR

MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_resolve_scopeGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

    // The fast path already pushed the return address.
#if CPU(X86_64)
    jit.push(X86Registers::ebp);
#elif CPU(ARM64)
    jit.pushPair(framePointerRegister, linkRegister);
#endif

    constexpr GPRReg bytecodeOffsetGPR = regT5;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg codeBlockGPR = argumentGPR3;
    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg instructionGPR = argumentGPR1;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), codeBlockGPR);
    jit.loadPtr(Address(codeBlockGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);
    jit.loadPtr(Address(codeBlockGPR, CodeBlock::offsetOfInstructionsRawPointer()), instructionGPR);
    jit.addPtr(bytecodeOffsetGPR, instructionGPR);

    jit.setupArguments<decltype(operationResolveScopeForBaseline)>(globalObjectGPR, instructionGPR);
    jit.prepareCallOperation(vm);
    Call operation = jit.call(OperationPtrTag);
    Jump exceptionCheck = jit.emitNonPatchableExceptionCheck(vm);

#if CPU(X86_64)
    jit.pop(X86Registers::ebp);
#elif CPU(ARM64)
    jit.popPair(CCallHelpers::framePointerRegister, CCallHelpers::linkRegister);
#endif
    jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::Thunk);
    patchBuffer.link(operation, FunctionPtr<OperationPtrTag>(operationResolveScopeForBaseline));
    auto handler = vm.getCTIStub(popThunkStackPreservesAndHandleExceptionGenerator);
    patchBuffer.link(exceptionCheck, CodeLocationLabel(handler.retaggedCode<NoPtrTag>()));
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_resolve_scope");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emitLoadWithStructureCheck(VirtualRegister scope, Structure** structureSlot)
{
    loadPtr(structureSlot, regT1);
    emitGetVirtualRegister(scope, regT0);
    addSlowCase(branchTestPtr(Zero, regT1));
    load32(Address(regT1, Structure::structureIDOffset()), regT1);
    addSlowCase(branch32(NotEqual, Address(regT0, JSCell::structureIDOffset()), regT1));
}

void JIT::emitGetVarFromPointer(JSValue* operand, GPRReg reg)
{
    loadPtr(operand, reg);
}

void JIT::emitGetVarFromIndirectPointer(JSValue** operand, GPRReg reg)
{
    loadPtr(operand, reg);
    loadPtr(reg, reg);
}

void JIT::emitGetClosureVar(VirtualRegister scope, uintptr_t operand)
{
    emitGetVirtualRegister(scope, regT0);
    loadPtr(Address(regT0, JSLexicalEnvironment::offsetOfVariables() + operand * sizeof(Register)), regT0);
}

#if !ENABLE(EXTRA_CTI_THUNKS)
void JIT::emit_op_get_from_scope(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetFromScope>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister scope = bytecode.m_scope;
    ResolveType resolveType = metadata.m_getPutInfo.resolveType();
    Structure** structureSlot = metadata.m_structure.slot();
    uintptr_t* operandSlot = reinterpret_cast<uintptr_t*>(&metadata.m_operand);

    auto emitCode = [&] (ResolveType resolveType, bool indirectLoadForOperand) {
        switch (resolveType) {
        case GlobalProperty:
        case GlobalPropertyWithVarInjectionChecks: {
            emitLoadWithStructureCheck(scope, structureSlot); // Structure check covers var injection since we don't cache structures for anything but the GlobalObject. Additionally, resolve_scope handles checking for the var injection.
            GPRReg base = regT0;
            GPRReg result = regT0;
            GPRReg offset = regT1;
            GPRReg scratch = regT2;

            jitAssert(scopedLambda<Jump(void)>([&] () -> Jump {
                return branchPtr(Equal, base, TrustedImmPtr(m_codeBlock->globalObject()));
            }));

            load32(operandSlot, offset);
            if (ASSERT_ENABLED) {
                Jump isOutOfLine = branch32(GreaterThanOrEqual, offset, TrustedImm32(firstOutOfLineOffset));
                abortWithReason(JITOffsetIsNotOutOfLine);
                isOutOfLine.link(this);
            }
            loadPtr(Address(base, JSObject::butterflyOffset()), scratch);
            neg32(offset);
            signExtend32ToPtr(offset, offset);
            load64(BaseIndex(scratch, offset, TimesEight, (firstOutOfLineOffset - 2) * sizeof(EncodedJSValue)), result);
            break;
        }
        case GlobalVar:
        case GlobalVarWithVarInjectionChecks:
        case GlobalLexicalVar:
        case GlobalLexicalVarWithVarInjectionChecks:
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            if (indirectLoadForOperand)
                emitGetVarFromIndirectPointer(bitwise_cast<JSValue**>(operandSlot), regT0);
            else
                emitGetVarFromPointer(bitwise_cast<JSValue*>(*operandSlot), regT0);
            if (resolveType == GlobalLexicalVar || resolveType == GlobalLexicalVarWithVarInjectionChecks) // TDZ check.
                addSlowCase(branchIfEmpty(regT0));
            break;
        case ClosureVar:
        case ClosureVarWithVarInjectionChecks:
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            emitGetClosureVar(scope, *operandSlot);
            break;
        case Dynamic:
            addSlowCase(jump());
            break;
        case ResolvedClosureVar:
        case ModuleVar:
        case UnresolvedProperty:
        case UnresolvedPropertyWithVarInjectionChecks:
            RELEASE_ASSERT_NOT_REACHED();
        }
    };

    switch (resolveType) {
    case GlobalProperty:
    case GlobalPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(&metadata.m_getPutInfo, regT0);
        and32(TrustedImm32(GetPutInfo::typeBits), regT0); // Load ResolveType into T0

        Jump isNotGlobalProperty = branch32(NotEqual, regT0, TrustedImm32(resolveType));
        emitCode(resolveType, false);
        skipToEnd.append(jump());

        isNotGlobalProperty.link(this);
        emitCode(needsVarInjectionChecks(resolveType) ? GlobalLexicalVarWithVarInjectionChecks : GlobalLexicalVar, true);

        skipToEnd.link(this);
        break;
    }
    case UnresolvedProperty:
    case UnresolvedPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(&metadata.m_getPutInfo, regT0);
        and32(TrustedImm32(GetPutInfo::typeBits), regT0); // Load ResolveType into T0

        Jump isGlobalProperty = branch32(Equal, regT0, TrustedImm32(GlobalProperty));
        Jump notGlobalPropertyWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalPropertyWithVarInjectionChecks));
        isGlobalProperty.link(this);
        emitCode(GlobalProperty, false);
        skipToEnd.append(jump());
        notGlobalPropertyWithVarInjections.link(this);

        Jump notGlobalLexicalVar = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVar));
        emitCode(GlobalLexicalVar, true);
        skipToEnd.append(jump());
        notGlobalLexicalVar.link(this);

        Jump notGlobalLexicalVarWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVarWithVarInjectionChecks));
        emitCode(GlobalLexicalVarWithVarInjectionChecks, true);
        skipToEnd.append(jump());
        notGlobalLexicalVarWithVarInjections.link(this);

        addSlowCase(jump());

        skipToEnd.link(this);
        break;
    }

    default:
        emitCode(resolveType, false);
        break;
    }
    emitPutVirtualRegister(dst);
    emitValueProfilingSite(metadata, regT0);
}

void JIT::emitSlow_op_get_from_scope(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpGetFromScope>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    callOperationWithProfile(metadata, operationGetFromScope, dst, TrustedImmPtr(m_codeBlock->globalObject()), currentInstruction);
}

#else // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emit_op_get_from_scope(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetFromScope>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister scope = bytecode.m_scope;
    ResolveType resolveType = metadata.m_getPutInfo.resolveType();

    VM& vm = this->vm();
    uint32_t bytecodeOffset = m_bytecodeIndex.offset();
    ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);
    ASSERT(m_codeBlock->instructionAt(m_bytecodeIndex) == currentInstruction);

    constexpr GPRReg metadataGPR = regT7;
    constexpr GPRReg scopeGPR = regT6;
    constexpr GPRReg bytecodeOffsetGPR = regT5;

    if (resolveType == GlobalVar) {
        uintptr_t* operandSlot = reinterpret_cast<uintptr_t*>(&metadata.m_operand);
        emitGetVarFromPointer(bitwise_cast<JSValue*>(*operandSlot), regT0);
    } else {
        ptrdiff_t metadataOffset = m_codeBlock->offsetInMetadataTable(&metadata);

#define GET_FROM_SCOPE_GENERATOR(resolveType) op_get_from_scope_##resolveType##Generator,
        static const ThunkGenerator generators[] = {
            FOR_EACH_RESOLVE_TYPE(GET_FROM_SCOPE_GENERATOR)
        };
#undef GET_FROM_SCOPE_GENERATOR

        emitGetVirtualRegister(scope, scopeGPR);
        move(TrustedImmPtr(metadataOffset), metadataGPR);
        move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);
        emitNakedNearCall(vm.getCTIStub(generators[resolveType]).retaggedCode<NoPtrTag>());
    }
    emitPutVirtualRegister(dst);
}

MacroAssemblerCodeRef<JITThunkPtrTag> JIT::generateOpGetFromScopeThunk(ResolveType resolveType, const char* thunkName)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    using Metadata = OpGetFromScope::Metadata;
    constexpr GPRReg metadataGPR = regT7;
    constexpr GPRReg scopeGPR = regT6;
    RELEASE_ASSERT(thunkIsUsedForOpGetFromScope(resolveType));

    tagReturnAddress();

    loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
    loadPtr(Address(regT3, CodeBlock::offsetOfMetadataTable()), regT3);
    addPtr(regT3, metadataGPR);

    JumpList slowCase;

    auto emitLoadWithStructureCheck = [&] (GPRReg scopeGPR, int32_t metadataStructureOffset) {
        loadPtr(Address(metadataGPR, metadataStructureOffset), regT1);
        move(scopeGPR, regT0);
        slowCase.append(branchTestPtr(Zero, regT1));
        load32(Address(regT1, Structure::structureIDOffset()), regT1);
        slowCase.append(branch32(NotEqual, Address(regT0, JSCell::structureIDOffset()), regT1));
    };

    auto emitVarInjectionCheck = [&] (bool needsVarInjectionChecks) {
        if (!needsVarInjectionChecks)
            return;
        loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
        loadPtr(Address(regT3, CodeBlock::offsetOfGlobalObject()), regT3);
        loadPtr(Address(regT3, OBJECT_OFFSETOF(JSGlobalObject, m_varInjectionWatchpoint)), regT3);
        slowCase.append(branch8(Equal, Address(regT3, WatchpointSet::offsetOfState()), TrustedImm32(IsInvalidated)));
    };
    
    auto emitGetVarFromPointer = [&] (int32_t operand, GPRReg reg) {
        loadPtr(Address(metadataGPR, operand), reg);
        loadPtr(reg, reg);
    };

    auto emitGetVarFromIndirectPointer = [&] (int32_t operand, GPRReg reg) {
        loadPtr(Address(metadataGPR, operand), reg);
        loadPtr(reg, reg);
    };

    auto emitGetClosureVar = [&] (GPRReg scopeGPR, GPRReg operandGPR) {
        static_assert(1 << 3 == sizeof(Register));
        lshift64(TrustedImm32(3), operandGPR);
        addPtr(scopeGPR, operandGPR);
        loadPtr(Address(operandGPR, JSLexicalEnvironment::offsetOfVariables()), regT0);
    };

    auto emitCode = [&] (ResolveType resolveType, bool indirectLoadForOperand) {
        switch (resolveType) {
        case GlobalProperty:
        case GlobalPropertyWithVarInjectionChecks: {
            emitLoadWithStructureCheck(scopeGPR, OBJECT_OFFSETOF(Metadata, m_structure)); // Structure check covers var injection since we don't cache structures for anything but the GlobalObject. Additionally, resolve_scope handles checking for the var injection.

            constexpr GPRReg base = regT0;
            constexpr GPRReg result = regT0;
            constexpr GPRReg offset = regT1;
            constexpr GPRReg scratch = regT2;

            jitAssert(scopedLambda<Jump(void)>([&] () -> Jump {
                loadPtr(addressFor(CallFrameSlot::codeBlock), regT3);
                loadPtr(Address(regT3, CodeBlock::offsetOfGlobalObject()), regT3);
                return branchPtr(Equal, base, regT3);
            }));

            loadPtr(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_operand)), offset);
            if (ASSERT_ENABLED) {
                Jump isOutOfLine = branch32(GreaterThanOrEqual, offset, TrustedImm32(firstOutOfLineOffset));
                abortWithReason(JITOffsetIsNotOutOfLine);
                isOutOfLine.link(this);
            }
            loadPtr(Address(base, JSObject::butterflyOffset()), scratch);
            neg32(offset);
            signExtend32ToPtr(offset, offset);
            load64(BaseIndex(scratch, offset, TimesEight, (firstOutOfLineOffset - 2) * sizeof(EncodedJSValue)), result);
            break;
        }
        case GlobalVar:
        case GlobalVarWithVarInjectionChecks:
        case GlobalLexicalVar:
        case GlobalLexicalVarWithVarInjectionChecks:
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            if (indirectLoadForOperand)
                emitGetVarFromIndirectPointer(OBJECT_OFFSETOF(Metadata, m_operand), regT0);
            else
                emitGetVarFromPointer(OBJECT_OFFSETOF(Metadata, m_operand), regT0);
            if (resolveType == GlobalLexicalVar || resolveType == GlobalLexicalVarWithVarInjectionChecks) // TDZ check.
                slowCase.append(branchIfEmpty(regT0));
            break;
        case ClosureVar:
        case ClosureVarWithVarInjectionChecks:
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            loadPtr(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_operand)), regT3);
            emitGetClosureVar(scopeGPR, regT3);
            break;
        case Dynamic:
            slowCase.append(jump());
            break;
        case ResolvedClosureVar:
        case ModuleVar:
        case UnresolvedProperty:
        case UnresolvedPropertyWithVarInjectionChecks:
            RELEASE_ASSERT_NOT_REACHED();
        }
    };

    switch (resolveType) {
    case GlobalProperty:
    case GlobalPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_getPutInfo)), regT0);
        and32(TrustedImm32(GetPutInfo::typeBits), regT0); // Load ResolveType into T0

        Jump isNotGlobalProperty = branch32(NotEqual, regT0, TrustedImm32(resolveType));
        emitCode(resolveType, false);
        skipToEnd.append(jump());

        isNotGlobalProperty.link(this);
        emitCode(needsVarInjectionChecks(resolveType) ? GlobalLexicalVarWithVarInjectionChecks : GlobalLexicalVar, true);

        skipToEnd.link(this);
        break;
    }
    case UnresolvedProperty:
    case UnresolvedPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_getPutInfo)), regT0);
        and32(TrustedImm32(GetPutInfo::typeBits), regT0); // Load ResolveType into T0

        Jump isGlobalProperty = branch32(Equal, regT0, TrustedImm32(GlobalProperty));
        Jump notGlobalPropertyWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalPropertyWithVarInjectionChecks));
        isGlobalProperty.link(this);
        emitCode(GlobalProperty, false);
        skipToEnd.append(jump());
        notGlobalPropertyWithVarInjections.link(this);

        Jump notGlobalLexicalVar = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVar));
        emitCode(GlobalLexicalVar, true);
        skipToEnd.append(jump());
        notGlobalLexicalVar.link(this);

        Jump notGlobalLexicalVarWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVarWithVarInjectionChecks));
        emitCode(GlobalLexicalVarWithVarInjectionChecks, true);
        skipToEnd.append(jump());
        notGlobalLexicalVarWithVarInjections.link(this);

        slowCase.append(jump());

        skipToEnd.link(this);
        break;
    }

    default:
        emitCode(resolveType, false);
        break;
    }

    static_assert(ValueProfile::numberOfBuckets == 1);
    store64(regT0, Address(metadataGPR, OBJECT_OFFSETOF(Metadata, m_profile)));

    ret();

    LinkBuffer patchBuffer(*this, GLOBAL_THUNK_ID, LinkBuffer::Profile::Thunk);
    auto slowCaseHandler = vm().getCTIStub(slow_op_get_from_scopeGenerator);
    patchBuffer.link(slowCase, CodeLocationLabel(slowCaseHandler.retaggedCode<NoPtrTag>()));
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, thunkName);
}

#define DEFINE_GET_FROM_SCOPE_GENERATOR(resolveType) \
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::op_get_from_scope_##resolveType##Generator(VM& vm) \
    { \
        if constexpr (!thunkIsUsedForOpGetFromScope(resolveType)) \
            return { }; \
        JIT jit(vm); \
        return jit.generateOpGetFromScopeThunk(resolveType, "Baseline: op_get_from_scope_" #resolveType); \
    }
FOR_EACH_RESOLVE_TYPE(DEFINE_GET_FROM_SCOPE_GENERATOR)
#undef DEFINE_GET_FROM_SCOPE_GENERATOR

MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_get_from_scopeGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

#if CPU(X86_64)
    jit.push(X86Registers::ebp);
#elif CPU(ARM64)
    jit.pushPair(framePointerRegister, linkRegister);
#endif

    using Metadata = OpGetFromScope::Metadata;
    constexpr GPRReg metadataGPR = regT7;
    constexpr GPRReg bytecodeOffsetGPR = regT5;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg codeBlockGPR = argumentGPR3;
    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg instructionGPR = argumentGPR1;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), codeBlockGPR);
    jit.loadPtr(Address(codeBlockGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);
    jit.loadPtr(Address(codeBlockGPR, CodeBlock::offsetOfInstructionsRawPointer()), instructionGPR);
    jit.addPtr(bytecodeOffsetGPR, instructionGPR);

    ASSERT(RegisterSet::calleeSaveRegisters().contains(GPRInfo::numberTagRegister));
    jit.move(metadataGPR, GPRInfo::numberTagRegister); // Preserve metadata in a callee saved register.
    jit.setupArguments<decltype(operationGetFromScope)>(globalObjectGPR, instructionGPR);
    jit.prepareCallOperation(vm);
    Call operation = jit.call(OperationPtrTag);
    Jump exceptionCheck = jit.emitNonPatchableExceptionCheck(vm);

    jit.store64(regT0, Address(GPRInfo::numberTagRegister, OBJECT_OFFSETOF(Metadata, m_profile)));
    jit.move(TrustedImm64(JSValue::NumberTag), GPRInfo::numberTagRegister);

#if CPU(X86_64)
    jit.pop(X86Registers::ebp);
#elif CPU(ARM64)
    jit.popPair(framePointerRegister, linkRegister);
#endif
    jit.ret();

    exceptionCheck.link(&jit);
    jit.move(TrustedImm64(JSValue::NumberTag), GPRInfo::numberTagRegister);
    Jump jumpToHandler = jit.jump();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    patchBuffer.link(operation, FunctionPtr<OperationPtrTag>(operationGetFromScope));
    auto handler = vm.getCTIStub(popThunkStackPreservesAndHandleExceptionGenerator);
    patchBuffer.link(jumpToHandler, CodeLocationLabel(handler.retaggedCode<NoPtrTag>()));
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_get_from_scope");
}
#endif // ENABLE(EXTRA_CTI_THUNKS)

void JIT::emitPutGlobalVariable(JSValue* operand, VirtualRegister value, WatchpointSet* set)
{
    emitGetVirtualRegister(value, regT0);
    emitNotifyWrite(set);
    storePtr(regT0, operand);
}
void JIT::emitPutGlobalVariableIndirect(JSValue** addressOfOperand, VirtualRegister value, WatchpointSet** indirectWatchpointSet)
{
    emitGetVirtualRegister(value, regT0);
    loadPtr(indirectWatchpointSet, regT1);
    emitNotifyWrite(regT1);
    loadPtr(addressOfOperand, regT1);
    storePtr(regT0, regT1);
}

void JIT::emitPutClosureVar(VirtualRegister scope, uintptr_t operand, VirtualRegister value, WatchpointSet* set)
{
    emitGetVirtualRegister(value, regT1);
    emitGetVirtualRegister(scope, regT0);
    emitNotifyWrite(set);
    storePtr(regT1, Address(regT0, JSLexicalEnvironment::offsetOfVariables() + operand * sizeof(Register)));
}

void JIT::emit_op_put_to_scope(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutToScope>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister scope = bytecode.m_scope;
    VirtualRegister value = bytecode.m_value;
    GetPutInfo getPutInfo = copiedGetPutInfo(bytecode);
    ResolveType resolveType = getPutInfo.resolveType();
    Structure** structureSlot = metadata.m_structure.slot();
    uintptr_t* operandSlot = reinterpret_cast<uintptr_t*>(&metadata.m_operand);

    auto emitCode = [&] (ResolveType resolveType, bool indirectLoadForOperand) {
        switch (resolveType) {
        case GlobalProperty:
        case GlobalPropertyWithVarInjectionChecks: {
            emitLoadWithStructureCheck(scope, structureSlot); // Structure check covers var injection since we don't cache structures for anything but the GlobalObject. Additionally, resolve_scope handles checking for the var injection.
            emitGetVirtualRegister(value, regT2);

            jitAssert(scopedLambda<Jump(void)>([&] () -> Jump {
                return branchPtr(Equal, regT0, TrustedImmPtr(m_codeBlock->globalObject()));
            }));

            loadPtr(Address(regT0, JSObject::butterflyOffset()), regT0);
            loadPtr(operandSlot, regT1);
            negPtr(regT1);
            storePtr(regT2, BaseIndex(regT0, regT1, TimesEight, (firstOutOfLineOffset - 2) * sizeof(EncodedJSValue)));
            emitWriteBarrier(m_codeBlock->globalObject(), value, ShouldFilterValue);
            break;
        }
        case GlobalVar:
        case GlobalVarWithVarInjectionChecks:
        case GlobalLexicalVar:
        case GlobalLexicalVarWithVarInjectionChecks: {
            JSScope* constantScope = JSScope::constantScopeForCodeBlock(resolveType, m_codeBlock);
            RELEASE_ASSERT(constantScope);
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            emitVarReadOnlyCheck(resolveType);
            if (!isInitialization(getPutInfo.initializationMode()) && (resolveType == GlobalLexicalVar || resolveType == GlobalLexicalVarWithVarInjectionChecks)) {
                // We need to do a TDZ check here because we can't always prove we need to emit TDZ checks statically.
                if (indirectLoadForOperand)
                    emitGetVarFromIndirectPointer(bitwise_cast<JSValue**>(operandSlot), regT0);
                else
                    emitGetVarFromPointer(bitwise_cast<JSValue*>(*operandSlot), regT0);
                addSlowCase(branchIfEmpty(regT0));
            }
            if (indirectLoadForOperand)
                emitPutGlobalVariableIndirect(bitwise_cast<JSValue**>(operandSlot), value, &metadata.m_watchpointSet);
            else
                emitPutGlobalVariable(bitwise_cast<JSValue*>(*operandSlot), value, metadata.m_watchpointSet);
            emitWriteBarrier(constantScope, value, ShouldFilterValue);
            break;
        }
        case ResolvedClosureVar:
        case ClosureVar:
        case ClosureVarWithVarInjectionChecks:
            emitVarInjectionCheck(needsVarInjectionChecks(resolveType));
            emitPutClosureVar(scope, *operandSlot, value, metadata.m_watchpointSet);
            emitWriteBarrier(scope, value, ShouldFilterValue);
            break;
        case ModuleVar:
        case Dynamic:
            addSlowCase(jump());
            break;
        case UnresolvedProperty:
        case UnresolvedPropertyWithVarInjectionChecks:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
    };

    switch (resolveType) {
    case GlobalProperty:
    case GlobalPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(&metadata.m_getPutInfo, regT0);
        and32(TrustedImm32(GetPutInfo::typeBits), regT0); // Load ResolveType into T0

        Jump isGlobalProperty = branch32(Equal, regT0, TrustedImm32(resolveType));
        Jump isGlobalLexicalVar = branch32(Equal, regT0, TrustedImm32(needsVarInjectionChecks(resolveType) ? GlobalLexicalVarWithVarInjectionChecks : GlobalLexicalVar));
        addSlowCase(jump()); // Dynamic, it can happen if we attempt to put a value to already-initialized const binding.

        isGlobalLexicalVar.link(this);
        emitCode(needsVarInjectionChecks(resolveType) ? GlobalLexicalVarWithVarInjectionChecks : GlobalLexicalVar, true);
        skipToEnd.append(jump());

        isGlobalProperty.link(this);
        emitCode(resolveType, false);
        skipToEnd.link(this);
        break;
    }
    case UnresolvedProperty:
    case UnresolvedPropertyWithVarInjectionChecks: {
        JumpList skipToEnd;
        load32(&metadata.m_getPutInfo, regT0);
        and32(TrustedImm32(GetPutInfo::typeBits), regT0); // Load ResolveType into T0

        Jump isGlobalProperty = branch32(Equal, regT0, TrustedImm32(GlobalProperty));
        Jump notGlobalPropertyWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalPropertyWithVarInjectionChecks));
        isGlobalProperty.link(this);
        emitCode(GlobalProperty, false);
        skipToEnd.append(jump());
        notGlobalPropertyWithVarInjections.link(this);

        Jump notGlobalLexicalVar = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVar));
        emitCode(GlobalLexicalVar, true);
        skipToEnd.append(jump());
        notGlobalLexicalVar.link(this);

        Jump notGlobalLexicalVarWithVarInjections = branch32(NotEqual, regT0, TrustedImm32(GlobalLexicalVarWithVarInjectionChecks));
        emitCode(GlobalLexicalVarWithVarInjectionChecks, true);
        skipToEnd.append(jump());
        notGlobalLexicalVarWithVarInjections.link(this);

        addSlowCase(jump());

        skipToEnd.link(this);
        break;
    }

    default:
        emitCode(resolveType, false);
        break;
    }
}

void JIT::emitSlow_op_put_to_scope(const Instruction* currentInstruction, Vector<SlowCaseEntry>::iterator& iter)
{
    linkAllSlowCases(iter);

    auto bytecode = currentInstruction->as<OpPutToScope>();
    ResolveType resolveType = copiedGetPutInfo(bytecode).resolveType();
    if (resolveType == ModuleVar) {
        JITSlowPathCall slowPathCall(this, currentInstruction, slow_path_throw_strict_mode_readonly_property_write_error);
        slowPathCall.call();
    } else {
#if !ENABLE(EXTRA_CTI_THUNKS)
        callOperation(operationPutToScope, TrustedImmPtr(m_codeBlock->globalObject()), currentInstruction);
#else
        VM& vm = this->vm();
        uint32_t bytecodeOffset = m_bytecodeIndex.offset();
        ASSERT(BytecodeIndex(bytecodeOffset) == m_bytecodeIndex);
        ASSERT(m_codeBlock->instructionAt(m_bytecodeIndex) == currentInstruction);

        constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
        move(TrustedImm32(bytecodeOffset), bytecodeOffsetGPR);

        emitNakedNearCall(vm.getCTIStub(slow_op_put_to_scopeGenerator).retaggedCode<NoPtrTag>());
#endif
    }
}

#if ENABLE(EXTRA_CTI_THUNKS)
MacroAssemblerCodeRef<JITThunkPtrTag> JIT::slow_op_put_to_scopeGenerator(VM& vm)
{
    // The thunk generated by this function can only work with the LLInt / Baseline JIT because
    // it makes assumptions about the right globalObject being available from CallFrame::codeBlock().
    // DFG/FTL may inline functions belonging to other globalObjects, which may not match
    // CallFrame::codeBlock().
    JIT jit(vm);

#if CPU(X86_64)
    jit.push(X86Registers::ebp);
#elif CPU(ARM64)
    jit.tagReturnAddress();
    jit.pushPair(framePointerRegister, linkRegister);
#endif

    constexpr GPRReg bytecodeOffsetGPR = argumentGPR2;
    jit.store32(bytecodeOffsetGPR, tagFor(CallFrameSlot::argumentCountIncludingThis));

    constexpr GPRReg codeBlockGPR = argumentGPR3;
    constexpr GPRReg globalObjectGPR = argumentGPR0;
    constexpr GPRReg instructionGPR = argumentGPR1;

    jit.loadPtr(addressFor(CallFrameSlot::codeBlock), codeBlockGPR);
    jit.loadPtr(Address(codeBlockGPR, CodeBlock::offsetOfGlobalObject()), globalObjectGPR);
    jit.loadPtr(Address(codeBlockGPR, CodeBlock::offsetOfInstructionsRawPointer()), instructionGPR);
    jit.addPtr(bytecodeOffsetGPR, instructionGPR);

    jit.prepareCallOperation(vm);
    CCallHelpers::Call operation = jit.call(OperationPtrTag);
    CCallHelpers::Jump exceptionCheck = jit.emitNonPatchableExceptionCheck(vm);

#if CPU(X86_64)
    jit.pop(X86Registers::ebp);
#elif CPU(ARM64)
    jit.popPair(framePointerRegister, linkRegister);
#endif
    jit.ret();

    LinkBuffer patchBuffer(jit, GLOBAL_THUNK_ID, LinkBuffer::Profile::ExtraCTIThunk);
    patchBuffer.link(operation, FunctionPtr<OperationPtrTag>(operationPutToScope));
    auto handler = vm.getCTIStub(popThunkStackPreservesAndHandleExceptionGenerator);
    patchBuffer.link(exceptionCheck, CodeLocationLabel(handler.retaggedCode<NoPtrTag>()));
    return FINALIZE_CODE(patchBuffer, JITThunkPtrTag, "Baseline: slow_op_put_to_scope");
}
#endif

void JIT::emit_op_get_from_arguments(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetFromArguments>();
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister arguments = bytecode.m_arguments;
    int index = bytecode.m_index;
    
    emitGetVirtualRegister(arguments, regT0);
    load64(Address(regT0, DirectArguments::storageOffset() + index * sizeof(WriteBarrier<Unknown>)), regT0);
    emitValueProfilingSite(bytecode.metadata(m_codeBlock), regT0);
    emitPutVirtualRegister(dst);
}

void JIT::emit_op_put_to_arguments(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutToArguments>();
    VirtualRegister arguments = bytecode.m_arguments;
    int index = bytecode.m_index;
    VirtualRegister value = bytecode.m_value;
    
    emitGetVirtualRegister(arguments, regT0);
    emitGetVirtualRegister(value, regT1);
    store64(regT1, Address(regT0, DirectArguments::storageOffset() + index * sizeof(WriteBarrier<Unknown>)));

    emitWriteBarrier(arguments, value, ShouldFilterValue);
}

void JIT::emitWriteBarrier(VirtualRegister owner, VirtualRegister value, WriteBarrierMode mode)
{
    // value may be invalid VirtualRegister if mode is UnconditionalWriteBarrier or ShouldFilterBase.
    Jump valueNotCell;
    if (mode == ShouldFilterValue || mode == ShouldFilterBaseAndValue) {
        emitGetVirtualRegister(value, regT0);
        valueNotCell = branchIfNotCell(regT0);
    }
    
    emitGetVirtualRegister(owner, regT0);
    Jump ownerNotCell;
    if (mode == ShouldFilterBaseAndValue || mode == ShouldFilterBase)
        ownerNotCell = branchIfNotCell(regT0);

    Jump ownerIsRememberedOrInEden = barrierBranch(vm(), regT0, regT1);
    callOperationNoExceptionCheck(operationWriteBarrierSlowPath, &vm(), regT0);
    ownerIsRememberedOrInEden.link(this);

    if (mode == ShouldFilterBaseAndValue || mode == ShouldFilterBase)
        ownerNotCell.link(this);
    if (mode == ShouldFilterValue || mode == ShouldFilterBaseAndValue) 
        valueNotCell.link(this);
}

void JIT::emitWriteBarrier(JSCell* owner, VirtualRegister value, WriteBarrierMode mode)
{
    emitGetVirtualRegister(value, regT0);
    Jump valueNotCell;
    if (mode == ShouldFilterValue)
        valueNotCell = branchIfNotCell(regT0);

    emitWriteBarrier(owner);

    if (mode == ShouldFilterValue) 
        valueNotCell.link(this);
}

void JIT::emit_op_get_internal_field(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpGetInternalField>();
    auto& metadata = bytecode.metadata(m_codeBlock);
    VirtualRegister dst = bytecode.m_dst;
    VirtualRegister base = bytecode.m_base;
    unsigned index = bytecode.m_index;

    emitGetVirtualRegister(base, regT1);
    loadPtr(Address(regT1, JSInternalFieldObjectImpl<>::offsetOfInternalField(index)), regT0);

    emitValueProfilingSite(metadata, regT0);
    emitPutVirtualRegister(dst);
}

void JIT::emit_op_put_internal_field(const Instruction* currentInstruction)
{
    auto bytecode = currentInstruction->as<OpPutInternalField>();
    VirtualRegister base = bytecode.m_base;
    VirtualRegister value = bytecode.m_value;
    unsigned index = bytecode.m_index;

    emitGetVirtualRegister(base, regT0);
    emitGetVirtualRegister(value, regT1);
    storePtr(regT1, Address(regT0, JSInternalFieldObjectImpl<>::offsetOfInternalField(index)));
    emitWriteBarrier(base, value, ShouldFilterValue);
}

template void JIT::emit_op_put_by_val<OpPutByVal>(const Instruction*);

#else // USE(JSVALUE64)

void JIT::emitWriteBarrier(VirtualRegister owner, VirtualRegister value, WriteBarrierMode mode)
{
    // value may be invalid VirtualRegister if mode is UnconditionalWriteBarrier or ShouldFilterBase.
    Jump valueNotCell;
    if (mode == ShouldFilterValue || mode == ShouldFilterBaseAndValue) {
        emitLoadTag(value, regT0);
        valueNotCell = branchIfNotCell(regT0);
    }
    
    emitLoad(owner, regT0, regT1);
    Jump ownerNotCell;
    if (mode == ShouldFilterBase || mode == ShouldFilterBaseAndValue)
        ownerNotCell = branchIfNotCell(regT0);

    Jump ownerIsRememberedOrInEden = barrierBranch(vm(), regT1, regT2);
    callOperationNoExceptionCheck(operationWriteBarrierSlowPath, &vm(), regT1);
    ownerIsRememberedOrInEden.link(this);

    if (mode == ShouldFilterBase || mode == ShouldFilterBaseAndValue)
        ownerNotCell.link(this);
    if (mode == ShouldFilterValue || mode == ShouldFilterBaseAndValue) 
        valueNotCell.link(this);
}

void JIT::emitWriteBarrier(JSCell* owner, VirtualRegister value, WriteBarrierMode mode)
{
    Jump valueNotCell;
    if (mode == ShouldFilterValue) {
        emitLoadTag(value, regT0);
        valueNotCell = branchIfNotCell(regT0);
    }

    emitWriteBarrier(owner);

    if (mode == ShouldFilterValue) 
        valueNotCell.link(this);
}

template <typename Op>
JITPutByIdGenerator JIT::emitPutByValWithCachedId(Op bytecode, PutKind putKind, CacheableIdentifier propertyName, JumpList& doneCases, JumpList& slowCases)
{
    // base: tag(regT1), payload(regT0)
    // property: tag(regT3), payload(regT2)

    VirtualRegister base = bytecode.m_base;
    VirtualRegister value = bytecode.m_value;

    slowCases.append(branchIfNotCell(regT3));
    emitByValIdentifierCheck(regT2, regT2, propertyName, slowCases);

    // Write barrier breaks the registers. So after issuing the write barrier,
    // reload the registers.
    //
    // IC can write new Structure without write-barrier if a base is cell.
    // We are emitting write-barrier before writing here but this is OK since 32bit JSC does not have concurrent GC.
    // FIXME: Use UnconditionalWriteBarrier in Baseline effectively to reduce code size.
    // https://bugs.webkit.org/show_bug.cgi?id=209395
    emitWriteBarrier(base, ShouldFilterBase);
    emitLoadPayload(base, regT0);
    emitLoad(value, regT3, regT2);

    JITPutByIdGenerator gen(
        m_codeBlock, JITType::BaselineJIT, CodeOrigin(m_bytecodeIndex), CallSiteIndex(m_bytecodeIndex), RegisterSet::stubUnavailableRegisters(), propertyName,
        JSValueRegs::payloadOnly(regT0), JSValueRegs(regT3, regT2), InvalidGPRReg, regT1, ecmaMode(bytecode), putKind);
    gen.generateFastPath(*this);
    doneCases.append(jump());

    Label coldPathBegin = label();
    gen.slowPathJump().link(this);

    // JITPutByIdGenerator only preserve the value and the base's payload, we have to reload the tag.
    emitLoadTag(base, regT1);

    Call call;
    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        gen.stubInfo()->m_slowOperation = gen.slowPathFunction();
        move(TrustedImmPtr(gen.stubInfo()), GPRInfo::nonArgGPR0);
        callOperation<decltype(gen.slowPathFunction())>(Address(GPRInfo::nonArgGPR0, StructureStubInfo::offsetOfSlowOperation()), m_codeBlock->globalObject(), GPRInfo::nonArgGPR0, JSValueRegs(regT3, regT2), JSValueRegs(regT1, regT0), propertyName.rawBits());
    } else
        call = callOperation(gen.slowPathFunction(), m_codeBlock->globalObject(), gen.stubInfo(), JSValueRegs(regT3, regT2), JSValueRegs(regT1, regT0), propertyName.rawBits());
    gen.reportSlowPathCall(coldPathBegin, call);
    doneCases.append(jump());

    return gen;
}

#endif // USE(JSVALUE64)

JITPutByIdGenerator JIT::emitPutPrivateNameWithCachedId(OpPutPrivateName bytecode, CacheableIdentifier propertyName, JumpList& doneCases, JumpList& slowCases)
{
    auto putKind = bytecode.m_putKind.isDefine() ? PutKind::DirectPrivateFieldDefine : PutKind::DirectPrivateFieldSet;
    return emitPutByValWithCachedId(bytecode, putKind, propertyName, doneCases, slowCases);
}

void JIT::emitWriteBarrier(VirtualRegister owner, WriteBarrierMode mode)
{
    ASSERT(mode == UnconditionalWriteBarrier || mode == ShouldFilterBase);
    emitWriteBarrier(owner, VirtualRegister(), mode);
}

void JIT::emitWriteBarrier(JSCell* owner)
{
    Jump ownerIsRememberedOrInEden = barrierBranch(vm(), owner, regT0);
    callOperationNoExceptionCheck(operationWriteBarrierSlowPath, &vm(), owner);
    ownerIsRememberedOrInEden.link(this);
}

void JIT::emitByValIdentifierCheck(RegisterID cell, RegisterID scratch, CacheableIdentifier propertyName, JumpList& slowCases)
{
    if (propertyName.isSymbolCell())
        slowCases.append(branchPtr(NotEqual, cell, TrustedImmPtr(propertyName.cell())));
    else {
        slowCases.append(branchIfNotString(cell));
        loadPtr(Address(cell, JSString::offsetOfValue()), scratch);
        slowCases.append(branchPtr(NotEqual, scratch, TrustedImmPtr(propertyName.uid())));
    }
}

template<typename Op>
void JIT::privateCompilePutByVal(const ConcurrentJSLocker&, ByValInfo* byValInfo, ReturnAddressPtr returnAddress, JITArrayMode arrayMode)
{
    const Instruction* currentInstruction = m_codeBlock->instructions().at(byValInfo->bytecodeIndex).ptr();
    auto bytecode = currentInstruction->as<Op>();
    
    PatchableJump badType;
    JumpList slowCases;

    bool needsLinkForWriteBarrier = false;

    switch (arrayMode) {
    case JITInt32:
        slowCases = emitInt32PutByVal(bytecode, badType, nullptr);
        break;
    case JITDouble:
        slowCases = emitDoublePutByVal(bytecode, badType, nullptr);
        break;
    case JITContiguous:
        slowCases = emitContiguousPutByVal(bytecode, badType, nullptr);
        needsLinkForWriteBarrier = true;
        break;
    case JITArrayStorage:
        slowCases = emitArrayStoragePutByVal(bytecode, badType, nullptr);
        needsLinkForWriteBarrier = true;
        break;
    default:
        TypedArrayType type = typedArrayTypeForJITArrayMode(arrayMode);
        if (isInt(type))
            slowCases = emitIntTypedArrayPutByVal(bytecode, badType, nullptr, type);
        else {
            // FIXME: Optimize BigInt64Array / BigUint64Array in IC
            // Currently, BigInt64Array / BigUint64Array never comes here.
            // https://bugs.webkit.org/show_bug.cgi?id=221183
            ASSERT(isFloat(type));
            slowCases = emitFloatTypedArrayPutByVal(bytecode, badType, nullptr, type);
        }
        break;
    }
    
    Jump done = jump();

    LinkBuffer patchBuffer(*this, m_codeBlock, LinkBuffer::Profile::InlineCache);
    patchBuffer.link(badType, byValInfo->slowPathTarget);
    patchBuffer.link(slowCases, byValInfo->slowPathTarget);
    patchBuffer.link(done, byValInfo->doneTarget);
    if (needsLinkForWriteBarrier) {
        ASSERT(removeCodePtrTag(m_farCalls.last().callee.executableAddress()) == removeCodePtrTag(operationWriteBarrierSlowPath));
        patchBuffer.link(m_farCalls.last().from, m_farCalls.last().callee);
    }
    
    bool isDirect = currentInstruction->opcodeID() == op_put_by_val_direct;
    if (!isDirect) {
        byValInfo->stubRoutine = FINALIZE_CODE_FOR_STUB(
            m_codeBlock, patchBuffer, JITStubRoutinePtrTag,
            "Baseline put_by_val stub for %s, return point %p", toCString(*m_codeBlock).data(), returnAddress.untaggedValue());
        
    } else {
        byValInfo->stubRoutine = FINALIZE_CODE_FOR_STUB(
            m_codeBlock, patchBuffer, JITStubRoutinePtrTag,
            "Baseline put_by_val_direct stub for %s, return point %p", toCString(*m_codeBlock).data(), returnAddress.untaggedValue());
    }

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        byValInfo->m_badTypeJumpTarget = CodeLocationLabel<JITStubRoutinePtrTag>(byValInfo->stubRoutine->code().code());
        byValInfo->m_slowOperation = isDirect ? operationDirectPutByValGeneric : operationPutByValGeneric;
    } else {
        MacroAssembler::repatchJump(byValInfo->m_badTypeJump, CodeLocationLabel<JITStubRoutinePtrTag>(byValInfo->stubRoutine->code().code()));
        MacroAssembler::repatchCall(CodeLocationCall<ReturnAddressPtrTag>(MacroAssemblerCodePtr<ReturnAddressPtrTag>(returnAddress)), FunctionPtr<OperationPtrTag>(isDirect ? operationDirectPutByValGeneric : operationPutByValGeneric));
    }
}
// This function is only consumed from another translation unit (JITOperations.cpp),
// so we list off the two expected specializations in advance.
template void JIT::privateCompilePutByVal<OpPutByVal>(const ConcurrentJSLocker&, ByValInfo*, ReturnAddressPtr, JITArrayMode);
template void JIT::privateCompilePutByVal<OpPutByValDirect>(const ConcurrentJSLocker&, ByValInfo*, ReturnAddressPtr, JITArrayMode);

void JIT::privateCompilePutPrivateNameWithCachedId(ByValInfo* byValInfo, ReturnAddressPtr returnAddress, CacheableIdentifier propertyName)
{
    const Instruction* currentInstruction = m_codeBlock->instructions().at(byValInfo->bytecodeIndex).ptr();
    auto bytecode = currentInstruction->as<OpPutPrivateName>();

    JumpList doneCases;
    JumpList slowCases;

    JITPutByIdGenerator gen = emitPutPrivateNameWithCachedId(bytecode, propertyName, doneCases, slowCases);

    ConcurrentJSLocker locker(m_codeBlock->m_lock);
    LinkBuffer patchBuffer(*this, m_codeBlock, LinkBuffer::Profile::InlineCache);
    patchBuffer.link(slowCases, byValInfo->slowPathTarget);
    patchBuffer.link(doneCases, byValInfo->doneTarget);
    if (!m_exceptionChecks.empty())
        patchBuffer.link(m_exceptionChecks, byValInfo->exceptionHandler);

    for (const auto& callSite : m_nearCalls) {
        if (callSite.callee)
            patchBuffer.link(callSite.from, callSite.callee);
    }
    for (const auto& callSite : m_farCalls) {
        if (callSite.callee)
            patchBuffer.link(callSite.from, callSite.callee);
    }
    gen.finalize(patchBuffer, patchBuffer);

    byValInfo->stubRoutine = FINALIZE_CODE_FOR_STUB(
        m_codeBlock, patchBuffer, JITStubRoutinePtrTag,
        "Baseline put_private_name with cached property name '%s' stub for %s, return point %p", propertyName.uid()->utf8().data(), toCString(*m_codeBlock).data(), returnAddress.untaggedValue());
    byValInfo->stubInfo = gen.stubInfo();

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        byValInfo->m_notIndexJumpTarget = CodeLocationLabel<JITStubRoutinePtrTag>(byValInfo->stubRoutine->code().code());
        byValInfo->m_slowOperation = operationPutPrivateNameGeneric;
    } else {
        MacroAssembler::repatchJump(byValInfo->m_notIndexJump, CodeLocationLabel<JITStubRoutinePtrTag>(byValInfo->stubRoutine->code().code()));
        MacroAssembler::repatchCall(CodeLocationCall<ReturnAddressPtrTag>(MacroAssemblerCodePtr<ReturnAddressPtrTag>(returnAddress)), FunctionPtr<OperationPtrTag>(operationPutPrivateNameGeneric));
    }
}

template<typename Op>
void JIT::privateCompilePutByValWithCachedId(ByValInfo* byValInfo, ReturnAddressPtr returnAddress, PutKind putKind, CacheableIdentifier propertyName)
{
    ASSERT((putKind == PutKind::Direct && Op::opcodeID == op_put_by_val_direct) || (putKind == PutKind::NotDirect && Op::opcodeID == op_put_by_val));
    const Instruction* currentInstruction = m_codeBlock->instructions().at(byValInfo->bytecodeIndex).ptr();
    auto bytecode = currentInstruction->as<Op>();

    JumpList doneCases;
    JumpList slowCases;

    JITPutByIdGenerator gen = emitPutByValWithCachedId(bytecode, putKind, propertyName, doneCases, slowCases);

    ConcurrentJSLocker locker(m_codeBlock->m_lock);
    LinkBuffer patchBuffer(*this, m_codeBlock, LinkBuffer::Profile::InlineCache);
    patchBuffer.link(slowCases, byValInfo->slowPathTarget);
    patchBuffer.link(doneCases, byValInfo->doneTarget);
    if (!m_exceptionChecks.empty())
        patchBuffer.link(m_exceptionChecks, byValInfo->exceptionHandler);

    for (const auto& callSite : m_nearCalls) {
        if (callSite.callee)
            patchBuffer.link(callSite.from, callSite.callee);
    }
    for (const auto& callSite : m_farCalls) {
        if (callSite.callee)
            patchBuffer.link(callSite.from, callSite.callee);
    }
    gen.finalize(patchBuffer, patchBuffer);

    byValInfo->stubRoutine = FINALIZE_CODE_FOR_STUB(
        m_codeBlock, patchBuffer, JITStubRoutinePtrTag,
        "Baseline put_by_val%s with cached property name '%s' stub for %s, return point %p", (putKind == PutKind::Direct) ? "_direct" : "", propertyName.uid()->utf8().data(), toCString(*m_codeBlock).data(), returnAddress.untaggedValue());
    byValInfo->stubInfo = gen.stubInfo();

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        byValInfo->m_notIndexJumpTarget = CodeLocationLabel<JITStubRoutinePtrTag>(byValInfo->stubRoutine->code().code());
        byValInfo->m_slowOperation = putKind == PutKind::Direct ? operationDirectPutByValGeneric : operationPutByValGeneric;
    } else {
        MacroAssembler::repatchJump(byValInfo->m_notIndexJump, CodeLocationLabel<JITStubRoutinePtrTag>(byValInfo->stubRoutine->code().code()));
        MacroAssembler::repatchCall(CodeLocationCall<ReturnAddressPtrTag>(MacroAssemblerCodePtr<ReturnAddressPtrTag>(returnAddress)), FunctionPtr<OperationPtrTag>(putKind == PutKind::Direct ? operationDirectPutByValGeneric : operationPutByValGeneric));
    }
}
// This function is only consumed from another translation unit (JITOperations.cpp),
// so we list off the two expected specializations in advance.
template void JIT::privateCompilePutByValWithCachedId<OpPutByVal>(ByValInfo*, ReturnAddressPtr, PutKind, CacheableIdentifier);
template void JIT::privateCompilePutByValWithCachedId<OpPutByValDirect>(ByValInfo*, ReturnAddressPtr, PutKind, CacheableIdentifier);

JIT::JumpList JIT::emitDoubleLoad(const Instruction*, PatchableJump& badType, ByValInfo* byValInfo)
{
#if USE(JSVALUE64)
    RegisterID base = regT0;
    RegisterID property = regT1;
    RegisterID indexing = regT2;
    RegisterID scratch = regT3;
#else
    RegisterID base = regT0;
    RegisterID property = regT2;
    RegisterID indexing = regT1;
    RegisterID scratch = regT3;
#endif

    JumpList slowCases;

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(Equal, indexing, TrustedImm32(DoubleShape));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(NotEqual, indexing, TrustedImm32(DoubleShape));
    } else
        badType = patchableBranch32(NotEqual, indexing, TrustedImm32(DoubleShape));

    loadPtr(Address(base, JSObject::butterflyOffset()), scratch);
    slowCases.append(branch32(AboveOrEqual, property, Address(scratch, Butterfly::offsetOfPublicLength())));
    loadDouble(BaseIndex(scratch, property, TimesEight), fpRegT0);
    slowCases.append(branchIfNaN(fpRegT0));

    return slowCases;
}

JIT::JumpList JIT::emitContiguousLoad(const Instruction*, PatchableJump& badType, ByValInfo* byValInfo, IndexingType expectedShape)
{
#if USE(JSVALUE64)
    RegisterID base = regT0;
    RegisterID property = regT1;
    RegisterID indexing = regT2;
    JSValueRegs result = JSValueRegs(regT0);
    RegisterID scratch = regT3;
#else
    RegisterID base = regT0;
    RegisterID property = regT2;
    RegisterID indexing = regT1;
    JSValueRegs result = JSValueRegs(regT1, regT0);
    RegisterID scratch = regT3;
#endif

    JumpList slowCases;

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(Equal, indexing, TrustedImm32(expectedShape));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(NotEqual, indexing, TrustedImm32(expectedShape));
    } else
        badType = patchableBranch32(NotEqual, indexing, TrustedImm32(expectedShape));

    loadPtr(Address(base, JSObject::butterflyOffset()), scratch);
    slowCases.append(branch32(AboveOrEqual, property, Address(scratch, Butterfly::offsetOfPublicLength())));
    loadValue(BaseIndex(scratch, property, TimesEight), result);
    slowCases.append(branchIfEmpty(result));

    return slowCases;
}

JIT::JumpList JIT::emitArrayStorageLoad(const Instruction*, PatchableJump& badType, ByValInfo* byValInfo)
{
#if USE(JSVALUE64)
    RegisterID base = regT0;
    RegisterID property = regT1;
    RegisterID indexing = regT2;
    JSValueRegs result = JSValueRegs(regT0);
    RegisterID scratch = regT3;
#else
    RegisterID base = regT0;
    RegisterID property = regT2;
    RegisterID indexing = regT1;
    JSValueRegs result = JSValueRegs(regT1, regT0);
    RegisterID scratch = regT3;
#endif

    JumpList slowCases;

    add32(TrustedImm32(-ArrayStorageShape), indexing, scratch);

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(BelowOrEqual, scratch, TrustedImm32(SlowPutArrayStorageShape - ArrayStorageShape));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(Above, scratch, TrustedImm32(SlowPutArrayStorageShape - ArrayStorageShape));
    } else
        badType = patchableBranch32(Above, scratch, TrustedImm32(SlowPutArrayStorageShape - ArrayStorageShape));

    loadPtr(Address(base, JSObject::butterflyOffset()), scratch);
    slowCases.append(branch32(AboveOrEqual, property, Address(scratch, ArrayStorage::vectorLengthOffset())));

    loadValue(BaseIndex(scratch, property, TimesEight, ArrayStorage::vectorOffset()), result);
    slowCases.append(branchIfEmpty(result));

    return slowCases;
}

template<typename Op>
JIT::JumpList JIT::emitIntTypedArrayPutByVal(Op bytecode, PatchableJump& badType, ByValInfo* byValInfo, TypedArrayType type)
{
    auto& metadata = bytecode.metadata(m_codeBlock);
    ArrayProfile* profile = &metadata.m_arrayProfile;
    ASSERT(isInt(type));
    
    VirtualRegister value = bytecode.m_value;

#if USE(JSVALUE64)
    RegisterID base = regT0;
    RegisterID property = regT1;
    RegisterID earlyScratch = regT3;
    RegisterID lateScratch = regT2;
    RegisterID lateScratch2 = regT4;
#else
    RegisterID base = regT0;
    RegisterID property = regT2;
    RegisterID earlyScratch = regT3;
    RegisterID lateScratch = regT1;
    RegisterID lateScratch2 = regT4;
#endif
    
    JumpList slowCases;
    
    load8(Address(base, JSCell::typeInfoTypeOffset()), earlyScratch);

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(Equal, earlyScratch, TrustedImm32(typeForTypedArrayType(type)));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(NotEqual, earlyScratch, TrustedImm32(typeForTypedArrayType(type)));
    } else
        badType = patchableBranch32(NotEqual, earlyScratch, TrustedImm32(typeForTypedArrayType(type)));

    load32(Address(base, JSArrayBufferView::offsetOfLength()), lateScratch2);
    Jump inBounds = branch32(Below, property, lateScratch2);
    emitArrayProfileOutOfBoundsSpecialCase(profile);
    slowCases.append(jump());
    inBounds.link(this);
    
#if USE(JSVALUE64)
    emitGetVirtualRegister(value, earlyScratch);
    slowCases.append(branchIfNotInt32(earlyScratch));
#else
    emitLoad(value, lateScratch, earlyScratch);
    slowCases.append(branchIfNotInt32(lateScratch));
#endif
    
    // We would be loading this into base as in get_by_val, except that the slow
    // path expects the base to be unclobbered.
    loadPtr(Address(base, JSArrayBufferView::offsetOfVector()), lateScratch);
    cageConditionallyAndUntag(Gigacage::Primitive, lateScratch, lateScratch2, lateScratch2, false);
    
    if (isClamped(type)) {
        ASSERT(elementSize(type) == 1);
        ASSERT(!JSC::isSigned(type));
        Jump inBounds = branch32(BelowOrEqual, earlyScratch, TrustedImm32(0xff));
        Jump tooBig = branch32(GreaterThan, earlyScratch, TrustedImm32(0xff));
        xor32(earlyScratch, earlyScratch);
        Jump clamped = jump();
        tooBig.link(this);
        move(TrustedImm32(0xff), earlyScratch);
        clamped.link(this);
        inBounds.link(this);
    }
    
    switch (elementSize(type)) {
    case 1:
        store8(earlyScratch, BaseIndex(lateScratch, property, TimesOne));
        break;
    case 2:
        store16(earlyScratch, BaseIndex(lateScratch, property, TimesTwo));
        break;
    case 4:
        store32(earlyScratch, BaseIndex(lateScratch, property, TimesFour));
        break;
    default:
        CRASH();
    }
    
    return slowCases;
}

template<typename Op>
JIT::JumpList JIT::emitFloatTypedArrayPutByVal(Op bytecode, PatchableJump& badType, ByValInfo* byValInfo, TypedArrayType type)
{
    auto& metadata = bytecode.metadata(m_codeBlock);
    ArrayProfile* profile = &metadata.m_arrayProfile;
    ASSERT(isFloat(type));
    
    VirtualRegister value = bytecode.m_value;

#if USE(JSVALUE64)
    RegisterID base = regT0;
    RegisterID property = regT1;
    RegisterID earlyScratch = regT3;
    RegisterID lateScratch = regT2;
    RegisterID lateScratch2 = regT4;
#else
    RegisterID base = regT0;
    RegisterID property = regT2;
    RegisterID earlyScratch = regT3;
    RegisterID lateScratch = regT1;
    RegisterID lateScratch2 = regT4;
#endif
    
    JumpList slowCases;
    
    load8(Address(base, JSCell::typeInfoTypeOffset()), earlyScratch);

    if (JITCode::useDataIC(JITType::BaselineJIT)) {
        if (byValInfo) {
            auto isCorrectType = branch32(Equal, earlyScratch, TrustedImm32(typeForTypedArrayType(type)));
            farJump(AbsoluteAddress(&byValInfo->m_badTypeJumpTarget), JITStubRoutinePtrTag);
            isCorrectType.link(this);
        } else
            badType = patchableBranch32(NotEqual, earlyScratch, TrustedImm32(typeForTypedArrayType(type)));
    } else
        badType = patchableBranch32(NotEqual, earlyScratch, TrustedImm32(typeForTypedArrayType(type)));

    load32(Address(base, JSArrayBufferView::offsetOfLength()), lateScratch2);
    Jump inBounds = branch32(Below, property, lateScratch2);
    emitArrayProfileOutOfBoundsSpecialCase(profile);
    slowCases.append(jump());
    inBounds.link(this);
    
#if USE(JSVALUE64)
    emitGetVirtualRegister(value, earlyScratch);
    Jump doubleCase = branchIfNotInt32(earlyScratch);
    convertInt32ToDouble(earlyScratch, fpRegT0);
    Jump ready = jump();
    doubleCase.link(this);
    slowCases.append(branchIfNotNumber(earlyScratch));
    add64(numberTagRegister, earlyScratch);
    move64ToDouble(earlyScratch, fpRegT0);
    ready.link(this);
#else
    emitLoad(value, lateScratch, earlyScratch);
    Jump doubleCase = branchIfNotInt32(lateScratch);
    convertInt32ToDouble(earlyScratch, fpRegT0);
    Jump ready = jump();
    doubleCase.link(this);
    slowCases.append(branch32(Above, lateScratch, TrustedImm32(JSValue::LowestTag)));
    moveIntsToDouble(earlyScratch, lateScratch, fpRegT0, fpRegT1);
    ready.link(this);
#endif
    
    // We would be loading this into base as in get_by_val, except that the slow
    // path expects the base to be unclobbered.
    loadPtr(Address(base, JSArrayBufferView::offsetOfVector()), lateScratch);
    cageConditionallyAndUntag(Gigacage::Primitive, lateScratch, lateScratch2, lateScratch2, false);
    
    switch (elementSize(type)) {
    case 4:
        convertDoubleToFloat(fpRegT0, fpRegT0);
        storeFloat(fpRegT0, BaseIndex(lateScratch, property, TimesFour));
        break;
    case 8:
        storeDouble(fpRegT0, BaseIndex(lateScratch, property, TimesEight));
        break;
    default:
        CRASH();
    }
    
    return slowCases;
}

} // namespace JSC

#endif // ENABLE(JIT)
