// Phase 4c: C++ and Rust dictionary setup, keystroke table, candidate range,
// traditional/simplified and frequency tables, and the preserved-key list.
#include "pch.h"
#include "TestAppData.h"
#include "CppUnitTest.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <msctf.h>
#include "../CompositionProcessorEngine.h"
#include "../Config.h"
#include "../File.h"
#include "../TableDictionaryEngine.h"
#if defined(DIME_RUST_AVAILABLE)
#include "dime_ffi.h"
#endif

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#if defined(DIME_RUST_AVAILABLE)

namespace {

const LCID kLocale = MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT);
const uint64_t kOld = 1700000000ULL;
const uint64_t kNew = 1700000100ULL;

std::wstring Cin(const wchar_t* sel, const wchar_t* endkey)
{
    std::wstring s = L"%gen_inp\n%selkey\t";
    s += sel;
    s += L"\n";
    if (endkey && endkey[0])
    {
        s += L"%endkey\t";
        s += endkey;
        s += L"\n";
    }
    s += L"%keyname begin\na\tA\n%keyname end\n%chardef begin\na\tZ\n%chardef end\n";
    return s;
}

void EnsureDir(const std::wstring& path)
{
    CreateDirectoryW(path.c_str(), nullptr);
}

void WriteUtf16(const std::wstring& path, const std::wstring& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    Assert::IsTrue(f.good(), path.c_str());
    const wchar_t bom = 0xFEFF;
    f.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
    f.write(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    Assert::IsTrue(f.good(), path.c_str());
}

void SetMtime(const std::wstring& path, uint64_t unixSeconds)
{
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Assert::IsTrue(h != INVALID_HANDLE_VALUE, path.c_str());
    uint64_t ticks = (unixSeconds + 11644473600ULL) * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)ticks;
    ft.dwHighDateTime = (DWORD)(ticks >> 32);
    Assert::IsTrue(SetFileTime(h, nullptr, nullptr, &ft) != FALSE);
    CloseHandle(h);
}

bool Exists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<unsigned char> ReadBytes(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void RemoveTree(const std::wstring& dir)
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

struct Tree
{
    std::wstring app;
    std::wstring pf;
    std::wstring Dime() const { return app + L"\\DIME"; }
    std::wstring PfDime() const { return pf + L"\\DIME"; }
};

struct EnvGuard
{
    WCHAR savedPf[MAX_PATH];
    DWORD hadPf;
    IME_MODE savedGlobal;
    IME_MODE savedConfig;
    ARRAY_SCOPE savedScope;
    BOOL savedSpace;
    BOOL savedAuto;
    UINT savedMax;
    DAYI_SELKEY_MODE savedSel;
    BOOL savedHan;
    BOOL savedBig5;
    BOOL savedPerm;
    WCHAR savedImeDesc[50];
    WCHAR savedWidthDesc[50];
    std::wstring base;

    EnvGuard()
    {
        savedPf[0] = 0;
        hadPf = GetEnvironmentVariableW(L"ProgramW6432", savedPf, MAX_PATH);
        savedGlobal = Global::imeMode;
        savedConfig = CConfig::GetIMEMode();
        savedScope = CConfig::GetArrayScope();
        savedSpace = CConfig::GetSpaceAsFirstCaniSelkey();
        savedAuto = CConfig::GetAutoCompose();
        savedMax = CConfig::GetMaxCodes();
        savedSel = CConfig::GetDayiSelkeyMode();
        savedHan = CConfig::GetDoHanConvert();
        savedBig5 = CConfig::GetBig5Filter();
        savedPerm = CConfig::GetAppPermissionSet();
        StringCchCopyW(savedImeDesc, 50, Global::ImeModeDescription);
        StringCchCopyW(savedWidthDesc, 50, Global::DoubleSingleByteDescription);
        CConfig::SetAppPermissionSet(TRUE);

        WCHAR temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        base = std::wstring(temp) + L"DIME_ES_" + std::to_wstring(GetCurrentProcessId());
        RemoveTree(base);
        EnsureDir(base);
    }

    ~EnvGuard()
    {
        DIMETestAppData::Restore();
        SetEnvironmentVariableW(L"ProgramW6432", hadPf ? savedPf : nullptr);
        IME_MODE cur = CConfig::GetIMEMode();
        if (cur == savedConfig)
            CConfig::SetIMEMode(savedConfig == IME_MODE::IME_MODE_NONE ? IME_MODE::IME_MODE_DAYI : IME_MODE::IME_MODE_NONE);
        CConfig::SetIMEMode(savedConfig);
        CConfig::SetArrayScope(savedScope);
        CConfig::SetSpaceAsFirstCaniSelkey(savedSpace);
        CConfig::SetAutoCompose(savedAuto);
        CConfig::SetMaxCodes(savedMax);
        CConfig::SetDayiSelkeyMode(savedSel);
        CConfig::SetDoHanConvert(savedHan);
        CConfig::SetBig5Filter(savedBig5);
        CConfig::SetAppPermissionSet(savedPerm);
        StringCchCopyW(Global::ImeModeDescription, 50, savedImeDesc);
        StringCchCopyW(Global::DoubleSingleByteDescription, 50, savedWidthDesc);
        Global::imeMode = savedGlobal;
        RemoveTree(base);
    }

    Tree Make(const wchar_t* name) const
    {
        Tree t;
        std::wstring root = base + L"\\" + name;
        t.app = root + L"\\app";
        t.pf = root + L"\\pf";
        EnsureDir(root);
        EnsureDir(t.app);
        EnsureDir(t.Dime());
        EnsureDir(t.pf);
        EnsureDir(t.PfDime());
        EnsureDir(t.pf + L"\\Windows NT");
        EnsureDir(t.pf + L"\\Windows NT\\TableTextService");
        return t;
    }

    void Use(const Tree& t) const
    {
        SetEnvironmentVariableW(L"ProgramW6432", t.pf.c_str());
        StringCchCopyW(Global::testAppDataOverride, MAX_PATH, t.app.c_str());
        IME_MODE cur = CConfig::GetIMEMode();
        CConfig::SetIMEMode(cur == IME_MODE::IME_MODE_NONE ? IME_MODE::IME_MODE_DAYI : IME_MODE::IME_MODE_NONE);
        CConfig::SetIMEMode(cur);
    }
};

void SeedPhrase(const Tree& t)
{
    WriteUtf16(t.Dime() + L"\\Phrase.cin", Cin(L"1234567890", L""));
}

} // namespace

