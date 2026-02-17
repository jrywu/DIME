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

#include "pch.h"
#include "CppUnitTest.h"
#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "Config.h"
#include <Windows.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace TSFIntegrationTests
{
    TEST_CLASS(SettingsDialogIntegrationTest)
    {
    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Logger::WriteMessage("MV-02: Settings Dialog Validation Test Setup\n");
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            Logger::WriteMessage("MV-02: Settings Dialog Validation Test Cleanup\n");
        }

        // MV-02-01: Settings Dialog Creation Tests
        TEST_METHOD(MV02_01_ConfigLoad_ArrayMode_Succeeds)
        {
            Logger::WriteMessage("Test: MV02_01_ConfigLoad_ArrayMode_Succeeds\n");

            // Act: Load Array mode configuration
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            BOOL result = CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);

            // Assert
            Assert::IsTrue(result, L"Config should load successfully");
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_ARRAY), static_cast<int>(CConfig::GetIMEMode()));
        }

        TEST_METHOD(MV02_01_ConfigLoad_DayiMode_Succeeds)
        {
            Logger::WriteMessage("Test: MV02_01_ConfigLoad_DayiMode_Succeeds\n");

            // Act
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
            BOOL result = CConfig::LoadConfig(IME_MODE::IME_MODE_DAYI);

            // Assert
            Assert::IsTrue(result);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_DAYI), static_cast<int>(CConfig::GetIMEMode()));
        }

        TEST_METHOD(MV02_01_ConfigLoad_PhoneticMode_Succeeds)
        {
            Logger::WriteMessage("Test: MV02_01_ConfigLoad_PhoneticMode_Succeeds\n");

            // Act
            CConfig::SetIMEMode(IME_MODE::IME_MODE_PHONETIC);
            BOOL result = CConfig::LoadConfig(IME_MODE::IME_MODE_PHONETIC);

            // Assert
            Assert::IsTrue(result);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_PHONETIC), static_cast<int>(CConfig::GetIMEMode()));
        }

        TEST_METHOD(MV02_01_ConfigLoad_GenericMode_Succeeds)
        {
            Logger::WriteMessage("Test: MV02_01_ConfigLoad_GenericMode_Succeeds\n");

            // Act
            CConfig::SetIMEMode(IME_MODE::IME_MODE_GENERIC);
            BOOL result = CConfig::LoadConfig(IME_MODE::IME_MODE_GENERIC);

            // Assert
            Assert::IsTrue(result);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_GENERIC), static_cast<int>(CConfig::GetIMEMode()));
        }

        // MV-02-02: Settings Controls Initialization Tests
        TEST_METHOD(MV02_02_Config_MaxCodes_GetSet)
        {
            Logger::WriteMessage("Test: MV02_02_Config_MaxCodes_GetSet\n");

            // Arrange
            int originalMaxCodes = CConfig::GetMaxCodes();

            // Act: Set MaxCodes
            CConfig::SetMaxCodes(8);

            // Assert
            Assert::AreEqual(8U, CConfig::GetMaxCodes());

            // Cleanup: Restore original value
            CConfig::SetMaxCodes(originalMaxCodes);
        }

        TEST_METHOD(MV02_02_Config_AutoCompose_GetSet)
        {
            Logger::WriteMessage("Test: MV02_02_Config_AutoCompose_GetSet\n");

            // Arrange
            BOOL originalAutoCompose = CConfig::GetAutoCompose();

            // Act
            CConfig::SetAutoCompose(TRUE);
            Assert::IsTrue(CConfig::GetAutoCompose());

            CConfig::SetAutoCompose(FALSE);
            Assert::IsFalse(CConfig::GetAutoCompose());

            // Cleanup
            CConfig::SetAutoCompose(originalAutoCompose);
        }

        TEST_METHOD(MV02_02_Config_ShowNotifyDesktop_GetSet)
        {
            Logger::WriteMessage("Test: MV02_02_Config_ShowNotifyDesktop_GetSet\n");

            // Arrange
            BOOL originalShowNotify = CConfig::GetShowNotifyDesktop();

            // Act
            CConfig::SetShowNotifyDesktop(TRUE);
            Assert::IsTrue(CConfig::GetShowNotifyDesktop());

            CConfig::SetShowNotifyDesktop(FALSE);
            Assert::IsFalse(CConfig::GetShowNotifyDesktop());

            // Cleanup
            CConfig::SetShowNotifyDesktop(originalShowNotify);
        }

        TEST_METHOD(MV02_02_Config_DoBeep_GetSet)
        {
            Logger::WriteMessage("Test: MV02_02_Config_DoBeep_GetSet\n");

            // Arrange
            BOOL originalDoBeep = CConfig::GetDoBeep();

            // Act
            CConfig::SetDoBeep(TRUE);
            Assert::IsTrue(CConfig::GetDoBeep());

            CConfig::SetDoBeep(FALSE);
            Assert::IsFalse(CConfig::GetDoBeep());

            // Cleanup
            CConfig::SetDoBeep(originalDoBeep);
        }

        TEST_METHOD(MV02_02_Config_DoBeepNotify_GetSet)
        {
            Logger::WriteMessage("Test: MV02_02_Config_DoBeepNotify_GetSet\n");

            // Arrange
            BOOL originalDoBeepNotify = CConfig::GetDoBeepNotify();

            // Act
            CConfig::SetDoBeepNotify(TRUE);
            Assert::IsTrue(CConfig::GetDoBeepNotify());

            CConfig::SetDoBeepNotify(FALSE);
            Assert::IsFalse(CConfig::GetDoBeepNotify());

            // Cleanup
            CConfig::SetDoBeepNotify(originalDoBeepNotify);
        }

        // MV-02-03: Settings Modification and Save Tests
        TEST_METHOD(MV02_03_Config_MultipleSettings_SaveAndReload)
        {
            Logger::WriteMessage("Test: MV02_03_Config_MultipleSettings_SaveAndReload\n");

            // Arrange: Save original values
            int originalMaxCodes = CConfig::GetMaxCodes();
            BOOL originalAutoCompose = CConfig::GetAutoCompose();
            BOOL originalShowNotify = CConfig::GetShowNotifyDesktop();

            // Act: Modify multiple settings
            CConfig::SetMaxCodes(7);
            CConfig::SetAutoCompose(TRUE);
            CConfig::SetShowNotifyDesktop(FALSE);

            // Write configuration
            CConfig::WriteConfig(FALSE);

            // Reload configuration
            CConfig::LoadConfig(CConfig::GetIMEMode());

            // Assert: Settings persisted
            Assert::AreEqual(7U, CConfig::GetMaxCodes());
            Assert::IsTrue(CConfig::GetAutoCompose());
            Assert::IsFalse(CConfig::GetShowNotifyDesktop());

            // Cleanup: Restore original values
            CConfig::SetMaxCodes(originalMaxCodes);
            CConfig::SetAutoCompose(originalAutoCompose);
            CConfig::SetShowNotifyDesktop(originalShowNotify);
            CConfig::WriteConfig(FALSE);
        }

        TEST_METHOD(MV02_03_Config_CustomTablePriority_GetSet)
        {
            Logger::WriteMessage("Test: MV02_03_Config_CustomTablePriority_GetSet\n");

            // Arrange: Use lowercase method names (actual API)
            BOOL originalPriority = CConfig::getCustomTablePriority();

            // Act
            CConfig::setCustomTablePriority(TRUE);
            Assert::IsTrue(CConfig::getCustomTablePriority());

            CConfig::setCustomTablePriority(FALSE);
            Assert::IsFalse(CConfig::getCustomTablePriority());

            // Cleanup
            CConfig::setCustomTablePriority(originalPriority);
        }

        // MV-02-05: Settings Validation Tests
        TEST_METHOD(MV02_05_Config_MaxCodes_BoundaryValues)
        {
            Logger::WriteMessage("Test: MV02_05_Config_MaxCodes_BoundaryValues\n");

            // Arrange
            int originalMaxCodes = CConfig::GetMaxCodes();

            // Act & Assert: Test boundary values
            CConfig::SetMaxCodes(1);
            Assert::AreEqual(1U, CConfig::GetMaxCodes());

            CConfig::SetMaxCodes(5);
            Assert::AreEqual(5U, CConfig::GetMaxCodes());

            CConfig::SetMaxCodes(10);
            Assert::AreEqual(10U, CConfig::GetMaxCodes());

            // Cleanup
            CConfig::SetMaxCodes(originalMaxCodes);
        }

        TEST_METHOD(MV02_05_Config_ArrayForceSP_GetSet)
        {
            Logger::WriteMessage("Test: MV02_05_Config_ArrayForceSP_GetSet\n");

            // Arrange
            BOOL originalArrayForceSP = CConfig::GetArrayForceSP();

            // Act
            CConfig::SetArrayForceSP(TRUE);
            Assert::IsTrue(CConfig::GetArrayForceSP());

            CConfig::SetArrayForceSP(FALSE);
            Assert::IsFalse(CConfig::GetArrayForceSP());

            // Cleanup
            CConfig::SetArrayForceSP(originalArrayForceSP);
        }

        // MV-02-06: Advanced Configuration Tests
        TEST_METHOD(MV02_06_Config_AllIMEModes_CanSwitch)
        {
            Logger::WriteMessage("Test: MV02_06_Config_AllIMEModes_CanSwitch\n");

            // Arrange
            IME_MODE originalMode = CConfig::GetIMEMode();

            // Act & Assert: Test IME mode switching
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_ARRAY), static_cast<int>(CConfig::GetIMEMode()));

            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_DAYI), static_cast<int>(CConfig::GetIMEMode()));

            CConfig::SetIMEMode(IME_MODE::IME_MODE_PHONETIC);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_PHONETIC), static_cast<int>(CConfig::GetIMEMode()));

            CConfig::SetIMEMode(IME_MODE::IME_MODE_GENERIC);
            Assert::AreEqual(static_cast<int>(IME_MODE::IME_MODE_GENERIC), static_cast<int>(CConfig::GetIMEMode()));

            // Cleanup
            CConfig::SetIMEMode(originalMode);
        }

        TEST_METHOD(MV02_06_Config_MakePhrase_GetSet)
        {
            Logger::WriteMessage("Test: MV02_06_Config_MakePhrase_GetSet\n");

            // Arrange
            BOOL originalMakePhrase = CConfig::GetMakePhrase();

            // Act
            CConfig::SetMakePhrase(TRUE);
            Assert::IsTrue(CConfig::GetMakePhrase());

            CConfig::SetMakePhrase(FALSE);
            Assert::IsFalse(CConfig::GetMakePhrase());

            // Cleanup
            CConfig::SetMakePhrase(originalMakePhrase);
        }

        TEST_METHOD(MV02_06_Config_MultipleConfigChanges_NoCrash)
        {
            Logger::WriteMessage("Test: MV02_06_Config_MultipleConfigChanges_NoCrash\n");

            // Act: Rapidly change multiple configuration settings
            for (int i = 0; i < 20; i++)
            {
                CConfig::SetMaxCodes((i % 5) + 5);
                CConfig::SetAutoCompose(i % 2 == 0);
                CConfig::SetShowNotifyDesktop(i % 3 == 0);
                CConfig::SetDoBeep(i % 4 == 0);
                CConfig::setCustomTablePriority(i % 5 == 0);
            }

            // Assert: No crash indicates success
            Assert::IsTrue(true, L"Multiple configuration changes completed without crash");
        }
    };
}
