!include MUI2.nsh
!include "Registry.nsh"
!include x64.nsh


!define PRODUCT_NAME "DIME"
!define PRODUCT_VERSION "1.1a"
!define PRODUCT_PUBLISHER "Jeremy Wu"
!define PRODUCT_WEB_SITE "http://github.com/jrywu/DIME"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
; ## HKLM = HKEY_LOCAL_MACHINE
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; ## HKCU = HKEY_CURRENT_USER

SetCompressor lzma
ManifestDPIAware true
BrandingText " "

; MUI 1.67 compatible ------
!include "MUI.nsh"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_NOSTRETCH

; Language Selection Dialog Settings
!define MUI_LANGDLL_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_LANGDLL_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "NSIS:Language"

; Welcome page
!insertmacro MUI_PAGE_WELCOME
; License page
;!insertmacro MUI_PAGE_LICENSE "LICENSE-zh-Hant.rtf"
; Directory page
;!insertmacro MUI_PAGE_DIRECTORY
; Instfiles page
!insertmacro MUI_PAGE_INSTFILES
; Finish page
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

; Language files
;!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "TradChinese"


; MUI end ------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
RequestExecutionLevel admin
OutFile "DIME-x8664.exe"
InstallDir "$PROGRAMFILES64\DIME"
ShowInstDetails show
ShowUnInstDetails show

; Language Strings
LangString DESC_REMAINING ${LANG_TradChinese} " ( 剩餘 %d %s%s )"
LangString DESC_PROGRESS ${LANG_TradChinese} "%d.%01dkB" ;"%dkB (%d%%) of %dkB @ %d.%01dkB/s"
LangString DESC_PLURAL ${LANG_TradChinese} " "
LangString DESC_HOUR ${LANG_TradChinese} "小時"
LangString DESC_MINUTE ${LANG_TradChinese} "分鐘"
LangString DESC_SECOND ${LANG_TradChinese} "秒"
LangString DESC_CONNECTING ${LANG_TradChinese} "連接中..."
LangString DESC_DOWNLOADING ${LANG_TradChinese} "下載 %s"
LangString DESC_INSTALLING ${LANG_TradChinese} "安裝中"
LangString DESC_DOWNLOADING1 ${LANG_TradChinese} "下載中"
LangString DESC_DOWNLOADFAILED ${LANG_TradChinese} "下載失敗:"
LangString DESC_VCX86 ${LANG_TradChinese} "Visual C++ 2015 Redistritable x86"
LangString DESC_VCX64 ${LANG_TradChinese} "Visual C++ 2015 Redistritable x64"
LangString DESC_VCX86_DECISION ${LANG_TradChinese} "安裝此輸入法之前，必須先安裝 $(DESC_VCX86)，若你想繼續安裝 \
  ，您的電腦必須連接網路。$\n您要繼續這項安裝嗎？"
LangString DESC_VCX64_DECISION ${LANG_TradChinese} "安裝此輸入法之前，必須先安裝 $(DESC_VCX64)，若你想繼續安裝 \
  ，您的電腦必須連接網路。$\n您要繼續這項安裝嗎？"
!define BASE_URL http://download.microsoft.com/download
;!define URL_VC_REDISTX64_2012U3  "${BASE_URL}/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU3/vcredist_x64.exe"
;!define URL_VC_REDISTX86_2012U3  "${BASE_URL}/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU3/vcredist_x86.exe";
;!define URL_VC_REDISTX64_2013  ${BASE_URL}/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x64.exe
;!define URL_VC_REDISTX86_2013  ${BASE_URL}/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x86.exe
;!define URL_VC_REDISTX64_2015  ${BASE_URL}/9/3/F/93FCF1E7-E6A4-478B-96E7-D4B285925B00/vc_redist.x64.exe
;!define URL_VC_REDISTX86_2015  ${BASE_URL}/9/3/F/93FCF1E7-E6A4-478B-96E7-D4B285925B00/vc_redist.x86.exe
;U2
;!define URL_VC_REDISTX86_2015 ${BASE_URL}/2/5/e/25e2436f-46a4-4854-b3ac-cfcb69cde2da/vc_redist.x86.exe
;!define URL_VC_REDISTX64_2015 ${BASE_URL}/a/3/6/a365bea0-3cc9-4f50-912a-3efaf5e96316/vc_redist.x64.exe
;U3
!define URL_VC_REDISTX64_2015 ${BASE_URL}/0/6/4/064F84EA-D1DB-4EAA-9A5C-CC2F0FF6A638/vc_redist.x64.exe
!define URL_VC_REDISTX86_2015 ${BASE_URL}/0/6/4/064F84EA-D1DB-4EAA-9A5C-CC2F0FF6A638/vc_redist.x86.exe

