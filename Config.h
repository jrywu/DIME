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
#ifndef CCONFIG_H
#define CCONFIG_H
#pragma once

#include <Commdlg.h>

struct ColorInfo
{
	int id;
	COLORREF color;
};

typedef HPROPSHEETPAGE (__stdcall *_T_CreatePropertySheetPage)( LPCPROPSHEETPAGE lppsp );
typedef INT_PTR (__stdcall * _T_PropertySheet)(LPCPROPSHEETHEADER lppsph );
typedef BOOL (__stdcall * _T_ChooseColor)(_Inout_  LPCHOOSECOLOR lpcc);
typedef BOOL (__stdcall * _T_ChooseFont)(_Inout_  LPCHOOSEFONT lpcf);
typedef BOOL (__stdcall * _T_GetOpenFileName)(_Inout_  LPOPENFILENAME  lpofn);
typedef BOOL(__stdcall * _T_GetSaveFileName)(_Inout_  LPOPENFILENAME  lpofn);

class CConfig
{
public:
	//The configuration settings maybe read/write from ITfFnConfigure::Show() by explorer and which is before OnActivateEX(), thus no objects are created at that time.
	//Thus all the settings should be static and can be accesses program wide.
	CConfig(){}
	~CConfig(){clearReverseConvervsionInfoList();}
	//  configuration set/get
	static void SetIMEMode(IME_MODE imeMode);// { _imeMode = imeMode; }
	static IME_MODE GetIMEMode() { return _imeMode; }
	static void SetArrayScope(ARRAY_SCOPE arrayScope) { _arrayScope = arrayScope; }
	static ARRAY_SCOPE GetArrayScope() { return _arrayScope; }
	static void SetAutoCompose(BOOL autoCompose) {_autoCompose = autoCompose;}
	static BOOL GetAutoCompose() {return _autoCompose;}
	static void SetFontSize(UINT fontSize) {_fontSize = fontSize;}
	static UINT GetFontSize() {return _fontSize;}
	static void SetFontWeight(UINT fontWeight) {_fontWeight = fontWeight;}
	static UINT GetFontWeight() {return _fontWeight;}
	static BOOL GetFontItalic() {return _fontItalic;}
	static void SetFontItalic(BOOL fontItalic) {_fontItalic = fontItalic;}
	static void SetIMEShiftMode(IME_SHIFT_MODE imeShiftMode) { _imeShiftMode = imeShiftMode ; }
	static IME_SHIFT_MODE GetIMEShiftMode() { return _imeShiftMode; }
	static void SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE doubleSingleMode) { _doubleSingleByteMode = doubleSingleMode; }
	static DOUBLE_SINGLE_BYTE_MODE GetDoubleSingleByteMode() { return _doubleSingleByteMode; }
	static void SetMaxCodes(UINT maxCodes) { _maxCodes = maxCodes;}
	static UINT GetMaxCodes(){return _maxCodes;}
	// clear composing on beep when error 
	static void SetClearOnBeep(BOOL doBeep) { _clearOnBeep = doBeep; }
	static BOOL GetClearOnBeep() { return _clearOnBeep; }
	static void SetDoBeep(BOOL doBeep) { _doBeep = doBeep;}
	static BOOL GetDoBeep() {return _doBeep;}
	static void SetDoBeepNotify(BOOL doBeepNotify) { _doBeepNotify = doBeepNotify; }
	static BOOL GetDoBeepNotify() { return _doBeepNotify; }
	static void SetDoBeepOnCandi(BOOL doBeepOnCandi) { _doBeepOnCandi = doBeepOnCandi; }
	static BOOL GetDoBeepOnCandi() { return _doBeepOnCandi; }
	static void SetMakePhrase(BOOL makePhrase) { _makePhrase = makePhrase;}
	static BOOL GetMakePhrase() {return _makePhrase;}
	static void SetShowNotifyDesktop(BOOL showNotifyDesktop) { _showNotifyDesktop = showNotifyDesktop;}
	static BOOL GetShowNotifyDesktop() {return _showNotifyDesktop ;}
	static void SetFontFaceName(WCHAR *pFontFaceName) {StringCchCopy(_pFontFaceName, LF_FACESIZE,pFontFaceName);}
	static WCHAR* GetFontFaceName(){ return _pFontFaceName;}
	static void SetLoadTableMode(BOOL loadTableMode) { _loadTableMode = loadTableMode; }
	static BOOL GetLoadTableMode() { return _loadTableMode; }
	//custom table priority 
	static void setCustomTablePriority(BOOL priority) { _customTablePriority = priority; }
	static BOOL getCustomTablePriority() { return _customTablePriority; }

	//colors
	static void SetItemColor(UINT itemColor) { _itemColor = itemColor;}
	static COLORREF GetItemColor(){return _itemColor;}
	static void SetPhraseColor(UINT phraseColor) { _phraseColor = phraseColor;}
	static COLORREF GetPhraseColor(){return _phraseColor;}
	static void SetNumberColor(UINT numberColor) { _numberColor = numberColor;}
	static COLORREF GetNumberColor(){return _numberColor;}
	static void SetItemBGColor(UINT itemBGColor) { _itemBGColor = itemBGColor;}
	static COLORREF GetItemBGColor(){return _itemBGColor;}
	static void SetSelectedColor(UINT selectedColor) { _selectedColor = selectedColor;}
	static COLORREF GetSelectedColor(){return _selectedColor;}
	static void SetSelectedBGColor(UINT selectedBGColor) { _selectedBGColor = selectedBGColor;}
	static COLORREF GetSelectedBGColor(){return _selectedBGColor;}

	static void SetSpaceAsPageDown(BOOL spaceAsPageDown) { _spaceAsPageDown = spaceAsPageDown;}
	static BOOL GetSpaceAsPageDown() {return _spaceAsPageDown;}
	static void SetSpaceAsFirstCaniSelkey(BOOL spaceAsFirstCaniSelkey) { _spaceAsFirstCandSelkey = spaceAsFirstCaniSelkey; }
	static BOOL GetSpaceAsFirstCaniSelkey() { return _spaceAsFirstCandSelkey; }

	static void SetArrowKeySWPages(BOOL arrowKeySWPages) { _arrowKeySWPages = arrowKeySWPages;}
	static BOOL GetArrowKeySWPages() {return _arrowKeySWPages;}
	static void SetActivatedKeyboardMode(BOOL activatedKeyboardMode) { _activatedKeyboardMode = activatedKeyboardMode;}
	static BOOL GetActivatedKeyboardMode() {return _activatedKeyboardMode;}
	
	static void SetAppPermissionSet(BOOL appPermissionSet) { _appPermissionSet = appPermissionSet;}
	static BOOL GetAppPermissionSet() {return _appPermissionSet;}
	static void SetReloadReverseConversion(BOOL reloadReverseConversion) { _reloadReverseConversion = reloadReverseConversion;}
	static BOOL GetReloadReverseConversion() {return _reloadReverseConversion;}
	//array special code
	static void SetArrayNotifySP(BOOL arrayNotifySP) { _arrayNotifySP = arrayNotifySP;}
	static BOOL GetArrayNotifySP() {return _arrayNotifySP;}
	static void SetArrayForceSP(BOOL arrayForceSP) { _arrayForceSP = arrayForceSP;}
	static BOOL GetArrayForceSP() {return _arrayForceSP;}
	//array customPhrase
	static BOOL GetArraySingleQuoteCustomPhrase() { return _arraySingleQuoteCustomPhrase; }
	static void SetArraySingleQuoteCustomPhrase(BOOL singleQuoteQueryCustomPhrase) { _arraySingleQuoteCustomPhrase = singleQuoteQueryCustomPhrase; }

	//numeric pad 
	static NUMERIC_PAD GetNumericPad() { return _numericPad; }
	static void SetNumericPad(NUMERIC_PAD numericPad) { _numericPad = numericPad; }

	//dayi address/article mode
	static void setDayiArticleMode(BOOL dayiArticleMode) { _dayiArticleMode = dayiArticleMode; }
	static BOOL getDayiArticleMode() { return _dayiArticleMode; }

	//convert output string to simplifed chinese
	static void SetDoHanConvert(BOOL doHanConvert) { _doHanConvert = doHanConvert;}
	static BOOL GetDoHanConvert() {return _doHanConvert;}

	//reversion conversion
	static void SetReverseConvervsionInfoList (CDIMEArray <LanguageProfileInfo> *reverseConvervsionInfoList);
	CDIMEArray <LanguageProfileInfo> *GetReverseConvervsionInfoList() {return _reverseConvervsionInfoList;}
	static void SetReverseConverstionCLSID(CLSID reverseConverstionCLSID) { _reverseConverstionCLSID = reverseConverstionCLSID;}
	static CLSID GetReverseConverstionCLSID() {return _reverseConverstionCLSID;}
	static void SetReverseConversionGUIDProfile(GUID reverseConversionGUIDProfile) { _reverseConversionGUIDProfile = reverseConversionGUIDProfile;}
	static GUID GetReverseConversionGUIDProfile() {return _reverseConversionGUIDProfile;}
	static void SetReverseConversionDescription(WCHAR* reverseConversionDescription) { _reverseConversionDescription = reverseConversionDescription;}
	static WCHAR* GetReverseConversionDescription() {return _reverseConversionDescription;}

	//phonetic keyboard layout
	static void setPhoneticKeyboardLayout(PHONETIC_KEYBOARD_LAYOUT layout) { _phoneticKeyboardLayout = layout; }
	static PHONETIC_KEYBOARD_LAYOUT getPhoneticKeyboardLayout() { return _phoneticKeyboardLayout; }

	static VOID WriteConfig(BOOL silent = FALSE);
	static BOOL LoadConfig(IME_MODE imeMode);
	
	static void SetDefaultTextFont(HWND hWnd = nullptr);

	//configuration propertysheet dialog
	static INT_PTR CALLBACK CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	static INT_PTR CALLBACK DictionaryPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	friend void DrawColor(HWND hwnd, HDC hdc, COLORREF col);
	
	//shcore.dll GetDpiForMonitor pointer;
	static void SetGetDpiForMonitor(_T_GetDpiForMonitor getDpiForMonitor) {	_GetDpiForMonitor = getDpiForMonitor; }
	

