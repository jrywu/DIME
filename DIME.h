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
#ifndef DIME_H
#define DIME_H

#pragma once

#include "KeyHandlerEditSession.h"
#include "BaseStructure.h"
#include "Compartment.h"
#include "LanguageBar.h"


class CLangBarItemButton;
class CUIPresenter;
class CCompositionProcessorEngine;
class CReverseConversion;

//const DWORD WM_CheckGlobalCompartment = WM_USER;
//LRESULT CALLBACK CDIME_WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam);

class CDIME : 
	public ITfTextInputProcessorEx,
    public ITfThreadMgrEventSink,
    public ITfTextEditSink,
    public ITfKeyEventSink,
    public ITfCompositionSink,
    public ITfDisplayAttributeProvider,
    public ITfActiveLanguageProfileNotifySink,
    public ITfThreadFocusSink,
    public ITfFunctionProvider,
    public ITfFnGetPreferredTouchKeyboardLayout,
	public ITfFnConfigure,//control panel application
	public ITfReverseConversionMgr
{
public:
    CDIME();
    ~CDIME();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
        return ActivateEx(pThreadMgr, tfClientId, 0);
    }
    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags);
    STDMETHODIMP Deactivate();

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(_In_ ITfDocumentMgr *pDocMgr);
    STDMETHODIMP OnUninitDocumentMgr(_In_ ITfDocumentMgr *pDocMgr);
    STDMETHODIMP OnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus, _In_opt_ ITfDocumentMgr *pDocMgrPrevFocus);
    STDMETHODIMP OnPushContext(_In_ ITfContext *pContext);
    STDMETHODIMP OnPopContext(_In_ ITfContext *pContext);

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(__RPC__in_opt ITfContext *pContext, TfEditCookie ecReadOnly, __RPC__in_opt ITfEditRecord *pEditRecord);

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground);
    STDMETHODIMP OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten);
    STDMETHODIMP OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten);
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten);
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten);
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pIsEaten);

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, _In_ ITfComposition *pComposition);

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(__RPC__deref_out_opt IEnumTfDisplayAttributeInfo **ppEnum);
    STDMETHODIMP GetDisplayAttributeInfo(__RPC__in REFGUID guidInfo, __RPC__deref_out_opt ITfDisplayAttributeInfo **ppInfo);

    // ITfActiveLanguageProfileNotifySink
    STDMETHODIMP OnActivated(_In_ REFCLSID clsid, _In_ REFGUID guidProfile, _In_ BOOL isActivated);

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus();
    STDMETHODIMP OnKillThreadFocus();

    // ITfFunctionProvider
    STDMETHODIMP GetType(__RPC__out GUID *pguid);
    STDMETHODIMP GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc);
    STDMETHODIMP GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk);

    // ITfFunction
    STDMETHODIMP GetDisplayName(_Out_ BSTR *pbstrDisplayName);

    // ITfFnGetPreferredTouchKeyboardLayout, it is the Optimized layout feature.
    STDMETHODIMP GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId);

	//ITfFnConfigure 
	STDMETHODIMP Show(_In_opt_ HWND hwndParent, _In_ LANGID langid, _In_ REFGUID rguidProfile);
	
	//ITfReverseConversionMgr 
	STDMETHODIMP GetReverseConversion(_In_ LANGID langid, _In_   REFGUID guidProfile, _In_ DWORD dwflag, _Out_ ITfReverseConversion **ppReverseConversion);

	void _AsyncReverseConversion(_In_ ITfContext* pContext);
	HRESULT _AsyncReverseConversionNotification(_In_ TfEditCookie ec,_In_ ITfContext *pContext);

	void ReleaseReverseConversion();
	 // Get language profile.
    GUID GetLanguageProfile(LANGID *plangid){ *plangid = _langid;  return _guidProfile;}
    // Get locale
    LCID GetLocale(){return MAKELCID(_langid, SORT_DEFAULT);}
    

    // CClassFactory factory callback
    static HRESULT CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj);

    // utility function for thread manager.
    ITfThreadMgr* _GetThreadMgr() { return _pThreadMgr; }
    TfClientId _GetClientId() { return _tfClientId; }

    // functions for the composition object.
    void _SetComposition(_In_ ITfComposition *pComposition);
    void _TerminateComposition(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCalledFromDeactivate = FALSE);
    void _SaveCompositionContext(_In_ ITfContext *pContext);

    // key event handlers for composition/candidate/phrase common objects.
    HRESULT _HandleComplete(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCancel(TfEditCookie ec, _In_ ITfContext *pContext);

    // key event handlers for composition object.
    HRESULT _HandleCompositionInput(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
    HRESULT _HandleCompositionFinalize(TfEditCookie ec, _In_ ITfContext *pContext, BOOL fCandidateList);
    HRESULT _HandleCompositionConvert(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isWildcardSearch, BOOL isArrayPhraseEnding = FALSE);
    HRESULT _HandleCompositionBackspace(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCompositionArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandleCompositionDoubleSingleByte(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
	HRESULT _HandleCompositionAddressChar(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
	
    // key event handlers for candidate object.
    HRESULT _HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch);

    // key event handlers for phrase object.
    HRESULT _HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch);

    static BOOL _IsSecureMode(void) { return (_dwActivateFlags & TF_TMAE_SECUREMODE) ? TRUE : FALSE; }
    static BOOL _IsComLess(void) { return (_dwActivateFlags & TF_TMAE_COMLESS) ? TRUE : FALSE; }
	static BOOL _IsStoreAppMode(void) { return (_dwActivateFlags & TF_TMF_IMMERSIVEMODE) ? TRUE : FALSE; }
	BOOL _IsUILessMode(void);

    CCompositionProcessorEngine* GetCompositionProcessorEngine() { return (_pCompositionProcessorEngine); }

    // comless helpers
    static HRESULT CreateInstance(REFCLSID rclsid, REFIID riid, _Outptr_result_maybenull_ LPVOID* ppv, _Out_opt_ HINSTANCE* phInst, BOOL isComLessMode);
    static HRESULT ComLessCreateInstance(REFGUID rclsid, REFIID riid, _Outptr_result_maybenull_ void **ppv, _Out_opt_ HINSTANCE *phInst);
    static HRESULT GetComModuleName(REFGUID rclsid, _Out_writes_(cchPath)WCHAR* wchPath, DWORD cchPath);
	
	
	//Called when compartment status changed.
	void OnKeyboardClosed();
	void OnKeyboardOpen();
	void OnSwitchedToFullShape();
	void OnSwitchedToHalfShape();
	void showChnEngNotify(BOOL isChinese, UINT delayShow = 0);
	void showFullHalfShapeNotify(BOOL isFullShape, UINT delayShow = 0);

	
	// function for probe caret position when showing notify 
	HRESULT _ProbeCompositionRangeNotification(_In_ TfEditCookie ec,_In_ ITfContext *pContext);
	void _ProbeComposition(_In_ ITfContext *pContext);

	BOOL _IsComposing();

	//warning beeps and messages in notify window
	void DoBeep(BEEP_TYPE type);

	//integrity level and process name specific functions
	BOOL isHighIntegrityProcess(){ return (_processIntegrityLevel == PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_HIGH || _processIntegrityLevel == PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_SYSTEM); }
	BOOL isLowIntegrityProcess() { return (_processIntegrityLevel == PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_LOW || _processIntegrityLevel == PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_UNKNOWN); }
	BOOL isWin7IEProcess() { return  !Global::isWindows8 && (CompareString(GetLocale(), NORM_IGNORECASE, _processName, -1, L"IEXPLORE.EXE", -1) == CSTR_EQUAL); }
	BOOL isCMDShell() { return  !Global::isWindows8 && (CompareString(GetLocale(), NORM_IGNORECASE, _processName, -1, L"CMD.EXE", -1) == CSTR_EQUAL); }
	BOOL isBlackListedProcessForProbeComposition() { return  (CompareString(GetLocale(), NORM_IGNORECASE, _processName, -1, L"firefox.exe", -1) == CSTR_EQUAL); }
	PROCESS_INTEGRITY_LEVEL GetProcessIntegrityLevel() { return _processIntegrityLevel; }

private:
	PROCESS_INTEGRITY_LEVEL _processIntegrityLevel;
	WCHAR _processName[MAX_PATH];
	
	void _LoadConfig(BOOL isForce = FALSE, IME_MODE imeMode = Global::imeMode);

    // functions for the composition object.
    HRESULT _HandleCompositionInputWorker(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext);

	//for probing the composition range to get correct caret position
	
    void _StartComposition(_In_ ITfContext *pContext);
    void _EndComposition(_In_opt_ ITfContext *pContext);
    BOOL _IsKeyboardDisabled();

    
    HRESULT _AddCharAndFinalize(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString);
	HRESULT _AddComposingAndChar(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString);
	BOOL _FindComposingRange(TfEditCookie ec, _In_ ITfContext *pContext, _In_ ITfRange *pSelection, _Outptr_result_maybenull_ ITfRange **ppRange);
    HRESULT _SetInputString(TfEditCookie ec, _In_ ITfContext *pContext, _Out_opt_ ITfRange *pRange, _In_ CStringRange *pstrAddString, BOOL exist_composing);
    HRESULT _InsertAtSelection(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString, _Outptr_ ITfRange **ppCompRange);

    HRESULT _RemoveDummyCompositionForComposing(TfEditCookie ec, _In_ ITfComposition *pComposition);

    // Invoke key handler edit session
    HRESULT _InvokeKeyHandler(_In_ ITfContext *pContext, UINT code, WCHAR wch, DWORD flags, _KEYSTROKE_STATE keyState);

    // function for the language property
    BOOL _SetCompositionLanguage(TfEditCookie ec, _In_ ITfContext *pContext);

    // function for the display attribute
    void _ClearCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext);
    BOOL _SetCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext, TfGuidAtom gaDisplayAttribute);
    BOOL _InitDisplayAttributeGuidAtom();

    BOOL _InitThreadMgrEventSink();
    void _UninitThreadMgrEventSink();

    BOOL _InitTextEditSink(_In_ ITfDocumentMgr *pDocMgr);
	void _UnInitTextEditSink();

    BOOL _UpdateLanguageBarOnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus);

    BOOL _InitKeyEventSink();
    void _UninitKeyEventSink();

    //BOOL _InitMouseEventSink();
    //void _UninitMouseEventSink();

    BOOL _InitActiveLanguageProfileNotifySink();
    void _UninitActiveLanguageProfileNotifySink();

    BOOL _IsKeyEaten(_In_ ITfContext *pContext, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);

    BOOL _IsRangeCovered(TfEditCookie ec, _In_ ITfRange *pRangeTest, _In_ ITfRange *pRangeCover);
   

    WCHAR ConvertVKey(UINT code);

    BOOL _InitThreadFocusSink();
    void _UninitThreadFocusSink();

    BOOL _InitFunctionProviderSink();
    void _UninitFunctionProviderSink();

	BOOL _AddTextProcessorEngine(LANGID inLangID = 0, GUID inGuidProfile = GUID_NULL);

    BOOL VerifyDIMECLSID(_In_ REFCLSID clsid);

    //friend LRESULT CALLBACK CDIME_WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam);

	// function for process candidate
	VOID _DeleteCandidateList(BOOL fForce, _In_opt_ ITfContext *pContext);

	//language bar private
    BOOL InitLanguageBar(_In_ CLangBarItemButton *pLanguageBar, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment);
    void SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode);
	void InitializeDIMECompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    void CreateLanguageBarButton(DWORD dwEnable, GUID guidLangBar, _In_z_ LPCWSTR pwszDescriptionValue, _In_z_ LPCWSTR pwszTooltipValue, DWORD dwOnIconIndex, DWORD dwOffIconIndex, _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton, BOOL isSecureMode);
    static HRESULT CompartmentCallback(_In_ void *pv, REFGUID guidCompartment);
    void PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr);
    void KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr, _In_ REFGUID guidCompartment);
	// Language bar control
    BOOL SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode);
    void SetLanguageBarStatus(DWORD status, BOOL isSet);
    void ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr, BOOL *setKeyboardOpenClose = NULL);
    void ShowAllLanguageBarIcons();
    void HideAllLanguageBarIcons();
	


