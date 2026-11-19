@echo off
rem Sestavi bin\MacroTool.exe (admin) a bin\MacroTool-dev.exe (bez UAC, na testovani).
rem Potrebuje Visual Studio 2022 s "Desktop development with C++".
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto novc
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto novc

if not exist obj mkdir obj
if not exist bin mkdir bin
set "CFLAGS=/nologo /std:c++20 /EHsc /O2 /MT /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /GL /Gy"
set "LIBS=user32.lib gdi32.lib ole32.lib shell32.lib advapi32.lib d2d1.lib dwrite.lib windowscodecs.lib dwmapi.lib dxguid.lib winmm.lib iphlpapi.lib ws2_32.lib uuid.lib comdlg32.lib winhttp.lib bcrypt.lib crypt32.lib"
rem /DEBUG:NONE = zadne .pdb ani jmena funkci v exe.
set "LFLAGS=/nologo /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:NO /DEBUG:NONE"

set "ICONDEF="
if exist res\app.ico set "ICONDEF=/DHAVE_ICON"

cl %CFLAGS% /c src\*.cpp /Foobj\ || exit /b 1
rc /nologo /DDEV_MANIFEST %ICONDEF% /fo obj\app-dev.res res\app.rc || exit /b 1
link %LFLAGS% obj\*.obj obj\app-dev.res %LIBS% /OUT:bin\MacroTool-dev.exe || exit /b 1
rc /nologo %ICONDEF% /fo obj\app.res res\app.rc || exit /b 1
link %LFLAGS% obj\*.obj obj\app.res %LIBS% /OUT:bin\MacroTool.exe || exit /b 1
echo BUILD OK
exit /b 0

:novs
echo Nenasel jsem Visual Studio Installer. Nainstaluj Visual Studio 2022 s "Desktop development with C++".
exit /b 1
:novc
echo Chybi C++ nastroje (VC Tools). Doinstaluj je ve Visual Studio Installeru.
exit /b 1