class EngineSetupParityAccess
{
public:
    static BOOL DictCpp(CCompositionProcessorEngine& e, IME_MODE m) { return e._SetupDictionaryFileCpp(m, kLocale); }
    static BOOL DictRust(CCompositionProcessorEngine& e, IME_MODE m) { return e._SetupDictionaryFileRust(m, kLocale); }
    static void KeyCpp(CCompositionProcessorEngine& e, IME_MODE m) { e._SetupKeystrokeCpp(m); }
    static void KeyRust(CCompositionProcessorEngine& e, IME_MODE m) { e._SetupKeystrokeRust(m); }
    static void RangeCpp(CCompositionProcessorEngine& e, IME_MODE m) { e._SetupCandidateListRangeCpp(m); }
    static void RangeRust(CCompositionProcessorEngine& e, IME_MODE m) { e._SetupCandidateListRangeRust(m); }
    static BOOL HanCpp(CCompositionProcessorEngine& e) { return e._SetupHanCovertTableCpp(); }
    static BOOL HanRust(CCompositionProcessorEngine& e) { return e._SetupHanCovertTableRust(); }
    static BOOL FreqCpp(CCompositionProcessorEngine& e) { return e._SetupTCFreqTableCpp(); }
    static BOOL FreqRust(CCompositionProcessorEngine& e) { return e._SetupTCFreqTableRust(); }
    static void PreservedCpp(CCompositionProcessorEngine& e) { e._SetupPreservedCpp(nullptr, 0); }
    static void PreservedRust(CCompositionProcessorEngine& e) { e._SetupPreservedRust(nullptr, 0); }

    static CFile* MainFile(CCompositionProcessorEngine& e, IME_MODE m) { return e._pTableDictionaryFile[(UINT)m]; }
    static CTableDictionaryEngine* MainEngine(CCompositionProcessorEngine& e, IME_MODE m) { return e._pTableDictionaryEngine[(UINT)m]; }
    static CFile* CustomFile(CCompositionProcessorEngine& e, IME_MODE m) { return e._pCustomTableDictionaryFile[(UINT)m]; }
    static CTableDictionaryEngine* PhraseEngine(CCompositionProcessorEngine& e) { return e._pPhraseTableDictionaryEngine; }
    static CFile* ExtBFile(CCompositionProcessorEngine& e) { return e._pArrayExtBDictionaryFile; }
    static CTableDictionaryEngine* ExtBEngine(CCompositionProcessorEngine& e) { return e._pArrayExtBTableDictionaryEngine; }
    static CTableDictionaryEngine* ExtCDEngine(CCompositionProcessorEngine& e) { return e._pArrayExtCDTableDictionaryEngine; }
    static CTableDictionaryEngine* ExtEEngine(CCompositionProcessorEngine& e) { return e._pArrayExtETableDictionaryEngine; }
    static CTableDictionaryEngine* ArrayPhraseEngine(CCompositionProcessorEngine& e) { return e._pArrayPhraseTableDictionaryEngine; }
    static CTableDictionaryEngine* SpecialEngine(CCompositionProcessorEngine& e) { return e._pArraySpecialCodeTableDictionaryEngine; }
    static CTableDictionaryEngine* ShortEngine(CCompositionProcessorEngine& e) { return e._pArrayShortCodeTableDictionaryEngine; }
    static CTableDictionaryEngine* Big5(CCompositionProcessorEngine& e, IME_MODE m) { return e._pBig5TableDictionaryEngine[(UINT)m]; }
    static CFile* TcscFile(CCompositionProcessorEngine& e) { return e._pTCSCTableDictionaryFile; }
    static CTableDictionaryEngine* TcscEngine(CCompositionProcessorEngine& e) { return e._pTCSCTableDictionaryEngine; }
    static CFile* FreqFile(CCompositionProcessorEngine& e) { return e._pTCFreqTableDictionaryFile; }
    static UINT KeyCount(CCompositionProcessorEngine& e) { return e._KeystrokeComposition.Count(); }
    static const _KEYSTROKE* KeyAt(CCompositionProcessorEngine& e, UINT i) { return e._KeystrokeComposition.GetAt(i); }
    static CCandidateRange& Cand(CCompositionProcessorEngine& e) { return e._candidateListIndexRange; }
    static CCandidateRange& PhraseRange(CCompositionProcessorEngine& e) { return e._phraseCandidateListIndexRange; }
    static bool HasEqual(CCompositionProcessorEngine& e, IME_MODE m)
    {
        _T_RadicalMap* map = e._pTableDictionaryEngine[(UINT)m] ? e._pTableDictionaryEngine[(UINT)m]->GetRadicalMap() : nullptr;
        return map && map->find(L'=') != map->end();
    }

