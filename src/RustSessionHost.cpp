// RustSessionHost.cpp - see RustSessionHost.h

#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "UIPresenter.h"
#include "RustSessionHost.h"

#ifdef DIME_RUST_AVAILABLE

// Must follow the field order of DimeHostVtbl (dime_ffi.h / dime-ffi lib.rs).
const DimeHostVtbl CRustSessionHost::s_vtbl = {
    IsComposing,
    IsFullShape,
    VirtualKeyLength,
    CapsLockOn,
    ShiftEnglishDigitMode,
    DayiArticleMode,
    IsAlpha,
    LayoutChar,
    AddCharAndFinalize,
    RemoveDummyComposition,
    DeleteCandidateList,
    TerminateComposition,
    HasUiPresenter,
    IsSentenceMode,
    ImeMode,
    ArrayScope,
    ClearOnBeep,
    ArrayForceSP,
    ArrayNotifySP,
    DoHanConvert,
    MakePhrase,
    IsCmdShell,
    IsUiLessMode,
    ReverseConversionActive,
    IsEscapeInputLeading,
    IsArrayShortCode,
    SelectedCandidate,
    CandidateSelection,
    SelkeyIndex,
    SetCandidateSelectionInPage,
    AdviseUiChangedByArrowKey,
    ArraySpecialCodeMatches,
    ArraySpecialCodeFor,
    ScFromTc,
    ShowNotify,
    NotifySelectedKeyCode,
    ShowAssociatedPhrases,
    StartComposition,
    AddComposingAndChar,
    SetCommitString,
    AsyncReverseConversion,
    Beep,
    Selection,
    AddVirtualKey,
    RemoveVirtualKey,
    IsEscapeInput,
    ReadingString,
    AutoCompose,
    DoBeepOnCandi,
    FetchCandidates,
    SetCandidateText,
    ClearCandidateList,
    HideCandidateWindow,
    CreateAndStartCandidate,
    CompositionArrowKey,
    SentenceWrite,
    SentenceModels,
    SentenceLearn,
    SentenceClearDisplay,
    KeystrokeCode,
    FetchedCount,
    FetchedString,
    ShowSentenceCandidates,
    NotifyShowFacts,
    NotifyProbe,
    CandidateTextFacts,
    FetchedKey,
    ApplyCandidateColors,
    UiShow,
    UiUpdate,
    UiShowMode,
    ArrowSwitchesPages,
    CandidateOpen,
    CandidatePlace,
    EndCandidateList,
    CandidateDeleteBegin,
    CandidateDeleteMid,
    CandidateDeleteEnd,
    FetchAssociated,
    ReadingForKey,
    NotifyEndLayout,
};

CRustSessionHost::CRustSessionHost(_In_ CDIME* pDime, TfEditCookie ec, _In_ ITfContext* pContext)
    : _pDime(pDime), _ec(ec), _pContext(pContext)
{
    _host.vtbl = &s_vtbl;
    _host.ctx = this;
#ifdef DIME_USE_RUST_ENGINE
    _host.session = pDime->_pRustSession;
#else
    _host.session = nullptr;
#endif
    _host.presenter = nullptr;
    if (pDime->_pUIPresenter)
        _host.presenter = pDime->_pUIPresenter->HostPresenter();
}

CCompositionProcessorEngine* CRustSessionHost::Engine() const { return _pDime->_pCompositionProcessorEngine; }
CUIPresenter* CRustSessionHost::UI() const { return _pDime->_pUIPresenter; }

// ---- state ----

int32_t CRustSessionHost::IsComposing(void* ctx) { return Self(ctx)->_pDime->_IsComposing() ? 1 : 0; }
int32_t CRustSessionHost::IsFullShape(void* ctx) { return Self(ctx)->_pDime->_isFullShape ? 1 : 0; }

size_t CRustSessionHost::VirtualKeyLength(void* ctx)
{
    CCompositionProcessorEngine* pEngine = Self(ctx)->Engine();
    return pEngine ? pEngine->GetVirtualKeyLength() : 0;
}

int32_t CRustSessionHost::CapsLockOn(void*) { return (GetKeyState(VK_CAPITAL) & 0x0001) != 0 ? 1 : 0; }
uint32_t CRustSessionHost::ShiftEnglishDigitMode(void*) { return (uint32_t)CConfig::GetShiftEnglishDigitMode(); }
int32_t CRustSessionHost::DayiArticleMode(void*) { return CConfig::getDayiArticleMode() ? 1 : 0; }
int32_t CRustSessionHost::IsAlpha(void*, uint16_t c) { return iswalpha(c) ? 1 : 0; }

