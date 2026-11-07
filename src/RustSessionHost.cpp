// RustSessionHost.cpp - see RustSessionHost.h

#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "RustSessionHost.h"

#ifdef DIME_RUST_AVAILABLE

const DimeHostVtbl CRustSessionHost::s_vtbl = {
    CandidateMode,
    IsComposing,
    IsFullShape,
    VirtualKeyLength,
    IsSentencePending,
    CapsLockOn,
    ShiftEnglishDigitMode,
    DayiArticleMode,
    IsAlpha,
    LayoutChar,
    AddCharAndFinalize,
    RemoveDummyComposition,
    DeleteCandidateList,
    TerminateComposition,
    HandleCandidateFinalize,
    SentenceCommitBeforeOutput,
    SentenceCloseReading,
    SentenceReset,
};

CRustSessionHost::CRustSessionHost(_In_ CDIME* pDime, TfEditCookie ec, _In_ ITfContext* pContext)
    : _pDime(pDime), _ec(ec), _pContext(pContext)
{
    _host.vtbl = &s_vtbl;
    _host.ctx = this;
}

// ---- state ----

uint32_t CRustSessionHost::CandidateMode(void* ctx)
{
    return (uint32_t)Self(ctx)->_pDime->_candidateMode;
}

int32_t CRustSessionHost::IsComposing(void* ctx)
{
    return Self(ctx)->_pDime->_IsComposing() ? 1 : 0;
}

int32_t CRustSessionHost::IsFullShape(void* ctx)
{
    return Self(ctx)->_pDime->_isFullShape ? 1 : 0;
}

size_t CRustSessionHost::VirtualKeyLength(void* ctx)
{
    CCompositionProcessorEngine* pEngine = Self(ctx)->_pDime->_pCompositionProcessorEngine;
    return pEngine ? pEngine->GetVirtualKeyLength() : 0;
}

int32_t CRustSessionHost::IsSentencePending(void* ctx)
{
    return Self(ctx)->_pDime->_IsSentencePending() ? 1 : 0;
}

int32_t CRustSessionHost::CapsLockOn(void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0 ? 1 : 0;
}

uint32_t CRustSessionHost::ShiftEnglishDigitMode(void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    return (uint32_t)CConfig::GetShiftEnglishDigitMode();
}

int32_t CRustSessionHost::DayiArticleMode(void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    return CConfig::getDayiArticleMode() ? 1 : 0;
}

int32_t CRustSessionHost::IsAlpha(void* ctx, uint16_t c)
{
    UNREFERENCED_PARAMETER(ctx);
    return iswalpha(c) ? 1 : 0;
}

// The key lookup of _HandleCompositionShiftEnglishInput, kept as it was
// (including VkKeyScanExW's SHORT result stored in a UINT).
int32_t CRustSessionHost::LayoutChar(void* ctx, uint16_t c, int32_t capsLockOn, uint16_t* out)
{
    UNREFERENCED_PARAMETER(ctx);
    UINT vKey = VkKeyScanExW(c, GetKeyboardLayout(0));
    if (vKey == 0xFFFF)
        return 0;

    BYTE scanCode = (BYTE)MapVirtualKey(LOBYTE(vKey), MAPVK_VK_TO_VSC);
    BYTE keyState[256] = { 0 };
    WCHAR result[2] = { 0 };
    if (capsLockOn)
    {
        keyState[VK_SHIFT] = 0x80;
        keyState[VK_CAPITAL] = 0x01;
    }
    if (ToUnicodeEx(LOBYTE(vKey), scanCode, keyState, result, 2, 0, GetKeyboardLayout(0)) == 1)
    {
        *out = result[0];
        return 1;
    }
    return 0;
}

// ---- TSF / UI primitives ----

int32_t CRustSessionHost::AddCharAndFinalize(void* ctx, const uint16_t* text, size_t len)
{
    CRustSessionHost* self = Self(ctx);
    CStringRange s;
    s.Set((const WCHAR*)text, len);
    return self->_pDime->_AddCharAndFinalize(self->_ec, self->_pContext, &s);
}

void CRustSessionHost::RemoveDummyComposition(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    self->_pDime->_RemoveDummyCompositionForComposing(self->_ec, self->_pDime->_pComposition);
}

void CRustSessionHost::DeleteCandidateList(void* ctx, int32_t force)
{
    CRustSessionHost* self = Self(ctx);
    self->_pDime->_DeleteCandidateList(force ? TRUE : FALSE, self->_pContext);
}

void CRustSessionHost::TerminateComposition(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    self->_pDime->_TerminateComposition(self->_ec, self->_pContext);
}

// ---- handlers still in C++ ----

int32_t CRustSessionHost::HandleCandidateFinalize(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    return self->_pDime->_HandleCandidateFinalize(self->_ec, self->_pContext);
}

void CRustSessionHost::SentenceCommitBeforeOutput(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    self->_pDime->_SentenceCommitBeforeOutput(self->_ec, self->_pContext);
}

int32_t CRustSessionHost::SentenceCloseReading(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    return self->_pDime->_SentenceCloseReading(self->_ec, self->_pContext);
}

void CRustSessionHost::SentenceReset(void* ctx)
{
    Self(ctx)->_pDime->_SentenceReset();
}

#endif // DIME_RUST_AVAILABLE
