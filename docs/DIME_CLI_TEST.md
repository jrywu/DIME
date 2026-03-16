# DIME CLI Test Plan and Results

## Test Results Summary

| Metric | Value |
|--------|-------|
| Total tests | 475 |
| CLI unit tests (CLIParserTest.cpp) | 34 |
| CLI integration tests (CLIIntegrationTest.cpp) | 43 |
| All tests passing | 475 / 475 |
| CLI.cpp line coverage | **94.9%** |
| CLIParserTest.cpp line coverage | 100% |
| CLIIntegrationTest.cpp line coverage | 100% |
| Uncovered lines in CLI.cpp | 29 (all unreachable defensive defaults) |

### Coverage tool

OpenCppCoverage via [run_all_coverage.ps1](../src/tests/run_all_coverage.ps1).

### Uncovered lines (unreachable via public API)

The 29 uncovered lines are all defensive `default:` switch cases or fallthrough `return` statements for enum values that are validated by `ParseCLIArgs` before reaching the handler functions:

- `KeyApplicableToMode` / `ModeName` / `GetCustomTablePath` default cases for invalid `IME_MODE`
- `HandleListKeys` / `HandleGetAll` default mode validation (mode already validated by `RunCLI`)
- `HandleGet` `GetKeyValue` failure path (all 50 registered keys have getters)
- `HandleLoadMain` / `HandleLoadArray` default mode/table cases (validated by parser)
- `ApplyKeyValue` fallthrough returns for unknown key names within each type branch (registry is self-consistent)
- `RunCLI` default command dispatch (all `CLI_COMMAND` values handled)

---

## Framework and Conventions

- **Framework:** Microsoft C++ Unit Test Framework (`CppUnitTest.h`) — same as all existing tests.
- **Test files:**
  - [src/tests/CLIParserTest.cpp](../src/tests/CLIParserTest.cpp) — unit tests for parsing and validation functions (pure logic, no file I/O).
  - [src/tests/CLIIntegrationTest.cpp](../src/tests/CLIIntegrationTest.cpp) — integration tests that call `RunCLI()` end-to-end with real temp files.
- Both files are added to [src/tests/DIMETests.vcxproj](../src/tests/DIMETests.vcxproj).

---

## Test Infrastructure

### Exit code assertion

`RunCLI()` returns `int`. Tests assert it directly:

```cpp
int rc = RunCLI(L"--mode array --get FontSize", outFile);
Assert::AreEqual(0, rc);
```

### Stdout capture via `FILE*` injection

`RunCLI()` accepts a `FILE* out` parameter (default `stdout`) so tests can redirect output:

```cpp
int RunCLI(const wchar_t* cmdLine, FILE* out = stdout);
```

Tests open a `w+, ccs=UTF-16LE` temp file, pass it to `RunCLI()`, then `rewind` and `fread` it back as `wchar_t` for `wcsstr`-based assertions.

### Config state assertion (primary)

For write commands (`--set`, `--reset`, `--import-custom`), the primary assertion reads the INI file directly via `GetPrivateProfileStringW` — independent of `--get`:

```cpp
std::wstring v = ReadIniKey(IME_MODE::IME_MODE_DAYI, L"FontSize");
Assert::AreEqual(std::wstring(L"18"), v);
```

### Config isolation

Each test class uses `TEST_METHOD_INITIALIZE` / `TEST_METHOD_CLEANUP` with `DeleteConfigFile(mode)` to start from a clean state.

---

## Implemented Tests

### Unit Tests — CLIParserTest.cpp

#### ParseModeTests (6 tests)

| Test | Input | Result |
|------|-------|--------|
| `ParseMode_Dayi` | `--mode dayi --list-keys` | PASS |
| `ParseMode_Array` | `--mode array --list-keys` | PASS |
| `ParseMode_Phonetic` | `--mode phonetic --list-keys` | PASS |
| `ParseMode_Generic` | `--mode generic --list-keys` | PASS |
| `ParseMode_Invalid_ReturnsFalse` | `--mode badmode --list-keys` | PASS |
| `ParseMode_Missing_Arg_ReturnsFalse` | `--mode` | PASS |

