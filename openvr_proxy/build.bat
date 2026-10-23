@echo off
setlocal
rem --- locate vcvars64 via vswhere (any machine, any VS edition, CI) ---------------
rem (vswhere output goes through a temp file: the (x86) in its path cannot be used
rem  directly inside a for /f backquote clause - the ) breaks cmd's parser)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSWFILE=%TEMP%\bl2vr_vswhere.txt"
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
rem some VS installs call vswhere unqualified from inside vcvars; make sure it resolves
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VCVARS%" || exit /b 1
rem --------------------------------------------------------------------------------
cd /d "%~dp0"
if not exist build mkdir build
set MH=%~dp0..\third_party\minhook-master
cl /nologo /LD /O2 /EHsc /W3 ^
   /I "%MH%\include" /I "%MH%\src" /I "%MH%\src\hde" ^
   src\proxy.cpp src\ovrhook.cpp ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /link /OUT:build\openvr_api.dll
if errorlevel 1 exit /b 1
echo.
echo Built: %~dp0build\openvr_api.dll
