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
#include "SentenceBuffer.h"

#ifdef DIME_UNIT_TESTING
// Forward declaration for integration tests
namespace DIMEIntegratedTests {
    class TSFIntegrationTest;
}
#endif


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
    BOOL _GetFullShape() { return _isFullShape; }

    // functions for the composition object.
    void _SetComposition(_In_ ITfComposition *pComposition);
    void _TerminateComposition(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCalledFromDeactivate = FALSE);
    void _SaveCompositionContext(_In_ ITfContext *pContext);

    // key event handlers for composition/candidate/phrase common objects.
    HRESULT _HandleComplete(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCancel(TfEditCookie ec, _In_ ITfContext *pContext);

    // Routing-table handlers (phase 4a). The Rust product does not compile them:
    // session::handle_key does not call them, and KeyStateCategory.cpp is C++-build
    // only. Tests still compile that table, so the wrappers stay under DIME_UNIT_TESTING.
#if !defined(DIME_USE_RUST_ENGINE) || defined(DIME_UNIT_TESTING)
#define DIME_COMPILE_ROUTING_HANDLERS 1
#endif

    // key event handlers for composition object.
#ifdef DIME_COMPILE_ROUTING_HANDLERS
    HRESULT _HandleCompositionInput(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
#endif
    HRESULT _HandleCompositionFinalize(TfEditCookie ec, _In_ ITfContext *pContext, BOOL fCandidateList);
#ifdef DIME_COMPILE_ROUTING_HANDLERS
    HRESULT _HandleCompositionConvert(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isWildcardSearch, BOOL isArrayPhraseEnding = FALSE);
    HRESULT _HandleCompositionBackspace(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCompositionArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandleCompositionDoubleSingleByte(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
	HRESULT _HandleCompositionShiftEnglishInput(TfEditCookie ec, _In_ ITfContext *pContext, UINT code, WCHAR wch);
	HRESULT _HandleCompositionAddressChar(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
#endif
	// C++ versions of the handlers ported to Rust (session.rs); the ones above
	// dispatch to Rust under DIME_USE_RUST_ENGINE.
	HRESULT _HandleCompleteCpp(TfEditCookie ec, _In_ ITfContext *pContext);
	HRESULT _HandleCancelCpp(TfEditCookie ec, _In_ ITfContext *pContext);
	HRESULT _HandleCompositionDoubleSingleByteCpp(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
	HRESULT _HandleCompositionShiftEnglishInputCpp(TfEditCookie ec, _In_ ITfContext *pContext, UINT code, WCHAR wch);
	HRESULT _HandleCompositionAddressCharCpp(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);

    // sentence mode (SentenceHandler.cpp): keys handled while a sentence is pending
    HRESULT _HandleSentenceCommit(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleSentenceCommitCpp(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleSentenceBackspaceCpp(TfEditCookie ec, _In_ ITfContext *pContext, UINT code);
    HRESULT _HandleSentenceArrowKeyCpp(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction);
#ifdef DIME_COMPILE_ROUTING_HANDLERS
    HRESULT _HandleSentenceBackspace(TfEditCookie ec, _In_ ITfContext *pContext, UINT code);
    HRESULT _HandleSentenceArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction);
#endif

    // key event handlers for candidate object.
    HRESULT _HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext);
#ifdef DIME_COMPILE_ROUTING_HANDLERS
    HRESULT _HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch);
#endif

    // key event handlers for phrase object.
    HRESULT _HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext);
#ifdef DIME_COMPILE_ROUTING_HANDLERS
    HRESULT _HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch);
#endif

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
	void showHanConvertNotify(BOOL isSimplified, UINT delayShow = 0);

	
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

#ifdef DIME_UNIT_TESTING
	// Key-flow tests (tests/KeyFlowHarness.*): activate against a test thread manager
	// without the sinks ActivateEx advises (key events are delivered directly), and
	// read the state the tests record.
	HRESULT _TestActivate(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId, LANGID langid, REFGUID guidProfile);
	CANDIDATE_MODE _TestCandidateMode() const { return _candidateMode; }
	CUIPresenter* _TestUIPresenter() const { return _pUIPresenter; }
	ITfComposition* _TestComposition() const { return _pComposition; }
#endif

private:
	PROCESS_INTEGRITY_LEVEL _processIntegrityLevel;
	WCHAR _processName[MAX_PATH];
	
	void _LoadConfig(BOOL isForce = FALSE, IME_MODE imeMode = Global::imeMode);

    // functions for the composition object.
    HRESULT _HandleCompositionInputWorker(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext);
    // C++ versions of the candidate handlers ported to Rust (session.rs).
    HRESULT _HandleCandidateWorkerCpp(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCompositionInputCpp(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch);
    HRESULT _HandleCompositionInputWorkerCpp(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCompositionFinalizeCpp(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCandidateList);
    HRESULT _HandleCompositionConvertCpp(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isWildcardSearch, BOOL isArrayPhraseEnding);
    HRESULT _HandleCompositionBackspaceCpp(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandleCompositionArrowKeyCpp(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandleCandidateArrowKeyCpp(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandleCandidateSelectByNumberCpp(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch);
    HRESULT _HandlePhraseFinalizeCpp(TfEditCookie ec, _In_ ITfContext *pContext);
    HRESULT _HandlePhraseArrowKeyCpp(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction);
    HRESULT _HandlePhraseSelectByNumberCpp(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch);

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

    // _IsKeyEaten = _GatherKeyFacts (TSF / Win32 reads) + _DecideKeyEaten (no TSF calls).
    struct _KEY_FACTS
    {
        BOOL isOpen;              // Chinese mode (DIMEGuidCompartmentIMEMode)
        BOOL isDoubleSingleByte;  // full-shape compartment
        BOOL isKeyboardDisabled;
        WCHAR wch;                // character of the key with the current keyboard state
        BOOL isComposing;         // a composition exists
        UINT candiCount;          // candidate window items
        INT candiSelection;       // candidate window selection, -1 = none
    };
    _KEY_FACTS _GatherKeyFacts(UINT codeIn);
    BOOL _DecideKeyEaten(const _KEY_FACTS& facts, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);
    BOOL _DecideKeyEatenCpp(const _KEY_FACTS& facts, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);
#ifdef DIME_USE_RUST_ENGINE
    BOOL _DecideKeyEatenRust(const _KEY_FACTS& facts, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);
#endif

    BOOL _IsRangeCovered(TfEditCookie ec, _In_ ITfRange *pRangeTest, _In_ ITfRange *pRangeCover);
   

    WCHAR ConvertVKey(UINT code);

    BOOL _InitThreadFocusSink();
    void _UninitThreadFocusSink();

    BOOL _InitFunctionProviderSink();
    void _UninitFunctionProviderSink();

	BOOL _AddTextProcessorEngine(LANGID inLangID = 0, GUID inGuidProfile = GUID_NULL);

    BOOL VerifyDIMECLSID(_In_ REFCLSID clsid);

	//friend LRESULT CALLBACK CDIME_WindowProc(HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam);

	// The host of the Rust key handler flow (RustSessionHost.cpp).
	friend class CRustSessionHost;
	// OnPreservedKey's C++ body calls the _show*Cpp leaves.
	friend class CCompositionProcessorEngine;
#if defined(DIME_USE_RUST_ENGINE) || (defined(DIME_UNIT_TESTING) && defined(DIME_RUST_AVAILABLE))
	friend struct CLangBarHost;
#endif

#ifdef DIME_UNIT_TESTING
	// Friend declaration for integration tests - allows access to private members for testing
	friend class DIMEIntegratedTests::TSFIntegrationTest;
	friend class LangBarParityAccess;
#endif

	// function for process candidate
	VOID _DeleteCandidateList(BOOL fForce, _In_opt_ ITfContext *pContext);

	// sentence mode (SentenceHandler.cpp)
	BOOL _IsSentenceMode();
#ifdef DIME_USE_RUST_ENGINE
	BOOL _IsSentencePending();     // the sentence is in the Rust session
#else
	BOOL _IsSentencePending() { return !_sentence.IsEmpty(); }
#endif
	BOOL _IsSentenceKeyEaten(UINT code, _Inout_opt_ _KEYSTROKE_STATE *pKeyState);
	CSentenceLexicon* _GetSentenceLexicon();
	void _SentenceConvert();
	void _SentenceLearn();
	BOOL _SentenceUserFilePath(_Out_writes_(MAX_PATH) WCHAR* path);
	std::wstring _GetKeystrokeCode();
	HRESULT _SentenceRender(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& reading);
	HRESULT _SentenceWrite(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& text, size_t caret, BOOL withAttributes);
	HRESULT _SentenceCommitBeforeOutput(TfEditCookie ec, _In_ ITfContext *pContext);
	HRESULT _SentenceAppend(TfEditCookie ec, _In_ ITfContext *pContext, const std::vector<std::wstring>& candidates, int pinned);
	HRESULT _SentenceAppendFromCandidateList(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CDIMEArray<CCandidateListItem> *pCandidateList, int pinned);
	HRESULT _SentenceSelectCandidate(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& selected, BOOL isExplicit);
	HRESULT _SentenceOpenCandidates(TfEditCookie ec, _In_ ITfContext *pContext);
	HRESULT _SentenceCloseReading(TfEditCookie ec, _In_ ITfContext *pContext);
	void _SentenceFinalizeInComposition(TfEditCookie ec);
	void _SentenceReset();

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

	void _ConversionModeCompartmentUpdatedCpp(_In_ ITfThreadMgr *pThreadMgr, BOOL *setKeyboardOpenClose);
	void _PrivateCompartmentsUpdatedCpp(_In_ ITfThreadMgr *pThreadMgr);
	void _KeyboardOpenCompartmentUpdatedCpp(_In_ ITfThreadMgr *pThreadMgr, _In_ REFGUID guidCompartment);
	void _OnKeyboardClosedCpp();
	void _OnKeyboardOpenCpp();
	void _OnSwitchedToFullShapeCpp();
	void _OnSwitchedToHalfShapeCpp();
	void _LangShowText(const WCHAR* text, UINT len, UINT delay);
	void _showChnEngNotifyCpp(BOOL isChinese, UINT delayShow = 0);
	void _showFullHalfShapeNotifyCpp(BOOL isFullShape, UINT delayShow = 0);
	void _showHanConvertNotifyCpp(BOOL isSimplified, UINT delayShow = 0);
#if defined(DIME_USE_RUST_ENGINE) || (defined(DIME_UNIT_TESTING) && defined(DIME_RUST_AVAILABLE))
	void _ConversionModeCompartmentUpdatedRust(_In_ ITfThreadMgr *pThreadMgr, BOOL *setKeyboardOpenClose);
	void _PrivateCompartmentsUpdatedRust(_In_ ITfThreadMgr *pThreadMgr);
	void _KeyboardOpenCompartmentUpdatedRust(_In_ ITfThreadMgr *pThreadMgr, _In_ REFGUID guidCompartment);
	void _OnKeyboardClosedRust();
	void _OnKeyboardOpenRust();
	void _OnSwitchedToFullShapeRust();
	void _OnSwitchedToHalfShapeRust();
	void _showChnEngNotifyRust(BOOL isChinese, UINT delayShow);
	void _showFullHalfShapeNotifyRust(BOOL isFullShape, UINT delayShow);
	void _showHanConvertNotifyRust(BOOL isSimplified, UINT delayShow);
#endif

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

#ifdef DIME_USE_RUST_ENGINE
    // Rust-owned state (session.rs SessionState). Declared before the references
    // below, which the constructor binds to fields of it.
    struct DimeSession* _pRustSession;
    CANDIDATE_MODE& _candidateMode;
    CUIPresenter *_pUIPresenter;
    BOOL& _isCandidateWithWildcard;
#else
    CANDIDATE_MODE _candidateMode;
    CUIPresenter *_pUIPresenter;
    BOOL _isCandidateWithWildcard : 1;
#endif

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
	BOOL _isShiftedEnglish;
	BOOL _pendingWildcardInput;
	BOOL _lastKeyboardMode;
	
	CStringRange lastReadingString;

	BOOL _newlyActivated;

	// sentence mode state
	CSentenceBuffer _sentence;
	CSentenceLexicon* _pSentenceLexicon;
	CSentenceUserModel* _pSentenceUser;     // learned data: file + this process
	CSentenceUserModel* _pSentencePending;  // learned by this process since the last save
	BOOL _sentenceLexiconLoaded;
	int _sentenceEditPos;          // position edited by the candidate window, or -1
	BOOL _sentencePinNext;         // the next candidate finalize came from a select key
	std::wstring _sentenceDisplay; // current composition text (backs lastReadingString)
	std::wstring _rustComposingText[2]; // composing text from Rust (backs lastReadingString, RustSessionHost.cpp)
	UINT _rustComposingSlot = 0;
};



#endif
