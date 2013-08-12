#ifndef CCONFIG_H
#define CCONFIG_H
#pragma once

class CConfig
{
public:
	//The configuration settings maybe read/write from ITfFnConfigure::Show() by explorer and which is before OnActivateEX(), thus no objects are created at that time.
	//Thus all the settings should be static and can be accesses program wide.
	CConfig(){}
	~CConfig(){}
	//  configuration set/get
	static void SetAutoCompose(BOOL autoCompose) {_autoCompose = autoCompose;}
	static BOOL GetAutoCompose() {return _autoCompose;}
	static void SetThreeCodeMode(BOOL threeCodeMode) {_threeCodeMode = threeCodeMode;}
	static BOOL GetThreeCodeMode() {return _threeCodeMode;}
	static void SetFontSize(UINT fontSize) {_fontSize = fontSize;}
	static UINT GetFontSize() {return _fontSize;}
	static void SetFontWeight(UINT fontWeight) {_fontWeight = fontWeight;}
	static UINT GetFontWeight() {return _fontWeight;}
	static BOOL GetFontItalic() {return _fontItalic;}
	static void SetFontItalic(BOOL fontItalic) {_fontItalic = fontItalic;}
	static void SetMaxCodes(UINT maxCodes) { _maxCodes = maxCodes;}
	static UINT GetMaxCodes(){return _maxCodes;}
	static void SetDoBeep(BOOL doBeep) { _doBeep = doBeep;}
	static BOOL GetDoBeep() {return _doBeep;}
	static void SetMakePhrase(BOOL makePhrase) { _makePhrase = makePhrase;}
	static BOOL GetMakePhrase() {return _makePhrase;}
	static void SetShowNotifyDesktop(BOOL showNotifyDesktop) { _showNotifyDesktop = showNotifyDesktop;}
	static BOOL GetShowNotifyDesktop() {return _showNotifyDesktop;}
	static void SetFontFaceName(WCHAR *pFontFaceName) {_pFontFaceName = pFontFaceName;}
	static WCHAR* GetFontFaceName(){ return _pFontFaceName;}
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
	static void SetArrowKeySWPages(BOOL arrowKeySWPages) { _arrowKeySWPages = arrowKeySWPages;}
	static BOOL GetArrowKeySWPages() {return _arrowKeySWPages;}
	static void SetActivatedKeyboardMode(BOOL activatedKeyboardMode) { _activatedKeyboardMode = activatedKeyboardMode;}
	static BOOL GetActivatedKeyboardMode() {return _activatedKeyboardMode;}
	static void SetAppPermissionSet(BOOL appPermissionSet) { _appPermissionSet = appPermissionSet;}
	static BOOL GetAppPermissionSet() {return _appPermissionSet;}

	static VOID WriteConfig();
	static VOID LoadConfig();
	
	static void SetDefaultTextFont();

	//configuration propertysheet dialog
	static INT_PTR CALLBACK CommonPropertyPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	friend void DrawColor(HWND hwnd, HDC hdc, COLORREF col);

private:
	//user setting variables
	static BOOL _autoCompose;
	static BOOL _threeCodeMode;
	static BOOL _doBeep;
	static BOOL _appPermissionSet;
	static BOOL _activatedKeyboardMode;
	static BOOL _makePhrase;
    static UINT _fontSize;
	static UINT _fontWeight;
	static BOOL _fontItalic;
	static BOOL _showNotifyDesktop;
	static UINT _maxCodes;
	static WCHAR* _pFontFaceName;
	static COLORREF _itemColor;
	static COLORREF _phraseColor;
	static COLORREF _numberColor;
	static COLORREF _itemBGColor;
	static COLORREF _selectedColor;
	static COLORREF _selectedBGColor;

	static BOOL _spaceAsPageDown;
	static BOOL _arrowKeySWPages;

	static struct _stat _initTimeStamp;

	
	
};

#endif