// Phase 4d: language-bar decisions. C++ and Rust run without compartment sinks
// (a sink would re-enter the public method and mix the two paths). Re-entry is
// covered by the Rust mock and by ThreadMgrTogglesChineseAndShape.
#include "pch.h"
#include "TestAppData.h"
#include "CppUnitTest.h"
#include <msctf.h>
#include <string>
#include <vector>
#include "../DIME.h"
#include "../CompositionProcessorEngine.h"
#include "../LanguageBar.h"
#include "../Config.h"
#include "../Compartment.h"
#include "../Globals.h"
#include "../Define.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#if defined(DIME_RUST_AVAILABLE)

namespace {

const GUID kIme = { 0x11111111, 0x1111, 0x1111, { 1, 2, 3, 4, 5, 6, 7, 8 } };
const GUID kWidth = { 0x22222222, 0x2222, 0x2222, { 1, 2, 3, 4, 5, 6, 7, 8 } };
const GUID kConfig = { 0x33333333, 0x3333, 0x3333, { 1, 2, 3, 4, 5, 6, 7, 8 } };
const GUID kHan = { 0x44444444, 0x4444, 0x4444, { 1, 2, 3, 4, 5, 6, 7, 8 } };
const GUID kUnknown = { 0x55555555, 0x5555, 0x5555, { 1, 2, 3, 4, 5, 6, 7, 8 } };

const uint16_t kChn[] = { 0x4E2D, 0x6587 };
const uint16_t kEng[] = { 0x82F1, 0x6578 };
const uint16_t kFull[] = { 0x5168, 0x5F62 };
const uint16_t kHalf[] = { 0x534A, 0x5F62 };
const uint16_t kSimpl[] = { 0x7C21 };
const uint16_t kTrad[] = { 0x7E41 };

struct Note
{
    std::vector<uint16_t> text;
    unsigned delay;
};

std::vector<Note> g_notes;

void OnNote(const wchar_t* text, unsigned len, unsigned delay)
{
    Note n;
    if (text && len)
        n.text.assign(text, text + len);
    n.delay = delay;
    g_notes.push_back(n);
}

struct Shot
{
    int hasConv;
    unsigned conv;
    int hasDub, dub, hasKey, key, hasIme, ime;
    int chinese, full, width, han;
    std::vector<Note> notes;
};

void RemoveTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            std::wstring child = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveTree(child);
            else
                DeleteFileW(child.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

int ReadBool(ITfThreadMgr* thread, TfClientId id, REFGUID guid, BOOL& flag)
{
    if (!thread) return 0;
    CCompartment box(thread, id, guid);
    HRESULT hr = box._GetCompartmentBOOL(flag);
    return FAILED(hr) ? 0 : 1;
}

// The installed DIME text service advises the real thread manager and rewrites
// these compartments. Side-by-side tests use this store so only the code under
// test runs. ThreadMgrTogglesChineseAndShape switches back to the real manager.
class FakeMgr;

class FakeComp : public ITfCompartment
{
public:
    FakeComp(FakeMgr* owner, REFGUID guid);
    ~FakeComp();
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP SetValue(TfClientId, const VARIANT* value);
    STDMETHODIMP GetValue(VARIANT* value);
private:
    FakeMgr* owner;
    GUID guid;
    LONG ref;
};

class FakeMgr : public ITfThreadMgr, public ITfCompartmentMgr
{
public:
    FakeMgr() : ref(1) {}
    struct Slot { GUID guid; LONG value; bool has; };
    std::vector<Slot> slots;

    Slot* Find(REFGUID guid)
    {
        for (size_t i = 0; i < slots.size(); i++)
            if (IsEqualGUID(slots[i].guid, guid))
                return &slots[i];
        return nullptr;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadMgr))
            *ppv = static_cast<ITfThreadMgr*>(this);
        else if (IsEqualIID(riid, IID_ITfCompartmentMgr))
            *ppv = static_cast<ITfCompartmentMgr*>(this);
        else
        {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        static_cast<ITfThreadMgr*>(this)->AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG n = InterlockedDecrement(&ref);
        if (n == 0) delete this;
        return n;
    }
    STDMETHODIMP Activate(TfClientId*) { return E_NOTIMPL; }
    STDMETHODIMP Deactivate() { return E_NOTIMPL; }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetFocus(ITfDocumentMgr** pp) { if (pp) *pp = nullptr; return E_FAIL; }
    STDMETHODIMP SetFocus(ITfDocumentMgr*) { return E_NOTIMPL; }
    STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP IsThreadFocus(BOOL* pf) { if (pf) *pf = FALSE; return S_OK; }
    STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetCompartment(REFGUID guid, ITfCompartment** pp)
    {
        if (!pp) return E_POINTER;
        if (!Find(guid))
        {
            Slot s;
            s.guid = guid;
            s.value = 0;
            s.has = false;
            slots.push_back(s);
        }
        *pp = new FakeComp(this, guid);
        return S_OK;
    }
    STDMETHODIMP ClearCompartment(TfClientId, REFGUID guid)
    {
        Slot* s = Find(guid);
        if (s) s->has = false;
        return S_OK;
    }
    STDMETHODIMP EnumCompartments(IEnumGUID** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
private:
    LONG ref;
};

FakeComp::FakeComp(FakeMgr* ownerIn, REFGUID guidIn) : owner(ownerIn), guid(guidIn), ref(1)
{
    static_cast<ITfThreadMgr*>(owner)->AddRef();
}
FakeComp::~FakeComp() { static_cast<ITfThreadMgr*>(owner)->Release(); }
STDMETHODIMP FakeComp::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompartment))
    {
        *ppv = static_cast<ITfCompartment*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) FakeComp::AddRef() { return InterlockedIncrement(&ref); }
