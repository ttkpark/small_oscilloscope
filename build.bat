@echo off
echo Building Small Oscilloscope Test Program...

REM ESP-IDF 환경 설정 (사용자 환경에 맞게 수정 필요)
REM set IDF_PATH=C:\esp\esp-idf
REM call %IDF_PATH%\export.bat

REM 프로젝트 빌드
idf.py build

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
    echo.
    echo To flash the program, run: idf.py -p COM_PORT flash
    echo To monitor output, run: idf.py -p COM_PORT monitor
    echo.
) else (
    echo.
    echo Build failed!
    echo.
)

pause 