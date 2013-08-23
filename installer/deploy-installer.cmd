@echo off
mkdir "..\Installer\system32.x86"
mkdir "..\Installer\system32.x64
copy ..\Release\Win32\TSFTTS.dll ..\Installer\system32.x86\
copy ..\Release\x64\TSFTTS.dll ..\Installer\system32.x64\
"c:\Program Files (x86)\NSIS\makensis.exe" TSFTTS-x8664.nsi
REM use the following line instead for buiding on x86 platform
REM "c:\Program Files\NSIS\makensis.exe" TSFTTS-x8664.nsi
echo ===============================================================================
echo Deployment and Installer packaging done!!
echo ===============================================================================
pause