STDMETHODIMP_(ULONG) FakeComp::Release()
{
    ULONG n = InterlockedDecrement(&ref);
    if (n == 0) delete this;
    return n;
}
STDMETHODIMP FakeComp::SetValue(TfClientId, const VARIANT* value)
{
    if (!value || value->vt != VT_I4) return E_INVALIDARG;
    FakeMgr::Slot* s = owner->Find(guid);
    if (!s) return E_FAIL;
    s->value = value->lVal;
    s->has = true;
    return S_OK;
}
STDMETHODIMP FakeComp::GetValue(VARIANT* value)
{
    if (!value) return E_POINTER;
    VariantInit(value);
    FakeMgr::Slot* s = owner->Find(guid);
    if (s && s->has)
    {
        value->vt = VT_I4;
        value->lVal = s->value;
    }
    return S_OK;
}

ITfThreadMgr* CreateFakeThreadMgr()
{
    return new FakeMgr();
}

} // namespace

class LangBarParityAccess
{
public:
    static void SetClient(CDIME& d, TfClientId id) { d._tfClientId = id; }
    static void SetConvBox(CDIME& d, CCompartment* c) { d._pCompartmentConversion = c; }
    static CCompartment* ConvBox(CDIME& d) { return d._pCompartmentConversion; }
    static void SetChinese(CDIME& d, BOOL v) { d._isChinese = v; }
    static BOOL Chinese(CDIME& d) { return d._isChinese; }
    static void SetFull(CDIME& d, BOOL v) { d._isFullShape = v; }
    static BOOL Full(CDIME& d) { return d._isFullShape; }

    static void ConvCpp(CDIME& d, ITfThreadMgr* t, BOOL* ov) { d._ConversionModeCompartmentUpdatedCpp(t, ov); }
    static void ConvRust(CDIME& d, ITfThreadMgr* t, BOOL* ov) { d._ConversionModeCompartmentUpdatedRust(t, ov); }
    static void ConvPub(CDIME& d, ITfThreadMgr* t, BOOL* ov) { d.ConversionModeCompartmentUpdated(t, ov); }
    static void PrivCpp(CDIME& d, ITfThreadMgr* t) { d._PrivateCompartmentsUpdatedCpp(t); }
    static void PrivRust(CDIME& d, ITfThreadMgr* t) { d._PrivateCompartmentsUpdatedRust(t); }
    static void PrivPub(CDIME& d, ITfThreadMgr* t) { d.PrivateCompartmentsUpdated(t); }
    static void KeyCpp(CDIME& d, ITfThreadMgr* t, REFGUID g) { d._KeyboardOpenCompartmentUpdatedCpp(t, g); }
    static void KeyRust(CDIME& d, ITfThreadMgr* t, REFGUID g) { d._KeyboardOpenCompartmentUpdatedRust(t, g); }
    static void OpenCpp(CDIME& d) { d._OnKeyboardOpenCpp(); }
    static void OpenRust(CDIME& d) { d._OnKeyboardOpenRust(); }
    static void ClosedCpp(CDIME& d) { d._OnKeyboardClosedCpp(); }
    static void ClosedRust(CDIME& d) { d._OnKeyboardClosedRust(); }
    static void FullCpp(CDIME& d) { d._OnSwitchedToFullShapeCpp(); }
    static void FullRust(CDIME& d) { d._OnSwitchedToFullShapeRust(); }
    static void HalfCpp(CDIME& d) { d._OnSwitchedToHalfShapeCpp(); }
    static void HalfRust(CDIME& d) { d._OnSwitchedToHalfShapeRust(); }
    static void ChnCpp(CDIME& d, BOOL on, UINT delay) { d._showChnEngNotifyCpp(on, delay); }
    static void ChnRust(CDIME& d, BOOL on, UINT delay) { d._showChnEngNotifyRust(on, delay); }
    static void ChnPub(CDIME& d, BOOL on, UINT delay) { d.showChnEngNotify(on, delay); }
    static void ShapeCpp(CDIME& d, BOOL on, UINT delay) { d._showFullHalfShapeNotifyCpp(on, delay); }
    static void ShapeRust(CDIME& d, BOOL on, UINT delay) { d._showFullHalfShapeNotifyRust(on, delay); }
    static void HanNCpp(CDIME& d, BOOL on, UINT delay) { d._showHanConvertNotifyCpp(on, delay); }
    static void HanNRust(CDIME& d, BOOL on, UINT delay) { d._showHanConvertNotifyRust(on, delay); }
    static void PresCpp(CCompositionProcessorEngine& e, REFGUID g, BOOL* eaten, ITfThreadMgr* t, TfClientId id)
    {
        e._OnPreservedKeyCpp(g, eaten, t, id);
    }
    static void PresRust(CCompositionProcessorEngine& e, REFGUID g, BOOL* eaten, ITfThreadMgr* t, TfClientId id)
    {
        e._OnPreservedKeyRust(g, eaten, t, id);
    }
    static void PresPub(CCompositionProcessorEngine& e, REFGUID g, BOOL* eaten, ITfThreadMgr* t, TfClientId id)
    {
        e.OnPreservedKey(g, eaten, t, id);
    }
    static void SetGuids(CCompositionProcessorEngine& e)
    {
        e._PreservedKey_IMEMode.Guid = kIme;
        e._PreservedKey_DoubleSingleByte.Guid = kWidth;
        e._PreservedKey_Config.Guid = kConfig;
        e._PreservedKey_HanConvert.Guid = kHan;
    }
    static void AddShiftOnly(CCompositionProcessorEngine& e)
    {
        TF_PRESERVEDKEY* k = e._PreservedKey_IMEMode.TSFPreservedKeyTable.Append();
        k->uVKey = VK_SHIFT;
        k->uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;
    }
    static CFile* Tcsc(CCompositionProcessorEngine& e) { return e._pTCSCTableDictionaryFile; }
    static CESCALLBACK Callback() { return CDIME::CompartmentCallback; }
};