    struct PreservedSnap
    {
        UINT count;
        UINT vkey;
        UINT modifiers;
        std::wstring description;
    };

    static PreservedSnap Preserved(CCompositionProcessorEngine& e, int which)
    {
        CCompositionProcessorEngine::XPreservedKey* k = &e._PreservedKey_IMEMode;
        if (which == 1) k = &e._PreservedKey_DoubleSingleByte;
        else if (which == 2) k = &e._PreservedKey_Config;
        else if (which == 3) k = &e._PreservedKey_HanConvert;
        PreservedSnap s;
        s.count = k->TSFPreservedKeyTable.Count();
        s.vkey = s.count ? k->TSFPreservedKeyTable.GetAt(0)->uVKey : 0;
        s.modifiers = s.count ? k->TSFPreservedKeyTable.GetAt(0)->uModifiers : 0;
        s.description = k->Description ? k->Description : L"";
        return s;
    }
};

namespace {
std::wstring FileName(CFile* f)
{
    return (f && f->GetFileName()) ? f->GetFileName() : L"";
}

std::wstring Selkey(CCompositionProcessorEngine& e, IME_MODE m)
{
    CTableDictionaryEngine* t = EngineSetupParityAccess::MainEngine(e, m);
    return (t && t->GetSelkey()) ? t->GetSelkey() : L"";
}

int DictType(CTableDictionaryEngine* e)
{
    return e ? (int)e->GetDictionaryType() : -1;
}

void AssertSameKeys(CCompositionProcessorEngine& a, CCompositionProcessorEngine& b)
{
    UINT n = EngineSetupParityAccess::KeyCount(a);
    Assert::AreEqual(n, EngineSetupParityAccess::KeyCount(b), L"keystroke count");
    for (UINT i = 0; i < n; i++)
    {
        const _KEYSTROKE* x = EngineSetupParityAccess::KeyAt(a, i);
        const _KEYSTROKE* y = EngineSetupParityAccess::KeyAt(b, i);
        wchar_t msg[48];
        swprintf_s(msg, L"key %u", i);
        Assert::AreEqual(x->Index, y->Index, msg);
        Assert::AreEqual((UINT)x->Printable, (UINT)y->Printable, msg);
        Assert::AreEqual((UINT)x->CandIndex, (UINT)y->CandIndex, msg);
        Assert::AreEqual(x->VirtualKey, y->VirtualKey, msg);
        Assert::AreEqual(x->Modifiers, y->Modifiers, msg);
        Assert::AreEqual((int)x->Function, (int)y->Function, msg);
    }
}

void AssertSameRange(CCandidateRange& a, CCandidateRange& b, const wchar_t* label)
{
    Assert::AreEqual(a.Count(), b.Count(), label);
    for (int i = 0; i < a.Count(); i++)
    {
        wchar_t msg[64];
        swprintf_s(msg, L"%s %d", label, i);
        Assert::AreEqual(a.GetAt(i)->Index, b.GetAt(i)->Index, msg);
        Assert::AreEqual((UINT)a.GetAt(i)->Printable, (UINT)b.GetAt(i)->Printable, msg);
        Assert::AreEqual((UINT)a.GetAt(i)->CandIndex, (UINT)b.GetAt(i)->CandIndex, msg);
        Assert::AreEqual(a.GetAt(i)->VirtualKey, b.GetAt(i)->VirtualKey, msg);
        Assert::AreEqual(a.GetAt(i)->Modifiers, b.GetAt(i)->Modifiers, msg);
    }
}

std::wstring Trunc(const wchar_t* s)
{
    std::wstring w(s);
    if (!w.empty()) w.pop_back();
    return w;
}

} // namespace

