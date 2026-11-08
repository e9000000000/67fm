@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "MODE=%~1"
if /I "%MODE%"=="clean" (
    if exist build rmdir /s /q build
    echo Cleaned.
    exit /b 0
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio Build Tools.
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo ERROR: Visual C++ Build Tools not found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed.
    exit /b 1
)

if not exist build mkdir build
if not exist build\obj mkdir build\obj

echo Compiling resources...
rc /nologo /fo build\app.res resources\app.rc
if errorlevel 1 (
    echo ERROR: resource compilation failed.
    exit /b 1
)

set "CFLAGS=/nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Ithird_party\miniz"
set "LDFLAGS=/nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /LTCG /MAP:build\67fm.map"
set "LIBS=user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib dwmapi.lib uxtheme.lib comctl32.lib shlwapi.lib winhttp.lib msimg32.lib"

if /I "%MODE%"=="debug" (
    set "CFLAGS=%CFLAGS% /Od /Zi /MT /DDEBUG"
    set "LDFLAGS=%LDFLAGS% /DEBUG"
    set "OUTPUT=build\67fm-debug.exe"
) else (
    set "CFLAGS=%CFLAGS% /O2 /GL /MT /DNDEBUG"
    set "OUTPUT=build\67fm.exe"
)

echo Compiling 67fm...
cl %CFLAGS% /Fobuild\obj\ src\*.cpp third_party\miniz\miniz.c third_party\miniz\miniz_tdef.c third_party\miniz\miniz_tinfl.c third_party\miniz\miniz_zip.c /Fe:%OUTPUT% /link %LDFLAGS% build\app.res %LIBS%
if errorlevel 1 (
    echo ERROR: 67fm build failed.
    exit /b 1
)

echo.
echo Built %OUTPUT%
for %%F in ("%OUTPUT%") do echo Size: %%~zF bytes
exit /b 0
