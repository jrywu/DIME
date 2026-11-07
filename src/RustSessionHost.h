// RustSessionHost.h - the CDIME side of the Rust key handler flow.
//
// The handlers ported to Rust (rust/crates/dime-core/src/session.rs) drive the
// text service through a DimeHost: a vtable of TSF / UI primitives and of the
// handlers still in C++. CRustSessionHost binds one to a CDIME and the edit
// cookie / context of the current edit session.

#pragma once

#ifdef DIME_RUST_AVAILABLE

#include <msctf.h>
#include "dime_ffi.h"

class CDIME;

class CRustSessionHost
{
public:
    CRustSessionHost(_In_ CDIME* pDime, TfEditCookie ec, _In_ ITfContext* pContext);
    const DimeHost* Get() const { return &_host; }

private:
    CDIME* _pDime;
    TfEditCookie _ec;
    ITfContext* _pContext;
    DimeHost _host;

    static const DimeHostVtbl s_vtbl;
    static CRustSessionHost* Self(void* ctx) { return static_cast<CRustSessionHost*>(ctx); }

    static uint32_t CandidateMode(void* ctx);
    static int32_t IsComposing(void* ctx);
    static int32_t IsFullShape(void* ctx);
    static size_t VirtualKeyLength(void* ctx);
    static int32_t IsSentencePending(void* ctx);
    static int32_t CapsLockOn(void* ctx);
    static uint32_t ShiftEnglishDigitMode(void* ctx);
    static int32_t DayiArticleMode(void* ctx);
    static int32_t IsAlpha(void* ctx, uint16_t c);
    static int32_t LayoutChar(void* ctx, uint16_t c, int32_t capsLockOn, uint16_t* out);
    static int32_t AddCharAndFinalize(void* ctx, const uint16_t* text, size_t len);
    static void RemoveDummyComposition(void* ctx);
    static void DeleteCandidateList(void* ctx, int32_t force);
    static void TerminateComposition(void* ctx);
    static int32_t HandleCandidateFinalize(void* ctx);
    static void SentenceCommitBeforeOutput(void* ctx);
    static int32_t SentenceCloseReading(void* ctx);
    static void SentenceReset(void* ctx);
};

#endif // DIME_RUST_AVAILABLE