namespace DIMEUnitTests
{
    TEST_CLASS(EngineSetupParityTest)
    {
        static void LoadBoth(EnvGuard& g, const Tree& cppTree, const Tree& rustTree, IME_MODE mode,
            CCompositionProcessorEngine& cpp, CCompositionProcessorEngine& rust, BOOL* cppRet, BOOL* rustRet)
        {
            Global::imeMode = mode;
            g.Use(cppTree);
            *cppRet = EngineSetupParityAccess::DictCpp(cpp, mode);
            g.Use(rustTree);
            *rustRet = EngineSetupParityAccess::DictRust(rust, mode);
        }

        static void SeedArrayCopy(const Tree& t, bool appNewer)
        {
            WriteUtf16(t.Dime() + L"\\Array.cin", Cin(L"1234567890", L""));
            WriteUtf16(t.PfDime() + L"\\Array.cin", Cin(L"0987654321", L""));
            SetMtime(t.Dime() + L"\\Array.cin", appNewer ? kNew : kOld);
            SetMtime(t.PfDime() + L"\\Array.cin", appNewer ? kOld : kNew);
            SeedPhrase(t);
        }

    public:
        TEST_METHOD(DayiDoesNotCopyProgramFiles)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->PfDime() + L"\\Dayi.cin", Cin(L"1234567890", L""));
                WriteUtf16(t->Dime() + L"\\TableTextServiceDaYi.txt", L"[Text]\r\n\"a\"=\"A\"\r\n");
            }
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_DAYI, cpp, rust, &cr, &rr);
            Assert::AreEqual((int)cr, (int)rr, L"return");
            Assert::IsTrue(cr != FALSE);
            Assert::IsFalse(Exists(ct.Dime() + L"\\Dayi.cin"));
            Assert::IsFalse(Exists(rt.Dime() + L"\\Dayi.cin"));
            Assert::AreEqual(FileName(EngineSetupParityAccess::MainFile(cpp, IME_MODE::IME_MODE_DAYI)),
                ct.Dime() + L"\\TableTextServiceDaYi.txt");
            Assert::AreEqual(FileName(EngineSetupParityAccess::MainFile(rust, IME_MODE::IME_MODE_DAYI)),
                rt.Dime() + L"\\TableTextServiceDaYi.txt");
            Assert::AreEqual((int)DICTIONARY_TYPE::TTS_DICTIONARY, DictType(EngineSetupParityAccess::MainEngine(cpp, IME_MODE::IME_MODE_DAYI)));
            Assert::AreEqual((int)DICTIONARY_TYPE::TTS_DICTIONARY, DictType(EngineSetupParityAccess::MainEngine(rust, IME_MODE::IME_MODE_DAYI)));
            Assert::IsTrue(EngineSetupParityAccess::PhraseEngine(cpp) == EngineSetupParityAccess::MainEngine(cpp, IME_MODE::IME_MODE_DAYI));
            Assert::IsTrue(EngineSetupParityAccess::PhraseEngine(rust) == EngineSetupParityAccess::MainEngine(rust, IME_MODE::IME_MODE_DAYI));
        }

        TEST_METHOD(ArrayCopiesWhenProgramFilesIsNewer)
        {
            EnvGuard g;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            SeedArrayCopy(ct, false);
            SeedArrayCopy(rt, false);
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_ARRAY, cpp, rust, &cr, &rr);
            Assert::AreEqual((int)cr, (int)rr);
            Assert::IsTrue(cr != FALSE);
            Assert::IsTrue(ReadBytes(ct.Dime() + L"\\Array.cin") == ReadBytes(ct.PfDime() + L"\\Array.cin"), L"cpp copied");
            Assert::IsTrue(ReadBytes(rt.Dime() + L"\\Array.cin") == ReadBytes(rt.PfDime() + L"\\Array.cin"), L"rust copied");
            Assert::AreEqual(std::wstring(L"0987654321"), Selkey(cpp, IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(std::wstring(L"0987654321"), Selkey(rust, IME_MODE::IME_MODE_ARRAY));
        }

        TEST_METHOD(ArrayKeepsNewerAppData)
        {
            EnvGuard g;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            SeedArrayCopy(ct, true);
            SeedArrayCopy(rt, true);
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_ARRAY, cpp, rust, &cr, &rr);
            Assert::AreEqual((int)cr, (int)rr);
            Assert::IsTrue(ReadBytes(ct.Dime() + L"\\Array.cin") != ReadBytes(ct.PfDime() + L"\\Array.cin"));
            Assert::IsTrue(ReadBytes(rt.Dime() + L"\\Array.cin") != ReadBytes(rt.PfDime() + L"\\Array.cin"));
            Assert::AreEqual(std::wstring(L"1234567890"), Selkey(cpp, IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(std::wstring(L"1234567890"), Selkey(rust, IME_MODE::IME_MODE_ARRAY));
        }

        TEST_METHOD(Array40SkipsAuxTables)
        {
            EnvGuard g;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY40_BIG5);
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->Dime() + L"\\Array40.cin", Cin(L"1234567890", L""));
                WriteUtf16(t->Dime() + L"\\Array-Ext-B.cin", Cin(L"1234567890", L""));
                SeedPhrase(*t);
            }
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_ARRAY, cpp, rust, &cr, &rr);
            Assert::IsTrue(cr != FALSE && rr != FALSE);
            Assert::AreEqual(ct.Dime() + L"\\Array40.cin", FileName(EngineSetupParityAccess::MainFile(cpp, IME_MODE::IME_MODE_ARRAY)));
            Assert::AreEqual(rt.Dime() + L"\\Array40.cin", FileName(EngineSetupParityAccess::MainFile(rust, IME_MODE::IME_MODE_ARRAY)));
            Assert::IsNull(EngineSetupParityAccess::ExtBEngine(cpp));
            Assert::IsNull(EngineSetupParityAccess::ExtBEngine(rust));
            Assert::IsNull(EngineSetupParityAccess::ExtBFile(cpp));
            Assert::IsNull(EngineSetupParityAccess::ExtBFile(rust));
        }

        TEST_METHOD(Array30LoadsAuxAndBig5)
        {
            EnvGuard g;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY30_BIG5);
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            const wchar_t* aux[] = { L"Array.cin", L"Array-Ext-B.cin", L"Array-Ext-CD.cin", L"Array-Ext-EF.cin", L"Array-Phrase.cin", L"Array-special.cin", L"Array-shortcode.cin" };
            for (const Tree* t : { &ct, &rt })
            {
                for (const wchar_t* name : aux)
                    WriteUtf16(t->Dime() + L"\\" + name, Cin(L"1234567890", L""));
                SeedPhrase(*t);
            }
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_ARRAY, cpp, rust, &cr, &rr);
            Assert::IsTrue(cr != FALSE && rr != FALSE);
            Assert::IsNotNull(EngineSetupParityAccess::ExtBEngine(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::ExtBEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::ExtCDEngine(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::ExtCDEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::ExtEEngine(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::ExtEEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::ArrayPhraseEngine(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::ArrayPhraseEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::SpecialEngine(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::SpecialEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::ShortEngine(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::ShortEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::Big5(cpp, IME_MODE::IME_MODE_ARRAY));
            Assert::IsNotNull(EngineSetupParityAccess::Big5(rust, IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(ct.Dime() + L"\\Array-Ext-B.cin", FileName(EngineSetupParityAccess::ExtBFile(cpp)));
            Assert::AreEqual(rt.Dime() + L"\\Array-Ext-B.cin", FileName(EngineSetupParityAccess::ExtBFile(rust)));
        }

        TEST_METHOD(RenamedTableDropsAndReloads)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->Dime() + L"\\Array.cin", Cin(L"1234567890", L""));
                WriteUtf16(t->Dime() + L"\\Array40.cin", Cin(L"0987654321", L""));
                SeedPhrase(*t);
            }
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            Global::imeMode = IME_MODE::IME_MODE_ARRAY;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
            g.Use(ct);
            Assert::IsTrue(EngineSetupParityAccess::DictCpp(cpp, IME_MODE::IME_MODE_ARRAY) != FALSE);
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY40_BIG5);
            Assert::IsTrue(EngineSetupParityAccess::DictCpp(cpp, IME_MODE::IME_MODE_ARRAY) != FALSE);
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
            g.Use(rt);
            Assert::IsTrue(EngineSetupParityAccess::DictRust(rust, IME_MODE::IME_MODE_ARRAY) != FALSE);
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY40_BIG5);
            Assert::IsTrue(EngineSetupParityAccess::DictRust(rust, IME_MODE::IME_MODE_ARRAY) != FALSE);
            Assert::AreEqual(ct.Dime() + L"\\Array40.cin", FileName(EngineSetupParityAccess::MainFile(cpp, IME_MODE::IME_MODE_ARRAY)));
            Assert::AreEqual(rt.Dime() + L"\\Array40.cin", FileName(EngineSetupParityAccess::MainFile(rust, IME_MODE::IME_MODE_ARRAY)));
            Assert::AreEqual(std::wstring(L"0987654321"), Selkey(cpp, IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(std::wstring(L"0987654321"), Selkey(rust, IME_MODE::IME_MODE_ARRAY));
        }

        TEST_METHOD(CustomTableFollowsActiveMode)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->Dime() + L"\\Generic.cin", Cin(L"1234567890", L""));
                WriteUtf16(t->Dime() + L"\\PHONETIC-CUSTOM.cin", Cin(L"0987654321", L""));
                WriteUtf16(t->Dime() + L"\\DAYI-CUSTOM.cin", Cin(L"1111111111", L""));
                SeedPhrase(*t);
            }
            Global::imeMode = IME_MODE::IME_MODE_PHONETIC;
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            g.Use(ct);
            Assert::IsTrue(EngineSetupParityAccess::DictCpp(cpp, IME_MODE::IME_MODE_GENERIC) != FALSE);
            g.Use(rt);
            Assert::IsTrue(EngineSetupParityAccess::DictRust(rust, IME_MODE::IME_MODE_GENERIC) != FALSE);
            Assert::AreEqual(ct.Dime() + L"\\Generic.cin", FileName(EngineSetupParityAccess::MainFile(cpp, IME_MODE::IME_MODE_GENERIC)));
            Assert::AreEqual(rt.Dime() + L"\\Generic.cin", FileName(EngineSetupParityAccess::MainFile(rust, IME_MODE::IME_MODE_GENERIC)));
            Assert::AreEqual(ct.Dime() + L"\\PHONETIC-CUSTOM.cin", FileName(EngineSetupParityAccess::CustomFile(cpp, IME_MODE::IME_MODE_GENERIC)));
            Assert::AreEqual(rt.Dime() + L"\\PHONETIC-CUSTOM.cin", FileName(EngineSetupParityAccess::CustomFile(rust, IME_MODE::IME_MODE_GENERIC)));
            Assert::IsNull(EngineSetupParityAccess::CustomFile(cpp, IME_MODE::IME_MODE_DAYI));
            Assert::IsNull(EngineSetupParityAccess::CustomFile(rust, IME_MODE::IME_MODE_DAYI));
        }

        TEST_METHOD(LockedCinReturnsFalse)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            WriteUtf16(ct.Dime() + L"\\Dayi.cin", Cin(L"1234567890", L""));
            WriteUtf16(rt.Dime() + L"\\Dayi.cin", Cin(L"1234567890", L""));
            HANDLE lockCpp = CreateFileW((ct.Dime() + L"\\Dayi.cin").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE lockRust = CreateFileW((rt.Dime() + L"\\Dayi.cin").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(lockCpp != INVALID_HANDLE_VALUE && lockRust != INVALID_HANDLE_VALUE);
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = TRUE, rr = TRUE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_DAYI, cpp, rust, &cr, &rr);
            CloseHandle(lockCpp);
            CloseHandle(lockRust);
            Assert::AreEqual(FALSE, cr);
            Assert::AreEqual(FALSE, rr);
            Assert::IsNull(EngineSetupParityAccess::MainFile(cpp, IME_MODE::IME_MODE_DAYI));
            Assert::IsNull(EngineSetupParityAccess::MainFile(rust, IME_MODE::IME_MODE_DAYI));
            Assert::IsNull(EngineSetupParityAccess::MainEngine(cpp, IME_MODE::IME_MODE_DAYI));
            Assert::IsNull(EngineSetupParityAccess::MainEngine(rust, IME_MODE::IME_MODE_DAYI));
        }

        TEST_METHOD(DayiKeystrokeInsertsEqualAndEndkeyConverts)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->Dime() + L"\\Dayi.cin", Cin(L"1234567890", L"A"));
                SeedPhrase(*t);
            }
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_DAYI, cpp, rust, &cr, &rr);
            Assert::IsTrue(cr != FALSE && rr != FALSE);
            cpp.SetupConfiguration(IME_MODE::IME_MODE_DAYI);
            rust.SetupConfiguration(IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::KeyCpp(cpp, IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::KeyRust(rust, IME_MODE::IME_MODE_DAYI);
            AssertSameKeys(cpp, rust);
            Assert::IsTrue(EngineSetupParityAccess::HasEqual(cpp, IME_MODE::IME_MODE_DAYI));
            Assert::IsTrue(EngineSetupParityAccess::HasEqual(rust, IME_MODE::IME_MODE_DAYI));
            // The CIN parser stores towupper(radical) as the map key, so the
            // printable is 'A' even though the table line is "a".
            UINT slot = (UINT)(L'A' - 32);
            Assert::AreEqual((int)KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT, (int)EngineSetupParityAccess::KeyAt(cpp, slot)->Function);
            Assert::AreEqual((UINT)L'A', (UINT)EngineSetupParityAccess::KeyAt(cpp, slot)->Printable);
            Assert::AreEqual((UINT)MAX_RADICAL + 1, EngineSetupParityAccess::KeyCount(cpp));
        }

        TEST_METHOD(EndkeyUsesRawCode)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->Dime() + L"\\Dayi.cin", Cin(L"1234567890", L"a"));
                SeedPhrase(*t);
            }
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            BOOL cr = FALSE, rr = FALSE;
            LoadBoth(g, ct, rt, IME_MODE::IME_MODE_DAYI, cpp, rust, &cr, &rr);
            cpp.SetupConfiguration(IME_MODE::IME_MODE_DAYI);
            rust.SetupConfiguration(IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::KeyCpp(cpp, IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::KeyRust(rust, IME_MODE::IME_MODE_DAYI);
            AssertSameKeys(cpp, rust);
            Assert::AreEqual((int)KEYSTROKE_FUNCTION::FUNCTION_INPUT, (int)EngineSetupParityAccess::KeyAt(cpp, (UINT)(L'A' - 32))->Function);
            Assert::AreEqual((int)KEYSTROKE_FUNCTION::FUNCTION_CONVERT, (int)EngineSetupParityAccess::KeyAt(cpp, (UINT)(L'a' - 32))->Function);
        }

        TEST_METHOD(CandidateRangesMatch)
        {
            EnvGuard g;
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            Global::imeMode = IME_MODE::IME_MODE_DAYI;
            CConfig::SetSpaceAsFirstCaniSelkey(TRUE);
            CConfig::SetDayiSelkeyMode(DAYI_SELKEY_MODE::DAYI_SELKEY_DAYI);
            EngineSetupParityAccess::RangeCpp(cpp, IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::RangeRust(rust, IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(cpp.GetCandidatePageSize(), rust.GetCandidatePageSize());
            Assert::AreEqual(10u, cpp.GetCandidatePageSize());
            AssertSameRange(EngineSetupParityAccess::Cand(cpp), EngineSetupParityAccess::Cand(rust), L"dayi");
            AssertSameRange(EngineSetupParityAccess::PhraseRange(cpp), EngineSetupParityAccess::PhraseRange(rust), L"dayi phrase");
            Assert::AreEqual((UINT)L' ', (UINT)EngineSetupParityAccess::Cand(cpp).GetAt(0)->Printable);

            CConfig::SetDayiSelkeyMode(DAYI_SELKEY_MODE::DAYI_SELKEY_NUMERIC);
            EngineSetupParityAccess::RangeCpp(cpp, IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::RangeRust(rust, IME_MODE::IME_MODE_DAYI);
            AssertSameRange(EngineSetupParityAccess::Cand(cpp), EngineSetupParityAccess::Cand(rust), L"numeric");
            Assert::AreEqual((UINT)L'1', (UINT)EngineSetupParityAccess::Cand(cpp).GetAt(0)->Printable);
            Assert::AreEqual(0u, EngineSetupParityAccess::Cand(cpp).GetAt(9)->Index);

            Global::imeMode = IME_MODE::IME_MODE_PHONETIC;
            CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
            EngineSetupParityAccess::RangeCpp(cpp, IME_MODE::IME_MODE_PHONETIC);
            EngineSetupParityAccess::RangeRust(rust, IME_MODE::IME_MODE_PHONETIC);
            Assert::AreEqual(9u, cpp.GetCandidatePageSize());
            Assert::AreEqual(9u, rust.GetCandidatePageSize());
            AssertSameRange(EngineSetupParityAccess::Cand(cpp), EngineSetupParityAccess::Cand(rust), L"phonetic");

            Global::imeMode = IME_MODE::IME_MODE_ARRAY;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY40_BIG5);
            CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
            EngineSetupParityAccess::RangeCpp(cpp, IME_MODE::IME_MODE_ARRAY);
            EngineSetupParityAccess::RangeRust(rust, IME_MODE::IME_MODE_ARRAY);
            AssertSameRange(EngineSetupParityAccess::Cand(cpp), EngineSetupParityAccess::Cand(rust), L"array40");
            Assert::AreEqual((UINT)L':', (UINT)EngineSetupParityAccess::Cand(cpp).GetAt(9)->CandIndex);

            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->Dime() + L"\\Dayi.cin", Cin(L"1234567890ab", L""));
                SeedPhrase(*t);
            }
            CConfig::SetSpaceAsFirstCaniSelkey(TRUE);
            CConfig::SetDayiSelkeyMode(DAYI_SELKEY_MODE::DAYI_SELKEY_FROM_TABLE);
            Global::imeMode = IME_MODE::IME_MODE_DAYI;
            g.Use(ct);
            Assert::IsTrue(EngineSetupParityAccess::DictCpp(cpp, IME_MODE::IME_MODE_DAYI) != FALSE);
            g.Use(rt);
            Assert::IsTrue(EngineSetupParityAccess::DictRust(rust, IME_MODE::IME_MODE_DAYI) != FALSE);
            EngineSetupParityAccess::RangeCpp(cpp, IME_MODE::IME_MODE_DAYI);
            EngineSetupParityAccess::RangeRust(rust, IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(10u, cpp.GetCandidatePageSize());
            Assert::AreEqual(10u, rust.GetCandidatePageSize());
            AssertSameRange(EngineSetupParityAccess::Cand(cpp), EngineSetupParityAccess::Cand(rust), L"space cap");
            Assert::AreEqual((UINT)L' ', (UINT)EngineSetupParityAccess::Cand(cpp).GetAt(0)->Printable);
            Assert::AreEqual((UINT)L'1', (UINT)EngineSetupParityAccess::Cand(cpp).GetAt(1)->Printable);
        }

        TEST_METHOD(PreservedKeysMatch)
        {
            Assert::AreEqual(4, dime_preserved_key_count());
            DimePreservedKey key = {};
            uint16_t desc[64];
            Assert::AreEqual(1, dime_preserved_key_at(0, &key, desc, 64));
            Assert::AreEqual(0u, key.id);
            Assert::AreEqual((UINT)VK_SHIFT, key.vkey);
            Assert::AreEqual((UINT)_TF_MOD_ON_KEYUP_SHIFT_ONLY, key.modifiers);
            Assert::AreEqual(0u, key.desc_len);
            Assert::AreEqual(1, dime_preserved_key_at(1, &key, desc, 64));
            Assert::AreEqual((UINT)VK_SPACE, key.vkey);
            Assert::AreEqual((UINT)TF_MOD_SHIFT, key.modifiers);
            Assert::AreEqual(1, dime_preserved_key_at(2, &key, desc, 64));
            Assert::AreEqual((UINT)VK_OEM_5, key.vkey);
            Assert::AreEqual((UINT)TF_MOD_CONTROL, key.modifiers);
            Assert::AreEqual(std::wstring(L"Show Config Pages"), std::wstring(desc, desc + key.desc_len));
            Assert::AreEqual(1, dime_preserved_key_at(3, &key, desc, 64));
            Assert::AreEqual((UINT)(TF_MOD_CONTROL | TF_MOD_SHIFT), key.modifiers);
            Assert::AreEqual(std::wstring(L"Toggle Simplified/Traditional"), std::wstring(desc, desc + key.desc_len));

            EnvGuard g;
            StringCchCopyW(Global::ImeModeDescription, 50, L"ModeSwitch");
            StringCchCopyW(Global::DoubleSingleByteDescription, 50, L"WidthKey");
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            EngineSetupParityAccess::PreservedCpp(cpp);
            EngineSetupParityAccess::PreservedRust(rust);
            const wchar_t* full[] = { L"ModeSwitch", L"WidthKey", L"Show Config Pages", L"Toggle Simplified/Traditional" };
            for (int i = 0; i < 4; i++)
            {
                auto a = EngineSetupParityAccess::Preserved(cpp, i);
                auto b = EngineSetupParityAccess::Preserved(rust, i);
                Assert::AreEqual(1u, a.count);
                Assert::AreEqual(a.count, b.count);
                Assert::AreEqual(a.vkey, b.vkey);
                Assert::AreEqual(a.modifiers, b.modifiers);
                Assert::AreEqual(Trunc(full[i]), a.description);
                Assert::AreEqual(a.description, b.description);
            }
        }

        TEST_METHOD(HanConvertAndTcFreq)
        {
            EnvGuard g;
            Tree ct = g.Make(L"cpp");
            Tree rt = g.Make(L"rust");
            for (const Tree* t : { &ct, &rt })
            {
                WriteUtf16(t->PfDime() + L"\\TCSC.cin", Cin(L"1234567890", L""));
                WriteUtf16(t->PfDime() + L"\\TCFreq.cin", Cin(L"1234567890", L""));
            }
            CConfig::SetDoHanConvert(TRUE);
            CCompositionProcessorEngine cpp(nullptr), rust(nullptr);
            g.Use(ct);
            BOOL hr = EngineSetupParityAccess::HanCpp(cpp);
            BOOL fr = EngineSetupParityAccess::FreqCpp(cpp);
            g.Use(rt);
            BOOL hrr = EngineSetupParityAccess::HanRust(rust);
            BOOL frr = EngineSetupParityAccess::FreqRust(rust);
            Assert::AreEqual((int)hr, (int)hrr);
            Assert::AreEqual((int)fr, (int)frr);
            Assert::IsTrue(hr != FALSE && fr != FALSE);
            Assert::IsTrue(Exists(ct.Dime() + L"\\TCSC.cin") && Exists(rt.Dime() + L"\\TCSC.cin"));
            Assert::IsTrue(Exists(ct.Dime() + L"\\TCFreq.cin") && Exists(rt.Dime() + L"\\TCFreq.cin"));
            Assert::IsNotNull(EngineSetupParityAccess::TcscFile(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::TcscFile(rust));
            Assert::IsNull(EngineSetupParityAccess::TcscEngine(cpp));
            Assert::IsNull(EngineSetupParityAccess::TcscEngine(rust));
            Assert::IsNotNull(EngineSetupParityAccess::FreqFile(cpp));
            Assert::IsNotNull(EngineSetupParityAccess::FreqFile(rust));
            // No text service, so the engine stays null and a second call still returns TRUE.
            g.Use(ct);
            Assert::IsTrue(EngineSetupParityAccess::HanCpp(cpp) != FALSE);
            Assert::IsTrue(EngineSetupParityAccess::FreqCpp(cpp) != FALSE);
            g.Use(rt);
            Assert::IsTrue(EngineSetupParityAccess::HanRust(rust) != FALSE);
            Assert::IsTrue(EngineSetupParityAccess::FreqRust(rust) != FALSE);

            CCompositionProcessorEngine off(nullptr);
            CConfig::SetDoHanConvert(FALSE);
            Tree ot = g.Make(L"off");
            WriteUtf16(ot.PfDime() + L"\\TCSC.cin", Cin(L"1234567890", L""));
            g.Use(ot);
            Assert::AreEqual(FALSE, EngineSetupParityAccess::HanCpp(off));
            Assert::AreEqual(FALSE, EngineSetupParityAccess::HanRust(off));
            Assert::IsFalse(Exists(ot.Dime() + L"\\TCSC.cin"));
        }

        TEST_METHOD(PublicDispatchMatchesBuild)
        {
            EnvGuard g;
            CConfig::SetArrayScope(ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
            Tree at = g.Make(L"a");
            Tree bt = g.Make(L"b");
            SeedArrayCopy(at, false);
            SeedArrayCopy(bt, false);
            CCompositionProcessorEngine viaPublic(nullptr), viaDirect(nullptr);
            Global::imeMode = IME_MODE::IME_MODE_ARRAY;
            g.Use(at);
            BOOL pr = viaPublic.SetupDictionaryFile(IME_MODE::IME_MODE_ARRAY, kLocale);
            g.Use(bt);
#if defined(DIME_USE_RUST_ENGINE)
            BOOL dr = EngineSetupParityAccess::DictRust(viaDirect, IME_MODE::IME_MODE_ARRAY);
#else
            BOOL dr = EngineSetupParityAccess::DictCpp(viaDirect, IME_MODE::IME_MODE_ARRAY);
#endif
            Assert::AreEqual((int)pr, (int)dr);
            Assert::AreEqual(Selkey(viaPublic, IME_MODE::IME_MODE_ARRAY), Selkey(viaDirect, IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(FileName(EngineSetupParityAccess::MainFile(viaPublic, IME_MODE::IME_MODE_ARRAY)), at.Dime() + L"\\Array.cin");
            Assert::AreEqual(FileName(EngineSetupParityAccess::MainFile(viaDirect, IME_MODE::IME_MODE_ARRAY)), bt.Dime() + L"\\Array.cin");
        }
    };
}

#endif
