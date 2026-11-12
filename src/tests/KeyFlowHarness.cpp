// KeyFlowHarness.cpp - see KeyFlowHarness.h

#include "pch.h"
#include "TestAppData.h"
#include "KeyFlowHarness.h"

#include <thread>
#include <sstream>
#include <fstream>
#include <map>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include "../Globals.h"
#include "../DIME.h"
#include "../UIPresenter.h"
#include "WindowClassTestHelper.h"
#include "../Config.h"
#include "../CompositionProcessorEngine.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

// ---------------------------------------------------------------------------
// Key scripts
// ---------------------------------------------------------------------------

static bool KeyFromToken(const std::wstring& t, UINT& vk)
{
    static const std::map<std::wstring, UINT> named = {
        { L"Space", VK_SPACE }, { L"Enter", VK_RETURN }, { L"Esc", VK_ESCAPE }, { L"Back", VK_BACK },
        { L"Del", VK_DELETE }, { L"Tab", VK_TAB }, { L"Left", VK_LEFT }, { L"Right", VK_RIGHT },
        { L"Up", VK_UP }, { L"Down", VK_DOWN }, { L"Home", VK_HOME }, { L"End", VK_END },
        { L"PgUp", VK_PRIOR }, { L"PgDn", VK_NEXT },
        { L"`", VK_OEM_3 }, { L"-", VK_OEM_MINUS }, { L"=", VK_OEM_PLUS }, { L"[", VK_OEM_4 },
        { L"]", VK_OEM_6 }, { L"\\", VK_OEM_5 }, { L";", VK_OEM_1 }, { L"'", VK_OEM_7 },
        { L",", VK_OEM_COMMA }, { L".", VK_OEM_PERIOD }, { L"/", VK_OEM_2 },
    };
    auto it = named.find(t);
    if (it != named.end()) { vk = it->second; return true; }
    if (t.size() == 1 && t[0] >= L'a' && t[0] <= L'z') { vk = (UINT)towupper(t[0]); return true; }
    if (t.size() == 1 && t[0] >= L'0' && t[0] <= L'9') { vk = (UINT)t[0]; return true; }
    return false;
}

std::vector<KeyFlowKey> ParseKeyScript(const std::wstring& script)
{
    std::vector<KeyFlowKey> keys;
    std::wistringstream in(script);
    std::wstring token;
    while (in >> token)
    {
        KeyFlowKey k;
        k.label = token;
        std::wstring body = token;
        if (body.rfind(L"Shift+", 0) == 0 && body.size() > 6)
        {
            k.shift = true;
            body = body.substr(6);
            // "Shift+a,8,Back": more keys typed while Shift is held. A lone ","
            // is the comma key itself.
            if (body != L"," && body.find(L',') != std::wstring::npos)
            {
                std::vector<std::wstring> items;
                std::wstring item;
                std::wistringstream parts(body);
                while (std::getline(parts, item, L','))
                    items.push_back(item);
                body = items[0];
                for (size_t n = 1; n < items.size(); n++)
                {
                    UINT vk = 0;
                    if (!KeyFromToken(items[n], vk))
                        throw std::invalid_argument("unknown key token");
                    k.more.push_back(vk);
                }
            }
        }
        if (!KeyFromToken(body, k.vk))
            throw std::invalid_argument("unknown key token");
        keys.push_back(k);
    }
    return keys;
}

// ---------------------------------------------------------------------------
// Snapshot formatting
// ---------------------------------------------------------------------------

static std::wstring Quote(const std::wstring& s)
{
    std::wstring out = L"\"";
    for (wchar_t c : s)
    {
        if (c == L'"' || c == L'\\') { out += L'\\'; out += c; }
        else if (c < 0x20) { wchar_t buf[8]; swprintf_s(buf, L"\\x%02X", c); out += buf; }
        else out += c;
    }
    return out + L"\"";
}