; VC2013 12.0.21005
;!define VC2013X64_key "{A749D8E6-B613-3BE3-8F5F-045C84EBA29B}"
;!define VC2013X86_key "{13A4EE12-23EA-3371-91EE-EFB36DDFFF3E}"
; VC2015 14.0.23026
;!define VC2015X64_key {0D3E9E15-DE7A-300B-96F1-B4AF12B96488}
;!define VC2015X86_key  {A2563E55-3BEC-3828-8D67-E5E8B9E8B675}
; VC2015 14.0.24123
!define VC2015X64_key {FDBE9DB4-7A91-3A28-B27E-705EF7CFAE57}
!define VC2015X86_key {06AE3BCC-7612-39D3-9F3B-B6601D877D02}

Var "URL_VCX86"
Var "URL_VCX64"

Function .onInit
  InitPluginsDir
  StrCpy $URL_VCX86 "${URL_VC_REDISTX86_2015}"
  StrCpy $URL_VCX64 "${URL_VC_REDISTX64_2015}"
    ${If} ${RunningX64}
  	SetRegView 64
  ${EndIf}
  ReadRegStr $0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion"
  StrCmp $0 "" StartInstall 0

  MessageBox MB_OKCANCEL|MB_ICONQUESTION "偵測到舊版 $0，必須先移除才能安裝新版。是否要現在進行？" IDOK +2
  	Abort
  ExecWait '"$INSTDIR\uninst.exe" /S _?=$INSTDIR'
   ${If} ${RunningX64}
  	${DisableX64FSRedirection}
  	IfFileExists "$SYSDIR\DIME.dll"  0 CheckX64     ;代表反安裝失敗
  		Abort
  CheckX64:
 	${EnableX64FSRedirection}
  ${EndIf}
  IfFileExists "$SYSDIR\DIME.dll"  0 RemoveFinished     ;代表反安裝失敗
        Abort
  RemoveFinished:
    	MessageBox MB_ICONINFORMATION|MB_OK "舊版已移除。"
StartInstall:
;!insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Section "CheckVCRedist" VCR
  Push $R0
  ${If} ${RunningX64}
  	SetRegView 64
  	ClearErrors
  	ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\${VC2015X64_key}" "Version"
  	IfErrors 0 VCx64RedistInstalled
  	MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 "$(DESC_VCX64_DECISION)" /SD IDNO IDYES +1 IDNO VCRedistInstalledAbort
  	AddSize 7000
  	nsisdl::download /TRANSLATE "$(DESC_DOWNLOADING)" "$(DESC_CONNECTING)" \
       "$(DESC_SECOND)" "$(DESC_MINUTE)" "$(DESC_HOUR)" "$(DESC_PLURAL)" \
       "$(DESC_PROGRESS)" "$(DESC_REMAINING)" \
       /TIMEOUT=30000 "$URL_VCX64" "$PLUGINSDIR\vcredist_x64.exe"
    	Pop $0
    	StrCmp "$0" "success" lbl_continue64
    	DetailPrint "$(DESC_DOWNLOADFAILED) $0"
    	Abort
     lbl_continue64:
      DetailPrint "$(DESC_INSTALLING) $(DESC_VCX64)..."
      nsExec::ExecToStack "$PLUGINSDIR\vcredist_x64.exe /quiet"
      ;pop $DOTNET_RETURN_CODE
VCx64RedistInstalled:
 SetRegView 32
${EndIf}
 ClearErrors
  ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\${VC2015X86_key}" "Version"
  IfErrors 0 VCRedistInstalled
  MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 "$(DESC_VCX86_DECISION)" /SD IDNO IDYES +1 IDNO VCRedistInstalledAbort
  AddSize 7000
  nsisdl::download /TRANSLATE "$(DESC_DOWNLOADING)" "$(DESC_CONNECTING)" \
       "$(DESC_SECOND)" "$(DESC_MINUTE)" "$(DESC_HOUR)" "$(DESC_PLURAL)" \
       "$(DESC_PROGRESS)" "$(DESC_REMAINING)" \
       /TIMEOUT=30000 "$URL_VCX86" "$PLUGINSDIR\vcredist_x86.exe"
    Pop $0
    StrCmp "$0" "success" lbl_continue
    DetailPrint "$(DESC_DOWNLOADFAILED) $0"
    Abort

    lbl_continue:
      DetailPrint "$(DESC_INSTALLING) $(DESC_VCX86)..."
      nsExec::ExecToStack "$PLUGINSDIR\vcredist_x86.exe /quiet"
      ;pop $DOTNET_RETURN_CODE
  Goto VCRedistInstalled
