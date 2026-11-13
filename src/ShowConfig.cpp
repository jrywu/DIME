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
#include "Globals.h"
#include "DIME.h"

//+---------------------------------------------------------------------------
//
// ITfFnConfigure::Show
//
// Launch DIMESettings.exe. The legacy PropertySheet used to follow a failed
// launch; phase 4e shows an error instead, and phase 5b-10 removed the
// PropertySheet (ConfigDialog.cpp) and its dialog resources.
//
//----------------------------------------------------------------------------

HRESULT CDIME::Show(_In_opt_ HWND hwndParent, _In_ LANGID inLangid, _In_ REFGUID inRefGuidProfile)
{
	debugPrint(L"CDIME::Show(), langid=%u", (UINT)inLangid);

	if (_IsStoreAppMode() || _IsUILessMode() || _IsSecureMode()) return S_OK;

	// Determine IME mode for the settings UI
	IME_MODE settingsMode = Global::imeMode;
	if (inRefGuidProfile == GUID_NULL)
		settingsMode = Global::imeMode;
	else if (inRefGuidProfile == Global::DIMEDayiGuidProfile)
		settingsMode = IME_MODE::IME_MODE_DAYI;
	else if (inRefGuidProfile == Global::DIMEPhoneticGuidProfile)
		settingsMode = IME_MODE::IME_MODE_PHONETIC;
	else if (inRefGuidProfile == Global::DIMEArrayGuidProfile)
		settingsMode = IME_MODE::IME_MODE_ARRAY;
	else if (inRefGuidProfile == Global::DIMEGenericGuidProfile)
		settingsMode = IME_MODE::IME_MODE_GENERIC;

	if (_IsComposing() && _pContext) _EndComposition(_pContext);
	_DeleteCandidateList(TRUE, _pContext);

	// Launch DIMESettings.exe --mode <mode>
	const wchar_t* modeName = L"dayi";
	switch (settingsMode) {
	case IME_MODE::IME_MODE_DAYI:     modeName = L"dayi"; break;
	case IME_MODE::IME_MODE_ARRAY:    modeName = L"array"; break;
	case IME_MODE::IME_MODE_PHONETIC: modeName = L"phonetic"; break;
	case IME_MODE::IME_MODE_GENERIC:  modeName = L"generic"; break;
	default: break;
	}

	// %ProgramW6432%\DIME\DIMESettings.exe, else %ProgramFiles%\DIME\.
	WCHAR exePath[MAX_PATH] = {};
	WCHAR programFiles[MAX_PATH] = {};
	if (GetEnvironmentVariable(L"ProgramW6432", programFiles, MAX_PATH) == 0)
		GetEnvironmentVariable(L"ProgramFiles", programFiles, MAX_PATH);
	StringCchPrintf(exePath, MAX_PATH, L"%s\\DIME\\DIMESettings.exe", programFiles);

	WCHAR cmdLine[MAX_PATH] = {};
	StringCchPrintf(cmdLine, MAX_PATH, L"\"%s\" --mode %s", exePath, modeName);
	debugPrint(L"CDIME::Show() launching: %s", cmdLine);

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	if (CreateProcessW(exePath, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		// Don't wait. DIMESettings runs on its own.
		// If already running, DIMESettings handles single-instance via mutex + WM_COPYDATA.
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return S_OK;
	}
	const DWORD err = GetLastError();
	debugPrint(L"CDIME::Show() CreateProcess failed: %lu", err);

	// ASCII source (code page 950). The text is the settings-program-missing error.
	WCHAR message[MAX_PATH + 160] = {};
	StringCchPrintf(message, _countof(message),
		L"\u627e\u4e0d\u5230\u8a2d\u5b9a\u7a0b\u5f0f\uff1a\n%s\n\n\u8acb\u91cd\u65b0\u5b89\u88dd DIME\u3002\uff08\u932f\u8aa4 %lu\uff09",
		exePath, err);
	MessageBoxW(hwndParent, message, L"DIME", MB_OK | MB_ICONERROR);
	return E_FAIL;
}