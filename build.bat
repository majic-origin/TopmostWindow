@echo off
rem Build script for TopMostDesk (Windows / MSVC)
rem Run inside a "x64 Native Tools Command Prompt for VS", or after calling vcvars64.bat.
rem NOTE: keep this file ASCII-only; cmd.exe reads .bat in the OEM codepage.

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cl.exe not found. Open a VS Developer Command Prompt first.
    exit /b 1
)

if not exist build mkdir build
pushd build

rem --- resources: app icon + version info ---
rc /nologo /fo app.res "..\src\app.rc"
if errorlevel 1 (
    echo [FAILED] rc error
    popd
    exit /b 1
)

cl /nologo /std:c++17 /EHsc /O2 /MT /W3 /utf-8 ^
   /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   ..\src\main.cpp ..\src\app.cpp ..\src\menu.cpp ..\src\common.cpp ^
   ..\src\config.cpp ..\src\winmgr.cpp ..\src\overlay.cpp ..\src\groups.cpp ^
   ..\src\dialogs.cpp ..\src\inputbox.cpp ..\src\ui_theme.cpp ^
   ..\src\input_hook.cpp ..\src\settings.cpp ..\src\toast.cpp ^
   ..\src\gesture.cpp ^
   ..\src\tray.cpp app.res ^
   /Fe:TopMostDesk.exe ^
   /link /SUBSYSTEM:WINDOWS ^
   user32.lib gdi32.lib gdiplus.lib comdlg32.lib shell32.lib shlwapi.lib ^
   dwmapi.lib ole32.lib advapi32.lib

if errorlevel 1 (
    echo [FAILED] compile error
    popd
    exit /b 1
)

echo.
echo [OK] output: %CD%\TopMostDesk.exe
popd