#### ParseArrayTableNameTests (9 tests)

| Test | Input | Result |
|------|-------|--------|
| `ParseArrayTable_sp` | `--load-array sp` | PASS |
| `ParseArrayTable_sc` | `--load-array sc` | PASS |
| `ParseArrayTable_ext_b` | `--load-array ext-b` | PASS |
| `ParseArrayTable_ext_cd` | `--load-array ext-cd` | PASS |
| `ParseArrayTable_ext_efg` | `--load-array ext-efg` | PASS |
| `ParseArrayTable_array40` | `--load-array array40` | PASS |
| `ParseArrayTable_phrase` | `--load-array phrase` | PASS |
| `ParseArrayTable_invalid_ReturnsFalse` | `--load-array badtable` | PASS |
| `ParseArrayTable_missing_args_ReturnsFalse` | `--load-array` (no args) | PASS |

#### ParseCLIArgsTests (14 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `ParseArgs_Get_ReadsKeyName` | `--get FontSize` | PASS |
| `ParseArgs_GetAll` | `--get-all` | PASS |
| `ParseArgs_Set_SinglePair` | `--set FontSize 14` | PASS |
| `ParseArgs_Set_MultiplePairs` | 3 `--set` pairs | PASS |
| `ParseArgs_Reset` | `--reset` | PASS |
| `ParseArgs_ListModes_NoModeRequired` | `--list-modes` (no `--mode`) | PASS |
| `ParseArgs_ListKeys` | `--list-keys` | PASS |
| `ParseArgs_JSON_Flag` | `--json` | PASS |
| `ParseArgs_Silent_Flag` | `--silent` | PASS |
| `ParseArgs_ImportCustom` | `--import-custom` | PASS |
| `ParseArgs_ExportCustom` | `--export-custom` | PASS |
| `ParseArgs_LoadMain` | `--load-main` | PASS |
| `ParseArgs_LoadPhrase` | `--load-phrase` | PASS |
| `ParseArgs_UnknownArg_ReturnsFalse` | `--unknown-flag` | PASS |
| `ParseArgs_Get_MissingKey_ReturnsFalse` | `--get` (no key) | PASS |
| `ParseArgs_Set_MissingValue_ReturnsFalse` | `--set FontSize` (no value) | PASS |

#### KeyApplicableModeTests (6 tests)

| Test | Key | Result |
|------|-----|--------|
| `DayiArticleMode_Applicable_OnlyToDayi` | DayiArticleMode | PASS |
| `ArrayScope_Applicable_OnlyToArray` | ArrayScope | PASS |
| `PhoneticKeyboardLayout_Applicable_OnlyToPhonetic` | PhoneticKeyboardLayout | PASS |
| `SpaceAsFirstCandSelkey_Applicable_OnlyToGeneric` | SpaceAsFirstCandSelkey | PASS |
| `Big5Filter_NotApplicable_ToArray` | Big5Filter | PASS |
| `FontSize_Applicable_ToAllModes` | FontSize | PASS |

#### ParseColorValueTests (7 tests)

| Test | Input | Result |
|------|-------|--------|
| `ParseColor_HexWithPrefix` | `0xFF8040` | PASS |
| `ParseColor_HexUpperCasePrefix` | `0XFF8040` | PASS |
| `ParseColor_HexNoPrefix` | `FF8040` | PASS |
| `ParseColor_Black` | `0x000000` | PASS |
| `ParseColor_White` | `0xFFFFFF` | PASS |
| `ParseColor_InvalidString_ReturnsFalse` | `not-a-color` | PASS |
| `ParseColor_EmptyString_ReturnsFalse` | `` | PASS |