std::wstring KeyFlowSnapshot::Format() const
{
    std::wstring s = eaten ? L"eaten" : L"passed";
    s += L" text=" + Quote(text);
    s += L" sel=" + std::to_wstring(selStart);
    if (selEnd != selStart) s += L"-" + std::to_wstring(selEnd);
    if (composing) s += L" comp=" + Quote(composition);
    static const wchar_t* modes[] = { L"none", L"original", L"phrase", L"incremental" };
    if (candidateMode != 0 || !candidates.empty())
    {
        s += L" cand=";
        s += (candidateMode >= 0 && candidateMode < 4) ? modes[candidateMode] : L"?";
        s += L"[";
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (i) s += L" ";
            if ((int)i == candidateSelection) s += L"*";
            s += candidates[i];
        }
        s += L"]";
    }
    return s;
}

// ---------------------------------------------------------------------------
// CKeyFlowTextStore: plain-text ITextStoreACP with one selection
// ---------------------------------------------------------------------------

class CKeyFlowTextStore : public ITextStoreACP
{
public:
    explicit CKeyFlowTextStore(HWND hwnd) : _ref(1), _hwnd(hwnd) {}

    const std::wstring& Text() const { return _text; }
    LONG SelStart() const { return _selStart; }
    LONG SelEnd() const { return _selEnd; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITextStoreACP) *ppv = static_cast<ITextStoreACP*>(this);
        if (!*ppv) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG r = InterlockedDecrement(&_ref);
        if (r == 0) delete this;
        return r;
    }

    // ITextStoreACP
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD) override
    {
        if (riid != IID_ITextStoreACPSink) return E_INVALIDARG;
        if (_sink) _sink->Release();
        _sink = nullptr;
        return punk->QueryInterface(IID_ITextStoreACPSink, (void**)&_sink);
    }
    STDMETHODIMP UnadviseSink(IUnknown*) override
    {
        if (_sink) { _sink->Release(); _sink = nullptr; }
        return S_OK;
    }
    STDMETHODIMP RequestLock(DWORD dwLockFlags, HRESULT* phrSession) override
    {
        if (!_sink || !phrSession) return E_UNEXPECTED;
        if (_lock)
        {
            // Already locked: queue asynchronous requests, refuse synchronous ones.
            if (dwLockFlags & TS_LF_SYNC) { *phrSession = TS_E_SYNCHRONOUS; return S_OK; }
            _pending.push_back(dwLockFlags & ~TS_LF_SYNC);
            *phrSession = TS_S_ASYNC;
            return S_OK;
        }
        _lock = dwLockFlags & TS_LF_READWRITE;
        *phrSession = _sink->OnLockGranted(_lock);
        _lock = 0;
        while (!_pending.empty())
        {
            DWORD f = _pending.front();
            _pending.erase(_pending.begin());
            _lock = f & TS_LF_READWRITE;
            _sink->OnLockGranted(_lock);
            _lock = 0;
        }
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* pdcs) override
    {
        if (!pdcs) return E_INVALIDARG;
        pdcs->dwDynamicFlags = 0;
        pdcs->dwStaticFlags = TS_SS_NOHIDDENTEXT;
        return S_OK;
    }
    STDMETHODIMP QueryInsert(LONG acpTestStart, LONG acpTestEnd, ULONG, LONG* pacpResultStart, LONG* pacpResultEnd) override
    {
        *pacpResultStart = acpTestStart;
        *pacpResultEnd = acpTestEnd;
        return S_OK;
    }
    STDMETHODIMP GetSelection(ULONG ulIndex, ULONG ulCount, TS_SELECTION_ACP* pSelection, ULONG* pcFetched) override
    {
        if (!_lock) return TS_E_NOLOCK;
        *pcFetched = 0;
        if (ulCount == 0) return S_OK;
        if (ulIndex != 0 && ulIndex != (ULONG)TS_DEFAULT_SELECTION) return E_INVALIDARG;
        pSelection[0].acpStart = _selStart;
        pSelection[0].acpEnd = _selEnd;
        pSelection[0].style.ase = TS_AE_END;
        pSelection[0].style.fInterimChar = FALSE;
        *pcFetched = 1;
        return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG ulCount, const TS_SELECTION_ACP* pSelection) override
    {
        if (!(_lock & TS_LF_READWRITE & ~TS_LF_READ)) return TS_E_NOLOCK;
        if (ulCount != 1) return E_INVALIDARG;
        _selStart = Clamp(pSelection[0].acpStart);
        _selEnd = Clamp(pSelection[0].acpEnd);
        return S_OK;
    }
    STDMETHODIMP GetText(LONG acpStart, LONG acpEnd, WCHAR* pchPlain, ULONG cchPlainReq, ULONG* pcchPlainRet,
                         TS_RUNINFO* prgRunInfo, ULONG cRunInfoReq, ULONG* pcRunInfoRet, LONG* pacpNext) override
    {
        if (!_lock) return TS_E_NOLOCK;
        LONG len = (LONG)_text.size();
        if (acpStart < 0 || acpStart > len) return TS_E_INVALIDPOS;
        if (acpEnd == -1 || acpEnd > len) acpEnd = len;
        ULONG n = (ULONG)(acpEnd - acpStart);
        if (n > cchPlainReq) n = cchPlainReq;
        if (pchPlain && n) memcpy(pchPlain, _text.data() + acpStart, n * sizeof(WCHAR));
        if (pcchPlainRet) *pcchPlainRet = n;
        if (pcRunInfoRet) *pcRunInfoRet = 0;
        if (prgRunInfo && cRunInfoReq && n)
        {
            prgRunInfo[0].uCount = n;
            prgRunInfo[0].type = TS_RT_PLAIN;
            if (pcRunInfoRet) *pcRunInfoRet = 1;
        }
        if (pacpNext) *pacpNext = acpStart + (LONG)n;
        return S_OK;
    }
    STDMETHODIMP SetText(DWORD, LONG acpStart, LONG acpEnd, const WCHAR* pchText, ULONG cch, TS_TEXTCHANGE* pChange) override
    {
        if (!(_lock & TS_LF_READWRITE & ~TS_LF_READ)) return TS_E_NOLOCK;
        Replace(acpStart, acpEnd, pchText, cch, pChange);
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* pfInsertable) override
    {
        if (pfInsertable) *pfInsertable = FALSE;
        return S_OK;
    }
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertTextAtSelection(DWORD dwFlags, const WCHAR* pchText, ULONG cch, LONG* pacpStart, LONG* pacpEnd, TS_TEXTCHANGE* pChange) override
    {
        if (!(_lock & TS_LF_READWRITE & ~TS_LF_READ)) return TS_E_NOLOCK;
        LONG start = (std::min)(_selStart, _selEnd), end = (std::max)(_selStart, _selEnd);
        if (dwFlags & TF_IAS_QUERYONLY)
        {
            if (pacpStart) *pacpStart = start;
            if (pacpEnd) *pacpEnd = end;
            return S_OK;
        }
        TS_TEXTCHANGE change;
        Replace(start, end, pchText, cch, &change);
        if (pChange) *pChange = change;
        if (pacpStart) *pacpStart = start;
        if (pacpEnd) *pacpEnd = start + (LONG)cch;
        _selStart = _selEnd = start + (LONG)cch;
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG, LONG, ULONG, const TS_ATTRID*, DWORD, LONG* pacpNext, BOOL* pfFound, LONG* plFoundOffset) override
    {
        if (pacpNext) *pacpNext = (LONG)_text.size();
        if (pfFound) *pfFound = FALSE;
        if (plFoundOffset) *plFoundOffset = 0;
        return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* pcFetched) override
    {
        if (pcFetched) *pcFetched = 0;
        return S_OK;
    }
    STDMETHODIMP GetEndACP(LONG* pacp) override
    {
        if (!_lock) return TS_E_NOLOCK;
        *pacp = (LONG)_text.size();
        return S_OK;
    }
    STDMETHODIMP GetActiveView(TsViewCookie* pvcView) override { *pvcView = 1; return S_OK; }
    STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG* pacp) override { *pacp = 0; return S_OK; }
    STDMETHODIMP GetTextExt(TsViewCookie, LONG acpStart, LONG, RECT* prc, BOOL* pfClipped) override
    {
        if (!_lock) return TS_E_NOLOCK;
        // A fixed caret position; 10px per character on one line.
        prc->left = 100 + acpStart * 10; prc->top = 100; prc->right = prc->left + 10; prc->bottom = 120;
        if (pfClipped) *pfClipped = FALSE;
        return S_OK;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT* prc) override
    {
        SetRect(prc, 0, 0, 800, 600);
        return S_OK;
    }
    STDMETHODIMP GetWnd(TsViewCookie, HWND* phwnd) override { *phwnd = _hwnd; return S_OK; }

