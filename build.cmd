@echo off
setlocal
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%V\VC\Auxiliary\Build\vcvars32.bat"
)
if not defined VCVARS (
  echo Visual Studio C++ x86 build tools were not found. Install the Desktop development with C++ workload.
  exit /b 1
)
if not exist "%VCVARS%" (
  echo Missing Visual Studio environment script: %VCVARS%
  exit /b 1
)
call "%VCVARS%"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c11 /TC /O2 /MT /utf-8 /c src\sw2resolution\sw2_resolution.c /Fobuild\sw2_resolution.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /utf-8 /LD src\padfix.cpp build\sw2_resolution.obj /Fobuild\padfix.obj /link /DEF:src\dinput8.def /OUT:build\dinput8.dll user32.lib dxguid.lib dinput8.lib
if errorlevel 1 exit /b 1
