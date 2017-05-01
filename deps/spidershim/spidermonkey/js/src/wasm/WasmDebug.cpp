/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmDebug.h"

#include "mozilla/BinarySearch.h"

#include "ds/Sort.h"
#include "jit/ExecutableAllocator.h"
#include "jit/MacroAssembler.h"
#include "vm/Debugger.h"
#include "vm/StringBuffer.h"
#include "wasm/WasmBinaryToText.h"
#include "wasm/WasmValidate.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

using mozilla::BinarySearchIf;

bool
GeneratedSourceMap::searchLineByOffset(JSContext* cx, uint32_t offset, size_t* exprlocIndex)
{
    MOZ_ASSERT(!exprlocs_.empty());
    size_t exprlocsLength = exprlocs_.length();

    // Lazily build sorted array for fast log(n) lookup.
    if (!sortedByOffsetExprLocIndices_) {
        ExprLocIndexVector scratch;
        auto indices = MakeUnique<ExprLocIndexVector>();
        if (!indices || !indices->resize(exprlocsLength) || !scratch.resize(exprlocsLength)) {
            ReportOutOfMemory(cx);
            return false;
        }
        sortedByOffsetExprLocIndices_ = Move(indices);

        for (size_t i = 0; i < exprlocsLength; i++)
            (*sortedByOffsetExprLocIndices_)[i] = i;

        auto compareExprLocViaIndex = [&](uint32_t i, uint32_t j, bool* lessOrEqualp) -> bool {
            *lessOrEqualp = exprlocs_[i].offset <= exprlocs_[j].offset;
            return true;
        };
        MOZ_ALWAYS_TRUE(MergeSort(sortedByOffsetExprLocIndices_->begin(), exprlocsLength,
                                  scratch.begin(), compareExprLocViaIndex));
    }

    // Allowing non-exact search and if BinarySearchIf returns out-of-bound
    // index, moving the index to the last index.
    auto lookupFn = [&](uint32_t i) -> int {
        const ExprLoc& loc = exprlocs_[i];
        return offset == loc.offset ? 0 : offset < loc.offset ? -1 : 1;
    };
    size_t match;
    Unused << BinarySearchIf(sortedByOffsetExprLocIndices_->begin(), 0, exprlocsLength, lookupFn, &match);
    if (match >= exprlocsLength)
        match = exprlocsLength - 1;
    *exprlocIndex = (*sortedByOffsetExprLocIndices_)[match];
    return true;
}

DebugState::DebugState(SharedCode code,
                       const Metadata& metadata,
                       const ShareableBytes* maybeBytecode)
  : code_(Move(code)),
    metadata_(&metadata),
    maybeBytecode_(maybeBytecode),
    enterAndLeaveFrameTrapsCounter_(0)
{
    MOZ_ASSERT_IF(metadata_->debugEnabled, maybeBytecode);
}

const char enabledMessage[] =
    "Restart with developer tools open to view WebAssembly source";

const char tooBigMessage[] =
    "Unfortunately, this WebAssembly module is too big to view as text.\n"
    "We are working hard to remove this limitation.";

static const unsigned TooBig = 1000000;

JSString*
DebugState::createText(JSContext* cx)
{
    StringBuffer buffer(cx);
    if (!maybeBytecode_) {
        if (!buffer.append(enabledMessage))
            return nullptr;

        MOZ_ASSERT(!maybeSourceMap_);
    } else if (maybeBytecode_->bytes.length() > TooBig) {
        if (!buffer.append(tooBigMessage))
            return nullptr;

        MOZ_ASSERT(!maybeSourceMap_);
    } else {
        const Bytes& bytes = maybeBytecode_->bytes;
        auto sourceMap = MakeUnique<GeneratedSourceMap>();
        if (!sourceMap) {
            ReportOutOfMemory(cx);
            return nullptr;
        }
        maybeSourceMap_ = Move(sourceMap);

        if (!BinaryToText(cx, bytes.begin(), bytes.length(), buffer, maybeSourceMap_.get()))
            return nullptr;

#if DEBUG
        // Check that expression locations are sorted by line number.
        uint32_t lastLineno = 0;
        for (const ExprLoc& loc : maybeSourceMap_->exprlocs()) {
            MOZ_ASSERT(lastLineno <= loc.lineno);
            lastLineno = loc.lineno;
        }
#endif
    }

    return buffer.finishString();
}

bool
DebugState::ensureSourceMap(JSContext* cx)
{
    if (maybeSourceMap_ || !maybeBytecode_)
        return true;

    // We just need to cache maybeSourceMap_, ignoring the text result.
    return createText(cx);
}