VCRedistInstalledAbort:
  Quit
VCRedistInstalled:
  Exch $R0
SectionEnd


Section "MainSection" SEC01
  SetOutPath "$SYSDIR"
  SetOverwrite ifnewer
  ${If} ${RunningX64}
  	${DisableX64FSRedirection}
  	File "system32.x64\DIME.dll"
  	ExecWait '"$SYSDIR\regsvr32.exe" /s $SYSDIR\DIME.dll'
  	File "system32.x64\*.dll"
  	${EnableX64FSRedirection}
  ${EndIf}
  File "system32.x86\DIME.dll"
  ExecWait '"$SYSDIR\regsvr32.exe" /s $SYSDIR\DIME.dll'
  File "system32.x86\*.dll"
  CreateDirectory  "$INSTDIR"
  SetOutPath "$INSTDIR"
  File "*.cin"
  SetOutPath "$APPDATA\DIME\"
  CreateDirectory "$APPDATA\DIME"
  ;File "config.ini"

SectionEnd

Section "Modules" SEC02
SetOutPath $PROGRAMFILES64
  SetOVerwrite ifnewer
SectionEnd

Section -AdditionalIcons
  SetShellVarContext all
  SetOutPath $SMPROGRAMS\DIME
  CreateDirectory "$SMPROGRAMS\DIME"
  CreateShortCut "$SMPROGRAMS\DIME\Uninstall.lnk" "$INSTDIR\uninst.exe"
SectionEnd

Section -Post
  SetOutPath  "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninst.exe"
  ${If} ${RunningX64}
  	SetRegView 64
  ${EndIf}
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$SYSDIR\DIME.dll"
  ${If} ${RunningX64}
  	WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "EstimatedSize" 286
  ${Else}
  	WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "EstimatedSize" 183
   ${EndIf}
SectionEnd

Function un.onUninstSuccess
  MessageBox MB_ICONINFORMATION|MB_OK "$(^Name)已移除成功。" /SD IDOK
FunctionEnd

Function un.onInit
;!insertmacro MUI_UNGETLANGUAGE
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "確定要完全移除$(^Name)？" /SD IDYES IDYES +2
  Abort
FunctionEnd

Section Uninstall
 ${If} ${RunningX64}
  ${DisableX64FSRedirection}
  ExecWait '"$SYSDIR\regsvr32.exe" /u /s $SYSDIR\DIME.dll'
  ${EnableX64FSRedirection}
 ${EndIf}
  ExecWait '"$SYSDIR\regsvr32.exe" /u /s $SYSDIR\DIME.dll'

  ClearErrors
  ${If} ${RunningX64}
  ${DisableX64FSRedirection}
  IfFileExists "$SYSDIR\DIME.dll"  0 +3
  Delete "$SYSDIR\DIME.dll"
  IfErrors lbNeedReboot +1
  ${EnableX64FSRedirection}
  ${EndIf}
  IfFileExists "$SYSDIR\DIME.dll"  0  lbContinueUninstall
  Delete "$SYSDIR\DIME.dll"
  IfErrors lbNeedReboot lbContinueUninstall

  lbNeedReboot:
  MessageBox MB_ICONSTOP|MB_YESNO "偵測到有程式正在使用輸入法，請重新開機以繼續移除舊版。是否要立即重新開機？" IDNO lbNoReboot
  Reboot

  lbNoReboot:
  MessageBox MB_ICONSTOP|MB_OK "請將所有程式關閉，再嘗試執行本安裝程式。若仍看到此畫面，請重新開機。" IDOK +1
  Quit
  lbContinueUninstall:

  Delete "$INSTDIR\uninst.exe"
  Delete "$INSTDIR\*.cin"
  RMDir /r "$INSTDIR"
  SetShellVarContext all
  Delete "$SMPROGRAMS\DIME\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\DIME"
  ${If} ${RunningX64}
  	SetRegView 64
  ${EndIf}
  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  SetAutoClose true
SectionEnd