private:
    ~CKeyFlowTextStore() { if (_sink) _sink->Release(); }

    LONG Clamp(LONG acp) const { return (std::max)(0L, (std::min)(acp, (LONG)_text.size())); }

    void Replace(LONG start, LONG end, const WCHAR* pch, ULONG cch, TS_TEXTCHANGE* pChange)
    {
        start = Clamp(start);
        end = Clamp(end);
        _text.replace(start, end - start, pch ? pch : L"", cch);
        LONG delta = (LONG)cch - (end - start);
        auto adjust = [&](LONG& p) { if (p >= end) p += delta; else if (p > start) p = start + (LONG)cch; };
        adjust(_selStart);
        adjust(_selEnd);
        if (pChange) { pChange->acpStart = start; pChange->acpOldEnd = end; pChange->acpNewEnd = start + (LONG)cch; }
    }

    LONG _ref;
    HWND _hwnd;
    ITextStoreACPSink* _sink = nullptr;
    DWORD _lock = 0;
    std::vector<DWORD> _pending;
    std::wstring _text;
    LONG _selStart = 0, _selEnd = 0;
};

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

namespace DIMEUnitTests
{
    // Resets CConfig's process-wide state between key-flow scenarios.
    class KeyFlowConfigAccess
    {
    public:
        static void Reset(IME_MODE mode)
        {
            CConfig::_imeMode = IME_MODE::IME_MODE_NONE;   // makes SetIMEMode rebuild the INI path
            CConfig::_configIMEMode = IME_MODE::IME_MODE_NONE;
            CConfig::_initTimeStamp = {};
            CConfig::ResetAllDefaults(mode);
            CConfig::SetLoadTableMode(FALSE);
            CConfig::SetAppPermissionSet(TRUE);            // no ACL changes in the temp profile
            // Process-wide flags set while parsing tables and never cleared; they change how
            // associated phrases are looked up, so every scenario starts from FALSE.
            Global::hasPhraseSection = FALSE;
            Global::hasCINPhraseSection = FALSE;
        }
    };
}