// The key lookup of _HandleCompositionShiftEnglishInput, kept as it was
// (including VkKeyScanExW's SHORT result stored in a UINT).
int32_t CRustSessionHost::LayoutChar(void*, uint16_t c, int32_t capsLockOn, uint16_t* out)
{
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

// ---- state (candidate handling) ----

int32_t CRustSessionHost::HasUiPresenter(void* ctx) { return Self(ctx)->UI() ? 1 : 0; }
int32_t CRustSessionHost::IsSentenceMode(void* ctx) { return Self(ctx)->_pDime->_IsSentenceMode() ? 1 : 0; }
uint32_t CRustSessionHost::ImeMode(void*) { return (uint32_t)Global::imeMode; }
uint32_t CRustSessionHost::ArrayScope(void*) { return (uint32_t)CConfig::GetArrayScope(); }
int32_t CRustSessionHost::ClearOnBeep(void*) { return CConfig::GetClearOnBeep() ? 1 : 0; }
int32_t CRustSessionHost::ArrayForceSP(void*) { return CConfig::GetArrayForceSP() ? 1 : 0; }
int32_t CRustSessionHost::ArrayNotifySP(void*) { return CConfig::GetArrayNotifySP() ? 1 : 0; }
int32_t CRustSessionHost::DoHanConvert(void*) { return CConfig::GetDoHanConvert() ? 1 : 0; }
int32_t CRustSessionHost::MakePhrase(void*) { return CConfig::GetMakePhrase() ? 1 : 0; }
int32_t CRustSessionHost::IsCmdShell(void* ctx) { return Self(ctx)->_pDime->isCMDShell() ? 1 : 0; }
int32_t CRustSessionHost::IsUiLessMode(void* ctx) { return Self(ctx)->_pDime->_IsUILessMode() ? 1 : 0; }

// Defense in depth from _HandleCandidateWorker: also check CLSID/GUIDProfile,
// not just the cached provider.
int32_t CRustSessionHost::ReverseConversionActive(void* ctx)
{
    return (Self(ctx)->_pDime->_pITfReverseConversion[(UINT)Global::imeMode]
        && !IsEqualCLSID(CConfig::GetReverseConverstionCLSID(), CLSID_NULL)
        && !IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL)) ? 1 : 0;
}

int32_t CRustSessionHost::IsEscapeInputLeading(void* ctx) { return Self(ctx)->Engine()->IsEscapeInputLeading() ? 1 : 0; }
int32_t CRustSessionHost::IsArrayShortCode(void* ctx) { return Self(ctx)->Engine()->IsArrayShortCode() ? 1 : 0; }

// ---- candidate window ----

size_t CRustSessionHost::SelectedCandidate(void* ctx, const uint16_t** out)
{
    const WCHAR* p = nullptr;
    DWORD_PTR len = Self(ctx)->UI()->_GetSelectedCandidateString(&p);
    *out = (const uint16_t*)p;
    return p ? len : 0;
}

int32_t CRustSessionHost::CandidateSelection(void* ctx) { return Self(ctx)->UI()->_GetCandidateSelection(); }

int32_t CRustSessionHost::SelkeyIndex(void* ctx, uint32_t code, uint16_t wch)
{
    CRustSessionHost* self = Self(ctx);
    return self->Engine()->GetCandidateListIndexRange()->GetIndex(code, wch, Global::ModifiersValue, self->_pDime->_candidateMode);
}

int32_t CRustSessionHost::SetCandidateSelectionInPage(void* ctx, int32_t index)
{
    return Self(ctx)->UI()->_SetCandidateSelectionInPage(index) ? 1 : 0;
}

void CRustSessionHost::AdviseUiChangedByArrowKey(void* ctx, uint32_t function)
{
    Self(ctx)->UI()->AdviseUIChangedByArrowKey((KEYSTROKE_FUNCTION)function);
}

// CollectWordFromArraySpeicalCode compares the first unit of the highlighted
// candidate string itself (the pointer, even when the length is zero).
int32_t CRustSessionHost::ArraySpecialCodeMatches(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    const WCHAR* p = nullptr;
    self->UI()->_GetSelectedCandidateString(&p);
    return self->Engine()->CollectWordFromArraySpeicalCode(&p) ? 1 : 0;
}

