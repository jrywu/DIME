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
	debugPrint(L"CTSFTTS::OnActivated() isActivated = %d", isActivated);
	guidProfile;

    if (FALSE == VerifyTSFTTSCLSID(clsid))
    {
        return S_OK;
    }

    if (isActivated)
    {
		if(!_AddTextProcessorEngine())  return S_OK;
		
		if(_pUIPresenter == nullptr)
			_pUIPresenter = new (std::nothrow) CUIPresenter(this, _pCompositionProcessorEngine);
		if (_pUIPresenter == nullptr) return S_OK;
    
		CConfig::LoadConfig();

		ShowAllLanguageBarIcons();
		BOOL activatedKeyboardMode = CConfig::GetActivatedKeyboardMode();
		ConversionModeCompartmentUpdated(_pThreadMgr, &activatedKeyboardMode );
    }
    else
    {
        _DeleteCandidateList(FALSE, nullptr);

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
