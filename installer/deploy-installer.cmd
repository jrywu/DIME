@echo off
mkdir "..\Installer\system32.x86"
mkdir "..\Installer\system32.x64
copy ..\Release\Win32\TSFTTS.dll ..\Installer\system32.x86\
copy ..\Release\x64\TSFTTS.dll ..\Installer\system32.x64\
echo ===============================================================================
echo Please download and install Nullosft Sciptable Install System (NSIS) comiler . 
echo                                  ---http://nsis.sourceforge.net/Download-----
echo Then compile TSFDayi-x86.nsi and TSFDayi-x64.nsi to build installer packages for 
echo windows 32bit and 64 bit or TSFDayi-x8664.nsi for integrated 32/64bit installer.
echo ===============================================================================
pause
