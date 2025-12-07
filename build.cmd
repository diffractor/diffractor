@echo off
setlocal enabledelayedexpansion

rem ============================================================================
rem Diffractor Build Script
rem ============================================================================
rem Usage: build.cmd [command]
rem
rem Commands:
rem   (none)     Show this usage information
rem   build      Build binaries only (Win32 and x64 Release, plus WinStore)
rem   release    Build binaries, create installers, sign, and package
rem
rem Prerequisites:
rem   - Visual Studio 2022 with C++ workload
rem   - Windows SDK 10.0.26100.0 or later
rem   - NSIS installer (for release builds)
rem   - Code signing certificate (for release builds)
rem ============================================================================

if "%~1"=="" goto :usage
if /i "%~1"=="help" goto :usage
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="build" goto :build
if /i "%~1"=="release" goto :release

echo Error: Unknown command '%~1'
echo.
goto :usage

:usage
echo.
echo Diffractor Build Script
echo ========================
echo.
echo Usage: build.cmd [command]
echo.
echo Commands:
echo   build      Build binaries only (Win32, x64, and WinStore configurations)
echo   release    Full release build: binaries, installers, signing, and packaging
echo.
echo Examples:
echo   build.cmd build      Build all binaries
echo   build.cmd release    Create full release package
echo.
exit /b 0

:setup
rem Setup environment variables
set VSDIR=%VSINSTALLDIR%
if "%VSDIR%"=="" (
    echo Error: Visual Studio environment not detected.
    echo Please run from a Developer Command Prompt or run vcvars64.bat first.
    exit /b 1
)

set TOOLSDIR=tools
set MSBUILDDIR=%VSDIR%\Msbuild\Current\Bin
set STORE_PACKAGE_NAME=Diffractor_1.26.2.1187_x64
set SOURCE_FILES_DIR=%~dp0exe
set PACKAGE_ROOT=%~dp0dist
set "SDK_BIN_DIR=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
set INSTALLER_DIR=%~dp0installer

exit /b 0

:build
echo.
echo ============================================================================
echo Building Diffractor Binaries
echo ============================================================================
echo.

call :setup
if %errorlevel% neq 0 exit /b %errorlevel%

echo Building Release ^| Win32...
"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=Win32 /m
if %errorlevel% neq 0 (
    echo Error: Win32 build failed
    exit /b %errorlevel%
)

echo.
echo Building Release ^| x64...
"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=x64 /m
if %errorlevel% neq 0 (
    echo Error: x64 build failed
    exit /b %errorlevel%
)

echo.
echo Building WinStore ^| x64 (app project only)...
"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=WinStore /p:Platform=x64 /m
if %errorlevel% neq 0 (
    echo Error: WinStore build failed
    exit /b %errorlevel%
)

echo.
echo ============================================================================
echo Build completed successfully!
echo ============================================================================
echo.
echo Output files:
echo   exe\diffractor32.exe    (32-bit desktop)
echo   exe\diffractor64.exe    (64-bit desktop)
echo   exe\diffractor.exe      (Windows Store)
echo.
exit /b 0

:release
echo.
echo ============================================================================
echo Building Diffractor Release Package
echo ============================================================================
echo.

call :setup
if %errorlevel% neq 0 exit /b %errorlevel%

rem Clean previous builds
echo Cleaning previous release artifacts...
del diffractor-setup.exe 2>nul
del diffractor-setup-test.exe 2>nul
del diffractor.zip 2>nul
del %STORE_PACKAGE_NAME%.msix 2>nul
if exist "%PACKAGE_ROOT%" rd /s /q "%PACKAGE_ROOT%"

rem Build all configurations
echo.
echo Building Release ^| Win32...
"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=Win32 /m
if %errorlevel% neq 0 (
    echo Error: Win32 build failed
    exit /b %errorlevel%
)

echo.
echo Building Release ^| x64...
"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=x64 /m
if %errorlevel% neq 0 (
    echo Error: x64 build failed
    exit /b %errorlevel%
)

echo.
echo Building WinStore ^| x64 (app project only)...
"%MSBUILDDIR%\msbuild" src\app.vcxproj /p:Configuration=WinStore /p:Platform=x64 /m
if %errorlevel% neq 0 (
    echo Error: WinStore build failed
    exit /b %errorlevel%
)

rem Sign executables
echo.
echo Signing executables...
"%SDK_BIN_DIR%\SignTool.exe" sign /fd SHA256 /tr http://timestamp.sectigo.com/?td=sha256 /td sha256 /d Diffractor /n Zachariah exe\diffractor32.exe exe\diffractor64.exe exe\diffractor.exe
if %errorlevel% neq 0 (
    echo Error: Code signing failed
    exit /b %errorlevel%
)
TIMEOUT /t 1 /nobreak >nul

