@echo off
setlocal
set "SRC=LogEditor.cpp"
set "RES=icon.rc"
set "RES_OUT=icon.o"
set "OUT=LogEditor.exe"

:: --- Try MinGW ---
where g++ >nul 2>&1
if %errorlevel% equ 0 (
    echo [MinGW] Compiling resources...
    windres "%RES%" "%RES_OUT%"
    if %errorlevel% neq 0 (echo [MinGW] windres FAILED & goto :end)

    echo [MinGW] Building %OUT% ...
    g++ -O2 -std=c++11 -Wall -mwindows -static "%SRC%" "%RES_OUT%" -o "%OUT%" -lcomctl32 -lcomdlg32 -lshell32
    if %errorlevel% equ 0 (echo [MinGW] OK: %OUT%) else (echo [MinGW] FAILED)
    goto :end
)

:: --- Try MSVC ---
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo [MSVC] Compiling resources...
    rc /fo icon.res icon.rc
    echo [MSVC] Building %OUT% ...
    cl /EHsc /O2 /Fe:%OUT% %SRC% icon.res comctl32.lib comdlg32.lib shell32.lib /link /SUBSYSTEM:WINDOWS
    if %errorlevel% equ 0 (echo [MSVC] OK: %OUT%) else (echo [MSVC] FAILED)
    goto :end
)

echo No compiler found (g++ or cl). Install MinGW or Visual Studio C++.
:end