int32_t CRustSessionHost::ArraySpecialCodeFor(void* ctx, const uint16_t* text, size_t len, const uint16_t** out, size_t* outLen)
{
    CRustSessionHost* self = Self(ctx);
    CStringRange in, code;
    in.Set((const WCHAR*)text, len);
    self->Engine()->GetArraySpeicalCodeFromConvertedText(&in, &code);
    if (!code.Get())
        return 0;
    self->_scratch.assign(code.Get(), code.GetLength());
    *out = (const uint16_t*)self->_scratch.c_str();
    *outLen = self->_scratch.size();
    return 1;
}

size_t CRustSessionHost::ScFromTc(void* ctx, const uint16_t* text, size_t len, const uint16_t** out)
{
    CRustSessionHost* self = Self(ctx);
    CStringRange in, converted;
    in.Set((const WCHAR*)text, len);
    converted = in;
    self->Engine()->GetSCFromTC(&in, &converted);
    self->_scratch.assign(converted.Get() ? converted.Get() : L"", converted.GetLength());
    *out = (const uint16_t*)self->_scratch.c_str();
    return self->_scratch.size();
}

void CRustSessionHost::ShowNotify(void* ctx, const uint16_t* text, size_t len, uint32_t kind)
{
    CStringRange s;
    s.Set((const WCHAR*)text, len);
    Self(ctx)->UI()->ShowNotifyText(&s, 0, 0, (NOTIFY_TYPE)kind);
}

// _HandleCandidateWorker: complete the real key code of a wildcard candidate.
void CRustSessionHost::NotifySelectedKeyCode(void* ctx)
{
    CDIME* dime = Self(ctx)->_pDime;
    const WCHAR* pCandidateKeyCode = nullptr;
    DWORD_PTR keyCodeLen = dime->_pUIPresenter->_GetSelectedCandidateKeyCode(&pCandidateKeyCode);
    StringCchCopy(dime->_commitKeyCode, 1, L"\0");
    if (pCandidateKeyCode)
        StringCchCatN(dime->_commitKeyCode, MAX_KEY_LENGTH, pCandidateKeyCode, keyCodeLen);
    CStringRange commitKeyCode, convertedKeyCode;
    dime->_pCompositionProcessorEngine->GetReadingString(&convertedKeyCode, NULL, &commitKeyCode.Set(dime->_commitKeyCode, wcslen(dime->_commitKeyCode)));
    dime->_pUIPresenter->ShowNotifyText(&convertedKeyCode, 0, 0, NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP);
}

// _HandleCandidateWorker: associated phrases ("make phrase") for the last character.
int32_t CRustSessionHost::ShowAssociatedPhrases(void* ctx, uint16_t lastChar)
{
    CRustSessionHost* self = Self(ctx);
    CDIME* dime = self->_pDime;

    // The candidates may point into it, so it is never freed (as before).
    PWCHAR pwch = new (std::nothrow) WCHAR[2];
    if (pwch == nullptr)
        return 0;
    pwch[0] = lastChar;
    pwch[1] = L'\0';
    CStringRange lastCharRange;
    lastCharRange.Set(pwch, 1);

    CDIMEArray<CCandidateListItem> candidatePhraseList;
    dime->_pCompositionProcessorEngine->GetCandidateStringInConverted(lastCharRange, &candidatePhraseList);
    if (candidatePhraseList.Count() == 0)
        return 0;

    dime->_pUIPresenter->_ClearCandidateList();
    dime->_pUIPresenter->_SetCandidateText(&candidatePhraseList, dime->_pCompositionProcessorEngine->GetCandidateListIndexRange(),
        FALSE, dime->_pCompositionProcessorEngine->GetCandidateWindowWidth(), true);
    dime->_pUIPresenter->_SetCandidateSelection(-1, FALSE);
    dime->_candidateMode = CANDIDATE_MODE::CANDIDATE_PHRASE;
    dime->_isCandidateWithWildcard = FALSE;

    // Positioned with the last saved composition rect (see _HandleCandidateWorker).
    ITfDocumentMgr* pDocMgr = nullptr;
    if (SUCCEEDED(self->_pContext->GetDocumentMgr(&pDocMgr)))
    {
        dime->_pUIPresenter->_StartCandidateList(dime->_tfClientId, pDocMgr, self->_pContext, self->_ec, nullptr,
            dime->_pCompositionProcessorEngine->GetCandidateWindowWidth());
        pDocMgr->Release();
    }
    return 1;
}

void CRustSessionHost::StartComposition(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    self->_pDime->_StartComposition(self->_pContext);
}

