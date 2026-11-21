@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%V\VC\Auxiliary\Build\vcvars32.bat"
if not defined VCVARS exit /b 1
call "%VCVARS%"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c11 /TC /O2 /MT /utf-8 /c src\sw2resolution\sw2_resolution.c /Fobuild\sw2_resolution.obj
if errorlevel 1 exit /b 1
for %%T in (test_padfix test_platform test_regressions) do (
 cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /utf-8 tests\%%T.cpp build\sw2_resolution.obj /Fobuild\%%T.obj /Febuild\%%T.exe /link user32.lib dxguid.lib dinput8.lib ole32.lib
 if errorlevel 1 exit /b 1
 build\%%T.exe
 if errorlevel 1 exit /b 1
)
