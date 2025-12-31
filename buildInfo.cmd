chcp 65001
git rev-list HEAD --count > commitcount.txt
FOR /f "tokens=1" %%a in ('findstr /R "^[0-9][0-9]*" commitcount.txt') do set _CommitCount=%%a
del commitcount.txt
set BUILD_YEAR_1=%DATE:~2,2%
rem set /a _CommitCount+=1
set _BuildDate1=%BUILD_YEAR_1%%DATE:~5,2%%DATE:~8,2%
set _BuildVersion=1.2.%_CommitCount%.%_BuildDate1%
echo #define BUILD_VER_MAJOR 1 >BuildInfo.h
echo #define BUILD_VER_MINOR 2 >>BuildInfo.h
echo #define BUILD_COMMIT_COUNT %_CommitCount% >>BuildInfo.h
echo #define BUILD_YEAR_4 %DATE:~0,4% >>BuildInfo.h
echo #define BUILD_YEAR_2 %DATE:~2,2% >>BuildInfo.h
echo #define BUILD_YEAR_1 %BUILD_YEAR_1% >>BuildInfo.h
echo #define BUILD_MONTH %DATE:~5,2% >>BuildInfo.h
echo #define BUILD_DAY %DATE:~8,2% >>BuildInfo.h
echo #define BUILD_DATE_1 %_BuildDate1% >>BuildInfo.h
echo #define BUILD_DATE_4 %DATE:~0,4%%DATE:~5,2%%DATE:~8,2% >>BuildInfo.h
echo #define BUILD_VERSION %_BuildVersion% >>BuildInfo.h
echo #define BUILD_VERSION_STR "%_BuildVersion%" >>BuildInfo.h

