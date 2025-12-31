Unicode True
!include MUI2.nsh
!include "Registry.nsh"
!include x64.nsh


!define PRODUCT_NAME "DIME"
!define PRODUCT_VERSION "1.2"
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
!define MUI_FINISHPAGE_TITLE "安裝完成"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

; Language files
;!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "TradChinese"


; MUI end ------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
RequestExecutionLevel admin
OutFile "DIME-x86armUniversal.exe"
InstallDir "$PROGRAMFILES64\DIME"
ShowInstDetails show
ShowUnInstDetails show

; Language Strings
LangString DESC_INSTALLING ${LANG_TradChinese} "安裝中"
LangString DESC_DOWNLOADING1 ${LANG_TradChinese} "下載中"
LangString DESC_DOWNLOADFAILED ${LANG_TradChinese} "下載失敗:"
LangString DESC_VCX86 ${LANG_TradChinese} "Visual Studio Redistritable x86"
LangString DESC_VCX64 ${LANG_TradChinese} "Visual Studio Redistritable x64"
LangString DESC_VCARM64 ${LANG_TradChinese} "Visual Studio Redistritable ARM64"
LangString DESC_VCX86_DECISION ${LANG_TradChinese} "安裝此輸入法之前，必須先安裝 $(DESC_VCX86)，若你想繼續安裝 \
  ，您的電腦必須連接網路。$\n您要繼續這項安裝嗎？"
LangString DESC_VCX64_DECISION ${LANG_TradChinese} "安裝此輸入法之前，必須先安裝 $(DESC_VCX64)，若你想繼續安裝 \
  ，您的電腦必須連接網路。$\n您要繼續這項安裝嗎？"
LangString DESC_VCARM64_DECISION ${LANG_TradChinese} "安裝此輸入法之前，必須先安裝 $(DESC_VCARM64)，若你想繼續安裝 \
  ，您的電腦必須連接網路。$\n您要繼續這項安裝嗎？"
!define URL_VC_REDISTX64 https://aka.ms/vs/17/release/vc_redist.x64.exe
!define URL_VC_REDISTX86 https://aka.ms/vs/17/release/vc_redist.x86.exe
!define URL_VC_REDISTARM64 https://aka.ms/vs/17/release/VC_redist.arm64.exe


Var "URL_VCX86"
Var "URL_VCX64"
Var "URL_VCARM64"

Function .onInit
  InitPluginsDir
  StrCpy $URL_VCX86 "${URL_VC_REDISTX86}"
  StrCpy $URL_VCX64 "${URL_VC_REDISTX64}"
  StrCpy $URL_VCARM64 "${URL_VC_REDISTARM64}"
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
	${If} ${IsNativeAMD64}
		ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" "Minor"
		IfErrors InstallVCx64Redist 0
		${If} $R0 > 31
			Goto VCx64RedistInstalled
		${EndIf}
		ClearErrors
		ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" "Bld"
		IfErrors InstallVCx64Redist 0
		${If} $R0 >= 31103
			Goto VCx64RedistInstalled
		${EndIf}
	InstallVCx64Redist:
		MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 "$(DESC_VCX64_DECISION)" /SD IDNO IDYES +1 IDNO VCRedistInstalledAbort
		AddSize 7000
		nsisdl::download /TIMEOUT=30000 "$URL_VCX64" "$PLUGINSDIR\vcredist_x64.exe"
			Pop $0
			StrCmp "$0" "success" lbl_continue64
			DetailPrint "$(DESC_DOWNLOADFAILED) $0"
			Abort
		 lbl_continue64:
		  DetailPrint "$(DESC_INSTALLING) $(DESC_VCX64)..."
		  nsExec::ExecToStack "$PLUGINSDIR\vcredist_x64.exe /q"
		  ;pop $DOTNET_RETURN_CODE
	VCx64RedistInstalled:
	${ElseIf} ${IsNativeARM64}
		ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\arm64" "Minor"
		IfErrors InstallVCarm64Redist 0
		${If} $R0 > 31
			Goto VCarm64RedistInstalled
		${EndIf}
		ClearErrors
		ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\arm64" "Bld"
		IfErrors InstallVCx64Redist 0
		${If} $R0 >= 31103
			Goto VCarm64RedistInstalled
		${EndIf}
	InstallVCarm64Redist:
		MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 "$(DESC_VCARM64_DECISION)" /SD IDNO IDYES +1 IDNO VCRedistInstalledAbort
		AddSize 7000
		nsisdl::download /TIMEOUT=30000 "$URL_VCARM64" "$PLUGINSDIR\vcredist_arm64.exe"
			Pop $0
			StrCmp "$0" "success" lbl_continueARM64
			DetailPrint "$(DESC_DOWNLOADFAILED) $0"
			Abort
		 lbl_continueARM64:
		  DetailPrint "$(DESC_INSTALLING) $(DESC_VCARM64)..."
		  nsExec::ExecToStack "$PLUGINSDIR\vcredist_arm64.exe /q"
		  ;pop $DOTNET_RETURN_CODE
	VCarm64RedistInstalled:
	${Endif}
    SetRegView 32
  ${EndIf}
  ClearErrors
  ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X86" "Minor"
  IfErrors InstallVCx86Redist 0
  ${If} $R0 > 31
  	Goto VCRedistInstalled
  ${EndIf}
  ClearErrors
  ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X86" "Bld"
  IfErrors InstallVCx86Redist 0
  ${If} $R0 >= 31103
	Goto VCRedistInstalled
  ${EndIf}
InstallVCx86Redist:
  MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 "$(DESC_VCX86_DECISION)" /SD IDNO IDYES +1 IDNO VCRedistInstalledAbort
  AddSize 7000
  nsisdl::download /TIMEOUT=30000 "$URL_VCX86" "$PLUGINSDIR\vcredist_x86.exe"
    Pop $0
    StrCmp "$0" "success" lbl_continue
    DetailPrint "$(DESC_DOWNLOADFAILED) $0"
    Abort

    lbl_continue:
      DetailPrint "$(DESC_INSTALLING) $(DESC_VCX86)..."
      nsExec::ExecToStack "$PLUGINSDIR\vcredist_x86.exe /q"
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
	${If} ${IsNativeAMD64}
    	File "system32.x64\DIME.dll"
	${ElseIf} ${IsNativeARM64}
		File "system32.arm64\DIME.dll"
	${EndIf}
  	ExecWait '"$SYSDIR\regsvr32.exe" /s $SYSDIR\DIME.dll'
  	${EnableX64FSRedirection}
  ${EndIf}
  File "system32.x86\DIME.dll"
  ExecWait '"$SYSDIR\regsvr32.exe" /s $SYSDIR\DIME.dll'
  CreateDirectory  "$INSTDIR"
  SetOutPath "$INSTDIR"
  File "*.cin"
  File "DIMESettings.exe"
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
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\DIME設定.lnk" "$INSTDIR\DIMESettings.exe"
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
  IfFileExists "$SYSDIR\DIME.dll"  0 +2
  ExecWait '"$SYSDIR\regsvr32.exe" /u /s $SYSDIR\DIME.dll'
  ${EnableX64FSRedirection}
 ${EndIf}
  IfFileExists "$SYSDIR\DIME.dll"  0 +2
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

  Delete "$INSTDIR\*.exe"
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