private:
	//user setting variables
	static BOOL _loadTableMode;
	static IME_MODE _imeMode;
	static ARRAY_SCOPE _arrayScope;
	static NUMERIC_PAD _numericPad;
	static BOOL _autoCompose;
	static BOOL _customTablePriority;
	static BOOL _clearOnBeep;
	static BOOL _doBeep;
	static BOOL _doBeepNotify;
	static BOOL _doBeepOnCandi;
	static BOOL _appPermissionSet;
	static BOOL _reloadReverseConversion;
	static BOOL _activatedKeyboardMode;
	static BOOL _makePhrase;
    static UINT _fontSize;
	static UINT _fontWeight;
	static BOOL _fontItalic;
	static BOOL _showNotifyDesktop;
	static UINT _maxCodes;
	static WCHAR _pFontFaceName[LF_FACESIZE];
	static COLORREF _itemColor;
	static COLORREF _phraseColor;
	static COLORREF _numberColor;
	static COLORREF _itemBGColor;
	static COLORREF _selectedColor;
	static COLORREF _selectedBGColor;

	static BOOL _spaceAsPageDown;
	static BOOL _spaceAsFirstCandSelkey;
	static BOOL _arrowKeySWPages;

	static BOOL _arrayNotifySP;
	static BOOL _arrayForceSP;

	static BOOL _doHanConvert;

	static BOOL _dayiArticleMode;  // Article mode: input full-shaped symbols with address keys

	static PHONETIC_KEYBOARD_LAYOUT _phoneticKeyboardLayout;

	static IME_SHIFT_MODE _imeShiftMode;

	static DOUBLE_SINGLE_BYTE_MODE _doubleSingleByteMode;

	static BOOL _customTableChanged;
	
	static BOOL _arraySingleQuoteCustomPhrase;

	static struct _stat _initTimeStamp;

	static CDIMEArray <LanguageProfileInfo> *_reverseConvervsionInfoList;
	static CLSID _reverseConverstionCLSID;
	static GUID _reverseConversionGUIDProfile;
	static WCHAR* _reverseConversionDescription;

	static WCHAR _pwszINIFileName[MAX_PATH];
	static IME_MODE _configIMEMode;
	static WCHAR _pwzsDIMEProfile[MAX_PATH];

	static void clearReverseConvervsionInfoList();

	static ColorInfo colors[6];

	static UINT _dpiY;
	static _T_GetDpiForMonitor _GetDpiForMonitor;
	

	static void ParseConfig(HWND hDlg, BOOL initDiag = FALSE);

	static BOOL importCustomTableFile(_In_ HWND hDlg, _In_ LPCWSTR pathToLoad);
	static BOOL exportCustomTableFile(_In_ HWND hDlg, _In_ LPCWSTR pathToWrite);
	static BOOL parseCINFile(_In_ LPCWSTR pathToLoad, _In_ LPCWSTR pathToWrite, _In_ BOOL customTableMode = FALSE);
};


#endif