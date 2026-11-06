/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/********************************************************************************
* Rust settings parity tests (Rust migration, phase 1 - docs/RUST_MIGRATION_PLAN.md)
*
* Runs the C++ CConfig code and its Rust port (rust/crates/dime-config via
* dime_ffi.lib) from the same starting state and asserts identical results:
*   - ApplyIniConfigEntry for every key with edge-case values
*   - SetIMEModeDefaults / ResetAllDefaults for every mode
*   - WriteConfig output, byte for byte
*   - LoadConfigFromFile (defaults + [Config] entries + ColorMode migration)
*
* This project does not define DIME_USE_RUST_ENGINE, so CConfig itself runs
* the C++ code; the Rust side is called directly through dime_ffi.h.
********************************************************************************/

#include "pch.h"

#ifdef DIME_RUST_AVAILABLE

#include "CppUnitTest.h"
#include "../Globals.h"
#include "../Config.h"
#include "../DictionarySearch.h"
#include "../DIMESettings/CLI.h"
#include "dime_ffi.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    namespace
    {
        const LCID kLcid = MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT);

        const IME_MODE kModes[] = { IME_MODE::IME_MODE_DAYI, IME_MODE::IME_MODE_ARRAY,
                                    IME_MODE::IME_MODE_PHONETIC, IME_MODE::IME_MODE_GENERIC };

        // Every [Config] key ApplyIniConfigEntry understands, plus aliases and unknown keys.
        const wchar_t* const kKeys[] = {
            L"AutoCompose", L"ArrayForceSP", L"ArrayNotifySP", L"ArraySingleQuoteCustomPhrase",
            L"DayiArticleMode", L"DayiSelkeyMode", L"SpaceAsPageDown", L"SpaceAsFirstCandSelkey",
            L"ArrowKeySWPages", L"ClearOnBeep", L"DoBeep", L"DoBeepNotify", L"DoBeepOnCandi",
            L"SentenceMode", L"FontSize", L"FontWeight", L"FontItalic", L"DoubleSingleByteMode",
            L"ShiftEnglishDigitMode", L"IMEShiftMode", L"MaxCodes", L"ActivatedKeyboardMode",
            L"MakePhrase", L"DoHanConvert", L"PhoneticKeyboardLayout", L"ArrayUnicodeScope",
            L"ArrayScope", L"Big5Filter", L"NumericPad", L"ReloadReverseConversion",
            L"ReverseConversionCLSID", L"ReverseConversionGUIDProfile", L"ReverseConversionDescription",
            L"ShowNotifyDesktop", L"AppPermissionSet", L"ColorMode",
            L"ItemColor", L"PhraseColor", L"NumberColor", L"ItemBGColor", L"SelectedItemColor", L"SelectedBGItemColor",
            L"LightItemColor", L"LightPhraseColor", L"LightNumberColor", L"LightItemBGColor",
            L"LightSelectedItemColor", L"LightSelectedBGItemColor",
            L"DarkItemColor", L"DarkPhraseColor", L"DarkNumberColor", L"DarkItemBGColor",
            L"DarkSelectedItemColor", L"DarkSelectedBGItemColor",
            L"CustomTablePriority", L"FontFaceName", L"LoadTableMode",
            // case-insensitive matching, unknown keys
            L"autocompose", L"FONTSIZE", L"itemcolor", L"NoSuchKey", L"FontSizeX", L"Font",
        };

        // Values chosen to exercise _wtoi / wcstol / wcstoul and the "1"-only booleans.
        const wchar_t* const kValues[] = {
            L"0", L"1", L"2", L"3", L"5", L"-1", L"20", L"21", L"12", L"400", L" 7", L"+4",
            L"010", L"0x1F", L"0X00FF00", L"0xFFFFFF", L"0x123456789", L"99999999999", L"-99999999999",
            L"\xFF11", L"\xFF11\xFF12", L"\x0661", L"abc", L"", L"1 ", L"1x", L"true",
            L"{36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B}", L"{36fe76ba-0a31-4e01-9e2b-0f4ad5a1c18b}",
            L"36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B", L"{36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18G}",
            L"Consolas", L"\x5FAE\x8EDF\x6B63\x9ED1\x9AD4", L"(\x7121)",
            L"123456789012345678901234567890", // 30 units: longest FontFaceName without the C++ assert
        };

        struct State
        {
            DimeConfigValues values;
            bool hasDescription;
            std::wstring description;

            // Bytes after the font face terminator are not part of the value
            // (StringCchCopyW leaves them, Rust zero-fills).
            void Normalize()
            {
                bool end = false;
                for (uint16_t& c : values.font_face_name) { if (end) c = 0; else if (c == 0) end = true; }
            }

            bool operator==(const State& o) const
            {
                return memcmp(&values, &o.values, sizeof(values)) == 0
                    && hasDescription == o.hasDescription && description == o.description;
            }
        };

        State FromCpp()
        {
            State s = {};
            CConfig::ExportValues(&s.values);
            WCHAR* d = CConfig::GetReverseConversionDescription();
            s.hasDescription = d != nullptr;
            if (d) s.description = d;
            s.Normalize();
            return s;
        }

        State FromRust(DimeSettings* r)
        {
            State s = {};
            Assert::IsNotNull(r, L"Rust returned NULL settings");
            Assert::AreEqual(1, (int)dime_settings_get_values(r, &s.values));
            const uint16_t* text = nullptr;
            size_t len = 0;
            s.hasDescription = dime_settings_get_description(r, &text, &len) != 0;
            if (s.hasDescription) s.description.assign(reinterpret_cast<const wchar_t*>(text), len);
            s.Normalize();
            return s;
        }

        void SetDescription(const wchar_t* d)
        {
            CConfig::SetReverseConversionDescriptionText(d);
        }

        // Set every setting to a distinctive, mostly non-default value.
        void Scramble(int seed, const wchar_t* description)
        {
            DimeConfigValues v = {};
            int32_t* ints = &v.auto_compose;
            for (int i = 0; i < 24; i++) ints[i] = (i * 7 + seed) % 3 == 0 ? 0 : 1;
            v.max_codes = 3 + seed % 5;
            v.font_size = 9 + seed;
            v.font_weight = 700;
            int32_t* enums = &v.ime_shift_mode;
            for (int i = 0; i < 8; i++) enums[i] = (i + seed) % 3;
            uint32_t* cols = &v.custom.item;
            for (int i = 0; i < 18; i++) cols[i] = 0x010203u * (uint32_t)(i + 1) + (uint32_t)seed;
            StringCchCopyW(reinterpret_cast<wchar_t*>(v.font_face_name), 32, seed % 2 ? L"Consolas" : L"\x7D30\x660E\x9AD4");
            for (int i = 0; i < 16; i++)
            {
                v.reverse_conversion_clsid[i] = (uint8_t)(i * 17 + seed);
                v.reverse_conversion_guid_profile[i] = (uint8_t)(255 - i * 3 - seed);
            }
            CConfig::ImportValues(&v);
            SetDescription(description);
        }

        std::wstring Describe(const State& a, const State& b)
        {
            std::wstring msg;
            const uint8_t* pa = reinterpret_cast<const uint8_t*>(&a.values);
            const uint8_t* pb = reinterpret_cast<const uint8_t*>(&b.values);
            for (size_t i = 0; i < sizeof(DimeConfigValues); i++)
                if (pa[i] != pb[i]) { msg += L" first differing byte at offset " + std::to_wstring(i) + L";"; break; }
            if (a.hasDescription != b.hasDescription || a.description != b.description)
                msg += L" description C++ '" + (a.hasDescription ? a.description : L"<null>") + L"' Rust '"
                    + (b.hasDescription ? b.description : L"<null>") + L"'";
            return msg;
        }

        void AssertSame(const State& cpp, const State& rust, const std::wstring& what)
        {
            if (cpp == rust) return;
            Assert::Fail((what + L":" + Describe(cpp, rust)).c_str());
        }

        std::wstring TempPath(const wchar_t* name)
        {
            WCHAR dir[MAX_PATH];
            GetTempPathW(MAX_PATH, dir);
            return std::wstring(dir) + L"DIME_RustConfigParity_" + name;
        }

        std::vector<uint8_t> ReadAll(const std::wstring& path)
        {
            std::ifstream f(path, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }

        void WriteUtf16File(const std::wstring& path, const std::wstring& text)
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            const wchar_t bom = 0xFEFF;
            f.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
            f.write(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
        }
    }

    TEST_CLASS(RustConfigParityTest)
    {
        WCHAR _savedIniPath[MAX_PATH] = {};
        DimeConfigValues _saved = {};
        std::wstring _savedDescription;
        bool _savedHasDescription = false;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            StringCchCopyW(_savedIniPath, MAX_PATH, CConfig::_pwszINIFileName);
            CConfig::ExportValues(&_saved);
            WCHAR* d = CConfig::GetReverseConversionDescription();
            _savedHasDescription = d != nullptr;
            _savedDescription = d ? d : L"";
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            StringCchCopyW(CConfig::_pwszINIFileName, MAX_PATH, _savedIniPath);
            CConfig::ImportValues(&_saved);
            SetDescription(_savedHasDescription ? _savedDescription.c_str() : nullptr);
        }

        TEST_METHOD(ExportImportRoundTrip)
        {
            Scramble(3, L"desc");
            State a = FromCpp();
            DimeSettings* r = CConfig::CreateRustSettings();
            AssertSame(a, FromRust(r), L"snapshot");
            Scramble(4, nullptr);
            CConfig::ImportRustSettings(r);
            dime_settings_free(r);
            AssertSame(a, FromCpp(), L"import");
        }

        TEST_METHOD(ApplyIniConfigEntry_AllKeysAllValues)
        {
            int checked = 0;
            for (const wchar_t* key : kKeys)
            {
                for (const wchar_t* value : kValues)
                {
#ifdef _DEBUG
                    // ApplyIniConfigEntry asserts FontFaceName is shorter than LF_FACESIZE - 1.
                    if (_wcsicmp(key, L"FontFaceName") == 0 && wcslen(value) >= LF_FACESIZE - 1) continue;
#endif
                    Scramble(checked % 5, checked % 2 ? L"before" : nullptr);
                    DimeSettings* r = CConfig::CreateRustSettings();

                    std::wstring k(key), v(value); // NUL-terminated: _wtoi/wcstoul read past the range
                    CStringRange keyRange, valueRange;
                    keyRange.Set(k.c_str(), k.length());
                    valueRange.Set(v.c_str(), v.length());
                    ApplyIniConfigEntry(kLcid, &keyRange, &valueRange);
                    State cpp = FromCpp();

                    dime_settings_apply_entry(r, kLcid, reinterpret_cast<const uint16_t*>(k.c_str()), k.length(),
                        reinterpret_cast<const uint16_t*>(v.c_str()), v.length());
                    State rust = FromRust(r);
                    dime_settings_free(r);

                    AssertSame(cpp, rust, std::wstring(L"key '") + key + L"' value '" + value + L"'");
                    checked++;
                }
            }
            Logger::WriteMessage((L"ApplyIniConfigEntry pairs checked: " + std::to_wstring(checked) + L"\n").c_str());
        }

        TEST_METHOD(ModeDefaults)
        {
            for (int mode = 0; mode <= 4; mode++)
            {
                for (int seed = 0; seed < 3; seed++)
                {
                    Scramble(seed, L"keep");
                    DimeSettings* r = CConfig::CreateRustSettings();
                    CConfig::SetIMEModeDefaults((IME_MODE)mode);
                    dime_settings_set_mode_defaults(r, mode);
                    AssertSame(FromCpp(), FromRust(r), L"SetIMEModeDefaults mode " + std::to_wstring(mode));
                    dime_settings_free(r);

                    Scramble(seed, L"keep");
                    r = CConfig::CreateRustSettings();
                    CConfig::ResetAllDefaults((IME_MODE)mode);
                    dime_settings_reset_all_defaults(r, mode);
                    AssertSame(FromCpp(), FromRust(r), L"ResetAllDefaults mode " + std::to_wstring(mode));
                    dime_settings_free(r);
                }
            }
        }

        TEST_METHOD(WriteConfig_ByteIdentical)
        {
            std::wstring path = TempPath(L"write.ini");
            StringCchCopyW(CConfig::_pwszINIFileName, MAX_PATH, path.c_str());
            int checked = 0;
            for (IME_MODE mode : kModes)
            {
                for (int variant = 0; variant < 5; variant++)
                {
                    if (variant == 0) CConfig::ResetAllDefaults(mode);
                    else Scramble(variant, variant % 2 ? nullptr : L"\x6CE8\x97F3 (Phonetic)");
                    if (variant == 3) CConfig::SetFontSize((UINT)-5);      // negative through %d
                    if (variant == 4) CConfig::SetLoadTableMode(FALSE);
                    if (variant == 1) CConfig::SetLoadTableMode(TRUE);
                    if (variant == 2) CConfig::SetReloadReverseConversion(7); // raw BOOL value is written

                    CConfig::WriteConfig(mode, FALSE);
                    std::vector<uint8_t> cpp = ReadAll(path);

                    DimeSettings* r = CConfig::CreateRustSettings();
                    DimeBuffer* b = dime_settings_serialize_ini(r, (int32_t)mode);
                    dime_settings_free(r);
                    Assert::IsNotNull(b);
                    size_t len = 0;
                    const uint16_t* data = dime_buffer_data(b, &len);
                    std::vector<uint8_t> rust(reinterpret_cast<const uint8_t*>(data),
                        reinterpret_cast<const uint8_t*>(data) + len * sizeof(uint16_t));
                    dime_buffer_free(b);

                    if (cpp != rust)
                    {
                        size_t i = 0;
                        while (i < cpp.size() && i < rust.size() && cpp[i] == rust[i]) i++;
                        size_t from = i > 40 ? i - 40 : 0;
                        std::wstring ctx(reinterpret_cast<const wchar_t*>(cpp.data() + (from & ~1)),
                            (std::min)(cpp.size() - (from & ~1), (size_t)120) / 2);
                        Assert::Fail((L"WriteConfig mode " + std::to_wstring((int)mode) + L" variant " + std::to_wstring(variant)
                            + L": C++ " + std::to_wstring(cpp.size()) + L" bytes, Rust " + std::to_wstring(rust.size())
                            + L", first difference at byte " + std::to_wstring(i) + L" near: " + ctx).c_str());
                    }
                    checked++;
                }
            }
            DeleteFileW(path.c_str());
            Logger::WriteMessage((L"WriteConfig outputs compared: " + std::to_wstring(checked) + L"\n").c_str());
        }

        TEST_METHOD(LoadConfigFromFile_SameResult)
        {
            const std::wstring files[] = {
                // ordinary file written by WriteConfig (ColorMode present)
                L"[Config]\nAutoCompose = 1\nMaxCodes = 6\nColorMode = 2\nFontFaceName = Consolas\nDayiSelkeyMode = 2\n",
                // legacy file: no ColorMode, factory colours -> SYSTEM / LIGHT
                L"[Config]\nFontSize = 14\n",
                // legacy file: no ColorMode, customised colours -> CUSTOM
                L"[Config]\nItemColor = 0x0000FF\nSelectedBGItemColor = 0x112233\n",
                // odd values and keys
                L"[Config]\nMaxCodes = 0\nDayiSelkeyMode = 9\nColorMode = 0x3\nFontWeight = -1\nunknown = 5\n"
                L"ReverseConversionDescription = \x6CE8\x97F3\nReverseConversionCLSID = {36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B}\n",
                // no LoadTableMode key -> stays TRUE
                L"[Config]\nLoadTableMode = 0\nBig5Filter = 1\nArrayScope = 4\n",
                // empty file
                L"",
            };
            std::wstring path = TempPath(L"load.ini");
            int checked = 0;
            for (const std::wstring& text : files)
            {
                WriteUtf16File(path, text);
                std::vector<uint8_t> bytes = ReadAll(path);
                for (IME_MODE mode : kModes)
                {
                    for (int seed = 0; seed < 2; seed++)
                    {
                        Scramble(seed, seed ? L"old" : nullptr);
                        DimeSettings* r = CConfig::CreateRustSettings();

                        Assert::IsTrue(CConfig::LoadConfigFromFile(mode, path.c_str()));
                        State cpp = FromCpp();

                        // Rust: dictionary engine for the [Config] entries, dime-config for the rest.
                        const uint16_t* body = reinterpret_cast<const uint16_t*>(bytes.data()) + 1; // skip BOM
                        size_t units = bytes.size() / 2 - 1;
                        DimeEngine* e = dime_engine_new(kLcid, body, units, DIME_DICT_INI);
                        Assert::IsNotNull(e);
                        dime_engine_parse_config(e);
                        dime_settings_set_mode_defaults(r, (int32_t)mode);
                        dime_settings_begin_load(r, (int32_t)mode);
                        for (size_t i = 0; i < dime_engine_config_entry_count(e); i++)
                        {
                            const uint16_t *k = nullptr, *v = nullptr;
                            size_t kLen = 0, vLen = 0;
                            if (dime_engine_config_entry_at(e, i, &k, &kLen, &v, &vLen))
                                dime_settings_apply_entry(r, kLcid, k, kLen, v, vLen);
                        }
                        dime_engine_free(e);
                        dime_settings_finish_load(r);

                        AssertSame(cpp, FromRust(r), L"file #" + std::to_wstring(checked / 8) + L" mode " + std::to_wstring((int)mode));
                        dime_settings_free(r);
                        checked++;
                    }
                }
            }
            DeleteFileW(path.c_str());
        }

        TEST_METHOD(LoadMissing_SameResult)
        {
            for (int mode = 0; mode <= 4; mode++)
            {
                Scramble(mode, nullptr);
                DimeSettings* r = CConfig::CreateRustSettings();
                // The C++ "file missing" branch of LoadConfig.
                CConfig::SetIMEModeDefaults((IME_MODE)mode);
                CConfig::SetColorMode(Global::isWindows1809OrLater ? IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM : IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
                dime_settings_load_missing(r, mode);
                AssertSame(FromCpp(), FromRust(r), L"load_missing mode " + std::to_wstring(mode));
                dime_settings_free(r);
            }
        }
    };

    // CLI --get / --set: DIMESettings/CLI.cpp (C++ in this project) vs dime_config::keys.
    TEST_CLASS(RustCliParityTest)
    {
        DimeConfigValues _saved = {};
        std::wstring _savedDescription;
        bool _savedHasDescription = false;

        static std::wstring RustText(DimeBuffer* b)
        {
            Assert::IsNotNull(b, L"Rust returned NULL buffer");
            size_t len = 0;
            const uint16_t* data = dime_buffer_data(b, &len);
            std::wstring s(reinterpret_cast<const wchar_t*>(data), len);
            dime_buffer_free(b);
            return s;
        }

        // Run C++ ApplyKeyValue and capture what it prints to the error stream.
        static int CppApply(const KeyInfo* ki, const wchar_t* value, std::wstring& errText)
        {
            std::wstring path = TempPath(L"clierr.txt");
            FILE* err = nullptr;
            Assert::AreEqual(0, (int)_wfopen_s(&err, path.c_str(), L"w, ccs=UTF-16LE"));
            int rc = CLI_ApplyKeyValue(ki, value, err);
            fclose(err);
            std::vector<uint8_t> bytes = ReadAll(path);
            DeleteFileW(path.c_str());
            errText.clear();
            if (bytes.size() > 2)
                errText.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
            for (size_t p; (p = errText.find(L"\r\n")) != std::wstring::npos;) errText.erase(p, 1); // text-mode CRLF
            return rc;
        }

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            CConfig::ExportValues(&_saved);
            WCHAR* d = CConfig::GetReverseConversionDescription();
            _savedHasDescription = d != nullptr;
            _savedDescription = d ? d : L"";
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            CConfig::ImportValues(&_saved);
            SetDescription(_savedHasDescription ? _savedDescription.c_str() : nullptr);
        }

        TEST_METHOD(KeyRegistry_Identical)
        {
            Assert::AreEqual(CLI_GetKeyCount(), (int)dime_key_count(), L"key count");
            const KeyInfo* reg = CLI_GetKeyRegistry();
            for (int i = 0; i < CLI_GetKeyCount(); i++)
            {
                const uint16_t* name = nullptr;
                size_t nameLen = 0;
                int32_t type = -1, mn = 0, mx = 0;
                uint32_t mask = 0;
                Assert::AreEqual(1, (int)dime_key_info(i, &name, &nameLen, &type, &mn, &mx, &mask));
                std::wstring what = std::wstring(L"key ") + reg[i].name;
                Assert::AreEqual(std::wstring(reg[i].name), std::wstring(reinterpret_cast<const wchar_t*>(name), nameLen), what.c_str());
                Assert::AreEqual((int)reg[i].type, (int)type, what.c_str());
                if (reg[i].type == BOOL_T || reg[i].type == INT_T)
                {
                    Assert::AreEqual(reg[i].minVal, (int)mn, what.c_str());
                    Assert::AreEqual(reg[i].maxVal, (int)mx, what.c_str());
                }
                Assert::AreEqual((uint32_t)reg[i].modeMask, mask, what.c_str());

                // lookup is case-insensitive on both sides
                std::wstring lower(reg[i].name);
                for (auto& c : lower) c = (wchar_t)towlower(c);
                Assert::AreEqual(i, (int)dime_key_find(reinterpret_cast<const uint16_t*>(lower.c_str()), lower.size()), what.c_str());
                Assert::IsTrue(CLI_FindKey(lower.c_str()) == &reg[i], what.c_str());
                for (int mode = 0; mode <= 4; mode++)
                    Assert::AreEqual(CLI_KeyApplicableToMode(&reg[i], (IME_MODE)mode), dime_key_applicable(i, mode) != 0, what.c_str());
            }
            Assert::AreEqual(-1, (int)dime_key_find(reinterpret_cast<const uint16_t*>(L"NoSuchKey"), 9));
            Assert::IsNull(CLI_FindKey(L"NoSuchKey"));
        }

        TEST_METHOD(GetValue_Identical)
        {
            const KeyInfo* reg = CLI_GetKeyRegistry();
            for (int seed = 0; seed < 4; seed++)
            {
                if (seed == 0) CConfig::ResetAllDefaults(IME_MODE::IME_MODE_DAYI);
                else Scramble(seed, seed % 2 ? L"\x6CE8\x97F3" : nullptr);
                if (seed == 0) SetDescription(nullptr);
                DimeSettings* r = CConfig::CreateRustSettings();
                for (int i = 0; i < CLI_GetKeyCount(); i++)
                {
                    WCHAR buf[512] = {};
                    Assert::IsTrue(CLI_GetKeyValue(&reg[i], buf, 512));
                    std::wstring rust = RustText(dime_settings_get_key(r, i));
                    Assert::AreEqual(std::wstring(buf), rust, (std::wstring(L"--get ") + reg[i].name + L" seed " + std::to_wstring(seed)).c_str());
                }
                dime_settings_free(r);
            }
        }

        TEST_METHOD(SetValue_Identical)
        {
            const wchar_t* const values[] = {
                L"0", L"1", L"2", L"3", L"5", L"7", L"8", L"20", L"21", L"72", L"73", L"100", L"900", L"901",
                L"-1", L"+3", L" 5", L"5 ", L"12px", L"abc", L"", L"99999999999", L"\xFF11",
                L"0x0A0B0C", L"0X0a0b0c", L"0A0B0C", L"ff00", L"0x", L"0xZZ", L"0x0x12", L"FFFFFFFFF", L"#123456",
                L"{36FE76BA-0A31-4E01-9E2B-0F4AD5A1C18B}", L"{36fe76ba-0a31-4e01-9e2b-0f4ad5a1c18b}", L"nope",
                L"Consolas", L"\x5FAE\x8EDF\x6B63\x9ED1\x9AD4",
                L"1234567890123456789012345678901234567890", // longer than LF_FACESIZE
            };
            const KeyInfo* reg = CLI_GetKeyRegistry();
            int checked = 0;
            for (int i = 0; i < CLI_GetKeyCount(); i++)
            {
                for (const wchar_t* value : values)
                {
                    Scramble(checked % 5, checked % 3 ? L"old" : nullptr);
                    DimeSettings* r = CConfig::CreateRustSettings();

                    std::wstring cppErr;
                    int cppRc = CppApply(&reg[i], value, cppErr);
                    State cpp = FromCpp();

                    DimeBuffer* message = nullptr;
                    int rustRc = dime_settings_set_key(r, i, reinterpret_cast<const uint16_t*>(value), wcslen(value), &message);
                    std::wstring rustErr = message ? RustText(message) + L"\n" : L"";
                    State rust = FromRust(r);
                    dime_settings_free(r);

                    std::wstring what = std::wstring(L"--set ") + reg[i].name + L" '" + value + L"'";
                    Assert::AreEqual(cppRc, rustRc, (what + L": return code").c_str());
                    Assert::AreEqual(cppErr, rustErr, (what + L": error text").c_str());
                    if (cppRc == 0) AssertSame(cpp, rust, what);
                    checked++;
                }
            }
            Logger::WriteMessage((L"--set cases checked: " + std::to_wstring(checked) + L"\n").c_str());
        }
    };
}

#endif // DIME_RUST_AVAILABLE