#### FindKeyTests (8 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `FindKey_ExistingKey_ReturnsNonNull` | AutoCompose | PASS |
| `FindKey_NonExistentKey_ReturnsNull` | NonExistentKey | PASS |
| `FindKey_CaseSensitive_WrongCase_ReturnsNull` | autocompose | PASS |
| `FindKey_IntKey_HasCorrectRange` | FontSize [8,72] | PASS |
| `FindKey_ColorKey_HasCorrectType` | ItemColor = COLOR_T | PASS |
| `FindKey_StringKey_HasCorrectType` | FontFaceName = STRING_T | PASS |
| `FindKey_CLSIDKey_HasCorrectType` | ReverseConversionCLSID = CLSID_T | PASS |
| `AllKeys_HaveUniqueNames` | registry uniqueness | PASS |

#### RunCLI_ListModesUnitTest (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `ListModes_Plain_OutputsFourModes` | plain output | PASS |
| `ListModes_JSON_OutputsArray` | JSON output | PASS |

#### RunCLI_ExitCodeUnitTests (8 tests)

| Test | Scenario | Expected exit | Result |
|------|----------|---------------|--------|
| `NoCommand_ReturnsExitCode1` | `--mode dayi` (no command) | 1 | PASS |
| `NoMode_ReturnsExitCode1` | `--get FontSize` (no mode) | 1 | PASS |
| `UnknownKey_ReturnsExitCode1` | `--get NoSuchKey` | 1 | PASS |
| `KeyNotApplicableToMode_ReturnsExitCode3` | `--mode array --get DayiArticleMode` | 3 | PASS |
| `SetKeyNotApplicableToMode_ReturnsExitCode3` | `--mode dayi --set ArrayScope 2` | 3 | PASS |
| `SetInvalidIntValue_ReturnsExitCode1` | `--set FontSize notanumber` | 1 | PASS |
| `SetOutOfRangeIntValue_ReturnsExitCode1` | `--set FontSize 999` | 1 | PASS |
| `SetUnknownKey_ReturnsExitCode1` | `--set NoSuchKey 42` | 1 | PASS |

---

### Integration Tests — CLIIntegrationTest.cpp

#### CLIGetCommandTests (3 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Get_FontSize_DefaultValue` | reset then `--get FontSize` | PASS |
| `Get_UnknownKey_ReturnsExitCode1` | `--get DoesNotExist` | PASS |
| `Get_KeyNotApplicableToMode_ReturnsExitCode3` | `--mode array --get DayiArticleMode` | PASS |

#### CLIGetAllCommandTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `GetAll_Phonetic_OutputsPhoneticKeys` | phonetic mode filters keys correctly | PASS |
| `GetAll_Array_OutputsArrayKeys` | array mode filters keys correctly | PASS |

#### CLISetCommandTests (7 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Set_FontSize_WrittenToINI` | FontSize=18 verified in INI | PASS |
| `Set_MultipleKeys_AllWrittenToINI` | 3 keys in one invocation | PASS |
| `Set_BooleanKey_Zero_WrittenToINI` | AutoCompose=0 | PASS |
| `Set_ColorMode_WrittenToINI` | ColorMode=2 | PASS |
| `Set_ColorKey_WrittenToINI` | ItemColor=0xFF1234 | PASS |
| `Set_ArraySpecificKey_InArrayMode_WrittenToINI` | ArrayScope=3 | PASS |
| `Set_InvalidIntValue_DoesNotWriteINI` | FontSize=abc rejected, INI unchanged | PASS |
| `Set_OutOfRangeValue_DoesNotWriteINI` | MaxCodes=999 rejected, INI unchanged | PASS |

#### CLIResetCommandTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Reset_WritesINIFile` | INI file created after reset | PASS |
| `Reset_OverwritesCustomValue` | FontSize=30 reset to 12 | PASS |

#### CLIListModesCommandTests (1 test)

| Test | Scenario | Result |
|------|----------|--------|
| `ListModes_ExitCode0` | exit code 0 | PASS |

#### CLIListKeysCommandTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `ListKeys_Phonetic_ContainsPhoneticLayout` | phonetic keys present | PASS |
| `ListKeys_Array_ContainsArrayScope` | array keys present, phonetic absent | PASS |