static void WriteUtf16File(const std::wstring& path, const std::wstring& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const wchar_t bom = 0xFEFF;
    f.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
    f.write(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
}

static void RemoveTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring p = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

static const wchar_t* ConfigFileName(IME_MODE mode)
{
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI: return L"DayiConfig.ini";
    case IME_MODE::IME_MODE_ARRAY: return L"ArrayConfig.ini";
    case IME_MODE::IME_MODE_PHONETIC: return L"PhoneConfig.ini";
    default: return L"GenericConfig.ini";
    }
}

static const GUID& ProfileGuid(IME_MODE mode)
{
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI: return Global::DIMEDayiGuidProfile;
    case IME_MODE::IME_MODE_ARRAY: return Global::DIMEArrayGuidProfile;
    case IME_MODE::IME_MODE_PHONETIC: return Global::DIMEPhoneticGuidProfile;
    default: return Global::DIMEGenericGuidProfile;
    }
}

namespace
{
    struct Session
    {
        ITfThreadMgr* threadMgr = nullptr;
        TfClientId clientId = TF_CLIENTID_NULL;
        ITfDocumentMgr* docMgr = nullptr;
        ITfContext* context = nullptr;
        CKeyFlowTextStore* store = nullptr;
        CDIME* dime = nullptr;
        HWND hwnd = nullptr;
    };

