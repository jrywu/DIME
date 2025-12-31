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
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "UIPresenter.h"


BOOL CDIME::VerifyDIMECLSID(_In_ REFCLSID clsid)
{
	if (IsEqualCLSID(clsid, Global::DIMECLSID))
	{
		return TRUE;
	}
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// ITfActiveLanguageProfileNotifySink::OnActivated
//
// Sink called by the framework when changes activate language profile.
//----------------------------------------------------------------------------

STDAPI CDIME::OnActivated(_In_ REFCLSID clsid, _In_ REFGUID guidProfile, _In_ BOOL isActivated)
{
	guidProfile;
	debugPrint(L"CDIME::OnActivated() isActivated = %d", isActivated);


	if (FALSE == VerifyDIMECLSID(clsid))
	{
		debugPrint(L"not our CLSID return now");
		return S_OK;
	}

	if (isActivated)
	{
		if (_pCompositionProcessorEngine == nullptr) return S_OK;
		Global::imeMode = _pCompositionProcessorEngine->GetImeModeFromGuidProfile(guidProfile);
		CConfig::SetIMEMode(Global::imeMode);
		_LoadConfig(TRUE);//force reversion coversion settings to be reload

		debugPrint(L"activating with imeMode = %d", Global::imeMode);
		_pCompositionProcessorEngine->SetImeMode(guidProfile);

		if (!_AddTextProcessorEngine())  return S_OK;

		ShowAllLanguageBarIcons();

		_lastKeyboardMode = CConfig::GetActivatedKeyboardMode();
		_isChinese = _lastKeyboardMode;
		debugPrint(L"CDIME::OnActivated() Set keyboard mode to last state = %d", _lastKeyboardMode);

		_newlyActivated = TRUE;


		// SetFocus to focused document manager for probing the composition range
		ITfDocumentMgr* pDocuMgr;
		if (SUCCEEDED(_GetThreadMgr()->GetFocus(&pDocuMgr)) && pDocuMgr)
		{
			OnSetFocus(pDocuMgr, NULL);
		}

	}
	else
	{
		debugPrint(L"_isChinese = %d", _isChinese);
		_lastKeyboardMode = _isChinese;
		_DeleteCandidateList(TRUE, nullptr);

		HideAllLanguageBarIcons();
	}

	return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InitActiveLanguageProfileNotifySink
//
// Advise a active language profile notify sink.
//----------------------------------------------------------------------------

BOOL CDIME::_InitActiveLanguageProfileNotifySink()
{
	ITfSource* pSource = nullptr;
	BOOL ret = FALSE;

	if (_pThreadMgr && _pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource) != S_OK)
	{
		return ret;
	}

	if (pSource && pSource->AdviseSink(IID_ITfActiveLanguageProfileNotifySink, (ITfActiveLanguageProfileNotifySink *)this, &_activeLanguageProfileNotifySinkCookie) != S_OK)
	{
		_activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;
		goto Exit;
	}

	ret = TRUE;

Exit:
	if (pSource)
		pSource->Release();
	return ret;
}

//+---------------------------------------------------------------------------
//
// _UninitActiveLanguageProfileNotifySink
//
// Unadvise a active language profile notify sink.  Assumes we have advised one already.
//----------------------------------------------------------------------------

void CDIME::_UninitActiveLanguageProfileNotifySink()
{
	ITfSource* pSource = nullptr;

	if (_activeLanguageProfileNotifySinkCookie == TF_INVALID_COOKIE)
	{
		return; // never Advised
	}

	if (_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource) == S_OK && pSource)
	{
		pSource->UnadviseSink(_activeLanguageProfileNotifySinkCookie);
		pSource->Release();
	}

	_activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;
}
