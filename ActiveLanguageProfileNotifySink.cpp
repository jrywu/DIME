//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "TSFTTS.h"
#include "CompositionProcessorEngine.h"
#include "UIPresenter.h"


BOOL CTSFTTS::VerifyTSFTTSCLSID(_In_ REFCLSID clsid)
{
    if (IsEqualCLSID(clsid, Global::TSFTTSCLSID))
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

STDAPI CTSFTTS::OnActivated(_In_ REFCLSID clsid, _In_ REFGUID guidProfile, _In_ BOOL isActivated)
{
	guidProfile;
	debugPrint(L"CTSFTTS::OnActivated() isActivated = %d", isActivated);


    if (FALSE == VerifyTSFTTSCLSID(clsid))
    {
		debugPrint(L"not our CLSID return now");
        return S_OK;
    }

    if (isActivated)
    {
		
		Global::imeMode = _pCompositionProcessorEngine->GetImeModeFromGuidProfile(guidProfile);
		debugPrint(L"activating with imeMode = %d", Global::imeMode);
		_pCompositionProcessorEngine->SetImeMode(guidProfile);
						
		if(!_AddTextProcessorEngine())  return S_OK;
		//_LoadConfig(TRUE);

		ShowAllLanguageBarIcons();
		
		//_lastKeyboardMode = CConfig::GetActivatedKeyboardMode();
		debugPrint(L"Set keyboard mode to last state = %d", _lastKeyboardMode);
		ConversionModeCompartmentUpdated(_pThreadMgr, &_lastKeyboardMode );
		if(CConfig::GetShowNotifyDesktop() )
		{	
			CStringRange notify;
			_pUIPresenter->ShowNotifyText(&notify.Set(_isChinese?L"中文":L"英文",2), 500, 3000, NOTIFY_CHN_ENG);
		}

		// SetFocus to focused document manager for probing the composition range
		ITfDocumentMgr* pDocuMgr;
		if(SUCCEEDED(_GetThreadMgr()->GetFocus(&pDocuMgr)) && pDocuMgr !=nullptr)
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

BOOL CTSFTTS::_InitActiveLanguageProfileNotifySink()
{
    ITfSource* pSource = nullptr;
    BOOL ret = FALSE;

    if (_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource) != S_OK)
    {
        return ret;
    }

    if (pSource->AdviseSink(IID_ITfActiveLanguageProfileNotifySink, (ITfActiveLanguageProfileNotifySink *)this, &_activeLanguageProfileNotifySinkCookie) != S_OK)
    {
        _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;
        goto Exit;
    }

    ret = TRUE;

Exit:
    pSource->Release();
    return ret;
}

//+---------------------------------------------------------------------------
//
// _UninitActiveLanguageProfileNotifySink
//
// Unadvise a active language profile notify sink.  Assumes we have advised one already.
//----------------------------------------------------------------------------

void CTSFTTS::_UninitActiveLanguageProfileNotifySink()
{
    ITfSource* pSource = nullptr;

    if (_activeLanguageProfileNotifySinkCookie == TF_INVALID_COOKIE)
    {
        return; // never Advised
    }

    if (_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource) == S_OK)
    {
        pSource->UnadviseSink(_activeLanguageProfileNotifySinkCookie);
        pSource->Release();
    }

    _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;
}
