@echo off
mkdir "..\Installer\system32.x64"
copy ..\Release\DIMESettings.exe ..\Installer\
copy ..\Release\x64\DIME.dll ..\Installer\system32.x64\
"c:\Program Files (x86)\NSIS\makensis.exe" DIME-x64OnlyCleaned.nsi
REM "c:\Program Files\NSIS\makensis.exe" DIME-x64OnlyCleaned.nsi
echo ===============================================================================
echo Deployment and Installer packaging done!!
echo ===============================================================================
pause
