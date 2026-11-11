// RustSessionHost.h - the CDIME side of the Rust key handler flow.
//
// The handlers ported to Rust (rust/crates/dime-core/src/session.rs) drive the
// text service through a DimeHost: a vtable of TSF / UI primitives, state
// accessors and the handlers still in C++. CRustSessionHost binds one to a
// CDIME and the edit cookie / context of the current edit session.

#pragma once

#ifdef DIME_RUST_AVAILABLE

#include <msctf.h>
#include <string>
#include "dime_ffi.h"
#include "BaseStructure.h"

class CDIME;
class CCompositionProcessorEngine;
class CUIPresenter;

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
    std::wstring _scratch;      // strings returned to Rust (valid until the next callback)
    CDIMEArray<CCandidateListItem> _fetched;   // fetch_candidates
    ITfContext* _notifyContext = nullptr;      // ShowNotifyText's context (the focus one is not released)

    static const DimeHostVtbl s_vtbl;
    static CRustSessionHost* Self(void* ctx) { return static_cast<CRustSessionHost*>(ctx); }
    CCompositionProcessorEngine* Engine() const;
    CUIPresenter* UI() const;

    // In DimeHostVtbl order.
    static int32_t IsComposing(void* ctx);
    static int32_t IsFullShape(void* ctx);
    static size_t VirtualKeyLength(void* ctx);
    static int32_t CapsLockOn(void* ctx);
    static uint32_t ShiftEnglishDigitMode(void* ctx);
    static int32_t DayiArticleMode(void* ctx);
    static int32_t IsAlpha(void* ctx, uint16_t c);
    static int32_t LayoutChar(void* ctx, uint16_t c, int32_t capsLockOn, uint16_t* out);
    static int32_t AddCharAndFinalize(void* ctx, const uint16_t* text, size_t len);
    static void RemoveDummyComposition(void* ctx);
    static void DeleteCandidateList(void* ctx, int32_t force);
    static void TerminateComposition(void* ctx);
    static int32_t HasUiPresenter(void* ctx);
    static int32_t IsSentenceMode(void* ctx);
    static uint32_t ImeMode(void* ctx);
    static uint32_t ArrayScope(void* ctx);
    static int32_t ClearOnBeep(void* ctx);
    static int32_t ArrayForceSP(void* ctx);
    static int32_t ArrayNotifySP(void* ctx);
    static int32_t DoHanConvert(void* ctx);
    static int32_t MakePhrase(void* ctx);
    static int32_t IsCmdShell(void* ctx);
    static int32_t IsUiLessMode(void* ctx);
    static int32_t ReverseConversionActive(void* ctx);
    static int32_t IsEscapeInputLeading(void* ctx);
    static int32_t IsArrayShortCode(void* ctx);
    static size_t SelectedCandidate(void* ctx, const uint16_t** out);
    static int32_t CandidateSelection(void* ctx);
    static int32_t SelkeyIndex(void* ctx, uint32_t code, uint16_t wch);
    static int32_t SetCandidateSelectionInPage(void* ctx, int32_t index);
    static void AdviseUiChangedByArrowKey(void* ctx, uint32_t function);
    static int32_t ArraySpecialCodeMatches(void* ctx);
    static int32_t ArraySpecialCodeFor(void* ctx, const uint16_t* text, size_t len, const uint16_t** out, size_t* outLen);
    static size_t ScFromTc(void* ctx, const uint16_t* text, size_t len, const uint16_t** out);
    static void ShowNotify(void* ctx, const uint16_t* text, size_t len, uint32_t kind);
    static void NotifySelectedKeyCode(void* ctx);
    static int32_t ShowAssociatedPhrases(void* ctx, uint16_t lastChar);
    static void StartComposition(void* ctx);
    static int32_t AddComposingAndChar(void* ctx, const uint16_t* text, size_t len);
    static void SetCommitString(void* ctx, const uint16_t* text, size_t len);
    static void AsyncReverseConversion(void* ctx);
    static void Beep(void* ctx, uint32_t kind);
    static int32_t Selection(void* ctx, int32_t strict);
    static int32_t AddVirtualKey(void* ctx, uint16_t wch);
    static void RemoveVirtualKey(void* ctx, size_t index);
    static int32_t IsEscapeInput(void* ctx);
    static size_t ReadingString(void* ctx, const uint16_t** out, int32_t* wildcard);
    static int32_t AutoCompose(void* ctx);
    static int32_t DoBeepOnCandi(void* ctx);
    static uint32_t FetchCandidates(void* ctx, int32_t incremental, int32_t wildcard, int32_t arrayPhraseEnding);
    static void SetCandidateText(void* ctx);
    static void ClearCandidateList(void* ctx);
    static void HideCandidateWindow(void* ctx);
    static int32_t CreateAndStartCandidate(void* ctx);
    static void CompositionArrowKey(void* ctx, uint32_t function);
    static int32_t SentenceWrite(void* ctx, const uint16_t* text, size_t len, size_t caret, int32_t withAttributes);
    static void SentenceModels(void* ctx, const DimeLexicon** lexicon, const DimeUserModel** user);
    static void SentenceLearn(void* ctx);
    static void SentenceClearDisplay(void* ctx);
    static size_t KeystrokeCode(void* ctx, const uint16_t** out);
    static uint32_t FetchedCount(void* ctx);
    static size_t FetchedString(void* ctx, uint32_t index, const uint16_t** out);
    static void ShowSentenceCandidates(void* ctx, const uint16_t* const* candidates, const size_t* lens, size_t count, const uint16_t* code, size_t codeLen, int32_t selection);
    // Phase 4b-2c, in DimeHostVtbl order after the slots above.
    static void NotifyShowFacts(void* ctx, uint32_t kind, DimeNotifyShowFacts* out);
    static void NotifyProbe(void* ctx, uint32_t kind);
    static void CandidateTextFacts(void* ctx, void** range, uint32_t* width, int32_t* showMode);
    static size_t FetchedKey(void* ctx, uint32_t index, const uint16_t** out);
    static void ApplyCandidateColors(void* ctx, int32_t phrase);
    static void UiShow(void* ctx, int32_t show);
    static void UiUpdate(void* ctx, uint32_t which);
    static int32_t UiShowMode(void* ctx);
    static int32_t ArrowSwitchesPages(void* ctx);
    static void CandidateOpen(void* ctx, int32_t nullRange, DimeCandOpenFacts* out);
    static void CandidatePlace(void* ctx, DimeLayoutFacts* out);
    static void EndCandidateList(void* ctx);
    static int32_t CandidateDeleteBegin(void* ctx, int32_t force);
    static void CandidateDeleteMid(void* ctx);
    static void CandidateDeleteEnd(void* ctx);
    static uint32_t FetchAssociated(void* ctx, uint16_t lastChar);
    static size_t ReadingForKey(void* ctx, const uint16_t* key, size_t len, const uint16_t** out);
    static void NotifyEndLayout(void* ctx);
};

#endif // DIME_RUST_AVAILABLE
