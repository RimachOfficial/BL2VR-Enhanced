@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
if not exist build mkdir build
ml64 /nologo /c /Fo build\stubs.obj stubs.asm
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /EHsc /W4 proxy.cpp build\stubs.obj /link /DEF:proxy.def /OUT:build\LibOVRRT64_1.dll
if errorlevel 1 exit /b 1
echo.
echo Built: %~dp0build\LibOVRRT64_1.dll