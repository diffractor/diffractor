<#
.SYNOPSIS
    Diffractor Build Script

.DESCRIPTION
    Build script for Diffractor photo and video organizer.

.PARAMETER Command
    The build command to execute:
    - desktop    : Build desktop versions (diffractor32.exe, diffractor64.exe), create ZIP and NSIS installer
    - store      : Build Windows Store version (MSIX package)
    - deploy     : Deploy build artifacts to Google Cloud Storage
    - release    : Create GitHub release with tag and upload installer/zip assets
    - run        : Run the recently built diffractor64.exe
    - bump-build : Increment the build number (e.g., 1187 -> 1188)
    - bump-ver   : Increment the minor version (e.g., 1.26.2 -> 1.26.3)

.EXAMPLE
    .\dd.ps1 desktop
    Build desktop binaries and installer

.EXAMPLE
    .\dd.ps1 store
    Build Windows Store MSIX package

.EXAMPLE
    .\dd.ps1 release
    Create GitHub release, tag code, and upload diffractor-setup.exe and diffractor.zip

.EXAMPLE
    .\dd.ps1 run
    Run diffractor64.exe

.EXAMPLE
    .\dd.ps1 bump-build
    Increment build number in all version files

.EXAMPLE
    .\dd.ps1 bump-ver
    Increment minor version in all version files
#>

param(
    [Parameter(Position = 0)]
    [ValidateSet("desktop", "store", "run", "bump-build", "bump-ver", "deploy", "release", "loc", "code", "clear-cache", "help", "")]
    [string]$Command = ""
)

$ErrorActionPreference = "Stop"

# Configuration
$ScriptDir = $PSScriptRoot
$ToolsDir = Join-Path $ScriptDir "tools"
$SourceFilesDir = Join-Path $ScriptDir "exe"
$PackageRoot = Join-Path $ScriptDir "dist"
$InstallerDir = Join-Path $ScriptDir "installer"
$SdkBinDir = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"

# Signing certificates
$DesktopSignName = "Zachariah"
$StoreSignThumbprint = "0BC1CD0A4F37CE2A5A2CE72DAA9B08B1EC1CB522"

# Version file paths
$NsiFile = Join-Path $InstallerDir "diff.nsi"
$AppxManifestFile = Join-Path $SourceFilesDir "AppxManifest.xml"
$ResourceFile = Join-Path $ScriptDir "src\platform_win_res.rc"
$AppCppFile = Join-Path $ScriptDir "src\app.cpp"

# ============================================================================
# Version Management Functions
# ============================================================================

function Get-CurrentVersion {
    # Read version from app.cpp as the source of truth
    $content = Get-Content $AppCppFile -Raw
    
    # Extract version: const auto s_app_version = u8"126.2"sv;
    if ($content -match 'const auto s_app_version = u8"(\d+)\.(\d+)"sv;') {
        $majorMinor = $Matches[1]  # e.g., "126"
        $patch = $Matches[2]       # e.g., "2"
        
        # Parse majorMinor: first digit is major, rest is minor
        $major = [int]$majorMinor.Substring(0, 1)
        $minor = [int]$majorMinor.Substring(1)
        $patchNum = [int]$patch
    }
    else {
        Write-Host "Error: Could not parse version from app.cpp" -ForegroundColor Red
        exit 1
    }
    
    # Extract build: const auto g_app_build = u8"1187"sv;
    if ($content -match 'const auto g_app_build = u8"(\d+)"sv;') {
        $build = [int]$Matches[1]
    }
    else {
        Write-Host "Error: Could not parse build number from app.cpp" -ForegroundColor Red
        exit 1
    }
    
    return @{
        Major = $major
        Minor = $minor
        Patch = $patchNum
        Build = $build
        VersionString = "$major.$minor.$patchNum"           # e.g., "1.26.2"
        AppVersion = "$major$minor.$patchNum"               # e.g., "126.2"
        FileVersion = "$major.$minor.$patchNum.$build"      # e.g., "1.26.2.1187"
        CommaVersion = "$major, $minor, $patchNum, $build"  # e.g., "1, 26, 2, 1187"
    }
}

