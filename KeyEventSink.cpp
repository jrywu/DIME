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
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "KeyHandlerEditSession.h"
#include "Compartment.h"

// 0xF003, 0xF004 are the keys that the touch keyboard sends for next/previous
#define THIRDPARTY_NEXTPAGE  static_cast<WORD>(0xF003)
#define THIRDPARTY_PREVPAGE  static_cast<WORD>(0xF004)

// Because the code mostly works with VKeys, here map a WCHAR back to a VKKey for certain
// vkeys that the IME handles specially
__inline UINT VKeyFromVKPacketAndWchar(UINT vk, WCHAR wch)
{
    UINT vkRet = vk;
	
	if (LOWORD(vk) == VK_PACKET)
    {
        if (wch == L' ')
        {
            vkRet = VK_SPACE;
        }
        else if ((wch >= L'0') && (wch <= L'9'))
        {
            vkRet = static_cast<UINT>(wch);
        }
        else if ((wch >= L'a') && (wch <= L'z'))
        {
            vkRet = (UINT)(L'A') + ((UINT)(L'z') - static_cast<UINT>(wch));
        }
        else if ((wch >= L'A') && (wch <= L'Z'))
        {
            vkRet = static_cast<UINT>(wch);
        }
		
        else if (wch == THIRDPARTY_NEXTPAGE)
        {
            vkRet = VK_NEXT;
        }
        else if (wch == THIRDPARTY_PREVPAGE)
        {
            vkRet = VK_PRIOR;
        }
    }
    return vkRet;
}

//+---------------------------------------------------------------------------
//
// _IsKeyEaten
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsKeyEaten(_In_ ITfContext *pContext, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState)
{
	debugPrint(L"CDIME::_IsKeyEaten(), codein = %d", codeIn);
    pContext;
    *pCodeOut = codeIn;


    BOOL isOpen = FALSE;
	CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentIMEMode);
    CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);

    BOOL isDoubleSingleByte = FALSE;
    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._GetCompartmentBOOL(isDoubleSingleByte);

  
    if (pKeyState)
    {
        pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
        pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;
    }
    if (pwch)
    {
        *pwch = L'\0';
    }

    // if the keyboard is disabled, we don't eat keys.
    if (_IsKeyboardDisabled())
    {
        return FALSE;
    }

    //
    // Map virtual key to character code
    //
    BOOL isTouchKeyboardSpecialKeys = FALSE;
    WCHAR wch = ConvertVKey(codeIn);
    *pCodeOut = VKeyFromVKPacketAndWchar(codeIn, wch);
    if ((wch == THIRDPARTY_NEXTPAGE) || (wch == THIRDPARTY_PREVPAGE))
    {
        // We always eat the above soft keyboard special keys
        isTouchKeyboardSpecialKeys = TRUE;
        if (pwch)
        {
            *pwch = wch;
        }
    }

    // if the keyboard is closed, we don't eat keys, with the exception of the touch keyboard specials keys
    if (!isOpen && !isDoubleSingleByte )
    {
        return isTouchKeyboardSpecialKeys;
    }

    if (pwch)
    {
        *pwch = wch;
    }

    //
    // Get composition engine
    //
    CCompositionProcessorEngine *pCompositionProcessorEngine;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

	//
	// check if the normal composition  need the key
	//
	if (isOpen && pCompositionProcessorEngine)
	{
		//
		// The candidate or phrase list handles the keys through ITfKeyEventSink.
		//
		// eat only keys that CKeyHandlerEditSession can handles.
		//
		UINT candiCount = 0;
		INT candiSelection = -1;
		if (_pUIPresenter)
		{
			_pUIPresenter->GetCount(&candiCount);
			candiSelection = _pUIPresenter->_GetCandidateSelection();
		}
	
		if (pCompositionProcessorEngine->IsVirtualKeyNeed(*pCodeOut, pwch, _IsComposing(), _candidateMode, _isCandidateWithWildcard, candiCount, candiSelection, pKeyState))
        {
            return TRUE;
        }
    }

    

    //
    // Double/Single byte
    //
    if (isDoubleSingleByte && pCompositionProcessorEngine && pCompositionProcessorEngine->IsDoubleSingleByte(wch))
    {
        if (_candidateMode == CANDIDATE_MODE::CANDIDATE_NONE)
        {
            if (pKeyState)
            {
                pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
                pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_DOUBLE_SINGLE_BYTE;
            }
            return TRUE;
        }
    }

    return isTouchKeyboardSpecialKeys;
}

//+---------------------------------------------------------------------------
//
// ConvertVKey
//
//----------------------------------------------------------------------------

WCHAR CDIME::ConvertVKey(UINT code)
{
    //
    // Map virtual key to scan code
    //
    UINT scanCode = 0;
    scanCode = MapVirtualKey(code, 0);

    //
    // Keyboard state
    //
    BYTE abKbdState[256] = {'\0'};
    if (!GetKeyboardState(abKbdState))
    {
        return 0;
    }

    //
    // Map virtual key to character code
    //
    WCHAR wch = '\0';
    if (ToUnicode(code, scanCode, abKbdState, &wch, 1, 0) == 1)
    {
        return wch;
    }

    return 0;
}

