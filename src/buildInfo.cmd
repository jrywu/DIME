@echo off
chcp 950 >nul

rem Get git commit count
git rev-list HEAD --count > commitcount.txt
FOR /f "tokens=1" %%a in ('findstr /R "^[0-9][0-9]*" commitcount.txt') do set _CommitCount=%%a
del commitcount.txt

rem Get date components using PowerShell (region-independent)
for /f %%a in ('powershell -NoProfile -Command "Get-Date -Format yyyy"') do set BUILD_YEAR_4=%%a
for /f %%a in ('powershell -NoProfile -Command "Get-Date -Format yy"') do set BUILD_YEAR_2=%%a
for /f %%a in ('powershell -NoProfile -Command "(Get-Date -Format yy).Substring(1,1)"') do set BUILD_YEAR_1=%%a
for /f %%a in ('powershell -NoProfile -Command "Get-Date -Format MM"') do set BUILD_MONTH=%%a
for /f %%a in ('powershell -NoProfile -Command "Get-Date -Format dd"') do set BUILD_DAY=%%a

rem Construct build date strings
set _BuildDate1=%BUILD_YEAR_1%%BUILD_MONTH%%BUILD_DAY%
set _BuildDate4=%BUILD_YEAR_4%%BUILD_MONTH%%BUILD_DAY%
set _BuildSubVersion=%_BuildDate1%
set _BuildVersion=1.3.%_CommitCount%.%_BuildDate1%
set _BuildVersionStrShort=1.3.%_CommitCount%

rem Generate into a per-invocation temp file (DIME and DIMESettings may run this in parallel)
set _Tmp=BuildInfo.h.%RANDOM%%RANDOM%.tmp
echo #define BUILD_VER_MAJOR 1 >%_Tmp%
echo #define BUILD_VER_MINOR 3 >>%_Tmp%
echo #define BUILD_COMMIT_COUNT %_CommitCount% >>%_Tmp%
echo #define BUILD_YEAR_4 %BUILD_YEAR_4% >>%_Tmp%
echo #define BUILD_YEAR_2 %BUILD_YEAR_2% >>%_Tmp%
echo #define BUILD_YEAR_1 %BUILD_YEAR_1% >>%_Tmp%
echo #define BUILD_MONTH %BUILD_MONTH% >>%_Tmp%
echo #define BUILD_DAY %BUILD_DAY% >>%_Tmp%
echo #define BUILD_DATE_1 %_BuildDate1% >>%_Tmp%
echo #define BUILD_DATE_4 %_BuildDate4% >>%_Tmp%
echo #define BUILD_VERSION %_BuildVersion% >>%_Tmp%
echo #define BUILD_SUBVERSION %_BuildSubVersion% >>%_Tmp%
echo #define BUILD_VERSION_STR "%_BuildVersion%" >>%_Tmp%
echo #define BUILD_SUBVERSION_STR "%_BuildSubVersion%" >>%_Tmp%
echo #define BUILD_VERSION_STR_SHORT "%_BuildVersionStrShort%" >>%_Tmp%

rem Only replace BuildInfo.h when the content changed, so unchanged builds don't recompile
fc /b %_Tmp% BuildInfo.h >nul 2>&1
if errorlevel 1 (
    move /y %_Tmp% BuildInfo.h >nul
    echo BuildInfo.h updated to %_BuildVersion%.
) else (
    del %_Tmp%
    echo BuildInfo.h is up to date ^(%_BuildVersion%^).
)
exit /b 0
