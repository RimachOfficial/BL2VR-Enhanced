@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
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
