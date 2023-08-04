@echo off
mkdir "..\Installer\system32.x86"
mkdir "..\Installer\system32.x64"
mkdir "..\Installer\system32.arm64"
copy ..\Release\DIMESettings.exe ..\Installer\
copy ..\Release\x64\DIME.dll ..\Installer\system32.x64\
copy ..\Release\arm64ec\DIME.dll ..\Installer\system32.arm64\
copy ..\Release\Win32\DIME.dll ..\Installer\system32.x86\
"c:\Program Files (x86)\NSIS\makensis.exe" DIME-x86armUniversal.nsi
REM use the following line instead for buiding on x86 platform
REM "c:\Program Files\NSIS\makensis.exe" DIME-x86armUniversal.nsi
echo ===============================================================================
echo Deployment and Installer packaging done!!
echo ===============================================================================
pause