//+---------------------------------------------------------------------------
//
// _IsKeyboardDisabled
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsKeyboardDisabled()
{
    ITfDocumentMgr* pDocMgrFocus = nullptr;
    ITfContext* pContext = nullptr;
    BOOL isDisabled = FALSE;

    if ((_pThreadMgr && _pThreadMgr->GetFocus(&pDocMgrFocus) != S_OK) ||
        (pDocMgrFocus == nullptr))
    {
        // if there is no focus document manager object, the keyboard 
        // is disabled.
        isDisabled = TRUE;
    }
    else if ((pDocMgrFocus && pDocMgrFocus->GetTop(&pContext) != S_OK) ||
        (pContext == nullptr))
    {
        // if there is no context object, the keyboard is disabled.
        isDisabled = TRUE;
    }
    else
    {
        CCompartment CompartmentKeyboardDisabled(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_DISABLED);
        CompartmentKeyboardDisabled._GetCompartmentBOOL(isDisabled);

        CCompartment CompartmentEmptyContext(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_EMPTYCONTEXT);
        CompartmentEmptyContext._GetCompartmentBOOL(isDisabled);
    }

    if (pContext)
    {
        pContext->Release();
    }

    if (pDocMgrFocus)
    {
        pDocMgrFocus->Release();
    }

	debugPrint(L" CDIME::_IsKeyboardDisabled(), isDisabled = %d", isDisabled);

    return isDisabled;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnSetFocus
//
// Called by the system whenever this service gets the keystroke device focus.
//----------------------------------------------------------------------------

STDAPI CDIME::OnSetFocus(BOOL fForeground)
{
	fForeground;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnTestKeyDown
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
 {
	debugPrint(L" CDIME::OnTestKeyDown()");
    Global::UpdateModifiers(wParam, lParam);

	_KEYSTROKE_STATE KeystrokeState = { KEYSTROKE_CATEGORY::CATEGORY_NONE, KEYSTROKE_FUNCTION::FUNCTION_NONE };
    WCHAR wch = '\0';
    UINT code = 0;

	
	if (_pUIPresenter)
	{
		_pUIPresenter->ClearNotify();
		showChnEngNotify(_isChinese, 3000);
	}


    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, &KeystrokeState);

    if (KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION)
    {
        //
        // Invoke key handler edit session
        //
        KeystrokeState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
        _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
    }
	else if (KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE 
        && KeystrokeState.Function == KEYSTROKE_FUNCTION::FUNCTION_CANCEL) //cancel associated phrase with anykey.
	{
		_InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
	}
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnKeyDown
//
// Called by the system to offer this service a keystroke.  If *pIsEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
	debugPrint(L" CDIME::OnKeyDown()");
    Global::UpdateModifiers(wParam, lParam);
   
	_KEYSTROKE_STATE KeystrokeState = { KEYSTROKE_CATEGORY::CATEGORY_NONE, KEYSTROKE_FUNCTION::FUNCTION_NONE };
    WCHAR wch = '\0';
    UINT code = 0;

	if (_pUIPresenter)
	{
		_pUIPresenter->ClearNotify();
		showChnEngNotify(_isChinese, 3000);
	}

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, &KeystrokeState);

    if (*pIsEaten)
    {
		debugPrint(L" CDIME::OnKeyDown() eating the key");
	
        bool needInvokeKeyHandler = true;
        //
        // Invoke key handler edit session
        //
        if (code == VK_ESCAPE)
        {
            KeystrokeState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
        }

        // Always eat THIRDPARTY_NEXTPAGE and THIRDPARTY_PREVPAGE keys, but don't always process them.
        if ((wch == THIRDPARTY_NEXTPAGE) || (wch == THIRDPARTY_PREVPAGE))
        {
            needInvokeKeyHandler = !((KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_NONE) 
                && (KeystrokeState.Function == KEYSTROKE_FUNCTION::FUNCTION_NONE));
        }

        if (needInvokeKeyHandler)
        {
            _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
        }
    }
    else if (KeystrokeState.Category == KEYSTROKE_CATEGORY::CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION)
    {
        // Invoke key handler edit session
        KeystrokeState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
        _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnTestKeyUp
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
	debugPrint(L" CDIME::OnTestKeyUp()");
    if (pIsEaten == nullptr)
    {
        return E_INVALIDARG;
    }

    Global::UpdateModifiers(wParam, lParam);

    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, NULL);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnKeyUp
//
// Called by the system to offer this service a keystroke.  If *pIsEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
	debugPrint(L" CDIME::OnKeyUp()");
    Global::UpdateModifiers(wParam, lParam);

    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, NULL);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnPreservedKey
//
// Called when a hotkey (registered by us, or by the system) is typed.
//----------------------------------------------------------------------------

STDAPI CDIME::OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pIsEaten)
{
	pContext;
	debugPrint(L" CDIME::OnPreservedKey()");
	
    CCompositionProcessorEngine *pCompositionProcessorEngine;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

	if(pCompositionProcessorEngine)
		pCompositionProcessorEngine->OnPreservedKey(rguid, pIsEaten, _GetThreadMgr(), _GetClientId());

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InitKeyEventSink
//
// Advise a keystroke sink.
//----------------------------------------------------------------------------

BOOL CDIME::_InitKeyEventSink()
{
	debugPrint(L"CDIME::_InitKeyEventSink()");
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    HRESULT hr = S_OK;

    if ( (_pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)) )|| pKeystrokeMgr==nullptr )
    {
		debugPrint(L"CDIME::_InitKeyEventSink() failed");
        return FALSE;
    }

    hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink *)this, TRUE);

    pKeystrokeMgr->Release();

    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _UninitKeyEventSink
//
// Unadvise a keystroke sink.  Assumes we have advised one already.
//----------------------------------------------------------------------------

void CDIME::_UninitKeyEventSink()
{
	debugPrint(L"CDIME::_UninitKeyEventSink()");
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

    if ( ( _pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr))) || pKeystrokeMgr == nullptr)
    {
		debugPrint(L"CDIME::_UninitKeyEventSink() failed");
        return;
    }

    pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);

    pKeystrokeMgr->Release();
}