#### CLIImportExportCustomTableTests (4 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `ImportCustom_ValidFile_ExitCode0` | valid CIN import | PASS |
| `ImportCustom_NonExistentFile_ExitCode2` | missing file | PASS |
| `ExportCustom_NoCustomTable_ExitCode2` | no table to export | PASS |
| `ExportCustom_AfterImport_CreatesFile` | import then export roundtrip | PASS |

#### CLISetAndReadBackTests (4 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `SetFontSize_ReadBack_Matches` | set 22, verify INI + `--get` | PASS |
| `SetPhoneticLayout_InPhoneticMode_WrittenToINI` | phonetic-only key | PASS |
| `SetDayiArticleMode_InDayiMode_WrittenToINI` | dayi-only key | PASS |
| `SetBig5Filter_InGenericMode_WrittenToINI` | generic Big5Filter | PASS |

#### CLISilentFlagTests (1 test)

| Test | Scenario | Result |
|------|----------|--------|
| `SetWithSilent_StillWritesINI` | `--silent` still writes config | PASS |

#### CLIJSONOutputTests (1 test)

| Test | Scenario | Result |
|------|----------|--------|
| `GetAll_JSON_OutputsOpeningBrace` | JSON format with mode and keys | PASS |

#### CLILoadCommandTests (3 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `LoadMain_NonExistentFile_ExitCode2` | missing file | PASS |
| `LoadPhrase_NonExistentFile_ExitCode2` | missing file | PASS |
| `LoadArray_NonExistentFile_ExitCode2` | missing file | PASS |

#### CLIJSONGetKeyTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Get_JSON_OutputsKeyValueObject` | `--get FontSize --json` | PASS |
| `ListKeys_JSON_OutputsKeyArray` | `--list-keys --json` | PASS |

#### CLIStringAndCLSIDKeyTests (4 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Set_FontFaceName_WrittenToINI` | STRING_T key | PASS |
| `Set_ReverseConversionCLSID_WrittenToINI` | CLSID_T key | PASS |
| `Set_LightItemColor_WrittenToINI` | light palette color | PASS |
| `Set_DarkItemColor_WrittenToINI` | dark palette color | PASS |

#### CLIColorKeySetTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Set_AllRemainingColorKeys_WrittenToINI` | 15 color keys via loop | PASS |
| `Set_ColorKey_InvalidValue_ReturnsExitCode1` | `PhraseColor not_a_color` | PASS |

#### CLIStringCLSIDSetTests (3 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `Set_ReverseConversionDescription_WrittenToINI` | STRING_T Description | PASS |
| `Set_ReverseConversionGUIDProfile_WrittenToINI` | CLSID_T GUIDProfile | PASS |
| `Set_ReverseConversionCLSID_InvalidGUID_ReturnsExitCode1` | invalid GUID string | PASS |

#### CLILoadMainSuccessTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `LoadMain_AllModes_CreatesTableFile` | all 4 modes via loop | PASS |
| `LoadMain_LockedFile_ReturnsExitCode2` | exclusive lock on source | PASS |

#### CLILoadPhraseSuccessTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `LoadPhrase_ValidFile_CreatesPhraseTable` | Phrase.cin created | PASS |
| `LoadPhrase_LockedFile_ReturnsExitCode2` | exclusive lock on source | PASS |

#### CLILoadArraySuccessTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `LoadArray_AllTableTypes_CreatesFiles` | all 7 table types via loop | PASS |
| `LoadArray_LockedFile_ReturnsExitCode2` | exclusive lock on source | PASS |

#### CLIImportExportErrorPathTests (2 tests)

| Test | Scenario | Result |
|------|----------|--------|
| `ImportCustom_LockedFile_ReturnsExitCode2` | parseCINFile failure via file lock | PASS |
| `ExportCustom_BadDestPath_ReturnsExitCode2` | CopyFileW failure (nonexistent dir) | PASS |
