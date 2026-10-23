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
cl /nologo /O2 /EHsc /std:c++17 ^
   /I imgui /I imgui\backends ^
   main.cpp ^
   imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
   /link d3d11.lib dxgi.lib shell32.lib ole32.lib advapi32.lib /OUT:build\bl2vr_panel.exe
if errorlevel 1 exit /b 1
if exist "%~dp0..\..\openvr_proxy\build\openvr_api.dll" copy /y "%~dp0..\..\openvr_proxy\build\openvr_api.dll" "%~dp0build\openvr_api.dll" >nul
echo.
echo Built: %~dp0build\bl2vr_panel.exe  (bundles openvr_api.dll next to it)