private:
	
	LANGID _langid;
    GUID _guidProfile;
	// Language bar data
    CLangBarItemButton* _pLanguageBar_IMEModeW8;
	CLangBarItemButton* _pLanguageBar_IMEMode;
    CLangBarItemButton* _pLanguageBar_DoubleSingleByte;

    // Compartment
    CCompartment* _pCompartmentConversion;
	CCompartmentEventSink* _pCompartmentIMEModeEventSink;
    CCompartmentEventSink* _pCompartmentConversionEventSink;
    CCompartmentEventSink* _pCompartmentKeyboardOpenEventSink;
    CCompartmentEventSink* _pCompartmentDoubleSingleByteEventSink;


    ITfThreadMgr* _pThreadMgr;
    TfClientId _tfClientId;
    static DWORD _dwActivateFlags;

    // The cookie of ThreadMgrEventSink
    DWORD _threadMgrEventSinkCookie;

    ITfContext* _pTextEditSinkContext;
    DWORD _textEditSinkCookie;

    // The cookie of ActiveLanguageProfileNotifySink
    DWORD _activeLanguageProfileNotifySinkCookie;

    // The cookie of ThreadFocusSink
    DWORD _dwThreadFocusSinkCookie;

    // Composition Processor Engine object.
    CCompositionProcessorEngine* _pCompositionProcessorEngine;

    // Language bar item object.
    CLangBarItemButton* _pLangBarItem;

    // the current composition object.
    ITfComposition* _pComposition;

    // guidatom for the display attribute.
    TfGuidAtom _gaDisplayAttributeInput;
    TfGuidAtom _gaDisplayAttributeConverted;

    CANDIDATE_MODE _candidateMode;
    CUIPresenter *_pUIPresenter;
    BOOL _isCandidateWithWildcard : 1;

    ITfDocumentMgr* _pDocMgrLastFocused;

    ITfContext* _pContext;

    ITfCompartment* _pSIPIMEOnOffCompartment;
    DWORD _dwSIPIMEOnOffCompartmentSinkCookie;

    HWND _msgWndHandle; 

    LONG _refCount;

    // Support the search integration
    ITfFnSearchCandidateProvider* _pITfFnSearchCandidateProvider;

	
	//ReverseConversion COM provider object
	CReverseConversion * _pReverseConversion[IM_SLOTS];
	//ReverseConversion Interface object
	ITfReverseConversion* _pITfReverseConversion[IM_SLOTS];

	WCHAR _commitString[MAX_COMMIT_LENGTH];
	WCHAR _commitKeyCode[MAX_KEY_LENGTH];

	BOOL _isChinese;
	BOOL _isFullShape;
	BOOL _lastKeyboardMode;
	
	CStringRange lastReadingString;

	BOOL _newlyActivated;
};



#endif