// _AddComposingAndChar keeps the string in lastReadingString and reads it on
// the next call, so the text is copied into storage owned by CDIME. Two slots
// alternate so that the previous string stays valid during the next call.
int32_t CRustSessionHost::AddComposingAndChar(void* ctx, const uint16_t* text, size_t len)
{
    CRustSessionHost* self = Self(ctx);
    CDIME* dime = self->_pDime;
    CStringRange s;
    if (text)
    {
        dime->_rustComposingSlot ^= 1;
        std::wstring& buf = dime->_rustComposingText[dime->_rustComposingSlot];
        buf.assign((const WCHAR*)text, len);
        s.Set(buf.c_str(), len);
    }
    return dime->_AddComposingAndChar(self->_ec, self->_pContext, &s);
}

void CRustSessionHost::SetCommitString(void* ctx, const uint16_t* text, size_t len)
{
    CDIME* dime = Self(ctx)->_pDime;
    StringCchCopy(dime->_commitString, 1, L"\0");
    StringCchCatN(dime->_commitString, MAX_COMMIT_LENGTH, (const WCHAR*)text, len);
}

void CRustSessionHost::AsyncReverseConversion(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    self->_pDime->_AsyncReverseConversion(self->_pContext);
}

void CRustSessionHost::Beep(void* ctx, uint32_t kind) { Self(ctx)->_pDime->DoBeep((BEEP_TYPE)kind); }

// ---- handlers still in C++ ----


// ---- composing ----

// The selection checks of _HandleCompositionInput (strict: anything but S_OK
// fails) and _HandleCompositionBackspace (only failures). -1 no selection,
// 0 outside the composition, 1 covered (or no composition range).
int32_t CRustSessionHost::Selection(void* ctx, int32_t strict)
{
    CRustSessionHost* self = Self(ctx);
    CDIME* dime = self->_pDime;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    HRESULT hr = self->_pContext->GetSelection(self->_ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched);
    if ((strict ? hr != S_OK : FAILED(hr)) || fetched != 1)
        return -1;

    int32_t result = 1;
    ITfRange* pRangeComposition = nullptr;
    if (SUCCEEDED(dime->_pComposition->GetRange(&pRangeComposition)))
    {
        BOOL isCovered = dime->_IsRangeCovered(self->_ec, tfSelection.range, pRangeComposition);
        pRangeComposition->Release();
        if (!isCovered)
            result = 0;
    }
    tfSelection.range->Release();
    return result;
}

int32_t CRustSessionHost::AddVirtualKey(void* ctx, uint16_t wch) { return Self(ctx)->Engine()->AddVirtualKey(wch) ? 1 : 0; }
void CRustSessionHost::RemoveVirtualKey(void* ctx, size_t index) { Self(ctx)->Engine()->RemoveVirtualKey(index); }
int32_t CRustSessionHost::IsEscapeInput(void* ctx) { return Self(ctx)->Engine()->IsEscapeInput() ? 1 : 0; }

// GetReadingString; *wildcard stays -1 when it is not set (no dictionary).
// The returned buffer is the engine's (never freed).
size_t CRustSessionHost::ReadingString(void* ctx, const uint16_t** out, int32_t* wildcard)
{
    CStringRange readingString;
    BOOL isWildcardIncluded = -1;
    Self(ctx)->Engine()->GetReadingString(&readingString, &isWildcardIncluded);
    *wildcard = isWildcardIncluded;
    *out = (const uint16_t*)readingString.Get();
    return readingString.Get() ? readingString.GetLength() : 0;
}

int32_t CRustSessionHost::AutoCompose(void*) { return CConfig::GetAutoCompose() ? 1 : 0; }
int32_t CRustSessionHost::DoBeepOnCandi(void*) { return CConfig::GetDoBeepOnCandi() ? 1 : 0; }

uint32_t CRustSessionHost::FetchCandidates(void* ctx, int32_t incremental, int32_t wildcard, int32_t arrayPhraseEnding)
{
    CRustSessionHost* self = Self(ctx);
    self->_fetched.Clear();
    self->Engine()->GetCandidateList(&self->_fetched, incremental ? TRUE : FALSE, wildcard ? TRUE : FALSE, arrayPhraseEnding ? TRUE : FALSE);
    return self->_fetched.Count();
}

void CRustSessionHost::SetCandidateText(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    CCompositionProcessorEngine* pEngine = self->Engine();
    self->UI()->_SetCandidateText(&self->_fetched, pEngine->GetCandidateListIndexRange(), TRUE, pEngine->GetCandidateWindowWidth());
}