    KeyFlowSnapshot Snapshot(Session& s, bool eaten)
    {
        KeyFlowSnapshot snap;
        snap.eaten = eaten;
        snap.text = s.store->Text();
        snap.selStart = s.store->SelStart();
        snap.selEnd = s.store->SelEnd();

        ITfComposition* comp = s.dime->_TestComposition();
        if (comp)
        {
            ITfRange* range = nullptr;
            if (SUCCEEDED(comp->GetRange(&range)) && range)
            {
                ITfRangeACP* acp = nullptr;
                if (SUCCEEDED(range->QueryInterface(IID_ITfRangeACP, (void**)&acp)) && acp)
                {
                    LONG start = 0, len = 0;
                    if (SUCCEEDED(acp->GetExtent(&start, &len)) && start >= 0 && start + len <= (LONG)snap.text.size())
                    {
                        snap.composing = true;
                        snap.composition = snap.text.substr(start, len);
                    }
                    acp->Release();
                }
                range->Release();
            }
        }

        snap.candidateMode = (int)s.dime->_TestCandidateMode();
        CUIPresenter* ui = s.dime->_TestUIPresenter();
        if (ui)
        {
            UINT count = 0;
            ui->GetCount(&count);
            for (UINT i = 0; i < count; i++)
            {
                BSTR b = nullptr;
                if (SUCCEEDED(ui->GetString(i, &b)))
                {
                    snap.candidates.push_back(b ? std::wstring(b, SysStringLen(b)) : L"");
                    SysFreeString(b);
                }
            }
            if (count)
                snap.candidateSelection = ui->_GetCandidateSelection(); // -1 when nothing is selected
        }
        return snap;
    }

    // ---- crash capture: an access violation in the IME fails the test with a stack ----
    std::wstring g_crash;

