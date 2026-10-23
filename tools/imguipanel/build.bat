@echo off
setlocal
rem vcvars needs vswhere from the VS installer folder; add it for shells that don't have it
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
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