// Null-safe: the no-UI branch of _HandleCompositionInputWorker called it on a
// null presenter.
void CRustSessionHost::ClearCandidateList(void* ctx)
{
    if (Self(ctx)->UI())
        Self(ctx)->UI()->_ClearCandidateList();
}

void CRustSessionHost::HideCandidateWindow(void* ctx) { Self(ctx)->UI()->Show(FALSE); }

int32_t CRustSessionHost::CreateAndStartCandidate(void* ctx)
{
    CRustSessionHost* self = Self(ctx);
    return self->_pDime->_CreateAndStartCandidate(self->Engine(), self->_ec, self->_pContext);
}

// _HandleCompositionArrowKey.
void CRustSessionHost::CompositionArrowKey(void* ctx, uint32_t function)
{
    function;   // the list moves in FfiHost before this TSF half
    CRustSessionHost* self = Self(ctx);
    CDIME* dime = self->_pDime;
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;

    if (FAILED(self->_pContext->GetSelection(self->_ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
        return;   // no selection, eat the keystroke

    if (SUCCEEDED(dime->_pComposition->GetRange(&pRangeComposition)))
    {
        // The candidate list moves first, in FfiHost, then this puts the selection back.
        self->_pContext->SetSelection(self->_ec, 1, &tfSelection);
        pRangeComposition->Release();
    }
    tfSelection.range->Release();
}

// ---- sentence mode (the state is in the Rust session) ----

int32_t CRustSessionHost::SentenceWrite(void* ctx, const uint16_t* text, size_t len, size_t caret, int32_t withAttributes)
{
    CRustSessionHost* self = Self(ctx);
    return self->_pDime->_SentenceWrite(self->_ec, self->_pContext, std::wstring((const WCHAR*)text, len), caret, withAttributes ? TRUE : FALSE);
}

// The lexicon (and the user model with it) is loaded on first use, as in _SentenceConvert.
void CRustSessionHost::SentenceModels(void* ctx, const DimeLexicon** lexicon, const DimeUserModel** user)
{
    CDIME* dime = Self(ctx)->_pDime;
    CSentenceLexicon* pLexicon = dime->_GetSentenceLexicon();
    *lexicon = pLexicon ? pLexicon->Handle() : nullptr;
    *user = dime->_pSentenceUser ? dime->_pSentenceUser->Handle() : nullptr;
}

void CRustSessionHost::SentenceLearn(void* ctx) { Self(ctx)->_pDime->_SentenceLearn(); }

void CRustSessionHost::SentenceClearDisplay(void* ctx)
{
    CDIME* dime = Self(ctx)->_pDime;
    dime->_sentenceDisplay.clear();
    dime->lastReadingString.Set(nullptr, 0);
}

size_t CRustSessionHost::KeystrokeCode(void* ctx, const uint16_t** out)
{
    CRustSessionHost* self = Self(ctx);
    self->_scratch = self->_pDime->_GetKeystrokeCode();
    *out = (const uint16_t*)self->_scratch.c_str();
    return self->_scratch.size();
}

uint32_t CRustSessionHost::FetchedCount(void* ctx) { return Self(ctx)->_fetched.Count(); }

size_t CRustSessionHost::FetchedString(void* ctx, uint32_t index, const uint16_t** out)
{
    CCandidateListItem* pItem = Self(ctx)->_fetched.GetAt(index);
    *out = pItem ? (const uint16_t*)pItem->_ItemString.Get() : nullptr;
    return (pItem && pItem->_ItemString.Get()) ? pItem->_ItemString.GetLength() : 0;
}

// _SentenceOpenCandidates: the window keeps the pointers, which point into the
// Rust session's sentence (unchanged while the window is open).
void CRustSessionHost::ShowSentenceCandidates(void* ctx, const uint16_t* const* candidates, const size_t* lens, size_t count,
    const uint16_t* code, size_t codeLen, int32_t selection)
{
    CRustSessionHost* self = Self(ctx);
    CDIME* dime = self->_pDime;
    CDIMEArray<CCandidateListItem> candidateList;
    for (size_t i = 0; i < count; i++)
    {
        CCandidateListItem* pItem = candidateList.Append();
        if (pItem == nullptr) return;
        pItem->_ItemString.Set((const WCHAR*)candidates[i], lens[i]);
        pItem->_FindKeyCode.Set((const WCHAR*)code, codeLen);
    }
    dime->_pUIPresenter->_ClearCandidateList();
    dime->_pUIPresenter->_SetCandidateText(&candidateList, self->Engine()->GetCandidateListIndexRange(),
        FALSE, self->Engine()->GetCandidateWindowWidth());
    dime->_pUIPresenter->_SetCandidateSelection(selection, FALSE);
}

// ---- presenter facts (phase 4b-2c): TSF only, the windows are Rust's ----

void CRustSessionHost::NotifyShowFacts(void* ctx, uint32_t kind, DimeNotifyShowFacts* out)
{
    if (!out)
        return;
    *out = {};
    CRustSessionHost* self = Self(ctx);
    CUIPresenter* ui = self->UI();
    CDIME* dime = self->_pDime;
    if (!ui)
        return;

    self->_notifyContext = ui->_GetContextDocument();
    if (self->_notifyContext == nullptr && dime)
    {
        ITfThreadMgr* pThreadMgr = dime->_GetThreadMgr();
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (pThreadMgr && SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) && pDocumentMgr)
            pDocumentMgr->GetTop(&self->_notifyContext);   // neither ref is released, as before
    }
    out->has_context = self->_notifyContext ? 1 : 0;
    out->hinstance = Global::dllInstanceHandle;
    DimeNotifyClasses classes = { Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
#ifdef DIME_UNIT_TESTING
    if (ui->_notifyAtomForTest)
        classes = { ui->_notifyAtomForTest, ui->_notifyShadowAtomForTest };
#endif
    out->notify_class = classes.notify;
    out->notify_shadow_class = classes.notify_shadow;
    if (CConfig::GetEffectiveDarkMode())
    {
        out->color = CConfig::GetDarkItemColor();
        out->bk = CConfig::GetDarkItemBGColor();
    }
    else if (CConfig::GetColorMode() == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
    {
        out->color = CConfig::GetItemColor();
        out->bk = CConfig::GetItemBGColor();
    }
    else
    {
        out->color = CConfig::GetLightItemColor();
        out->bk = CConfig::GetLightItemBGColor();
    }
    out->refresh = (kind == (uint32_t)NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP
                    || kind == (uint32_t)NOTIFY_TYPE::NOTIFY_SPECIAL_CODE
                    || kind == (uint32_t)NOTIFY_TYPE::NOTIFY_BEEP) ? 1 : 0;
    if (!self->_notifyContext)
        return;

    HWND parent = nullptr;
    ITfContextView* pView = nullptr;
    if (SUCCEEDED(self->_notifyContext->GetActiveView(&pView)) && pView)
        pView->GetWnd(&parent);   // the view is not released, as MakeNotifyWindow
    out->parent = parent;

    HWND viewParent = nullptr;
    ITfContextView* pView2 = nullptr;
    if (SUCCEEDED(self->_notifyContext->GetActiveView(&pView2)) && pView2)
    {
        out->have_view = 1;
        pView2->GetWnd(&viewParent);
        out->view_parent = viewParent;
        if (viewParent)
        {
            GUITHREADINFO guiInfo = {};
            guiInfo.cbSize = sizeof(GUITHREADINFO);
            if (!GetGUIThreadInfo(NULL, &guiInfo))
                guiInfo.rcCaret = { 0, 0, 0, 0 };
            POINT pt = { guiInfo.rcCaret.left, guiInfo.rcCaret.bottom };
            ClientToScreen(viewParent, &pt);
            out->has_parent = 1;
            out->rc_left = guiInfo.rcCaret.left;
            out->rc_top = guiInfo.rcCaret.top;
            out->rc_right = guiInfo.rcCaret.right;
            out->rc_bottom = guiInfo.rcCaret.bottom;
            out->caret_x = pt.x;
            out->caret_y = pt.y;
        }
    }
}

void CRustSessionHost::NotifyProbe(void* ctx, uint32_t kind)
{
    CRustSessionHost* self = Self(ctx);
    CUIPresenter* ui = self->UI();
    if (!ui || !self->_pDime || !self->_notifyContext)
        return;
    if (ui->_GetContextDocument() == nullptr
        && (kind == (uint32_t)NOTIFY_TYPE::NOTIFY_CHN_ENG
            || kind == (uint32_t)NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP
            || kind == (uint32_t)NOTIFY_TYPE::NOTIFY_SPECIAL_CODE
            || kind == (uint32_t)NOTIFY_TYPE::NOTIFY_BEEP))
        self->_pDime->_ProbeComposition(self->_notifyContext);
}

void CRustSessionHost::CandidateTextFacts(void* ctx, void** range, uint32_t* width, int32_t* showMode)
{
    CRustSessionHost* self = Self(ctx);
    CCompositionProcessorEngine* engine = self->Engine();
    CUIPresenter* ui = self->UI();
    CCandidateRange* indexRange = engine ? engine->GetCandidateListIndexRange() : nullptr;
    if (ui)
        ui->_pIndexRange = indexRange;
    if (range)
        *range = indexRange;
    if (width)
        *width = engine ? engine->GetCandidateWindowWidth() : 0;
    if (showMode)
        *showMode = (ui && ui->_isShowMode) ? 1 : 0;
}

size_t CRustSessionHost::FetchedKey(void* ctx, uint32_t index, const uint16_t** out)
{
    CCandidateListItem* pItem = Self(ctx)->_fetched.GetAt(index);
    *out = (pItem) ? (const uint16_t*)pItem->_FindKeyCode.Get() : nullptr;
    return (pItem && pItem->_FindKeyCode.Get()) ? pItem->_FindKeyCode.GetLength() : 0;
}

void CRustSessionHost::ApplyCandidateColors(void* ctx, int32_t phrase)
{
    if (CUIPresenter* ui = Self(ctx)->UI())
        CConfig::ApplyIMEColorSet(ui, phrase != 0);
}

void CRustSessionHost::UiShow(void* ctx, int32_t show)
{
    if (CUIPresenter* ui = Self(ctx)->UI())
        ui->Show(show ? TRUE : FALSE);
}

void CRustSessionHost::UiUpdate(void* ctx, uint32_t which)
{
    CUIPresenter* ui = Self(ctx)->UI();
    if (!ui)
        return;
    switch (which)
    {
    case 1:
        ui->_updatedFlags = TF_CLUIE_SELECTION;
        break;
    case 2:
    case 4:
        ui->_updatedFlags = TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
        break;
    case 3:
        ui->_updatedFlags = TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
        break;
    default:
        break;
    }
    ui->_UpdateUIElement();
}

int32_t CRustSessionHost::UiShowMode(void* ctx)
{
    CUIPresenter* ui = Self(ctx)->UI();
    return (ui && ui->_isShowMode) ? 1 : 0;
}

int32_t CRustSessionHost::ArrowSwitchesPages(void*)
{
    return CConfig::GetArrowKeySWPages() ? 1 : 0;
}

void CRustSessionHost::CandidateOpen(void* ctx, int32_t nullRange, DimeCandOpenFacts* out)
{
    if (!out)
        return;
    *out = {};
    out->go = 0;
    out->hr = S_OK;
    CRustSessionHost* self = Self(ctx);
    CUIPresenter* ui = self->UI();
    CCompositionProcessorEngine* engine = self->Engine();
    CDIME* dime = self->_pDime;
    if (!ui || !engine || !dime || !self->_pContext)
        return;

    ITfDocumentMgr* pDoc = nullptr;
    ITfRange* pRange = nullptr;
    if (!nullRange)
    {
        if (dime->_candidateMode != CANDIDATE_MODE::CANDIDATE_NONE)
            return;
        if (FAILED(self->_pContext->GetDocumentMgr(&pDoc)) || !pDoc)
            return;
        if (!dime->_pComposition || FAILED(dime->_pComposition->GetRange(&pRange)))
        {
            pDoc->Release();
            return;
        }
    }
    else if (FAILED(self->_pContext->GetDocumentMgr(&pDoc)) || !pDoc)
    {
        return;
    }

    HRESULT hrLayout = ui->_StartLayout(self->_pContext, self->_ec, nullRange ? nullptr : pRange);
    if (pRange)
        pRange->Release();
    pDoc->Release();
    if (FAILED(hrLayout))
    {
        out->go = 2;
        out->hr = hrLayout;
        return;
    }
    ui->BeginUIElement();

    out->go = 1;
    out->text_service = ui->_pTextService ? 1 : 0;
    out->show_mode = ui->_isShowMode ? 1 : 0;
    out->hinstance = Global::dllInstanceHandle;
    out->range = engine->GetCandidateListIndexRange();
    ui->_pIndexRange = (CCandidateRange*)out->range;
    out->width = engine->GetCandidateWindowWidth();

    HWND parent = nullptr;
    ITfContextView* pView = nullptr;
    if (SUCCEEDED(self->_pContext->GetActiveView(&pView)) && pView)
        pView->GetWnd(&parent);   // not released, as MakeCandidateWindow
    out->parent = parent;

    COLORREF text = CANDWND_ITEM_COLOR;
    COLORREF bk = GetSysColor(COLOR_WINDOW);
    out->number = text;
    out->number_bk = bk;
    out->text = text;
    out->bk = bk;
    out->selected_text = text;
    out->selected_bk = bk;
    out->fill = GetSysColor(COLOR_3DHIGHLIGHT);

    DimeCandClasses classes = { Global::AtomCandidateWindow, Global::AtomCandidateShadowWindow,
                                Global::AtomCandidateScrollBarWindow, Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
#ifdef DIME_UNIT_TESTING
    if (ui->_candClassesForTest.candidate)
        classes = ui->_candClassesForTest;
#endif
    out->cls_candidate = classes.candidate;
    out->cls_shadow = classes.candidate_shadow;
    out->cls_scroll = classes.scroll_bar;
    out->cls_notify = classes.notify;
    out->cls_notify_shadow = classes.notify_shadow;
}

void CRustSessionHost::CandidatePlace(void* ctx, DimeLayoutFacts* out)
{
    if (!out)
        return;
    *out = {};
    CUIPresenter* ui = Self(ctx)->UI();
    if (!ui)
        return;
    RECT rc = {};
    BOOL firstCall = FALSE;
    if (FAILED(ui->_GetTextExt(&rc)))
    {
        ui->_GetPlacement(nullptr, nullptr, &rc);
        firstCall = TRUE;
    }
    PresenterLayoutFacts facts = {};
    ui->_ReadLayoutFacts(rc, firstCall, facts);
    static_assert(sizeof(PresenterLayoutFacts) == sizeof(DimeLayoutFacts), "DimeLayoutFacts layout");
    memcpy(out, &facts, sizeof(*out));
}

void CRustSessionHost::EndCandidateList(void* ctx)
{
    if (CUIPresenter* ui = Self(ctx)->UI())
        ui->_EndCandidateList();
}

int32_t CRustSessionHost::CandidateDeleteBegin(void* ctx, int32_t force)
{
    CRustSessionHost* self = Self(ctx);
    if (self->Engine())
        self->Engine()->PurgeVirtualKey();
    if (force && self->UI())
    {
        self->UI()->EndUIElement();
        return 1;
    }
    return 0;
}

void CRustSessionHost::CandidateDeleteMid(void* ctx)
{
    if (CUIPresenter* ui = Self(ctx)->UI())
        ui->_UpdateUIElement();
}

void CRustSessionHost::CandidateDeleteEnd(void* ctx)
{
    if (CUIPresenter* ui = Self(ctx)->UI())
        ui->_EndLayout();
}

uint32_t CRustSessionHost::FetchAssociated(void* ctx, uint16_t lastChar)
{
    CRustSessionHost* self = Self(ctx);
    // The candidates may point into it, so it is never freed (as before).
    PWCHAR pwch = new (std::nothrow) WCHAR[2];
    if (pwch == nullptr || !self->Engine())
        return 0;
    pwch[0] = (WCHAR)lastChar;
    pwch[1] = L'\0';
    CStringRange lastCharRange;
    lastCharRange.Set(pwch, 1);
    self->_fetched.Clear();
    self->Engine()->GetCandidateStringInConverted(lastCharRange, &self->_fetched);
    return self->_fetched.Count();
}

size_t CRustSessionHost::ReadingForKey(void* ctx, const uint16_t* key, size_t len, const uint16_t** out)
{
    CRustSessionHost* self = Self(ctx);
    CDIME* dime = self->_pDime;
    StringCchCopy(dime->_commitKeyCode, 1, L"\0");
    if (key && len)
        StringCchCatN(dime->_commitKeyCode, MAX_KEY_LENGTH, (const WCHAR*)key, len);
    CStringRange commitKeyCode, converted;
    commitKeyCode.Set(dime->_commitKeyCode, wcslen(dime->_commitKeyCode));
    if (self->Engine())
        self->Engine()->GetReadingString(&converted, NULL, &commitKeyCode);
    self->_scratch.assign(converted.Get() ? converted.Get() : L"", converted.Get() ? converted.GetLength() : 0);
    *out = (const uint16_t*)self->_scratch.c_str();
    return self->_scratch.size();
}

void CRustSessionHost::NotifyEndLayout(void* ctx)
{
    CUIPresenter* ui = Self(ctx)->UI();
    if (ui && ui->_GetContextDocument() && !ui->_HasCand())
        ui->_EndLayout();
}

#endif // DIME_RUST_AVAILABLE