rem Prepare MSIX package directory
echo.
echo Preparing MSIX package...
mkdir "%PACKAGE_ROOT%"
mkdir "%PACKAGE_ROOT%\Assets"
mkdir "%PACKAGE_ROOT%\languages"
mkdir "%PACKAGE_ROOT%\dictionaries"

xcopy "%SOURCE_FILES_DIR%\diffractor.exe" "%PACKAGE_ROOT%\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\location-countries.txt" "%PACKAGE_ROOT%\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\location-places.txt" "%PACKAGE_ROOT%\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\location-states.txt" "%PACKAGE_ROOT%\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\diffractor-tools.json" "%PACKAGE_ROOT%\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\languages\*.po" "%PACKAGE_ROOT%\languages\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\dictionaries\*.aff" "%PACKAGE_ROOT%\dictionaries\" /Y >nul
xcopy "%SOURCE_FILES_DIR%\dictionaries\*.dic" "%PACKAGE_ROOT%\dictionaries\" /Y >nul
copy "%SOURCE_FILES_DIR%\AppxManifest.xml" "%PACKAGE_ROOT%\AppxManifest.xml" /Y >nul
xcopy "%INSTALLER_DIR%\StoreLogo.png" "%PACKAGE_ROOT%\Assets\" /Y >nul
xcopy "%INSTALLER_DIR%\Square150x150Logo.png" "%PACKAGE_ROOT%\Assets\" /Y >nul
xcopy "%INSTALLER_DIR%\Square44x44Logo.png" "%PACKAGE_ROOT%\Assets\" /Y >nul
xcopy "%INSTALLER_DIR%\Wide310x150Logo.png" "%PACKAGE_ROOT%\Assets\" /Y >nul
xcopy "%INSTALLER_DIR%\SplashScreen.png" "%PACKAGE_ROOT%\Assets\" /Y >nul
xcopy "%INSTALLER_DIR%\DiffractorFile.png" "%PACKAGE_ROOT%\Assets\" /Y >nul

rem Build NSIS installer
echo.
echo Building NSIS installer...
"C:\Program Files (x86)\NSIS\makensis.exe" /INPUTCHARSET UTF8 %INSTALLER_DIR%\diff.nsi
if %errorlevel% neq 0 (
    echo Error: NSIS installer build failed
    exit /b %errorlevel%
)

rem Build MSIX package
echo.
echo Building MSIX package...
"%SDK_BIN_DIR%\MakeAppx.exe" pack /d "%PACKAGE_ROOT%" /p "%STORE_PACKAGE_NAME%.msix" /o
if %errorlevel% neq 0 (
    echo Error: MSIX package build failed
    exit /b %errorlevel%
)

rem Sign installer and MSIX
echo.
echo Signing installer...
"%SDK_BIN_DIR%\SignTool.exe" sign /fd SHA256 /tr http://timestamp.sectigo.com?td=sha256 /td sha256 /d Diffractor /n Zachariah diffractor-setup.exe
if %errorlevel% neq 0 (
    echo Error: Installer signing failed
    exit /b %errorlevel%
)
TIMEOUT /t 1 /nobreak >nul

echo.
echo Signing MSIX package...
"%SDK_BIN_DIR%\SignTool.exe" sign /fd SHA256 /sha1 0BC1CD0A4F37CE2A5A2CE72DAA9B08B1EC1CB522 /d Diffractor /tr http://timestamp.sectigo.com?td=sha256 /td sha256 %STORE_PACKAGE_NAME%.msix
if %errorlevel% neq 0 (
    echo Error: MSIX signing failed
    exit /b %errorlevel%
)
TIMEOUT /t 1 /nobreak >nul

rem Add symbols to symbol store
echo.
echo Adding symbols to symbol store...
"%TOOLSDIR%\symstore" add /r /f exe\diffractor32.exe /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor32.pdb /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor64.exe /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor64.pdb /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor.exe /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor.pdb /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%

rem Create test installer copy
copy diffractor-setup.exe diffractor-setup-test.exe >nul

rem Create portable ZIP
echo.
echo Creating portable ZIP...
pushd exe
"C:\Program Files\7-Zip\7z.exe" a -tzip -mx9 ../diffractor.zip diffractor.exe diffractor64.exe diffractor-tools.json location-countries.txt location-places.txt location-states.txt languages/de.po languages/it.po dictionaries/en_US.aff dictionaries/en_US.dic
if %errorlevel% neq 0 (
    popd
    echo Error: ZIP creation failed
    exit /b %errorlevel%
)
popd

echo.
echo ============================================================================
echo Release build completed successfully!
echo ============================================================================
echo.
echo Output files:
echo   diffractor-setup.exe           Desktop installer
echo   %STORE_PACKAGE_NAME%.msix      Windows Store package
echo   diffractor.zip                 Portable distribution
echo.
exit /b 0