function Update-AllVersionFiles {
    param(
        [int]$Major,
        [int]$Minor,
        [int]$Patch,
        [int]$Build
    )
    
    $appVersion = "$Major$Minor.$Patch"                    # e.g., "126.2"
    $fileVersion = "$Major.$Minor.$Patch.$Build"           # e.g., "1.26.2.1187"
    $commaVersion = "$Major, $Minor, $Patch, $Build"       # e.g., "1, 26, 2, 1187"
    $productVersion = "$Major$Minor.$Patch"                # e.g., "126.2"
    
    Write-Host ""
    Write-Host "Updating version to: $fileVersion" -ForegroundColor Cyan
    Write-Host "  App version: $appVersion"
    Write-Host "  Build: $Build"
    Write-Host ""
    
    # Update app.cpp
    Write-Host "Updating $AppCppFile..." -ForegroundColor Yellow
    $content = Get-Content $AppCppFile -Raw
    $content = $content -replace 'const auto s_app_version = u8"\d+\.\d+"sv;', "const auto s_app_version = u8`"$appVersion`"sv;"
    $content = $content -replace 'const auto g_app_build = u8"\d+"sv;', "const auto g_app_build = u8`"$Build`"sv;"
    Set-Content $AppCppFile $content -NoNewline
    
    # Update diff.nsi
    Write-Host "Updating $NsiFile..." -ForegroundColor Yellow
    $content = Get-Content $NsiFile -Raw
    $content = $content -replace '!define BUILD_NUM "\d+"', "!define BUILD_NUM `"$Build`""
    $content = $content -replace '!define PRODUCT_VERSION "\d+\.\d+"', "!define PRODUCT_VERSION `"$productVersion`""
    Set-Content $NsiFile $content -NoNewline
    
    # Update AppxManifest.xml (Store requires revision/4th part to be 0)
    Write-Host "Updating $AppxManifestFile..." -ForegroundColor Yellow
    $storeVersion = "$Major.$Minor.$Patch.0"
    # Get current Windows build for MaxVersionTested
    $winVer = [System.Environment]::OSVersion.Version
    $maxVersionTested = "10.0.$($winVer.Build).0"
    $content = Get-Content $AppxManifestFile -Raw
    # Use specific attribute prefixes to avoid matching MinVersion
    $content = $content -replace '(<Identity[^>]*\s)Version="\d+\.\d+\.\d+\.\d+"', "`$1Version=`"$storeVersion`""
    $content = $content -replace 'MaxVersionTested="\d+\.\d+\.\d+\.\d+"', "MaxVersionTested=`"$maxVersionTested`""
    Set-Content $AppxManifestFile $content -NoNewline
    
    # Update platform_win_res.rc
    Write-Host "Updating $ResourceFile..." -ForegroundColor Yellow
    $content = Get-Content $ResourceFile -Raw
    $content = $content -replace 'FILEVERSION \d+, \d+, \d+, \d+', "FILEVERSION $commaVersion"
    $content = $content -replace 'PRODUCTVERSION \d+, \d+, \d+, \d+', "PRODUCTVERSION $commaVersion"
    $content = $content -replace 'VALUE "FileVersion", "\d+\.\d+\.\d+\.\d+"', "VALUE `"FileVersion`", `"$fileVersion`""
    $content = $content -replace 'VALUE "ProductVersion", "\d+\.\d+\.\d+\.\d+"', "VALUE `"ProductVersion`", `"$fileVersion`""
    Set-Content $ResourceFile $content -NoNewline
    
    # Update dd.ps1 StorePackageName
    Write-Host "Updating dd.ps1 StorePackageName..." -ForegroundColor Yellow
    $ddContent = Get-Content $PSCommandPath -Raw
    $ddContent = $ddContent -replace '\$StorePackageName = "Diffractor_\d+\.\d+\.\d+\.\d+_x64"', "`$StorePackageName = `"Diffractor_${fileVersion}_x64`""
    Set-Content $PSCommandPath $ddContent -NoNewline
    
    Write-Host ""
    Write-Host "Version updated successfully!" -ForegroundColor Green
    Write-Host ""
}

function Invoke-BumpBuild {
    $version = Get-CurrentVersion
    $newBuild = $version.Build + 1
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Incrementing Build Number" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Current: $($version.FileVersion)"
    Write-Host "New:     $($version.Major).$($version.Minor).$($version.Patch).$newBuild"
    
    Update-AllVersionFiles -Major $version.Major -Minor $version.Minor -Patch $version.Patch -Build $newBuild
}

function Invoke-BumpVersion {
    $version = Get-CurrentVersion
    $newPatch = $version.Patch + 1
    $newBuild = $version.Build + 1
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Incrementing Version Number" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Current: $($version.FileVersion)"
    Write-Host "New:     $($version.Major).$($version.Minor).$newPatch.$newBuild"
    
    Update-AllVersionFiles -Major $version.Major -Minor $version.Minor -Patch $newPatch -Build $newBuild
}

# ============================================================================
# Build Functions
# ============================================================================

# Dynamic store package name based on current version
$currentVer = Get-CurrentVersion
$StorePackageName = "Diffractor_$($currentVer.FileVersion)_x64"

function Show-Usage {
    $version = Get-CurrentVersion
    
    Write-Host ""
    Write-Host "Diffractor Build Script" -ForegroundColor Cyan
    Write-Host "========================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Current Version: $($version.FileVersion)" -ForegroundColor Green
    Write-Host ""
    Write-Host "Usage: .\dd.ps1 <command>"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  desktop      Build desktop versions (Win32 + x64), auto-increments build number"
    Write-Host "  store        Build Windows Store version (MSIX), auto-increments build number"
    Write-Host "  deploy       Deploy desktop build artifacts to Google Cloud Storage"
    Write-Host "  release      Create GitHub release with tag and upload installers"
    Write-Host "  run          Run the recently built diffractor64.exe"
    Write-Host "  code         Open VS Code with Developer Command Prompt environment"
    Write-Host "  loc          Regenerate location database files from geonames"
    Write-Host "  bump-build   Manually increment build number (e.g., $($version.Build) -> $($version.Build + 1))"
    Write-Host "  bump-ver     Increment version (e.g., $($version.VersionString) -> $($version.Major).$($version.Minor).$($version.Patch + 1))"
    Write-Host "  clear-cache  Clear Windows icon and thumbnail cache (requires restart)"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\dd.ps1 desktop      Build desktop release (auto-increments build)"
    Write-Host "  .\dd.ps1 deploy       Upload installers to GCS"
    Write-Host "  .\dd.ps1 release      Create GitHub release and tag code"
    Write-Host "  .\dd.ps1 store        Build Windows Store package (auto-increments build)"
    Write-Host "  .\dd.ps1 bump-ver     Increment version before a major release"
    Write-Host "  .\dd.ps1 run          Run diffractor64.exe"
    Write-Host ""
}

function Test-VisualStudioEnvironment {
    if (-not $env:VSINSTALLDIR) {
        Write-Host "Error: Visual Studio environment not detected." -ForegroundColor Red
        Write-Host "Please run from a Developer PowerShell or run vcvars64.bat first." -ForegroundColor Red
        exit 1
    }
    return $env:VSINSTALLDIR
}

function Get-MSBuildPath {
    $vsDir = Test-VisualStudioEnvironment
    $msbuildPath = Join-Path $vsDir "Msbuild\Current\Bin\msbuild.exe"
    if (-not (Test-Path $msbuildPath)) {
        Write-Host "Error: MSBuild not found at $msbuildPath" -ForegroundColor Red
        exit 1
    }
    return $msbuildPath
}

function Invoke-MSBuild {
    param(
        [string]$Project,
        [string]$Configuration,
        [string]$Platform
    )
    
    $msbuild = Get-MSBuildPath
    Write-Host ""
    Write-Host "Building $Configuration | $Platform..." -ForegroundColor Yellow
    
    & $msbuild $Project /p:Configuration=$Configuration /p:Platform=$Platform /m
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Build failed for $Configuration | $Platform" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

function Invoke-SignTool {
    param(
        [string]$Description,
        [string[]]$Files,
        [switch]$UseThumbprint
    )
    
    $signTool = Join-Path $SdkBinDir "SignTool.exe"
    if (-not (Test-Path $signTool)) {
        Write-Host "Error: SignTool not found at $signTool" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Signing: $Description..." -ForegroundColor Yellow
    
    if ($UseThumbprint) {
        & $signTool sign /fd SHA256 /sha1 $StoreSignThumbprint /d Diffractor /tr "http://timestamp.sectigo.com?td=sha256" /td sha256 @Files
    }
    else {
        & $signTool sign /fd SHA256 /tr "http://timestamp.sectigo.com/?td=sha256" /td sha256 /d Diffractor /n $DesktopSignName @Files
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Signing failed" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    Start-Sleep -Seconds 1
}

function Add-ToSymbolStore {
    param(
        [string]$File
    )
    
    $symstore = Join-Path $ToolsDir "symstore"
    & $symstore add /r /f $File /s "c:\code\symbols" /t Diffractor /compress
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to add $File to symbol store" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

function Build-Desktop {
    # Auto-increment build number before building
    Invoke-BumpBuild
    
    $version = Get-CurrentVersion
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Building Diffractor Desktop Release v$($version.FileVersion)" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    
    # Clean previous builds
    Write-Host ""
    Write-Host "Cleaning previous release artifacts..." -ForegroundColor Yellow
    $filesToClean = @(
        "diffractor-setup.exe",
        "diffractor.zip"
    )
    foreach ($file in $filesToClean) {
        $path = Join-Path $ScriptDir $file
        if (Test-Path $path) {
            Remove-Item $path -Force
        }
    }
    
    # Build Win32 and x64
    Invoke-MSBuild -Project "df.sln" -Configuration "Release" -Platform "Win32"
    Invoke-MSBuild -Project "df.sln" -Configuration "Release" -Platform "x64"
    
    # Sign executables
    Write-Host ""
    $exe32 = Join-Path $SourceFilesDir "diffractor32.exe"
    $exe64 = Join-Path $SourceFilesDir "diffractor64.exe"
    Invoke-SignTool -Description "desktop executables" -Files @($exe32, $exe64)
    
    # Build NSIS installer
    Write-Host ""
    Write-Host "Building NSIS installer..." -ForegroundColor Yellow
    $nsis = "C:\Program Files (x86)\NSIS\makensis.exe"
    $nsiScript = Join-Path $InstallerDir "diff.nsi"
    
    if (-not (Test-Path $nsis)) {
        Write-Host "Error: NSIS not found at $nsis" -ForegroundColor Red
        exit 1
    }
    
    & $nsis /INPUTCHARSET UTF8 $nsiScript
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: NSIS installer build failed" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    # Sign installer
    Write-Host ""
    $installer = Join-Path $ScriptDir "diffractor-setup.exe"
    Invoke-SignTool -Description "installer" -Files @($installer)
    
    # Add symbols to symbol store
    Write-Host ""
    Write-Host "Adding symbols to symbol store..." -ForegroundColor Yellow
    Add-ToSymbolStore -File (Join-Path $SourceFilesDir "diffractor32.exe")
    Add-ToSymbolStore -File (Join-Path $SourceFilesDir "diffractor32.pdb")
    Add-ToSymbolStore -File (Join-Path $SourceFilesDir "diffractor64.exe")
    Add-ToSymbolStore -File (Join-Path $SourceFilesDir "diffractor64.pdb")
    
    # Create portable ZIP
    Write-Host ""
    Write-Host "Creating portable ZIP..." -ForegroundColor Yellow
    Push-Location $SourceFilesDir
    
    $sevenZip = "C:\Program Files\7-Zip\7z.exe"
    if (-not (Test-Path $sevenZip)) {
        Write-Host "Error: 7-Zip not found at $sevenZip" -ForegroundColor Red
        Pop-Location
        exit 1
    }
    
    $zipFiles = @(
        "diffractor32.exe",
        "diffractor64.exe",
        "diffractor-tools.json",
        "location-countries.txt",
        "location-places.txt",
        "location-states.txt",
        "languages\de.po",
        "languages\it.po",
        "dictionaries\en_US.aff",
        "dictionaries\en_US.dic"
    )
    
    & $sevenZip a -tzip -mx9 "..\diffractor.zip" @zipFiles
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: ZIP creation failed" -ForegroundColor Red
        Pop-Location
        exit $LASTEXITCODE
    }
    
    Pop-Location
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host "Desktop build completed successfully!" -ForegroundColor Green
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Version: $($version.FileVersion)"
    Write-Host ""
    Write-Host "Output files:"
    Write-Host "  diffractor-setup.exe    Desktop installer"
    Write-Host "  diffractor.zip          Portable distribution"
    Write-Host "  exe\diffractor32.exe    32-bit executable"
    Write-Host "  exe\diffractor64.exe    64-bit executable"
    Write-Host ""
}

function Build-Store {
    $version = Get-CurrentVersion
    $storePackage = "Diffractor_$($version.FileVersion)_x64"
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Building Diffractor Windows Store Package v$($version.FileVersion)" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    
    # Clean previous builds
    Write-Host ""
    Write-Host "Cleaning previous release artifacts..." -ForegroundColor Yellow
    $msixPath = Join-Path $ScriptDir "$storePackage.msix"
    if (Test-Path $msixPath) {
        Remove-Item $msixPath -Force
    }
    if (Test-Path $PackageRoot) {
        Remove-Item $PackageRoot -Recurse -Force
    }
    
    # Build WinStore configuration
    # Invoke-MSBuild -Project "src\app.vcxproj" -Configuration "WinStore" -Platform "x64"
    Invoke-MSBuild -Project "df.sln" -Configuration "WinStore" -Platform "x64"
    
    # Sign executable
    Write-Host ""
    $storeExe = Join-Path $SourceFilesDir "diffractor.exe"
    Invoke-SignTool -Description "store executable" -Files @($storeExe) -UseThumbprint
    
    # Prepare MSIX package directory
    Write-Host ""
    Write-Host "Preparing MSIX package..." -ForegroundColor Yellow
    
    New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $PackageRoot "Assets") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $PackageRoot "languages") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $PackageRoot "dictionaries") -Force | Out-Null
    
    # Generate store assets
    Write-Host ""
    Write-Host "Generating store assets..." -ForegroundColor Yellow
    $assetsDir = Join-Path $PackageRoot "Assets"
    $generateScript = Join-Path $ToolsDir "generate_store_assets.py"
    
    & python $generateScript -o $assetsDir
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to generate store assets" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    # Copy files
    Copy-Item (Join-Path $SourceFilesDir "diffractor.exe") $PackageRoot -Force
    Copy-Item (Join-Path $SourceFilesDir "location-countries.txt") $PackageRoot -Force
    Copy-Item (Join-Path $SourceFilesDir "location-places.txt") $PackageRoot -Force
    Copy-Item (Join-Path $SourceFilesDir "location-states.txt") $PackageRoot -Force
    Copy-Item (Join-Path $SourceFilesDir "diffractor-tools.json") $PackageRoot -Force
    Copy-Item (Join-Path $SourceFilesDir "languages\*.po") (Join-Path $PackageRoot "languages") -Force
    Copy-Item (Join-Path $SourceFilesDir "dictionaries\*.aff") (Join-Path $PackageRoot "dictionaries") -Force
    Copy-Item (Join-Path $SourceFilesDir "dictionaries\*.dic") (Join-Path $PackageRoot "dictionaries") -Force
    Copy-Item (Join-Path $SourceFilesDir "AppxManifest.xml") $PackageRoot -Force
    
    # Generate resources.pri using MakePri (required for asset qualifiers like _altform-unplated)
    Write-Host ""
    Write-Host "Generating resources.pri..." -ForegroundColor Yellow
    $makePri = Join-Path $SdkBinDir "MakePri.exe"
    
    if (-not (Test-Path $makePri)) {
        Write-Host "Error: MakePri not found at $makePri" -ForegroundColor Red
        exit 1
    }
    
    # Create priconfig.xml
    $priConfig = Join-Path $PackageRoot "priconfig.xml"
    Push-Location $PackageRoot
    & $makePri createconfig /cf $priConfig /dq en-US /o
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to create priconfig.xml" -ForegroundColor Red
        Pop-Location
        exit $LASTEXITCODE
    }
    
    # Generate resources.pri
    & $makePri new /pr $PackageRoot /cf $priConfig /o
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to generate resources.pri" -ForegroundColor Red
        Pop-Location
        exit $LASTEXITCODE
    }
    
    # Remove priconfig.xml (not needed in package)
    Remove-Item $priConfig -Force -ErrorAction SilentlyContinue
    Pop-Location
    
    # Build MSIX package
    Write-Host ""
    Write-Host "Building MSIX package..." -ForegroundColor Yellow
    $makeAppx = Join-Path $SdkBinDir "MakeAppx.exe"
    
    if (-not (Test-Path $makeAppx)) {
        Write-Host "Error: MakeAppx not found at $makeAppx" -ForegroundColor Red
        exit 1
    }
    
    & $makeAppx pack /d $PackageRoot /p $msixPath /o
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: MSIX package build failed" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    # Sign MSIX package
    Write-Host ""
    Invoke-SignTool -Description "MSIX package" -Files @($msixPath) -UseThumbprint
    
    # Add symbols to symbol store
    Write-Host ""
    Write-Host "Adding symbols to symbol store..." -ForegroundColor Yellow
    Add-ToSymbolStore -File (Join-Path $SourceFilesDir "diffractor.exe")
    Add-ToSymbolStore -File (Join-Path $SourceFilesDir "diffractor.pdb")
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host "Windows Store build completed successfully!" -ForegroundColor Green
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Version: $($version.FileVersion)"
    Write-Host ""
    Write-Host "Output files:"
    Write-Host "  $storePackage.msix    Windows Store package"
    Write-Host "  exe\diffractor.exe        Store executable"
    Write-Host ""
}

function Start-Diffractor {
    $exe64 = Join-Path $SourceFilesDir "diffractor64.exe"
    
    if (-not (Test-Path $exe64)) {
        Write-Host "Error: diffractor64.exe not found at $exe64" -ForegroundColor Red
        Write-Host "Run '.\dd.ps1 desktop' to build first." -ForegroundColor Yellow
        exit 1
    }
    
    Write-Host "Starting diffractor64.exe..." -ForegroundColor Yellow
    Start-Process $exe64
}

function New-GitHubRelease {
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Creating GitHub Release" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    
    $version = Get-CurrentVersion
    $tagName = "v$($version.VersionString)"
    $releaseName = "Diffractor $($version.VersionString)"
    
    # Check that build artifacts exist
    $installer = Join-Path $ScriptDir "diffractor-setup.exe"
    $zipFile = Join-Path $ScriptDir "diffractor.zip"
    
    $missingFiles = @()
    if (-not (Test-Path $installer)) { $missingFiles += "diffractor-setup.exe" }
    if (-not (Test-Path $zipFile)) { $missingFiles += "diffractor.zip" }
    
    if ($missingFiles.Count -gt 0) {
        Write-Host "Error: Missing build artifacts:" -ForegroundColor Red
        foreach ($file in $missingFiles) {
            Write-Host "  - $file" -ForegroundColor Red
        }
        Write-Host ""
        Write-Host "Run '.\dd.ps1 desktop' to build first." -ForegroundColor Yellow
        exit 1
    }
    
    # Check that gh CLI is available
    $gh = Get-Command "gh" -ErrorAction SilentlyContinue
    if (-not $gh) {
        Write-Host "Error: GitHub CLI (gh) not found." -ForegroundColor Red
        Write-Host "Install from: https://cli.github.com/" -ForegroundColor Yellow
        Write-Host "Then run: gh auth login" -ForegroundColor Yellow
        exit 1
    }
    
    # Check if authenticated
    $authStatus = & gh auth status 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Not authenticated with GitHub CLI." -ForegroundColor Red
        Write-Host "Run: gh auth login" -ForegroundColor Yellow
        exit 1
    }
    
    # Check if tag already exists
    $existingTag = & git tag -l $tagName 2>&1
    if ($existingTag -eq $tagName) {
        Write-Host "Warning: Tag $tagName already exists locally." -ForegroundColor Yellow
        $response = Read-Host "Do you want to delete the existing tag and create a new release? (y/N)"
        if ($response -ne "y" -and $response -ne "Y") {
            Write-Host "Aborted." -ForegroundColor Yellow
            exit 0
        }
        # Delete local and remote tag
        Write-Host "Deleting existing tag..." -ForegroundColor Yellow
        & git tag -d $tagName 2>&1 | Out-Null
        & git push origin --delete $tagName 2>&1 | Out-Null
        # Also delete the release if it exists
        & gh release delete $tagName --yes 2>&1 | Out-Null
    }
    
    Write-Host ""
    Write-Host "Creating release: $releaseName" -ForegroundColor Yellow
    Write-Host "Tag: $tagName" -ForegroundColor Yellow
    Write-Host ""
    
    # Create release with assets, generate notes automatically
    # Note: gh release create will create the tag automatically
    & gh release create $tagName `
        --title $releaseName `
        --generate-notes `
        $installer `
        $zipFile
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Failed to create GitHub release" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host "GitHub Release created successfully!" -ForegroundColor Green
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Release: $releaseName"
    Write-Host "Tag: $tagName"
    Write-Host "URL: https://github.com/diffractor/diffractor/releases/tag/$tagName"
    Write-Host ""
    Write-Host "Uploaded assets:"
    Write-Host "  - diffractor-setup.exe"
    Write-Host "  - diffractor.zip"
    Write-Host ""
}

function Update-Locations {
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Regenerating Location Database Files" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host ""
    
    $pythonScript = Join-Path $ToolsDir "generate_locations.py"
    
    if (-not (Test-Path $pythonScript)) {
        Write-Host "Error: Python script not found at $pythonScript" -ForegroundColor Red
        exit 1
    }
    
    # Check for Python
    $python = Get-Command "python" -ErrorAction SilentlyContinue
    if (-not $python) {
        Write-Host "Error: Python not found. Please install Python 3." -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Running generate_locations.py..." -ForegroundColor Yellow
    Write-Host ""
    
    Push-Location $ScriptDir
    try {
        & python $pythonScript
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host ""
            Write-Host "Error: Location generation failed" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host "Location files updated successfully!" -ForegroundColor Green
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
}

function Open-VSCode {
    Write-Host ""
    Write-Host "Opening VS Code with Developer environment..." -ForegroundColor Cyan
    Write-Host ""
    
    # Find VS Code
    $codePath = Get-Command "code" -ErrorAction SilentlyContinue
    if (-not $codePath) {
        Write-Host "Error: VS Code 'code' command not found in PATH." -ForegroundColor Red
        Write-Host "Make sure VS Code is installed and added to PATH." -ForegroundColor Yellow
        exit 1
    }
    
    # Check if already in a dev environment
    if ($env:VSINSTALLDIR) {
        Write-Host "Developer environment already active." -ForegroundColor Gray
    }
    else {
        # Find Visual Studio installation using vswhere
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vswhere)) {
            Write-Host "Error: vswhere.exe not found. Is Visual Studio installed?" -ForegroundColor Red
            exit 1
        }
        
        $vsPath = & $vswhere -latest -property installationPath
        if (-not $vsPath) {
            Write-Host "Error: Could not find Visual Studio installation." -ForegroundColor Red
            exit 1
        }
        
        Write-Host "Using Visual Studio at: $vsPath" -ForegroundColor Gray
        
        # Use the DevShell module to set up environment in current session
        $devShellModule = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
        if (Test-Path $devShellModule) {
            Import-Module $devShellModule
            Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -no_logo"
        }
        else {
            Write-Host "Error: DevShell module not found at $devShellModule" -ForegroundColor Red
            exit 1
        }
    }
    
    Write-Host ""
    
    # Launch VS Code from current environment
    & code $ScriptDir
    
    Write-Host "VS Code launched with Developer environment." -ForegroundColor Green
    Write-Host ""
}

function Clear-IconCache {
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Clearing Windows Icon and Thumbnail Cache" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host ""
    
    # Stop Explorer to release file locks on cache files
    Write-Host "Stopping Explorer..." -ForegroundColor Yellow
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    
    $localAppData = $env:LOCALAPPDATA
    $deletedCount = 0
    
    # Delete icon cache files
    Write-Host "Deleting icon cache files..." -ForegroundColor Yellow
    $iconCacheFiles = Get-ChildItem -Path $localAppData -Filter "iconcache*.db" -ErrorAction SilentlyContinue
    foreach ($file in $iconCacheFiles) {
        try {
            Remove-Item $file.FullName -Force
            Write-Host "  Deleted: $($file.Name)" -ForegroundColor Gray
            $deletedCount++
        }
        catch {
            Write-Host "  Failed to delete: $($file.Name)" -ForegroundColor Red
        }
    }
    
    # Delete thumbnail cache files
    Write-Host "Deleting thumbnail cache files..." -ForegroundColor Yellow
    $thumbCacheDir = Join-Path $localAppData "Microsoft\Windows\Explorer"
    if (Test-Path $thumbCacheDir) {
        $thumbCacheFiles = Get-ChildItem -Path $thumbCacheDir -Filter "thumbcache*.db" -ErrorAction SilentlyContinue
        foreach ($file in $thumbCacheFiles) {
            try {
                Remove-Item $file.FullName -Force
                Write-Host "  Deleted: $($file.Name)" -ForegroundColor Gray
                $deletedCount++
            }
            catch {
                Write-Host "  Failed to delete: $($file.Name)" -ForegroundColor Red
            }
        }
        
        # Delete IconCache.db
        $iconCacheDb = Join-Path $thumbCacheDir "IconCache.db"
        if (Test-Path $iconCacheDb) {
            try {
                Remove-Item $iconCacheDb -Force
                Write-Host "  Deleted: IconCache.db" -ForegroundColor Gray
                $deletedCount++
            }
            catch {
                Write-Host "  Failed to delete: IconCache.db" -ForegroundColor Red
            }
        }
        
        # Delete iconcache_*.db files in Explorer folder
        $iconCacheFiles2 = Get-ChildItem -Path $thumbCacheDir -Filter "iconcache_*.db" -ErrorAction SilentlyContinue
        foreach ($file in $iconCacheFiles2) {
            try {
                Remove-Item $file.FullName -Force
                Write-Host "  Deleted: $($file.Name)" -ForegroundColor Gray
                $deletedCount++
            }
            catch {
                Write-Host "  Failed to delete: $($file.Name)" -ForegroundColor Red
            }
        }
    }
    
    # Restart Explorer
    Write-Host ""
    Write-Host "Restarting Explorer..." -ForegroundColor Yellow
    Start-Process explorer
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host "Cache cleared! Deleted $deletedCount file(s)." -ForegroundColor Green
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Note: You may need to log out and back in for all changes to take effect." -ForegroundColor Yellow
    Write-Host ""
}

function Deploy-Desktop {
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Cyan
    Write-Host "Deploying Desktop Build to Google Cloud Storage" -ForegroundColor Cyan
    Write-Host "============================================================================" -ForegroundColor Cyan
    
    # Check that build artifacts exist
    $installer = Join-Path $ScriptDir "diffractor-setup.exe"
    $zipFile = Join-Path $ScriptDir "diffractor.zip"
    
    $missingFiles = @()
    if (-not (Test-Path $installer)) { $missingFiles += "diffractor-setup.exe" }
    if (-not (Test-Path $zipFile)) { $missingFiles += "diffractor.zip" }
    
    if ($missingFiles.Count -gt 0) {
        Write-Host "Error: Missing build artifacts:" -ForegroundColor Red
        foreach ($file in $missingFiles) {
            Write-Host "  - $file" -ForegroundColor Red
        }
        Write-Host ""
        Write-Host "Run '.\dd.ps1 desktop' to build first." -ForegroundColor Yellow
        exit 1
    }
    
    # Check that gsutil is available
    $gsutil = Get-Command "gsutil" -ErrorAction SilentlyContinue
    if (-not $gsutil) {
        Write-Host "Error: gsutil not found. Please install Google Cloud SDK." -ForegroundColor Red
        Write-Host "https://cloud.google.com/sdk/docs/install" -ForegroundColor Yellow
        exit 1
    }
    
    Write-Host ""
    Write-Host "Uploading to gs://diffractor/..." -ForegroundColor Yellow
    
    & gsutil -m cp $installer $zipFile gs://diffractor/
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Upload failed" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    
    Write-Host ""
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host "Deploy completed successfully!" -ForegroundColor Green
    Write-Host "============================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Uploaded files:"
    Write-Host "  https://storage.googleapis.com/diffractor/diffractor-setup.exe"
    Write-Host "  https://storage.googleapis.com/diffractor/diffractor.zip"
    Write-Host ""
}

# Main entry point
switch ($Command) {
    "desktop" { Build-Desktop }
    "store" { Build-Store }
    "deploy" { Deploy-Desktop }
    "release" { New-GitHubRelease }
    "run" { Start-Diffractor }
    "code" { Open-VSCode }
    "loc" { Update-Locations }
    "bump-build" { Invoke-BumpBuild }
    "bump-ver" { Invoke-BumpVersion }
    "clear-cache" { Clear-IconCache }
    "help" { Show-Usage }
    "" { Show-Usage }
    default { Show-Usage }
}
