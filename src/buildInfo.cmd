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
set _BuildDate1=%BUILD_YEAR_2%%BUILD_MONTH%%BUILD_DAY%
set _BuildDate4=%BUILD_YEAR_4%%BUILD_MONTH%%BUILD_DAY%
set _BuildVersion=1.2.%_CommitCount%.%_BuildDate1%

rem Generate BuildInfo.h
echo #define BUILD_VER_MAJOR 1 >BuildInfo.h
echo #define BUILD_VER_MINOR 2 >>BuildInfo.h
echo #define BUILD_COMMIT_COUNT %_CommitCount% >>BuildInfo.h
echo #define BUILD_YEAR_4 %BUILD_YEAR_4% >>BuildInfo.h
echo #define BUILD_YEAR_2 %BUILD_YEAR_2% >>BuildInfo.h
echo #define BUILD_YEAR_1 %BUILD_YEAR_1% >>BuildInfo.h
echo #define BUILD_MONTH %BUILD_MONTH% >>BuildInfo.h
echo #define BUILD_DAY %BUILD_DAY% >>BuildInfo.h
echo #define BUILD_DATE_1 %_BuildDate1% >>BuildInfo.h
echo #define BUILD_DATE_4 %_BuildDate4% >>BuildInfo.h
echo #define BUILD_VERSION %_BuildVersion% >>BuildInfo.h
echo #define BUILD_VERSION_STR "%_BuildVersion%" >>BuildInfo.h

echo BuildInfo.h generated successfully.

