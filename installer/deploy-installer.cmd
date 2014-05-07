@echo off
mkdir "..\Installer\system32.x86"
mkdir "..\Installer\system32.x64
copy ..\Release\Win32\DIME.dll ..\Installer\system32.x86\
copy ..\Release\x64\DIME.dll ..\Installer\system32.x64\
"c:\Program Files (x86)\NSIS\makensis.exe" DIME-x8664.nsi
REM use the following line instead for buiding on x86 platform
REM "c:\Program Files\NSIS\makensis.exe" DIME-x8664.nsi
echo ===============================================================================
echo Deployment and Installer packaging done!!
echo ===============================================================================
pause