    std::wstring DescribeStack(CONTEXT ctx)
    {
        HANDLE process = GetCurrentProcess(), thread = GetCurrentThread();
        static bool symInit = false;
        if (!symInit)
        {
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            symInit = SymInitialize(process, nullptr, TRUE) != FALSE;
        }
        STACKFRAME64 frame = {};
#ifdef _M_X64
        DWORD machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx.Rip; frame.AddrFrame.Offset = ctx.Rbp; frame.AddrStack.Offset = ctx.Rsp;
#else
        DWORD machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx.Eip; frame.AddrFrame.Offset = ctx.Ebp; frame.AddrStack.Offset = ctx.Esp;
#endif
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        std::wstring out;
        for (int i = 0; i < 16 && StackWalk64(machine, process, thread, &frame, &ctx, nullptr,
                 SymFunctionTableAccess64, SymGetModuleBase64, nullptr) && frame.AddrPC.Offset; i++)
        {
            char buf[sizeof(SYMBOL_INFO) + 256] = {};
            SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            DWORD64 disp = 0;
            IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
            DWORD lineDisp = 0;
            std::string name = SymFromAddr(process, frame.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
            std::string where;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line))
            {
                std::string file = line.FileName;
                where = " (" + file.substr(file.find_last_of("\\/") + 1) + ":" + std::to_string(line.LineNumber) + ")";
            }
            std::string entry = "\n    " + name + where;
            out += std::wstring(entry.begin(), entry.end());
        }
        return out;
    }

    int CrashFilter(EXCEPTION_POINTERS* ep)
    {
        wchar_t head[96];
        swprintf_s(head, L"exception 0x%08X at %p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        g_crash = head + DescribeStack(*ep->ContextRecord);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Calls OnTestKeyDown/OnKeyDown/... without C++ objects on this frame (SEH requirement).
    HRESULT CallKeyHandler(CDIME* dime, int which, ITfContext* ctx, WPARAM vk, LPARAM lp, BOOL* eaten)
    {
        __try
        {
            switch (which)
            {
            case 0: return dime->OnTestKeyDown(ctx, vk, lp, eaten);
            case 1: return dime->OnKeyDown(ctx, vk, lp, eaten);
            case 2: return dime->OnTestKeyUp(ctx, vk, lp, eaten);
            default: return dime->OnKeyUp(ctx, vk, lp, eaten);
            }
        }
        __except (CrashFilter(GetExceptionInformation()))
        {
            return E_UNEXPECTED;
        }
    }

    void SetKeyState(BYTE* ks, UINT vk, bool down)
    {
        ks[vk] = down ? 0x80 : 0;
    }

    // Deliver one physical key transition the way ITfKeystrokeMgr does for app keys.
    bool Deliver(Session& s, UINT vk, bool down, bool repeat = false)
    {
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LPARAM lp = 1 | ((LPARAM)scan << 16);
        if (!down) lp |= (1LL << 30) | (1LL << 31);
        else if (repeat) lp |= (1LL << 30);

        BOOL eaten = FALSE, eaten2 = FALSE;
        if (g_crash.empty()) CallKeyHandler(s.dime, down ? 0 : 2, s.context, vk, lp, &eaten);
        if (g_crash.empty() && eaten) CallKeyHandler(s.dime, down ? 1 : 3, s.context, vk, lp, &eaten2);
        return eaten != FALSE;
    }

    bool PressKey(Session& s, const KeyFlowKey& k, bool capsLock)
    {
        BYTE ks[256] = {};
        if (capsLock) ks[VK_CAPITAL] = 0x01;
        if (k.shift)
        {
            SetKeyState(ks, VK_SHIFT, true);
            SetKeyState(ks, VK_LSHIFT, true);
            SetKeyboardState(ks);
            Deliver(s, VK_SHIFT, true);
        }
        bool eaten = false;
        std::vector<UINT> vks(1, k.vk);
        vks.insert(vks.end(), k.more.begin(), k.more.end());
        for (UINT vk : vks)
        {
            SetKeyState(ks, vk, true);
            SetKeyboardState(ks);
            eaten = Deliver(s, vk, true);
            SetKeyState(ks, vk, false);
            SetKeyboardState(ks);
            Deliver(s, vk, false);
        }
        if (k.shift)
        {
            SetKeyState(ks, VK_SHIFT, false);
            SetKeyState(ks, VK_LSHIFT, false);
            SetKeyboardState(ks);
            Deliver(s, VK_SHIFT, false);
        }
        return eaten;
    }

    void RegisterWindowClassesOnce()
    {
        static bool done = false;
        if (done) return;
        done = true;
        EnsureWindowClassesRegistered(); // recovers the atoms when other tests registered first
    }
}

std::vector<std::wstring> RunKeyFlow(const KeyFlowOptions& options, const std::wstring& script)
{
    std::vector<KeyFlowKey> keys = ParseKeyScript(script);
    std::vector<std::wstring> lines;
    std::wstring error;

    std::thread worker([&]() {
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        HKL us = LoadKeyboardLayoutW(L"00000409", 0);
        HKL previousLayout = us ? ActivateKeyboardLayout(us, 0) : nullptr;

        // Private profile: %TEMP%\DIME_KeyFlow_<pid>_<tid>\{appdata,pf}
        WCHAR temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        std::wstring base = std::wstring(temp) + L"DIME_KeyFlow_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetCurrentThreadId());
        RemoveTree(base);
        std::wstring appData = base + L"\\appdata", pf = base + L"\\pf";
        CreateDirectoryW(base.c_str(), nullptr);
        CreateDirectoryW(appData.c_str(), nullptr);
        CreateDirectoryW((appData + L"\\DIME").c_str(), nullptr);
        CreateDirectoryW(pf.c_str(), nullptr);
        CreateDirectoryW((pf + L"\\DIME").c_str(), nullptr);
        for (const auto& f : options.appDataFiles)
        {
            std::wstring dst = appData + L"\\DIME\\" + f.first;
            if (!f.second.empty() && f.second[0] == L'@') CopyFileW(f.second.c_str() + 1, dst.c_str(), FALSE);
            else WriteUtf16File(dst, f.second);
        }
        std::wstring ini = L"[Config]\n";
        bool hasDoBeep = false;
        for (const auto& l : options.config) { ini += l + L"\n"; if (l.rfind(L"DoBeep ", 0) == 0) hasDoBeep = true; }
        if (!hasDoBeep) ini += L"DoBeep = 0\n";
        ini += L"AppPermissionSet = 1\n";
        WriteUtf16File(appData + L"\\DIME\\" + ConfigFileName(options.mode), ini);

        WCHAR savedPf[MAX_PATH] = {};
        DWORD hadPf = GetEnvironmentVariableW(L"ProgramW6432", savedPf, MAX_PATH);
        SetEnvironmentVariableW(L"ProgramW6432", pf.c_str());
        StringCchCopyW(Global::testAppDataOverride, MAX_PATH, appData.c_str());
        DIMEUnitTests::KeyFlowConfigAccess::Reset(options.mode);

        RegisterWindowClassesOnce();

        Session s;
        s.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"DIME key flow", WS_POPUP, -2000, -2000, 400, 200,
            nullptr, nullptr, nullptr, nullptr);

        HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&s.threadMgr);
        if (SUCCEEDED(hr)) hr = s.threadMgr->Activate(&s.clientId);
        if (SUCCEEDED(hr)) hr = s.threadMgr->CreateDocumentMgr(&s.docMgr);
        if (SUCCEEDED(hr))
        {
            s.store = new CKeyFlowTextStore(s.hwnd);
            TfEditCookie ecTextStore = 0;
            hr = s.docMgr->CreateContext(s.clientId, 0, static_cast<ITextStoreACP*>(s.store), &s.context, &ecTextStore);
        }
        if (SUCCEEDED(hr)) hr = s.docMgr->Push(s.context);
        if (SUCCEEDED(hr)) hr = s.threadMgr->SetFocus(s.docMgr);
        if (SUCCEEDED(hr))
        {
            s.dime = new CDIME();
            s.dime->AddRef();
            hr = s.dime->_TestActivate(s.threadMgr, s.clientId, MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), ProfileGuid(options.mode));
        }

        if (FAILED(hr))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"harness setup failed: hr=0x%08X", (unsigned)hr);
            error = buf;
        }
        else
        {
            lines.push_back(L"init -> " + Snapshot(s, false).Format());
            for (const KeyFlowKey& k : keys)
            {
                bool eaten = PressKey(s, k, options.capsLock);
                if (!g_crash.empty())
                {
                    lines.push_back(k.label + L" -> CRASH " + g_crash);
                    break;
                }
                lines.push_back(k.label + L" -> " + Snapshot(s, eaten).Format());
            }
        }

        BYTE ks[256] = {};
        SetKeyboardState(ks);
        if (s.dime && g_crash.empty())   // after a crash the IME state is not trustworthy: leak it
        {
            s.dime->Deactivate();
            s.dime->Release();
        }
        g_crash.clear();
        if (s.docMgr) { s.docMgr->Pop(TF_POPF_ALL); s.docMgr->Release(); }
        if (s.context) s.context->Release();
        if (s.store) s.store->Release();
        if (s.threadMgr) { s.threadMgr->Deactivate(); s.threadMgr->Release(); }
        if (s.hwnd) DestroyWindow(s.hwnd);

        DIMETestAppData::Restore();
        SetEnvironmentVariableW(L"ProgramW6432", hadPf ? savedPf : nullptr);
        DIMEUnitTests::KeyFlowConfigAccess::Reset(options.mode);
        RemoveTree(base);
        if (previousLayout) ActivateKeyboardLayout(previousLayout, 0);
        if (SUCCEEDED(hrCo)) CoUninitialize();
    });
    worker.join();

    if (!error.empty())
    {
        std::string narrow;
        for (wchar_t ch : error) narrow += (ch < 0x80) ? static_cast<char>(ch) : '?';
        throw std::runtime_error(narrow);
    }
    return lines;
}
