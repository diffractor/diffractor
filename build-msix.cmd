@echo off
setlocal

set VSDIR=%VSINSTALLDIR%
set TOOLSDIR=tools
set MSBUILDDIR=%VSDIR%\Msbuild\Current\Bin

Call  "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat"

"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=x64
if %errorlevel% neq 0 exit /b %errorlevel%
set "SOURCE_FILES_DIR=%~dp0.\exe"
set "INSTALLER_DIR=%~dp0.\installer"
set "OUTPUT_DIR=%~msix"
set "PACKAGE_NAME=Diffractor_1.26.1.1185_x64"
set "PFX_FILE_PATH="
set "PFX_PASSWORD="
set "SDK_BIN_DIR=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"

:: =================================================================================

:: The temporary staging directory for the package contents.
set "PACKAGE_ROOT=%~dp0dist"

echo.
echo [INFO] Cleaning up previous build directories...
if exist "%PACKAGE_ROOT%" rd /s /q "%PACKAGE_ROOT%"
if exist "%OUTPUT_DIR%" rd /s /q "%OUTPUT_DIR%"
mkdir "%PACKAGE_ROOT%"
mkdir "%OUTPUT_DIR%"
mkdir "%PACKAGE_ROOT%\Assets"
mkdir "%PACKAGE_ROOT%\languages"
mkdir "%PACKAGE_ROOT%\dictionaries"


:: --- Step 1: Stage all application files ---
echo.
echo [INFO] Staging application files into "%PACKAGE_ROOT%"...

:: Copy main executables and support files
xcopy "%SOURCE_FILES_DIR%\diffractor64.exe" "%PACKAGE_ROOT%\" /Y
xcopy "%SOURCE_FILES_DIR%\diffractor.exe" "%PACKAGE_ROOT%\" /Y
xcopy "%SOURCE_FILES_DIR%\location-countries.txt" "%PACKAGE_ROOT%\" /Y
xcopy "%SOURCE_FILES_DIR%\location-places.txt" "%PACKAGE_ROOT%\" /Y
xcopy "%SOURCE_FILES_DIR%\location-states.txt" "%PACKAGE_ROOT%\" /Y
xcopy "%SOURCE_FILES_DIR%\diffractor-tools.json" "%PACKAGE_ROOT%\" /Y

:: Copy language files
xcopy "%SOURCE_FILES_DIR%\languages\*.po" "%PACKAGE_ROOT%\languages\" /Y

:: Copy dictionary files
xcopy "%SOURCE_FILES_DIR%\dictionaries\*.aff" "%PACKAGE_ROOT%\dictionaries\" /Y
xcopy "%SOURCE_FILES_DIR%\dictionaries\*.dic" "%PACKAGE_ROOT%\dictionaries\" /Y

:: Copy the manifest into the root of the staging directory
copy "%SOURCE_FILES_DIR%\AppxManifest.xml" "%PACKAGE_ROOT%\AppxManifest.xml" /Y

:: PNG files
xcopy "%INSTALLER_DIR%\StoreLogo.png" "%PACKAGE_ROOT%\Assets\" /Y
xcopy "%INSTALLER_DIR%\Square150x150Logo.png" "%PACKAGE_ROOT%\Assets\" /Y
xcopy "%INSTALLER_DIR%\Square44x44Logo.png" "%PACKAGE_ROOT%\Assets\" /Y
xcopy "%INSTALLER_DIR%\Wide310x150Logo.png" "%PACKAGE_ROOT%\Assets\" /Y
xcopy "%INSTALLER_DIR%\SplashScreen.png" "%PACKAGE_ROOT%\Assets\" /Y
xcopy "%INSTALLER_DIR%\DiffractorFile.png" "%PACKAGE_ROOT%\Assets\" /Y

:: --- Step 2: Verify SDK tools exist ---
echo.
echo [INFO] Verifying SDK tools...
if not exist "%SDK_BIN_DIR%\MakeAppx.exe" (
    echo [ERROR] MakeAppx.exe not found at "%SDK_BIN_DIR%".
    echo [ERROR] Please update the SDK_BIN_DIR variable in this script.
    goto :eof
)
if not exist "%SDK_BIN_DIR%\SignTool.exe" (
    echo [ERROR] SignTool.exe not found at "%SDK_BIN_DIR%".
    echo [ERROR] Please update the SDK_BIN_DIR variable in this script.
    goto :eof
)
echo [INFO] SDK tools found.

:: --- Step 3: Create the MSIX package ---
echo.
echo [INFO] Creating MSIX package...
"%SDK_BIN_DIR%\MakeAppx.exe" pack /d "%PACKAGE_ROOT%" /p "%OUTPUT_DIR%\%PACKAGE_NAME%.msix" /o
if errorlevel 1 (
    echo [ERROR] MakeAppx.exe failed to create the package.
    goto :eof
)
echo [INFO] Package created successfully: %OUTPUT_DIR%\%PACKAGE_NAME%.msix

:: --- Step 4: Sign the MSIX package ---
echo.
echo [INFO] Signing the package...
if not exist "%PFX_FILE_PATH%" (
    echo [ERROR] Certificate file not found at "%PFX_FILE_PATH%".
    echo [ERROR] Please update the PFX_FILE_PATH variable in this script.
    goto :eof
)
"%SDK_BIN_DIR%\SignTool.exe" sign /fd SHA256 /a /f "%PFX_FILE_PATH%" /p "%PFX_PASSWORD%" "%OUTPUT_DIR%\%PACKAGE_NAME%.msix"
if errorlevel 1 (
    echo [ERROR] SignTool.exe failed to sign the package.
    goto :eof
)
echo [INFO] Package signed successfully.

echo.
echo [SUCCESS] Build process completed.
echo.

endlocal