struct LineComparator
{
    const uint32_t lineno;
    explicit LineComparator(uint32_t lineno) : lineno(lineno) {}

    int operator()(const ExprLoc& loc) const {
        return lineno == loc.lineno ? 0 : lineno < loc.lineno ? -1 : 1;
    }
};

bool
DebugState::getLineOffsets(JSContext* cx, size_t lineno, Vector<uint32_t>* offsets)
{
    if (!metadata_->debugEnabled)
        return true;

    if (!ensureSourceMap(cx))
        return false;

    if (!maybeSourceMap_)
        return true; // no source text available, keep offsets empty.

    ExprLocVector& exprlocs = maybeSourceMap_->exprlocs();

    // Binary search for the expression with the specified line number and
    // rewind to the first expression, if more than one expression on the same line.
    size_t match;
    if (!BinarySearchIf(exprlocs, 0, exprlocs.length(), LineComparator(lineno), &match))
        return true;

    while (match > 0 && exprlocs[match - 1].lineno == lineno)
        match--;

    // Return all expression offsets that were printed on the specified line.
    for (size_t i = match; i < exprlocs.length() && exprlocs[i].lineno == lineno; i++) {
        if (!offsets->append(exprlocs[i].offset))
            return false;
    }

    return true;
}

bool
DebugState::getOffsetLocation(JSContext* cx, uint32_t offset, bool* found, size_t* lineno, size_t* column)
{
    *found = false;
    if (!metadata_->debugEnabled)
        return true;

    if (!ensureSourceMap(cx))
        return false;

    if (!maybeSourceMap_ || maybeSourceMap_->exprlocs().empty())
        return true; // no source text available

    size_t foundAt;
    if (!maybeSourceMap_->searchLineByOffset(cx, offset, &foundAt))
        return false;

    const ExprLoc& loc = maybeSourceMap_->exprlocs()[foundAt];
    *found = true;
    *lineno = loc.lineno;
    *column = loc.column;
    return true;
}

bool
DebugState::totalSourceLines(JSContext* cx, uint32_t* count)
{
    *count = 0;
    if (!metadata_->debugEnabled)
        return true;

    if (!ensureSourceMap(cx))
        return false;

    if (maybeSourceMap_)
        *count = maybeSourceMap_->totalLines();
    return true;
}

bool
DebugState::stepModeEnabled(uint32_t funcIndex) const
{
    return stepModeCounters_.initialized() && stepModeCounters_.lookup(funcIndex);
}

bool
DebugState::incrementStepModeCount(JSContext* cx, uint32_t funcIndex)
{
    MOZ_ASSERT(metadata_->debugEnabled);
    const CodeRange& codeRange = metadata_->codeRanges[metadata_->debugFuncToCodeRange[funcIndex]];
    MOZ_ASSERT(codeRange.isFunction());

    if (!stepModeCounters_.initialized() && !stepModeCounters_.init()) {
        ReportOutOfMemory(cx);
        return false;
    }

    StepModeCounters::AddPtr p = stepModeCounters_.lookupForAdd(funcIndex);
    if (p) {
        MOZ_ASSERT(p->value() > 0);
        p->value()++;
        return true;
    }
    if (!stepModeCounters_.add(p, funcIndex, 1)) {
        ReportOutOfMemory(cx);
        return false;
    }

    AutoWritableJitCode awjc(cx->runtime(), code_->segment().base() + codeRange.begin(),
                             codeRange.end() - codeRange.begin());
    AutoFlushICache afc("Code::incrementStepModeCount");

    for (const CallSite& callSite : metadata_->callSites) {
        if (callSite.kind() != CallSite::Breakpoint)
            continue;
        uint32_t offset = callSite.returnAddressOffset();
        if (codeRange.begin() <= offset && offset <= codeRange.end())
            toggleDebugTrap(offset, true);
    }
    return true;
}

bool
DebugState::decrementStepModeCount(JSContext* cx, uint32_t funcIndex)
{
    MOZ_ASSERT(metadata_->debugEnabled);
    const CodeRange& codeRange = metadata_->codeRanges[metadata_->debugFuncToCodeRange[funcIndex]];
    MOZ_ASSERT(codeRange.isFunction());

    MOZ_ASSERT(stepModeCounters_.initialized() && !stepModeCounters_.empty());
    StepModeCounters::Ptr p = stepModeCounters_.lookup(funcIndex);
    MOZ_ASSERT(p);
    if (--p->value())
        return true;

    stepModeCounters_.remove(p);

    AutoWritableJitCode awjc(cx->runtime(), code_->segment().base() + codeRange.begin(),
                             codeRange.end() - codeRange.begin());
    AutoFlushICache afc("Code::decrementStepModeCount");

    for (const CallSite& callSite : metadata_->callSites) {
        if (callSite.kind() != CallSite::Breakpoint)
            continue;
        uint32_t offset = callSite.returnAddressOffset();
        if (codeRange.begin() <= offset && offset <= codeRange.end()) {
            bool enabled = breakpointSites_.initialized() && breakpointSites_.has(offset);
            toggleDebugTrap(offset, enabled);
        }
    }
    return true;
}