namespace DIMEUnitTests
{

TEST_CLASS(LangBarParityTest)
{
    static ITfThreadMgr* tm;
    static ITfThreadMgr* store;
    static TfClientId client;
    static CDIME* dime;
    static CCompartment* conv;
    static bool comOwned;
    static BOOL savedWin8;
    static USHORT savedMods;
    static BOOL savedShiftOnly;
    static BOOL savedDesktop;
    static DOUBLE_SINGLE_BYTE_MODE savedWidth;
    static BOOL savedHan;
    static IME_SHIFT_MODE savedShift;
    static IME_MODE savedConfigMode;
    static std::wstring base;
    static WCHAR savedPf[MAX_PATH];
    static DWORD hadPf;

    static void EnsureDir(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }

    static Shot Take()
    {
        Shot s;
        s.hasConv = 0;
        s.conv = 0;
        if (conv)
        {
            DWORD mode = 0;
            HRESULT hr = conv->_GetCompartmentDWORD(mode);
            s.hasConv = FAILED(hr) ? 0 : 1;
            s.conv = mode;
        }
        BOOL flag = FALSE;
        s.hasDub = ReadBool(store, client, Global::DIMEGuidCompartmentDoubleSingleByte, flag);
        s.dub = flag ? 1 : 0;
        s.hasKey = ReadBool(store, client, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, flag);
        s.key = flag ? 1 : 0;
        s.hasIme = ReadBool(store, client, Global::DIMEGuidCompartmentIMEMode, flag);
        s.ime = flag ? 1 : 0;
        s.chinese = LangBarParityAccess::Chinese(*dime) ? 1 : 0;
        s.full = LangBarParityAccess::Full(*dime) ? 1 : 0;
        s.width = (int)CConfig::GetDoubleSingleByteMode();
        s.han = CConfig::GetDoHanConvert() ? 1 : 0;
        s.notes = g_notes;
        return s;
    }

    static void Apply(DWORD mode, BOOL dub, BOOL key, BOOL ime, BOOL chinese, BOOL full)
    {
        CCompartment cnv(store, client, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
        cnv._SetCompartmentDWORD(mode);
        CCompartment d(store, client, Global::DIMEGuidCompartmentDoubleSingleByte);
        d._SetCompartmentBOOL(dub);
        CCompartment k(store, client, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        k._SetCompartmentBOOL(key);
        CCompartment i(store, client, Global::DIMEGuidCompartmentIMEMode);
        i._SetCompartmentBOOL(ime);
        LangBarParityAccess::SetChinese(*dime, chinese);
        LangBarParityAccess::SetFull(*dime, full);
        g_notes.clear();
    }

    static void AssertSame(const Shot& a, const Shot& b)
    {
        Assert::AreEqual(a.hasConv, b.hasConv);
        Assert::AreEqual(a.conv, b.conv);
        Assert::AreEqual(a.hasDub, b.hasDub);
        Assert::AreEqual(a.dub, b.dub);
        Assert::AreEqual(a.hasKey, b.hasKey);
        Assert::AreEqual(a.key, b.key);
        Assert::AreEqual(a.hasIme, b.hasIme);
        Assert::AreEqual(a.ime, b.ime);
        Assert::AreEqual(a.chinese, b.chinese);
        Assert::AreEqual(a.full, b.full);
        Assert::AreEqual(a.width, b.width);
        Assert::AreEqual(a.han, b.han);
        Assert::AreEqual(a.notes.size(), b.notes.size());
        for (size_t i = 0; i < a.notes.size(); i++)
        {
            Assert::AreEqual(a.notes[i].delay, b.notes[i].delay);
            Assert::AreEqual(a.notes[i].text.size(), b.notes[i].text.size());
            for (size_t u = 0; u < a.notes[i].text.size(); u++)
                Assert::AreEqual(a.notes[i].text[u], b.notes[i].text[u]);
        }
    }

    static void AssertText(const Shot& s, size_t index, const uint16_t* text, size_t len, unsigned delay)
    {
        Assert::IsTrue(s.notes.size() > index);
        Assert::AreEqual(delay, s.notes[index].delay);
        Assert::AreEqual(len, s.notes[index].text.size());
        for (size_t i = 0; i < len; i++)
            Assert::AreEqual(text[i], s.notes[index].text[i]);
    }

    static void UseTemp()
    {
        std::wstring app = base + L"\\app";
        std::wstring pf = base + L"\\pf";
        EnsureDir(base);
        EnsureDir(app);
        EnsureDir(app + L"\\DIME");
        EnsureDir(pf);
        EnsureDir(pf + L"\\DIME");
        SetEnvironmentVariableW(L"ProgramW6432", pf.c_str());
        StringCchCopyW(Global::testAppDataOverride, MAX_PATH, app.c_str());
    }

    TEST_CLASS_INITIALIZE(ClassInit)
    {
        savedWin8 = Global::isWindows8;
        savedMods = Global::ModifiersValue;
        savedShiftOnly = Global::IsShiftKeyDownOnly;
        savedDesktop = CConfig::GetShowNotifyDesktop();
        savedWidth = CConfig::GetDoubleSingleByteMode();
        savedHan = CConfig::GetDoHanConvert();
        savedShift = CConfig::GetIMEShiftMode();
        savedConfigMode = CConfig::GetIMEMode();
        savedPf[0] = 0;
        hadPf = GetEnvironmentVariableW(L"ProgramW6432", savedPf, MAX_PATH);
        WCHAR temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        base = std::wstring(temp) + L"DIME_LB_" + std::to_wstring(GetCurrentProcessId());
        RemoveTree(base);
        UseTemp();
        if (savedConfigMode == IME_MODE::IME_MODE_NONE)
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
        else
            CConfig::SetIMEMode(IME_MODE::IME_MODE_NONE);
        CConfig::SetIMEMode(savedConfigMode);

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comOwned = (hr == S_OK);
        Assert::IsTrue(SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE);
        hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&tm);
        Assert::IsTrue(SUCCEEDED(hr) && tm);
        client = 0;
        Assert::IsTrue(SUCCEEDED(tm->Activate(&client)));
        dime = new CDIME();
        LangBarParityAccess::SetClient(*dime, client);
        store = CreateFakeThreadMgr();
        conv = new CCompartment(store, client, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
        LangBarParityAccess::SetConvBox(*dime, conv);
        g_dimeLangNotify = OnNote;
    }

    TEST_CLASS_CLEANUP(ClassCleanup)
    {
        g_dimeLangNotify = nullptr;
        g_notes.clear();
        if (dime)
        {
            LangBarParityAccess::SetConvBox(*dime, conv);
            dime->Release();
            dime = nullptr;
            conv = nullptr;
        }
        else if (conv)
        {
            delete conv;
            conv = nullptr;
        }
        if (store)
        {
            store->Release();
            store = nullptr;
        }
        if (tm)
        {
            tm->Deactivate();
            tm->Release();
            tm = nullptr;
        }
        if (comOwned)
            CoUninitialize();
        Global::isWindows8 = savedWin8;
        Global::ModifiersValue = savedMods;
        Global::IsShiftKeyDownOnly = savedShiftOnly;
        CConfig::SetShowNotifyDesktop(savedDesktop);
        CConfig::SetDoubleSingleByteMode(savedWidth);
        CConfig::SetDoHanConvert(savedHan);
        CConfig::SetIMEShiftMode(savedShift);
        DIMETestAppData::Restore();
        SetEnvironmentVariableW(L"ProgramW6432", hadPf ? savedPf : nullptr);
        if (CConfig::GetIMEMode() == savedConfigMode)
            CConfig::SetIMEMode(savedConfigMode == IME_MODE::IME_MODE_NONE ? IME_MODE::IME_MODE_DAYI : IME_MODE::IME_MODE_NONE);
        CConfig::SetIMEMode(savedConfigMode);
        RemoveTree(base);
    }

    TEST_METHOD_CLEANUP(EachCleanup)
    {
        g_notes.clear();
        Global::isWindows8 = savedWin8;
        Global::ModifiersValue = savedMods;
        Global::IsShiftKeyDownOnly = savedShiftOnly;
        CConfig::SetShowNotifyDesktop(savedDesktop);
        CConfig::SetDoubleSingleByteMode(savedWidth);
        CConfig::SetDoHanConvert(savedHan);
        CConfig::SetIMEShiftMode(savedShift);
        if (dime && LangBarParityAccess::ConvBox(*dime) != conv)
            LangBarParityAccess::SetConvBox(*dime, conv);
    }

    TEST_METHOD(NoConversionIsNoop)
    {
        Apply(TF_CONVERSIONMODE_FULLSHAPE, FALSE, TRUE, TRUE, TRUE, TRUE);
        LangBarParityAccess::SetConvBox(*dime, nullptr);
        LangBarParityAccess::ConvCpp(*dime, store, nullptr);
        Shot a = Take();
        Apply(TF_CONVERSIONMODE_FULLSHAPE, FALSE, TRUE, TRUE, TRUE, TRUE);
        LangBarParityAccess::SetConvBox(*dime, nullptr);
        LangBarParityAccess::ConvRust(*dime, store, nullptr);
        Shot b = Take();
        LangBarParityAccess::SetConvBox(*dime, conv);
        AssertSame(a, b);
        Assert::AreEqual(1, a.dub == 0 ? 1 : 0);
        Assert::AreEqual(1, a.chinese);
    }

    TEST_METHOD(ConversionSyncsDoubleAndLeavesKeyboardOnWin7)
    {
        Global::isWindows8 = FALSE;
        Apply(TF_CONVERSIONMODE_FULLSHAPE, FALSE, TRUE, FALSE, FALSE, FALSE);
        LangBarParityAccess::ConvCpp(*dime, store, nullptr);
        Shot a = Take();
        Apply(TF_CONVERSIONMODE_FULLSHAPE, FALSE, TRUE, FALSE, FALSE, FALSE);
        LangBarParityAccess::ConvRust(*dime, store, nullptr);
        AssertSame(a, Take());
        Assert::AreEqual(1, a.dub);
        Assert::AreEqual(1, a.key);
        Assert::AreEqual(0, a.ime);
        Assert::AreEqual(0, a.chinese);
    }

    TEST_METHOD(ConversionSyncsImeToNative)
    {
        Global::isWindows8 = TRUE;
        Apply(TF_CONVERSIONMODE_NATIVE, TRUE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::ConvCpp(*dime, store, nullptr);
        Shot a = Take();
        Apply(TF_CONVERSIONMODE_NATIVE, TRUE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::ConvRust(*dime, store, nullptr);
        AssertSame(a, Take());
        Assert::AreEqual(0, a.dub);
        Assert::AreEqual(1, a.key);
        Assert::AreEqual(1, a.ime);
    }

    TEST_METHOD(ConversionOverrideWritesOpenWithoutReading)
    {
        Global::isWindows8 = TRUE;
        BOOL on = TRUE;
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::ConvCpp(*dime, store, &on);
        Shot a = Take();
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::ConvRust(*dime, store, &on);
        AssertSame(a, Take());
        Assert::AreEqual(1, a.key);
        Assert::AreEqual(1, a.ime);

        Global::isWindows8 = FALSE;
        BOOL off = FALSE;
        Apply(0, FALSE, TRUE, TRUE, FALSE, FALSE);
        LangBarParityAccess::ConvCpp(*dime, store, &off);
        Shot c = Take();
        Apply(0, FALSE, TRUE, TRUE, FALSE, FALSE);
        LangBarParityAccess::ConvRust(*dime, store, &off);
        AssertSame(c, Take());
        Assert::AreEqual(1, c.key);
        Assert::AreEqual(0, c.ime);
    }

    TEST_METHOD(PrivateClearsFullShapeWhenSingle)
    {
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE);
        DWORD mode = TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE;
        Apply(mode, FALSE, FALSE, TRUE, TRUE, TRUE);
        LangBarParityAccess::PrivCpp(*dime, store);
        Shot a = Take();
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE);
        Apply(mode, FALSE, FALSE, TRUE, TRUE, TRUE);
        LangBarParityAccess::PrivRust(*dime, store);
        AssertSame(a, Take());
        Assert::AreEqual((unsigned)TF_CONVERSIONMODE_NATIVE, a.conv);
        Assert::AreEqual(0, a.full);
        Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE, a.width);
        Assert::AreEqual((size_t)0, a.notes.size());
    }

    TEST_METHOD(PrivateSetsFullShapeWhenDouble)
    {
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
        Apply(TF_CONVERSIONMODE_NATIVE, TRUE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PrivCpp(*dime, store);
        Shot a = Take();
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
        Apply(TF_CONVERSIONMODE_NATIVE, TRUE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PrivRust(*dime, store);
        AssertSame(a, Take());
        Assert::AreEqual((unsigned)(TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE), a.conv);
        Assert::AreEqual(1, a.full);
        Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE, a.width);
    }

    TEST_METHOD(PrivateMatchedAndFailedGet)
    {
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE);
        Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PrivCpp(*dime, store);
        Shot a = Take();
        Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PrivRust(*dime, store);
        AssertSame(a, Take());
        Assert::AreEqual((unsigned)TF_CONVERSIONMODE_NATIVE, a.conv);
        Assert::AreEqual(0, a.full);
        Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE, a.width);

        Apply(TF_CONVERSIONMODE_FULLSHAPE, FALSE, FALSE, FALSE, TRUE, TRUE);
        LangBarParityAccess::PrivCpp(*dime, nullptr);
        Shot c = Take();
        Apply(TF_CONVERSIONMODE_FULLSHAPE, FALSE, FALSE, FALSE, TRUE, TRUE);
        LangBarParityAccess::PrivRust(*dime, nullptr);
        AssertSame(c, Take());
        // Null thread: CCompartment reports success and a false double flag, so fullshape is cleared.
        Assert::AreEqual(0u, c.conv);
        Assert::AreEqual(0, c.full);
    }

    TEST_METHOD(KeyboardOpenSetsNativeAndChinese)
    {
        CConfig::SetShowNotifyDesktop(TRUE);
        Apply(0, FALSE, TRUE, FALSE, FALSE, FALSE);
        LangBarParityAccess::KeyCpp(*dime, store, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        Shot a = Take();
        Apply(0, FALSE, TRUE, FALSE, FALSE, FALSE);
        LangBarParityAccess::KeyRust(*dime, store, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        AssertSame(a, Take());
        Assert::AreEqual((unsigned)TF_CONVERSIONMODE_NATIVE, a.conv);
        Assert::AreEqual(1, a.chinese);
        AssertText(a, 0, kChn, 2, 0);
    }

    TEST_METHOD(KeyboardCloseClearsNativeWithoutDesktopNotify)
    {
        CConfig::SetShowNotifyDesktop(FALSE);
        Apply(TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE, FALSE, TRUE, FALSE, TRUE, FALSE);
        LangBarParityAccess::KeyCpp(*dime, store, Global::DIMEGuidCompartmentIMEMode);
        Shot a = Take();
        Apply(TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE, FALSE, TRUE, FALSE, TRUE, FALSE);
        LangBarParityAccess::KeyRust(*dime, store, Global::DIMEGuidCompartmentIMEMode);
        AssertSame(a, Take());
        Assert::AreEqual((unsigned)TF_CONVERSIONMODE_FULLSHAPE, a.conv);
        Assert::AreEqual(0, a.chinese);
        Assert::AreEqual((size_t)0, a.notes.size());
    }

    TEST_METHOD(KeyboardUnknownAndFailedGet)
    {
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::KeyCpp(*dime, store, kUnknown);
        Shot a = Take();
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::KeyRust(*dime, store, kUnknown);
        AssertSame(a, Take());
        Assert::AreEqual(0u, a.conv);
        Assert::AreEqual((size_t)0, a.notes.size());

        CConfig::SetShowNotifyDesktop(FALSE);
        Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, FALSE, TRUE, FALSE);
        LangBarParityAccess::KeyCpp(*dime, nullptr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        Shot c = Take();
        Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, FALSE, TRUE, FALSE);
        LangBarParityAccess::KeyRust(*dime, nullptr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        AssertSame(c, Take());
        // Null thread: the keyboard flag reads false, so an open native mode is closed.
        Assert::AreEqual(0u, c.conv);
        Assert::AreEqual(0, c.chinese);
        Assert::AreEqual((size_t)0, c.notes.size());
    }

    TEST_METHOD(ShapeSwitchSetsWidthUnlessShiftSpace)
    {
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::FullCpp(*dime);
        Shot a = Take();
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::FullRust(*dime);
        AssertSame(a, Take());
        Assert::AreEqual(1, a.full);
        Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE, a.width);

        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, TRUE);
        LangBarParityAccess::HalfCpp(*dime);
        Shot c = Take();
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, TRUE);
        LangBarParityAccess::HalfRust(*dime);
        AssertSame(c, Take());
        Assert::AreEqual(0, c.full);
        Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE, c.width);
    }

    TEST_METHOD(ShowGatesDesktopAndText)
    {
        CConfig::SetShowNotifyDesktop(FALSE);
        g_notes.clear();
        LangBarParityAccess::ChnCpp(*dime, TRUE, 4);
        Shot a = Take();
        g_notes.clear();
        LangBarParityAccess::ChnRust(*dime, TRUE, 4);
        AssertSame(a, Take());
        Assert::AreEqual((size_t)0, a.notes.size());

        CConfig::SetShowNotifyDesktop(TRUE);
        g_notes.clear();
        LangBarParityAccess::ChnCpp(*dime, FALSE, 9);
        Shot c = Take();
        g_notes.clear();
        LangBarParityAccess::ChnRust(*dime, FALSE, 9);
        AssertSame(c, Take());
        AssertText(c, 0, kEng, 2, 9);

        CConfig::SetShowNotifyDesktop(FALSE);
        g_notes.clear();
        LangBarParityAccess::ShapeCpp(*dime, TRUE, 1);
        LangBarParityAccess::HanNCpp(*dime, TRUE, 2);
        Shot e = Take();
        g_notes.clear();
        LangBarParityAccess::ShapeRust(*dime, TRUE, 1);
        LangBarParityAccess::HanNRust(*dime, TRUE, 2);
        AssertSame(e, Take());
        AssertText(e, 0, kFull, 2, 1);
        AssertText(e, 1, kSimpl, 1, 2);
    }

    TEST_METHOD(PreservedImeRejectsAndToggles)
    {
        CCompositionProcessorEngine eng(dime);
        LangBarParityAccess::SetGuids(eng);
        CConfig::SetShowNotifyDesktop(TRUE);
        CConfig::SetIMEShiftMode(IME_SHIFT_MODE::IME_NO_SHIFT);
        Apply(0, FALSE, FALSE, TRUE, TRUE, FALSE);
        BOOL eaten = TRUE;
        LangBarParityAccess::PresCpp(eng, kIme, &eaten, store, client);
        Shot a = Take();
        BOOL eatenR = TRUE;
        Apply(0, FALSE, FALSE, TRUE, TRUE, FALSE);
        LangBarParityAccess::PresRust(eng, kIme, &eatenR, store, client);
        AssertSame(a, Take());
        Assert::AreEqual(FALSE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual(1, a.ime);
        Assert::AreEqual((size_t)0, a.notes.size());

        CConfig::SetIMEShiftMode(IME_SHIFT_MODE::IME_RIGHT_SHIFT_ONLY);
        Global::ModifiersValue = TF_MOD_LSHIFT;
        Global::IsShiftKeyDownOnly = TRUE;
        eaten = TRUE;
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PresCpp(eng, kIme, &eaten, store, client);
        Shot c = Take();
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kIme, &eatenR, store, client);
        AssertSame(c, Take());
        Assert::AreEqual(FALSE, eaten);
        Assert::AreEqual(eaten, eatenR);

        CConfig::SetIMEShiftMode(IME_SHIFT_MODE::IME_BOTH_SHIFT);
        Global::ModifiersValue = TF_MOD_LSHIFT;
        Apply(0, FALSE, FALSE, TRUE, TRUE, FALSE);
        LangBarParityAccess::PresCpp(eng, kIme, &eaten, store, client);
        Shot e = Take();
        Apply(0, FALSE, FALSE, TRUE, TRUE, FALSE);
        LangBarParityAccess::PresRust(eng, kIme, &eatenR, store, client);
        AssertSame(e, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual(0, e.ime);
        Assert::AreEqual(1, e.chinese);
        AssertText(e, 0, kEng, 2, 0);

        Global::isWindows8 = TRUE;
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PresCpp(eng, kIme, &eaten, store, client);
        Shot w = Take();
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kIme, &eatenR, store, client);
        AssertSame(w, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(1, w.key);
        Assert::AreEqual(1, w.ime);
        AssertText(w, 0, kChn, 2, 0);
    }

    TEST_METHOD(PreservedShiftKeyOnlyRejects)
    {
        CCompositionProcessorEngine eng(dime);
        LangBarParityAccess::SetGuids(eng);
        LangBarParityAccess::AddShiftOnly(eng);
        CConfig::SetIMEShiftMode(IME_SHIFT_MODE::IME_BOTH_SHIFT);
        Global::IsShiftKeyDownOnly = FALSE;
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        BOOL eaten = TRUE;
        BOOL eatenR = TRUE;
        LangBarParityAccess::PresCpp(eng, kIme, &eaten, store, client);
        Shot a = Take();
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kIme, &eatenR, store, client);
        AssertSame(a, Take());
        Assert::AreEqual(FALSE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual(0, a.ime);
    }

    TEST_METHOD(PreservedWidthInjectsOrToggles)
    {
        CCompositionProcessorEngine eng(dime);
        LangBarParityAccess::SetGuids(eng);
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
        CConfig::SetShowNotifyDesktop(TRUE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        BOOL eaten = FALSE;
        BOOL eatenR = FALSE;
        LangBarParityAccess::PresCpp(eng, kWidth, &eaten, store, client);
        Shot a = Take();
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kWidth, &eatenR, store, client);
        AssertSame(a, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual(0, a.dub);
        Assert::AreEqual((size_t)0, a.notes.size());

        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE);
        Apply(0, TRUE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresCpp(eng, kWidth, &eaten, store, client);
        Shot c = Take();
        Apply(0, TRUE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kWidth, &eatenR, store, client);
        AssertSame(c, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(0, c.dub);
        Assert::AreEqual(0, c.full);
        AssertText(c, 0, kHalf, 2, 0);
    }

    TEST_METHOD(PreservedUnknownAndConfigWithoutServiceAreEaten)
    {
        CCompositionProcessorEngine eng(dime);
        LangBarParityAccess::SetGuids(eng);
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        BOOL eaten = FALSE;
        BOOL eatenR = FALSE;
        LangBarParityAccess::PresCpp(eng, kUnknown, &eaten, store, client);
        Shot a = Take();
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kUnknown, &eatenR, store, client);
        AssertSame(a, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual((size_t)0, a.notes.size());

        CCompositionProcessorEngine bare(nullptr);
        LangBarParityAccess::SetGuids(bare);
        eaten = FALSE;
        LangBarParityAccess::PresCpp(bare, kConfig, &eaten, store, client);
        Shot c = Take();
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresRust(bare, kConfig, &eatenR, store, client);
        AssertSame(c, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual((size_t)0, c.notes.size());
    }

    TEST_METHOD(PreservedHanClosedAndToggleOff)
    {
        CCompositionProcessorEngine eng(dime);
        LangBarParityAccess::SetGuids(eng);
        CConfig::SetDoHanConvert(TRUE);
        CConfig::SetShowNotifyDesktop(FALSE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        BOOL eaten = TRUE;
        BOOL eatenR = TRUE;
        LangBarParityAccess::PresCpp(eng, kHan, &eaten, store, client);
        Shot a = Take();
        CConfig::SetDoHanConvert(TRUE);
        Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kHan, &eatenR, store, client);
        AssertSame(a, Take());
        Assert::AreEqual(FALSE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual(1, a.han);

        CConfig::SetDoHanConvert(TRUE);
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PresCpp(eng, kHan, &eaten, store, client);
        Shot c = Take();
        CConfig::SetDoHanConvert(TRUE);
        Apply(0, FALSE, FALSE, TRUE, FALSE, FALSE);
        LangBarParityAccess::PresRust(eng, kHan, &eatenR, store, client);
        AssertSame(c, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(0, c.han);
        AssertText(c, 0, kTrad, 1, 0);
    }

    TEST_METHOD(PreservedHanEnableSetsUpTable)
    {
        CCompositionProcessorEngine a(dime);
        CCompositionProcessorEngine b(dime);
        LangBarParityAccess::SetGuids(a);
        LangBarParityAccess::SetGuids(b);
        CConfig::SetDoHanConvert(FALSE);
        Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, TRUE, FALSE, FALSE);
        BOOL eaten = FALSE;
        LangBarParityAccess::PresCpp(a, kHan, &eaten, store, client);
        Shot sa = Take();
        CConfig::SetDoHanConvert(FALSE);
        Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, TRUE, FALSE, FALSE);
        BOOL eatenR = FALSE;
        LangBarParityAccess::PresRust(b, kHan, &eatenR, store, client);
        AssertSame(sa, Take());
        Assert::AreEqual(TRUE, eaten);
        Assert::AreEqual(eaten, eatenR);
        Assert::AreEqual(1, sa.han);
        AssertText(sa, 0, kSimpl, 1, 0);
        Assert::IsTrue(LangBarParityAccess::Tcsc(a) != nullptr);
        Assert::IsTrue(LangBarParityAccess::Tcsc(b) != nullptr);
    }

    TEST_METHOD(PublicDispatchMatchesBuild)
    {
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE);
        DWORD mode = TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE;
        Apply(mode, FALSE, FALSE, TRUE, TRUE, TRUE);
        LangBarParityAccess::PrivPub(*dime, store);
        Shot pub = Take();
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE);
        Apply(mode, FALSE, FALSE, TRUE, TRUE, TRUE);
#if defined(DIME_USE_RUST_ENGINE)
        LangBarParityAccess::PrivRust(*dime, store);
#else
        LangBarParityAccess::PrivCpp(*dime, store);
#endif
        AssertSame(pub, Take());

        CConfig::SetShowNotifyDesktop(TRUE);
        g_notes.clear();
        LangBarParityAccess::ChnPub(*dime, TRUE, 3);
        Shot shown = Take();
        g_notes.clear();
#if defined(DIME_USE_RUST_ENGINE)
        LangBarParityAccess::ChnRust(*dime, TRUE, 3);
#else
        LangBarParityAccess::ChnCpp(*dime, TRUE, 3);
#endif
        AssertSame(shown, Take());
        AssertText(shown, 0, kChn, 2, 3);
    }

    struct RealStore
    {
        ITfThreadMgr* prevStore;
        CCompartment* prevConv;
        RealStore()
        {
            prevStore = store;
            prevConv = conv;
            store = tm;
            conv = new CCompartment(tm, client, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
            LangBarParityAccess::SetConvBox(*dime, conv);
        }
        ~RealStore()
        {
            CCompartment* real = conv;
            conv = prevConv;
            store = prevStore;
            if (dime)
                LangBarParityAccess::SetConvBox(*dime, conv);
            delete real;
        }
    };

    TEST_METHOD(ThreadMgrTogglesChineseAndShape)
    {
        RealStore real;
        (void)real;
        Global::isWindows8 = FALSE;
        CConfig::SetShowNotifyDesktop(TRUE);
        CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE);
        struct Sinks
        {
            CCompartmentEventSink* ime;
            CCompartmentEventSink* convs;
            CCompartmentEventSink* dub;
            Sinks(CDIME* d, ITfThreadMgr* t)
            {
                ime = new CCompartmentEventSink(LangBarParityAccess::Callback(), d);
                convs = new CCompartmentEventSink(LangBarParityAccess::Callback(), d);
                dub = new CCompartmentEventSink(LangBarParityAccess::Callback(), d);
                ime->_Advise(t, Global::DIMEGuidCompartmentIMEMode);
                convs->_Advise(t, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
                dub->_Advise(t, Global::DIMEGuidCompartmentDoubleSingleByte);
            }
            ~Sinks()
            {
                ime->_Unadvise();
                convs->_Unadvise();
                dub->_Unadvise();
                delete ime;
                delete convs;
                delete dub;
            }
        };

        {
            Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, TRUE, TRUE, FALSE);
            Sinks sinks(dime, store);
            g_notes.clear();
            CCompartment ime(store, client, Global::DIMEGuidCompartmentIMEMode);
            ime._SetCompartmentBOOL(FALSE);
            Shot closed = Take();
            Assert::AreEqual(0, closed.ime);
            Assert::AreEqual(0u, closed.conv);
            Assert::AreEqual(0, closed.chinese);
            AssertText(closed, 0, kEng, 2, 0);
        }

        {
            CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE);
            Apply(TF_CONVERSIONMODE_NATIVE, FALSE, FALSE, TRUE, TRUE, FALSE);
            Sinks sinks(dime, store);
            g_notes.clear();
            CCompositionProcessorEngine eng(dime);
            LangBarParityAccess::SetGuids(eng);
            BOOL eaten = FALSE;
            LangBarParityAccess::PresPub(eng, kWidth, &eaten, store, client);
            Shot wide = Take();
            Assert::AreEqual(TRUE, eaten);
            Assert::AreEqual(1, wide.dub);
            Assert::AreEqual((unsigned)(TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE), wide.conv);
            Assert::AreEqual(1, wide.full);
            Assert::AreEqual(1, wide.chinese);
            Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE, wide.width);
            AssertText(wide, 0, kFull, 2, 0);
        }

        {
            CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
            Apply(0, FALSE, FALSE, FALSE, FALSE, FALSE);
            Sinks sinks(dime, store);
            g_notes.clear();
            CCompartment dub(store, client, Global::DIMEGuidCompartmentDoubleSingleByte);
            dub._SetCompartmentBOOL(TRUE);
            Shot forced = Take();
            Assert::AreEqual(1, forced.dub);
            Assert::AreEqual((unsigned)TF_CONVERSIONMODE_FULLSHAPE, forced.conv);
            Assert::AreEqual(1, forced.full);
            Assert::AreEqual((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE, forced.width);
            Assert::AreEqual((size_t)0, forced.notes.size());
        }
    }
};

ITfThreadMgr* LangBarParityTest::tm = nullptr;
ITfThreadMgr* LangBarParityTest::store = nullptr;
TfClientId LangBarParityTest::client = 0;
CDIME* LangBarParityTest::dime = nullptr;
CCompartment* LangBarParityTest::conv = nullptr;
bool LangBarParityTest::comOwned = false;
BOOL LangBarParityTest::savedWin8 = FALSE;
USHORT LangBarParityTest::savedMods = 0;
BOOL LangBarParityTest::savedShiftOnly = FALSE;
BOOL LangBarParityTest::savedDesktop = FALSE;
DOUBLE_SINGLE_BYTE_MODE LangBarParityTest::savedWidth = DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE;
BOOL LangBarParityTest::savedHan = FALSE;
IME_SHIFT_MODE LangBarParityTest::savedShift = IME_SHIFT_MODE::IME_BOTH_SHIFT;
IME_MODE LangBarParityTest::savedConfigMode = IME_MODE::IME_MODE_NONE;
std::wstring LangBarParityTest::base;
WCHAR LangBarParityTest::savedPf[MAX_PATH] = {};
DWORD LangBarParityTest::hadPf = 0;

} // namespace DIMEUnitTests

#endif
