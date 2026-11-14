@echo off
setlocal
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%V\VC\Auxiliary\Build\vcvars32.bat"
)
if not defined VCVARS exit /b 1
if not exist "%VCVARS%" exit /b 1
call "%VCVARS%"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /utf-8 /LD /DPADFIX_PLATFORM_ONLY src\padfix.cpp /Fobuild\padfix_platform.obj /link /DEF:src\dinput8-platform.def /OUT:build\dinput8-platform.dll user32.lib dxguid.lib dinput8.lib
if errorlevel 1 exit /b 1
