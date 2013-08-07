//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


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
	debugPrint(L"CTSFTTS::OnActivated()\n");
	guidProfile;

    if (FALSE == VerifyTSFTTSCLSID(clsid))
    {
        return S_OK;
    }

    if (isActivated)
    {
        _AddTextProcessorEngine();
    }

    if (nullptr == _pCompositionProcessorEngine)
    {
        return S_OK;
    }

	 if (isActivated)
    {
		_pTSFTTSUIPresenter = new (std::nothrow) UIPresenter(this, _pCompositionProcessorEngine);
	}
    if (!_pTSFTTSUIPresenter)
    {
        return S_OK;
    }

    if (isActivated)
    {
        ShowAllLanguageBarIcons();

        ConversionModeCompartmentUpdated(_pThreadMgr);
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
