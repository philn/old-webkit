/*
 * Copyright (C) 2017-2021 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(JIT)

#include "CacheableIdentifier.h"
#include "GCAwareJITStubRoutine.h"
#include "JITStubRoutine.h"
#include "JSFunctionInlines.h"
#include "ObjectPropertyConditionSet.h"
#include "PolyProtoAccessChain.h"
#include <wtf/CommaPrinter.h>
#include <wtf/VectorHash.h>

namespace JSC {

struct AccessGenerationState;

DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(AccessCase);

// An AccessCase describes one of the cases of a PolymorphicAccess. A PolymorphicAccess represents a
// planned (to generate in future) or generated stub for some inline cache. That stub contains fast
// path code for some finite number of fast cases, each described by an AccessCase object.
//
// An AccessCase object has a lifecycle that proceeds through several states. Note that the states
// of AccessCase have a lot to do with the global effect epoch (we'll say epoch for short). This is
// a simple way of reasoning about the state of the system outside this AccessCase. Any observable
// effect - like storing to a property, changing an object's structure, etc. - increments the epoch.
// The states are:
//
// Primordial:   This is an AccessCase that was just allocated. It does not correspond to any actual
//               code and it is not owned by any PolymorphicAccess. In this state, the AccessCase
//               assumes that it is in the same epoch as when it was created. This is important
//               because it may make claims about itself ("I represent a valid case so long as you
//               register a watchpoint on this set") that could be contradicted by some outside
//               effects (like firing and deleting the watchpoint set in question). This is also the
//               state that an AccessCase is in when it is cloned (AccessCase::clone()).
//
// Committed:    This happens as soon as some PolymorphicAccess takes ownership of this AccessCase.
//               In this state, the AccessCase no longer assumes anything about the epoch. To
//               accomplish this, PolymorphicAccess calls AccessCase::commit(). This must be done
//               during the same epoch when the AccessCase was created, either by the client or by
//               clone(). When created by the client, committing during the same epoch works because
//               we can be sure that whatever watchpoint sets they spoke of are still valid. When
//               created by clone(), we can be sure that the set is still valid because the original
//               of the clone still has watchpoints on it.
//
// Generated:    This is the state when the PolymorphicAccess generates code for this case by
//               calling AccessCase::generate() or AccessCase::generateWithGuard(). At this point
//               the case object will have some extra stuff in it, like possibly the CallLinkInfo
//               object associated with the inline cache.
//               FIXME: Moving into the Generated state should not mutate the AccessCase object or
//               put more stuff into it. If we fix this, then we can get rid of AccessCase::clone().
//               https://bugs.webkit.org/show_bug.cgi?id=156456
//
// An AccessCase may be destroyed while in any of these states.
//
// We will sometimes buffer committed AccessCases in the PolymorphicAccess object before generating
// code. This allows us to only regenerate once we've accumulated (hopefully) more than one new
// AccessCase.
class AccessCase : public ThreadSafeRefCounted<AccessCase> {
    WTF_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(AccessCase);
public:
    enum AccessType : uint8_t {
        Load,
        Transition,
        Delete,
        DeleteNonConfigurable,
        DeleteMiss,
        Replace,
        Miss,
        GetGetter,
        Getter,
        Setter,
        CustomValueGetter,
        CustomAccessorGetter,
        CustomValueSetter,
        CustomAccessorSetter,
        IntrinsicGetter,
        InHit,
        InMiss,
        ArrayLength,
        StringLength,
        DirectArgumentsLength,
        ScopedArgumentsLength,
        ModuleNamespaceLoad,
        InstanceOfHit,
        InstanceOfMiss,
        InstanceOfGeneric,
        CheckPrivateBrand,
        SetPrivateBrand,
        IndexedInt32Load,
        IndexedDoubleLoad,
        IndexedContiguousLoad,
        IndexedArrayStorageLoad,
        IndexedScopedArgumentsLoad,
        IndexedDirectArgumentsLoad,
        IndexedTypedArrayInt8Load,
        IndexedTypedArrayUint8Load,
        IndexedTypedArrayUint8ClampedLoad,
        IndexedTypedArrayInt16Load,
        IndexedTypedArrayUint16Load,
        IndexedTypedArrayInt32Load,
        IndexedTypedArrayUint32Load,
        IndexedTypedArrayFloat32Load,
        IndexedTypedArrayFloat64Load,
        IndexedStringLoad
    };

    enum State : uint8_t {
        Primordial,
        Committed,
        Generated
    };

    template<typename T>
    T& as() { return *static_cast<T*>(this); }

    template<typename T>
    const T& as() const { return *static_cast<const T*>(this); }


    template<typename AccessCaseType, typename... Arguments>
    static std::unique_ptr<AccessCaseType> create(Arguments... arguments)
    {
        return std::unique_ptr<AccessCaseType>(new AccessCaseType(arguments...));
    }

    static Ref<AccessCase> create(VM&, JSCell* owner, AccessType, CacheableIdentifier, PropertyOffset = invalidOffset,
        Structure* = nullptr, const ObjectPropertyConditionSet& = ObjectPropertyConditionSet(), RefPtr<PolyProtoAccessChain>&& = nullptr);

    static RefPtr<AccessCase> createTransition(VM&, JSCell* owner, CacheableIdentifier, PropertyOffset, Structure* oldStructure,
        Structure* newStructure, const ObjectPropertyConditionSet&, RefPtr<PolyProtoAccessChain>&&);

    static Ref<AccessCase> createDelete(VM&, JSCell* owner, CacheableIdentifier, PropertyOffset, Structure* oldStructure,
        Structure* newStructure);

    static Ref<AccessCase> createCheckPrivateBrand(VM&, JSCell* owner, CacheableIdentifier, Structure*);
    static Ref<AccessCase> createSetPrivateBrand(VM&, JSCell* owner, CacheableIdentifier, Structure* oldStructure, Structure* newStructure);
    
    static RefPtr<AccessCase> fromStructureStubInfo(VM&, JSCell* owner, CacheableIdentifier, StructureStubInfo&);

    AccessType type() const { return m_type; }
    State state() const { return m_state; }
    PropertyOffset offset() const { return m_offset; }

    Structure* structure() const
    {
        if (m_type == Transition || m_type == Delete || m_type == SetPrivateBrand)
            return m_structure->previousID();
        return m_structure.get();
    }
    bool guardedByStructureCheck(const StructureStubInfo&) const;

    Structure* newStructure() const
    {
        ASSERT(m_type == Transition || m_type == Delete || m_type == SetPrivateBrand);
        return m_structure.get();
    }

    ObjectPropertyConditionSet conditionSet() const { return m_conditionSet; }

    virtual bool hasAlternateBase() const;
    virtual JSObject* alternateBase() const;
    
    virtual WatchpointSet* additionalSet() const { return nullptr; }
    bool viaProxy() const { return m_viaProxy; }

    // If you supply the optional vector, this will append the set of cells that this will need to keep alive
    // past the call.
    bool doesCalls(VM&, Vector<JSCell*>* cellsToMark = nullptr) const;

    bool isCustom() const
    {
        switch (type()) {
        case CustomValueGetter:
        case CustomAccessorGetter:
        case CustomValueSetter:
        case CustomAccessorSetter:
            return true;
        default:
            return false;
        }
    }

    bool isGetter() const
    {
        switch (type()) {
        case Getter:
        case CustomValueGetter:
        case CustomAccessorGetter:
            return true;
        default:
            return false;
        }
    }

    bool isAccessor() const { return isGetter() || type() == Setter; }

    // Is it still possible for this case to ever be taken? Must call this as a prerequisite for
    // calling generate() and friends. If this returns true, then you can call generate(). If
    // this returns false, then generate() will crash. You must call generate() in the same epoch
    // as when you called couldStillSucceed().
    bool couldStillSucceed() const;

    // If this method returns true, then it's a good idea to remove 'other' from the access once 'this'
    // is added. This method assumes that in case of contradictions, 'this' represents a newer, and so
    // more useful, truth. This method can be conservative; it will return false when it doubt.
    bool canReplace(const AccessCase& other) const;

    void dump(PrintStream& out) const;
    virtual void dumpImpl(PrintStream&, CommaPrinter&) const { }

    virtual ~AccessCase();

    bool usesPolyProto() const
    {
        return !!m_polyProtoAccessChain;
    }

    bool requiresIdentifierNameMatch() const;
    bool requiresInt32PropertyCheck() const;
    bool needsScratchFPR() const;

    static TypedArrayType toTypedArrayType(AccessType);

    UniquedStringImpl* uid() const { return m_identifier.uid(); }
    CacheableIdentifier identifier() const { return m_identifier; }

#if ASSERT_ENABLED
    void checkConsistency(StructureStubInfo&);
#else
    ALWAYS_INLINE void checkConsistency(StructureStubInfo&) { }
#endif

    unsigned hash() const
    {
        return computeHash(m_conditionSet.hash(), static_cast<unsigned>(m_type), m_viaProxy, m_structure.unvalidatedGet(), m_offset);
    }

    static bool canBeShared(const AccessCase&, const AccessCase&);

protected:
    AccessCase(VM&, JSCell* owner, AccessType, CacheableIdentifier, PropertyOffset, Structure*, const ObjectPropertyConditionSet&, RefPtr<PolyProtoAccessChain>&&);
    AccessCase(AccessCase&& other)
        : m_type(WTFMove(other.m_type))
        , m_state(WTFMove(other.m_state))
        , m_viaProxy(WTFMove(other.m_viaProxy))
        , m_offset(WTFMove(other.m_offset))
        , m_structure(WTFMove(other.m_structure))
        , m_conditionSet(WTFMove(other.m_conditionSet))
        , m_polyProtoAccessChain(WTFMove(other.m_polyProtoAccessChain))
        , m_identifier(WTFMove(other.m_identifier))
    { }

    AccessCase(const AccessCase& other)
        : m_type(other.m_type)
        , m_state(other.m_state)
        , m_viaProxy(other.m_viaProxy)
        , m_offset(other.m_offset)
        , m_structure(other.m_structure)
        , m_conditionSet(other.m_conditionSet)
        , m_polyProtoAccessChain(other.m_polyProtoAccessChain)
        , m_identifier(other.m_identifier)
    { }

    AccessCase& operator=(const AccessCase&) = delete;
    void resetState() { m_state = Primordial; }

private:
    friend class CodeBlock;
    friend class PolymorphicAccess;

    template<typename Functor>
    void forEachDependentCell(VM&, const Functor&) const;

    DECLARE_VISIT_AGGREGATE_WITH_MODIFIER(const);
    bool visitWeak(VM&) const;
    template<typename Visitor> void propagateTransitions(Visitor&) const;

    // FIXME: This only exists because of how AccessCase puts post-generation things into itself.
    // https://bugs.webkit.org/show_bug.cgi?id=156456
    virtual Ref<AccessCase> clone() const;

    // Perform any action that must be performed before the end of the epoch in which the case
    // was created. Returns a set of watchpoint sets that will need to be watched.
    Vector<WatchpointSet*, 2> commit(VM&);

    // Fall through on success. Two kinds of failures are supported: fall-through, which means that we
    // should try a different case; and failure, which means that this was the right case but it needs
    // help from the slow path.
    void generateWithGuard(AccessGenerationState&, MacroAssembler::JumpList& fallThrough);

    // Fall through on success, add a jump to the failure list on failure.
    void generate(AccessGenerationState&);

    void generateImpl(AccessGenerationState&);

    bool guardedByStructureCheckSkippingConstantIdentifierCheck() const;

    AccessType m_type;
    State m_state { Primordial };
protected:
    // m_viaProxy is true only if the instance inherits (or it is) ProxyableAccessCase.
    // We put this value here instead of ProxyableAccessCase to reduce the size of ProxyableAccessCase and its
    // derived classes, which are super frequently allocated.
    bool m_viaProxy { false };
private:
    PropertyOffset m_offset;

    // Usually this is the structure that we expect the base object to have. But, this is the *new*
    // structure for a transition and we rely on the fact that it has a strong reference to the old
    // structure. For proxies, this is the structure of the object behind the proxy.
    WriteBarrier<Structure> m_structure;

    ObjectPropertyConditionSet m_conditionSet;

    RefPtr<PolyProtoAccessChain> m_polyProtoAccessChain;

    CacheableIdentifier m_identifier;
};

class SharedJITStubSet {
    WTF_MAKE_FAST_ALLOCATED(SharedJITStubSet);
public:
    SharedJITStubSet() = default;

    struct Hash {
        struct Key {
            Key() = default;

            Key(GPRReg baseGPR, GPRReg valueGPR, GPRReg extraGPR, GPRReg stubInfoGPR, RegisterSet usedRegisters, PolymorphicAccessJITStubRoutine* wrapped)
                : m_wrapped(wrapped)
                , m_baseGPR(baseGPR)
                , m_valueGPR(valueGPR)
                , m_extraGPR(extraGPR)
                , m_stubInfoGPR(stubInfoGPR)
                , m_usedRegisters(usedRegisters)
            { }

            Key(WTF::HashTableDeletedValueType)
                : m_wrapped(bitwise_cast<PolymorphicAccessJITStubRoutine*>(static_cast<uintptr_t>(1)))
            { }

            bool isHashTableDeletedValue() const { return m_wrapped == bitwise_cast<PolymorphicAccessJITStubRoutine*>(static_cast<uintptr_t>(1)); }

            friend bool operator==(const Key& a, const Key& b)
            {
                return a.m_wrapped == b.m_wrapped
                    && a.m_baseGPR == b.m_baseGPR
                    && a.m_valueGPR == b.m_valueGPR
                    && a.m_extraGPR == b.m_extraGPR
                    && a.m_stubInfoGPR == b.m_stubInfoGPR
                    && a.m_usedRegisters == b.m_usedRegisters;
            }

            PolymorphicAccessJITStubRoutine* m_wrapped { nullptr };
            GPRReg m_baseGPR;
            GPRReg m_valueGPR;
            GPRReg m_extraGPR;
            GPRReg m_stubInfoGPR;
            RegisterSet m_usedRegisters;
        };

        using KeyTraits = SimpleClassHashTraits<Key>;

        static unsigned hash(const Key& p)
        {
            if (!p.m_wrapped)
                return 1;
            return p.m_wrapped->hash();
        }

        static bool equal(const Key& a, const Key& b)
        {
            return a == b;
        }

        static constexpr bool safeToCompareToEmptyOrDeleted = false;
    };

    struct Searcher {
        struct Translator {
            static unsigned hash(const Searcher& searcher)
            {
                return PolymorphicAccessJITStubRoutine::computeHash(searcher.m_cases, searcher.m_weakStructures);
            }

            static bool equal(const Hash::Key a, const Searcher& b)
            {
                if (a.m_baseGPR == b.m_baseGPR
                    && a.m_valueGPR == b.m_valueGPR
                    && a.m_extraGPR == b.m_extraGPR
                    && a.m_stubInfoGPR == b.m_stubInfoGPR
                    && a.m_usedRegisters == b.m_usedRegisters) {
                    // FIXME: The ordering of cases does not matter for sharing capabilities.
                    // We can potentially increase success rate by making this comparison / hashing non ordering sensitive.
                    const auto& aCases = a.m_wrapped->cases();
                    const auto& bCases = b.m_cases;
                    if (aCases.size() != bCases.size())
                        return false;
                    for (unsigned index = 0; index < bCases.size(); ++index) {
                        if (!AccessCase::canBeShared(*aCases[index], *bCases[index]))
                            return false;
                    }
                    const auto& aWeak = a.m_wrapped->weakStructures();
                    const auto& bWeak = b.m_weakStructures;
                    if (aWeak.size() != bWeak.size())
                        return false;
                    for (unsigned i = 0, size = aWeak.size(); i < size; ++i) {
                        if (aWeak[i] != bWeak[i])
                            return false;
                    }
                    return true;
                }
                return false;
            }
        };

        GPRReg m_baseGPR;
        GPRReg m_valueGPR;
        GPRReg m_extraGPR;
        GPRReg m_stubInfoGPR;
        RegisterSet m_usedRegisters;
        const FixedVector<RefPtr<AccessCase>>& m_cases;
        const FixedVector<StructureID>& m_weakStructures;
    };

    struct PointerTranslator {
        static unsigned hash(PolymorphicAccessJITStubRoutine* stub)
        {
            return stub->hash();
        }

        static bool equal(const Hash::Key& key, PolymorphicAccessJITStubRoutine* stub)
        {
            return key.m_wrapped == stub;
        }
    };

    void add(Hash::Key&& key)
    {
        m_stubs.add(WTFMove(key));
    }

    void remove(PolymorphicAccessJITStubRoutine* stub)
    {
        auto iter = m_stubs.find<PointerTranslator>(stub);
        if (iter != m_stubs.end())
            m_stubs.remove(iter);
    }

    PolymorphicAccessJITStubRoutine* find(const Searcher& searcher)
    {
        auto entry = m_stubs.find<SharedJITStubSet::Searcher::Translator>(searcher);
        if (entry != m_stubs.end())
            return entry->m_wrapped;
        return nullptr;
    }

private:
    HashSet<Hash::Key, Hash, Hash::KeyTraits> m_stubs;
};

} // namespace JSC

#endif