static const CallSite*
SlowCallSiteSearchByOffset(const Metadata& metadata, uint32_t offset)
{
    for (const CallSite& callSite : metadata.callSites) {
        if (callSite.lineOrBytecode() == offset && callSite.kind() == CallSiteDesc::Breakpoint)
            return &callSite;
    }
    return nullptr;
}

bool
DebugState::hasBreakpointTrapAtOffset(uint32_t offset)
{
    if (!metadata_->debugEnabled)
        return false;
    return SlowCallSiteSearchByOffset(*metadata_, offset);
}

void
DebugState::toggleBreakpointTrap(JSRuntime* rt, uint32_t offset, bool enabled)
{
    MOZ_ASSERT(metadata_->debugEnabled);
    const CallSite* callSite = SlowCallSiteSearchByOffset(*metadata_, offset);
    if (!callSite)
        return;
    size_t debugTrapOffset = callSite->returnAddressOffset();

    const CodeRange* codeRange = code_->lookupRange(code_->segment().base() + debugTrapOffset);
    MOZ_ASSERT(codeRange && codeRange->isFunction());

    if (stepModeCounters_.initialized() && stepModeCounters_.lookup(codeRange->funcIndex()))
        return; // no need to toggle when step mode is enabled

    AutoWritableJitCode awjc(rt, code_->segment().base(), code_->segment().length());
    AutoFlushICache afc("Code::toggleBreakpointTrap");
    AutoFlushICache::setRange(uintptr_t(code_->segment().base()), code_->segment().length());
    toggleDebugTrap(debugTrapOffset, enabled);
}

WasmBreakpointSite*
DebugState::getOrCreateBreakpointSite(JSContext* cx, uint32_t offset)
{
    WasmBreakpointSite* site;
    if (!breakpointSites_.initialized() && !breakpointSites_.init()) {
        ReportOutOfMemory(cx);
        return nullptr;
    }

    WasmBreakpointSiteMap::AddPtr p = breakpointSites_.lookupForAdd(offset);
    if (!p) {
        site = cx->runtime()->new_<WasmBreakpointSite>(this, offset);
        if (!site || !breakpointSites_.add(p, offset, site)) {
            js_delete(site);
            ReportOutOfMemory(cx);
            return nullptr;
        }
    } else {
        site = p->value();
    }
    return site;
}

bool
DebugState::hasBreakpointSite(uint32_t offset)
{
    return breakpointSites_.initialized() && breakpointSites_.has(offset);
}

void
DebugState::destroyBreakpointSite(FreeOp* fop, uint32_t offset)
{
    MOZ_ASSERT(breakpointSites_.initialized());
    WasmBreakpointSiteMap::Ptr p = breakpointSites_.lookup(offset);
    MOZ_ASSERT(p);
    fop->delete_(p->value());
    breakpointSites_.remove(p);
}

bool
DebugState::clearBreakpointsIn(JSContext* cx, WasmInstanceObject* instance, js::Debugger* dbg, JSObject* handler)
{
    MOZ_ASSERT(instance);
    if (!breakpointSites_.initialized())
        return true;

    // Make copy of all sites list, so breakpointSites_ can be modified by
    // destroyBreakpointSite calls.
    Vector<WasmBreakpointSite*> sites(cx);
    if (!sites.resize(breakpointSites_.count()))
        return false;
    size_t i = 0;
    for (WasmBreakpointSiteMap::Range r = breakpointSites_.all(); !r.empty(); r.popFront())
        sites[i++] = r.front().value();

    for (WasmBreakpointSite* site : sites) {
        Breakpoint* nextbp;
        for (Breakpoint* bp = site->firstBreakpoint(); bp; bp = nextbp) {
            nextbp = bp->nextInSite();
            if (bp->asWasm()->wasmInstance == instance &&
                (!dbg || bp->debugger == dbg) &&
                (!handler || bp->getHandler() == handler))
            {
                bp->destroy(cx->runtime()->defaultFreeOp());
            }
        }
    }
    return true;
}

