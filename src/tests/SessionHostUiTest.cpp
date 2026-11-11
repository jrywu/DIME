/********************************************************************************
* SessionHostUiTest: phase 4b-2c (docs/RUST_TSF_SHELL.md). With DimeHost.presenter
* set, the session drives the Rust presenter directly. The old UI vtable slots
* (create_and_start_candidate, set_candidate_text, show_notify, ...) stay at
* zero; the new fact callbacks supply TSF stand-ins and real test windows.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include "WindowClassTestHelper.h"

#include <string.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE

namespace
{
struct HostCtx
{
    DimePresenter* presenter = nullptr;
    DimeSession* session = nullptr;
    int legacy = 0;
    int nextFetch = 0;
    uint32_t fetched = 0;
    int phraseStrings = 0;
    int makePhrase = 0;
    int arrayNotifySp = 0;
    int arrayForceSp = 0;
    int autoCompose = 0;
    int isComposing = 1;
    int hasUi = 1;
    int cmdShell = 0;
    int assocCount = 0;
    int phraseOpen = 0;
    uint32_t modeAtPhraseOpen = 0;
    int midCalls = 0;
    int midExists = -1;
    int midCount = -1;
    int arrowSelection = -2;
    int beginFalse = 0;
    int beginTrue = 0;
};

HostCtx* H(void* ctx) { return static_cast<HostCtx*>(ctx); }

CCandidateRange& Keys()
{
    static CCandidateRange range;
    static bool made = false;
    if (!made)
    {
        for (int i = 0; i < 10; i++)
            range.Append()->CandIndex = L"1234567890"[i];
        made = true;
    }
    return range;
}

uint32_t Dpi(void*) { return 96; }
int32_t Zero() { return 0; }
void* Font() { return Global::defaultlFontHandle; }
void SetFont(void*) {}
void CandInvoke(void*, int32_t) {}
void NotifyInvoke(void*, int32_t, uintptr_t, intptr_t) {}
int32_t RangeCount(const void* range)
{
    if (!range) return -1;
    return static_cast<const CCandidateRange*>(range)->Count();
}
uint16_t RangeChar(const void* range, int32_t row)
{
    CCandidateRange* r = static_cast<CCandidateRange*>(const_cast<void*>(range));
    if (!r || row < 0 || row >= r->Count()) return 0;
    return static_cast<uint16_t>(r->GetAt(row)->CandIndex);
}

extern "C"
{
int32_t HostIsComposing(void* ctx) { return H(ctx)->isComposing; }
int32_t HostIsFullShape(void*) { return 0; }
size_t HostVirtualKeyLength(void*) { return 0; }
int32_t HostCapsLock(void*) { return 0; }
uint32_t HostShiftDigit(void*) { return 0; }
int32_t HostDayiArticle(void*) { return 0; }
int32_t HostIsAlpha(void*, uint16_t) { return 0; }
int32_t HostLayoutChar(void*, uint16_t, int32_t, uint16_t*) { return 0; }
int32_t HostAddChar(void*, const uint16_t*, size_t) { return 0; }
void HostRemoveDummy(void*) {}
void HostLegacy(void* ctx) { H(ctx)->legacy++; }
void HostDeleteList(void* ctx, int32_t) { HostLegacy(ctx); }
void HostTerminate(void*) {}
int32_t HostHasUi(void* ctx) { return H(ctx)->hasUi; }
int32_t HostSentence(void*) { return 0; }
uint32_t HostIme(void*) { return static_cast<uint32_t>(IME_MODE::IME_MODE_ARRAY); }
uint32_t HostArrayScope(void*) { return 0; }
int32_t HostClearOnBeep(void*) { return 0; }
int32_t HostArrayForce(void* ctx) { return H(ctx)->arrayForceSp; }
int32_t HostArrayNotify(void* ctx) { return H(ctx)->arrayNotifySp; }
int32_t HostHan(void*) { return 0; }
int32_t HostMakePhrase(void* ctx) { return H(ctx)->makePhrase; }
int32_t HostCmd(void* ctx) { return H(ctx)->cmdShell; }
int32_t HostUiLess(void*) { return 0; }
int32_t HostReverse(void*) { return 0; }
int32_t HostEscapeLeading(void*) { return 0; }
int32_t HostShortCode(void*) { return 0; }
size_t HostSelected(void* ctx, const uint16_t**) { HostLegacy(ctx); return 0; }
int32_t HostCandSel(void* ctx) { HostLegacy(ctx); return 0; }
int32_t HostSelkey(void*, uint32_t, uint16_t) { return -1; }
int32_t HostSelInPage(void* ctx, int32_t) { HostLegacy(ctx); return 0; }
void HostAdvise(void* ctx, uint32_t) { HostLegacy(ctx); }
int32_t HostSpMatch(void*) { return 0; }
int32_t HostSpFor(void*, const uint16_t*, size_t, const uint16_t** out, size_t* outLen)
{
    static const wchar_t text[] = L"sp";
    *out = reinterpret_cast<const uint16_t*>(text);
    *outLen = 2;
    return 1;
}
size_t HostSc(void*, const uint16_t*, size_t, const uint16_t** out) { *out = nullptr; return 0; }
void HostShowNotify(void* ctx, const uint16_t*, size_t, uint32_t) { HostLegacy(ctx); }
void HostNotifyKey(void* ctx) { HostLegacy(ctx); }
int32_t HostShowPhrases(void* ctx, uint16_t) { HostLegacy(ctx); return 0; }
void HostStartComp(void*) {}
int32_t HostAddComposing(void*, const uint16_t*, size_t) { return 0; }
void HostCommit(void*, const uint16_t*, size_t) {}
void HostAsync(void*) {}
void HostBeep(void*, uint32_t) {}
int32_t HostSelection(void*, int32_t) { return 1; }
int32_t HostAddKey(void*, uint16_t) { return 1; }
void HostRemoveKey(void*, size_t) {}
int32_t HostEscape(void*) { return 0; }
size_t HostReading(void*, const uint16_t** out, int32_t* wildcard)
{
    static const wchar_t text[] = L"r";
    *out = reinterpret_cast<const uint16_t*>(text);
    *wildcard = 0;
    return 1;
}
int32_t HostAuto(void* ctx) { return H(ctx)->autoCompose; }
int32_t HostBeepCandi(void*) { return 0; }
uint32_t HostFetch(void* ctx, int32_t, int32_t, int32_t)
{
    HostCtx* h = H(ctx);
    h->phraseStrings = 0;
    h->fetched = static_cast<uint32_t>(h->nextFetch);
    return h->fetched;
}
void HostSetText(void* ctx) { HostLegacy(ctx); }
void HostClearList(void* ctx) { HostLegacy(ctx); }
void HostHide(void* ctx) { HostLegacy(ctx); }
int32_t HostCreate(void* ctx) { HostLegacy(ctx); return 0; }
void HostArrow(void* ctx, uint32_t)
{
    // The TSF half. Advise has already moved the presenter selection.
    H(ctx)->arrowSelection = dime_ui_presenter_cand_query(H(ctx)->presenter, 4);
}
int32_t HostSentenceWrite(void*, const uint16_t*, size_t, size_t, int32_t) { return 0; }
void HostSentenceModels(void*, const DimeLexicon**, const DimeUserModel**) {}
void HostSentenceLearn(void*) {}
void HostSentenceClear(void*) {}
size_t HostKeystroke(void*, const uint16_t** out) { *out = nullptr; return 0; }
uint32_t HostFetchedCount(void* ctx) { return H(ctx)->fetched; }
size_t HostFetchedString(void* ctx, uint32_t, const uint16_t** out)
{
    static const wchar_t item[] = L"x";
    static const wchar_t phrase[] = L"ph";
    const wchar_t* text = H(ctx)->phraseStrings ? phrase : item;
    *out = reinterpret_cast<const uint16_t*>(text);
    return wcslen(text);
}
void HostShowSentence(void* ctx, const uint16_t* const*, const size_t*, size_t, const uint16_t*, size_t, int32_t) { HostLegacy(ctx); }
void HostNotifyFacts(void*, uint32_t, DimeNotifyShowFacts* out)
{
    DimeNotifyClasses classes = RustNotifyTestClasses();
    *out = {};
    out->has_context = 1;
    out->have_view = 1;
    out->hinstance = Global::dllInstanceHandle;
    out->notify_class = classes.notify;
    out->notify_shadow_class = classes.notify_shadow;
    out->bk = 0x00FFFFFFu;
}
void HostNotifyProbe(void*, uint32_t) {}
void HostTextFacts(void*, void** range, uint32_t* width, int32_t* showMode)
{
    *range = &Keys();
    *width = 4;
    *showMode = 1;
}
size_t HostFetchedKey(void*, uint32_t, const uint16_t** out)
{
    static const wchar_t key[] = L"a";
    *out = reinterpret_cast<const uint16_t*>(key);
    return 1;
}
void HostColors(void*, int32_t) {}
void HostUiShow(void* ctx, int32_t show)
{
    dime_ui_presenter_cand_op(H(ctx)->presenter, 0, show, 0);
}
void HostUiUpdate(void*, uint32_t) {}
int32_t HostUiShowMode(void*) { return 1; }
int32_t HostArrowPages(void*) { return 0; }
void HostCandOpen(void* ctx, int32_t nullRange, DimeCandOpenFacts* out)
{
    HostCtx* h = H(ctx);
    uint32_t mode = *dime_session_candidate_mode_ptr(h->session);
    if (nullRange)
    {
        h->phraseOpen = 1;
        h->modeAtPhraseOpen = mode;
    }
    DimeCandClasses classes = RustCandidateTestClasses();
    *out = {};
    out->go = 1;
    out->hr = S_OK;
    out->text_service = 1;
    out->show_mode = 1;
    out->hinstance = Global::dllInstanceHandle;
    out->range = &Keys();
    out->width = 4;
    out->number_bk = 0x00FFFFFFu;
    out->bk = 0x00FFFFFFu;
    out->selected_bk = 0x00FFFFFFu;
    out->fill = 0x00F0F0F0u;
    out->cls_candidate = classes.candidate;
    out->cls_shadow = classes.candidate_shadow;
    out->cls_scroll = classes.scroll_bar;
    out->cls_notify = classes.notify;
    out->cls_notify_shadow = classes.notify_shadow;
}
void HostCandPlace(void*, DimeLayoutFacts* out) { *out = {}; }
void HostEndList(void*) {}
int32_t HostDeleteBegin(void* ctx, int32_t force)
{
    HostCtx* h = H(ctx);
    if (force)
    {
        h->beginTrue++;
        return 1;
    }
    h->beginFalse++;
    return 0;
}
void HostDeleteMid(void* ctx)
{
    HostCtx* h = H(ctx);
    if (h->midCalls == 0)
    {
        h->midExists = dime_ui_presenter_cand_query(h->presenter, 0);
        h->midCount = dime_ui_presenter_cand_query(h->presenter, 3);
    }
    h->midCalls++;
}
void HostDeleteEnd(void*) {}
uint32_t HostFetchAssoc(void* ctx, uint16_t)
{
    HostCtx* h = H(ctx);
    h->phraseStrings = 1;
    h->fetched = static_cast<uint32_t>(h->assocCount);
    return h->fetched;
}
size_t HostReadingKey(void*, const uint16_t*, size_t, const uint16_t** out) { *out = nullptr; return 0; }
void HostNotifyEnd(void*) {}
} // extern "C"

const DimeHostVtbl& Vtbl()
{
    static const DimeHostVtbl v = {
        HostIsComposing,
        HostIsFullShape,
        HostVirtualKeyLength,
        HostCapsLock,
        HostShiftDigit,
        HostDayiArticle,
        HostIsAlpha,
        HostLayoutChar,
        HostAddChar,
        HostRemoveDummy,
        HostDeleteList,
        HostTerminate,
        HostHasUi,
        HostSentence,
        HostIme,
        HostArrayScope,
        HostClearOnBeep,
        HostArrayForce,
        HostArrayNotify,
        HostHan,
        HostMakePhrase,
        HostCmd,
        HostUiLess,
        HostReverse,
        HostEscapeLeading,
        HostShortCode,
        HostSelected,
        HostCandSel,
        HostSelkey,
        HostSelInPage,
        HostAdvise,
        HostSpMatch,
        HostSpFor,
        HostSc,
        HostShowNotify,
        HostNotifyKey,
        HostShowPhrases,
        HostStartComp,
        HostAddComposing,
        HostCommit,
        HostAsync,
        HostBeep,
        HostSelection,
        HostAddKey,
        HostRemoveKey,
        HostEscape,
        HostReading,
        HostAuto,
        HostBeepCandi,
        HostFetch,
        HostSetText,
        HostClearList,
        HostHide,
        HostCreate,
        HostArrow,
        HostSentenceWrite,
        HostSentenceModels,
        HostSentenceLearn,
        HostSentenceClear,
        HostKeystroke,
        HostFetchedCount,
        HostFetchedString,
        HostShowSentence,
        HostNotifyFacts,
        HostNotifyProbe,
        HostTextFacts,
        HostFetchedKey,
        HostColors,
        HostUiShow,
        HostUiUpdate,
        HostUiShowMode,
        HostArrowPages,
        HostCandOpen,
        HostCandPlace,
        HostEndList,
        HostDeleteBegin,
        HostDeleteMid,
        HostDeleteEnd,
        HostFetchAssoc,
        HostReadingKey,
        HostNotifyEnd,
    };
    return v;
}

struct Fixture
{
    HFONT font = nullptr;
    HFONT saved = nullptr;
    DimePresenter* presenter = nullptr;
    DimeSession* session = nullptr;
    HostCtx ctx;
    DimeHost host = {};

    Fixture()
    {
        EnsureWindowClassesRegistered();
        saved = Global::defaultlFontHandle;
        font = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
        Global::defaultlFontHandle = font;

        static const DimeCandEnv env = { Dpi, Zero, Zero, Font, SetFont };
        static const DimeNotifyCallback callback = { nullptr, NotifyInvoke };
        static const DimePresenterCandSource source = { nullptr, CandInvoke, RangeCount, RangeChar };
        presenter = dime_ui_presenter_create(&env, &callback, &source);
        session = dime_session_new();
        Assert::IsNotNull(presenter);
        Assert::IsNotNull(session);
        ctx.presenter = presenter;
        ctx.session = session;
        host.vtbl = &Vtbl();
        host.ctx = &ctx;
        host.session = session;
        host.presenter = presenter;
    }
    ~Fixture()
    {
        if (presenter) dime_ui_presenter_free(presenter);
        if (session) dime_session_free(session);
        Global::defaultlFontHandle = saved;
        if (font) DeleteObject(font);
        ReleaseCapture();
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

void Convert(Fixture& f)
{
    Assert::AreEqual(static_cast<int32_t>(S_OK), dime_session_handle_composition_convert(&f.host, 0, 0));
}
} // namespace

namespace DIMEUnitTests
{
    TEST_CLASS(SessionHostUiTest)
    {
    public:
        TEST_METHOD(FactSizesMatch)
        {
            size_t notify = 0, open = 0;
            dime_host_fact_sizes(&notify, &open);
            Assert::AreEqual(sizeof(DimeNotifyShowFacts), notify);
            Assert::AreEqual(sizeof(DimeCandOpenFacts), open);
        }

        TEST_METHOD(ConvertFillsPagesWithoutLegacyUi)
        {
            Fixture f;
            f.ctx.nextFetch = 25;
            Convert(f);
            Assert::AreEqual(0, f.ctx.legacy);
            Assert::AreEqual(static_cast<int32_t>(25), dime_ui_presenter_cand_query(f.presenter, 3));
            uint32_t pages[8] = {};
            uint32_t pageCount = 0;
            Assert::AreEqual(static_cast<int32_t>(S_OK), dime_ui_presenter_cand_get_page_index(f.presenter, pages, 8, &pageCount));
            Assert::AreEqual(3u, pageCount);
            Assert::AreEqual(0u, pages[0]);
            Assert::AreEqual(10u, pages[1]);
            Assert::AreEqual(20u, pages[2]);
        }

        TEST_METHOD(FinalizeShowsSpecialCodeAndClearsBeforeUpdate)
        {
            Fixture f;
            f.ctx.nextFetch = 2;
            Convert(f);
            f.ctx.legacy = 0;
            f.ctx.arrayNotifySp = 1;
            Assert::AreEqual(static_cast<int32_t>(S_OK), dime_session_handle_candidate_finalize(&f.host));
            Assert::AreEqual(0, f.ctx.legacy);
            Assert::AreEqual(static_cast<int32_t>(1), dime_ui_presenter_notify_query(f.presenter, static_cast<uint32_t>(NOTIFY_TYPE::NOTIFY_SPECIAL_CODE), 0));
            Assert::AreEqual(static_cast<int32_t>(0), dime_ui_presenter_cand_query(f.presenter, 0));
            Assert::AreEqual(1, f.ctx.beginFalse);
            Assert::AreEqual(1, f.ctx.beginTrue);
            Assert::AreEqual(1, f.ctx.midCalls);
            Assert::AreEqual(1, f.ctx.midExists);
            Assert::AreEqual(0, f.ctx.midCount);
        }

        TEST_METHOD(PhraseOpensWithNullRangeAfterMode)
        {
            Fixture f;
            f.ctx.nextFetch = 2;
            Convert(f);
            f.ctx.legacy = 0;
            f.ctx.makePhrase = 1;
            f.ctx.assocCount = 1;
            Assert::AreEqual(static_cast<int32_t>(S_OK), dime_session_handle_candidate_finalize(&f.host));
            Assert::AreEqual(0, f.ctx.legacy);
            Assert::AreEqual(1, f.ctx.phraseOpen);
            Assert::AreEqual(static_cast<uint32_t>(CANDIDATE_MODE::CANDIDATE_PHRASE), f.ctx.modeAtPhraseOpen);
            Assert::AreEqual(static_cast<int32_t>(1), dime_ui_presenter_cand_query(f.presenter, 0));
        }

        TEST_METHOD(HideLeavesCandidateWindow)
        {
            Fixture f;
            f.ctx.nextFetch = 2;
            Convert(f);
            Assert::AreEqual(static_cast<int32_t>(1), dime_ui_presenter_cand_query(f.presenter, 0));
            Assert::AreEqual(static_cast<int32_t>(1), dime_ui_presenter_cand_query(f.presenter, 1));
            f.ctx.legacy = 0;
            f.ctx.nextFetch = 0;
            f.ctx.autoCompose = 1;
            Assert::AreEqual(static_cast<int32_t>(S_OK), dime_session_handle_composition_input_worker(&f.host));
            Assert::AreEqual(0, f.ctx.legacy);
            Assert::AreEqual(static_cast<int32_t>(1), dime_ui_presenter_cand_query(f.presenter, 0));
            Assert::AreEqual(static_cast<int32_t>(0), dime_ui_presenter_cand_query(f.presenter, 1));
        }

        TEST_METHOD(ArrowMovesSelectionBeforeSetSelection)
        {
            Fixture f;
            f.ctx.nextFetch = 2;
            Convert(f);
            f.ctx.legacy = 0;
            Assert::AreEqual(static_cast<int32_t>(S_OK),
                dime_session_handle_composition_arrow_key(&f.host, static_cast<uint32_t>(KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN)));
            Assert::AreEqual(0, f.ctx.legacy);
            Assert::AreEqual(1, f.ctx.arrowSelection);
        }
    };
}

#endif // DIME_RUST_AVAILABLE
