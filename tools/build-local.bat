@echo off
rem ============================================================
rem  build-local.bat - build TopMostDesk WITHOUT vcvars64.bat
rem
rem  Why: vcvars64.bat calls reg.exe, which some locked-down
rem  environments block. This script only sets INCLUDE / LIB /
rem  PATH by hand, then hands over to build.bat.
rem
rem  Keep this file ASCII-only (cmd.exe reads .bat in the OEM
rem  codepage, non-ASCII bytes break parsing).
rem
rem  NOTE: no parenthesised "if ... (" blocks anywhere - the
rem  "(x86)" in the SDK path would close the block early and
rem  cmd would try to run "\Windows" as a command.
rem
rem  Usage:  tools\build-local.bat
rem ============================================================
setlocal

set "ROOT=%~dp0.."
set "VSBASE=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC"
set "KITS=C:\Program Files (x86)\Windows Kits\10"

if not exist "%VSBASE%" goto :no_msvc
if not exist "%KITS%\Include" goto :no_sdk

rem ---- newest MSVC toolset ----
set "MSVC="
for /f "delims=" %%D in ('dir /b /ad /o-n "%VSBASE%" 2^>nul') do if not defined MSVC set "MSVC=%VSBASE%\%%D"

rem ---- newest Windows 10/11 SDK ----
set "SDKV="
for /f "delims=" %%D in ('dir /b /ad /o-n "%KITS%\Include" 2^>nul') do if not defined SDKV set "SDKV=%%D"

if not defined MSVC goto :no_msvc
if not defined SDKV goto :no_sdk

set "PATH=%MSVC%\bin\Hostx64\x64;%KITS%\bin\%SDKV%\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%KITS%\Include\%SDKV%\ucrt;%KITS%\Include\%SDKV%\um;%KITS%\Include\%SDKV%\shared"
set "LIB=%MSVC%\lib\x64;%KITS%\Lib\%SDKV%\ucrt\x64;%KITS%\Lib\%SDKV%\um\x64"

echo [env] MSVC = %MSVC%
echo [env] SDK  = %SDKV%
echo.

pushd "%ROOT%"
call build.bat
set "RC=%ERRORLEVEL%"
popd

endlocal & exit /b %RC%

:no_msvc
echo [ERROR] MSVC not found under: %VSBASE%
echo         Edit VSBASE in this script if Visual Studio lives elsewhere.
endlocal & exit /b 1

:no_sdk
echo [ERROR] Windows SDK not found under: %KITS%\Include
endlocal & exit /b 1