void
DebugState::toggleDebugTrap(uint32_t offset, bool enabled)
{
    MOZ_ASSERT(offset);
    uint8_t* trap = code_->segment().base() + offset;
    const Uint32Vector& farJumpOffsets = metadata_->debugTrapFarJumpOffsets;
    if (enabled) {
        MOZ_ASSERT(farJumpOffsets.length() > 0);
        size_t i = 0;
        while (i < farJumpOffsets.length() && offset < farJumpOffsets[i])
            i++;
        if (i >= farJumpOffsets.length() ||
            (i > 0 && offset - farJumpOffsets[i - 1] < farJumpOffsets[i] - offset))
            i--;
        uint8_t* farJump = code_->segment().base() + farJumpOffsets[i];
        MacroAssembler::patchNopToCall(trap, farJump);
    } else {
        MacroAssembler::patchCallToNop(trap);
    }
}

void
DebugState::adjustEnterAndLeaveFrameTrapsState(JSContext* cx, bool enabled)
{
    MOZ_ASSERT(metadata_->debugEnabled);
    MOZ_ASSERT_IF(!enabled, enterAndLeaveFrameTrapsCounter_ > 0);

    bool wasEnabled = enterAndLeaveFrameTrapsCounter_ > 0;
    if (enabled)
        ++enterAndLeaveFrameTrapsCounter_;
    else
        --enterAndLeaveFrameTrapsCounter_;
    bool stillEnabled = enterAndLeaveFrameTrapsCounter_ > 0;
    if (wasEnabled == stillEnabled)
        return;

    AutoWritableJitCode awjc(cx->runtime(), code_->segment().base(), code_->segment().length());
    AutoFlushICache afc("Code::adjustEnterAndLeaveFrameTrapsState");
    AutoFlushICache::setRange(uintptr_t(code_->segment().base()), code_->segment().length());
    for (const CallSite& callSite : metadata_->callSites) {
        if (callSite.kind() != CallSite::EnterFrame && callSite.kind() != CallSite::LeaveFrame)
            continue;
        toggleDebugTrap(callSite.returnAddressOffset(), stillEnabled);
    }
}

bool
DebugState::debugGetLocalTypes(uint32_t funcIndex, ValTypeVector* locals, size_t* argsLength)
{
    MOZ_ASSERT(metadata_->debugEnabled);

    const ValTypeVector& args = metadata_->debugFuncArgTypes[funcIndex];
    *argsLength = args.length();
    if (!locals->appendAll(args))
        return false;

    // Decode local var types from wasm binary function body.
    const CodeRange& range = metadata_->codeRanges[metadata_->debugFuncToCodeRange[funcIndex]];
    // In wasm, the Code points to the function start via funcLineOrBytecode.
    MOZ_ASSERT(!metadata_->isAsmJS() && maybeBytecode_);
    size_t offsetInModule = range.funcLineOrBytecode();
    Decoder d(maybeBytecode_->begin() + offsetInModule,  maybeBytecode_->end(),
              offsetInModule, /* error = */ nullptr);
    return DecodeLocalEntries(d, metadata_->kind, locals);
}

ExprType
DebugState::debugGetResultType(uint32_t funcIndex)
{
    MOZ_ASSERT(metadata_->debugEnabled);
    return metadata_->debugFuncReturnTypes[funcIndex];
}

JSString*
DebugState::debugDisplayURL(JSContext* cx) const
{
    // Build wasm module URL from following parts:
    // - "wasm:" as protocol;
    // - URI encoded filename from metadata (if can be encoded), plus ":";
    // - 64-bit hash of the module bytes (as hex dump).
    js::StringBuffer result(cx);
    if (!result.append("wasm:"))
        return nullptr;
    if (const char* filename = metadata_->filename.get()) {
        js::StringBuffer filenamePrefix(cx);
        // EncodeURI returns false due to invalid chars or OOM -- fail only
        // during OOM.
        if (!EncodeURI(cx, filenamePrefix, filename, strlen(filename))) {
            if (!cx->isExceptionPending())
                return nullptr;
            cx->clearPendingException(); // ignore invalid URI
        } else if (!result.append(filenamePrefix.finishString()) || !result.append(":")) {
            return nullptr;
        }
    }

    const ModuleHash& hash = metadata_->hash;
    for (size_t i = 0; i < sizeof(ModuleHash); i++) {
        char digit1 = hash[i] / 16, digit2 = hash[i] % 16;
        if (!result.append((char)(digit1 < 10 ? digit1 + '0' : digit1 + 'a' - 10)))
            return nullptr;
        if (!result.append((char)(digit2 < 10 ? digit2 + '0' : digit2 + 'a' - 10)))
            return nullptr;
    }
    return result.finishString();

}
