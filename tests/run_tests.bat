@echo off
setlocal
rem --- locate vcvars64 via vswhere (same logic as openvr_proxy\build.bat) ----------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSWFILE=%TEMP%\bl2vr_vswhere_tests.txt"
set "VCVARS="
if exist "%VSWHERE%" "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSWFILE%" 2>nul
if exist "%VSWFILE%" for /f "usebackq delims=" %%i in ("%VSWFILE%") do if not defined VCVARS set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VSWFILE%" del "%VSWFILE%" >nul 2>nul
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
  echo ERROR: no MSVC x64 toolchain found. Install Visual Studio with the C++ workload.
  exit /b 1
)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VCVARS%" || exit /b 1
rem --------------------------------------------------------------------------------
cd /d "%~dp0.."
if not exist build\tests mkdir build\tests
set "INC=/I %~dp0..\openvr_proxy\src /I %~dp0..\third_party\minhook-master\include /I %~dp0"

echo == building test_filters (filters.h unit tests) ==
cl /nologo /EHsc /W3 /O2 /std:c++17 /D_CRT_SECURE_NO_WARNINGS %INC% tests\test_filters.cpp /Fo:build\tests\ /Fe:build\tests\test_filters.exe
if errorlevel 1 exit /b 1

echo == building test_ovrhook (ovrhook.cpp unit tests, MinHook + Bl2Log stubbed) ==
cl /nologo /EHsc /W3 /O2 /std:c++17 /D_CRT_SECURE_NO_WARNINGS %INC% tests\test_ovrhook.cpp /Fo:build\tests\ /Fe:build\tests\test_ovrhook.exe
if errorlevel 1 exit /b 1

echo.
build\tests\test_filters.exe
if errorlevel 1 goto :fail
build\tests\test_ovrhook.exe
if errorlevel 1 goto :fail
echo.
echo ALL TESTS PASSED
exit /b 0

:fail
echo.
echo TESTS FAILED
exit /b 1