# Agent Redactor - Quick Build Script
# Builds the WinUI 3 app quickly for local testing (no MSI/MSIX packaging).
# For release packaging, use build.ps1 instead.

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$outDir = "$root\build\$Platform\$Configuration"

# Stop any running instance so the build can overwrite the EXE
$proc = Get-Process -Name "AgentRedactorUI","agentredactor" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Stopping running AgentRedactorUI.exe / agentredactor.exe..." -ForegroundColor Yellow
    Stop-Process -Name "AgentRedactorUI","agentredactor" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

function Find-Tool {
    param([string]$Name, [string[]]$Paths)
    foreach ($p in $Paths) {
        if (Test-Path $p) { return $p }
    }
    $inPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }
    return $null
}

$msbuild = Find-Tool "msbuild.exe" @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)
if (-not $msbuild) {
    # Fall back to vswhere (present on all machines with any VS install,
    # including GitHub Actions runners where MSBuild is not on PATH)
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    }
}
if (-not $msbuild) { throw "Could not find msbuild.exe" }
Write-Host "Found MSBuild: $msbuild" -ForegroundColor Green

$nuget = Find-Tool "nuget.exe" @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\NuGet.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\NuGet.exe",
    "${env:ProgramFiles}\NuGet\nuget.exe"
)
if (-not $nuget) {
    # Download nuget if not found. tests/gui/windows/build.ps1 expects it at
    # this path, so keep the location in sync with build.ps1.
    $nuget = "$root\build\tools\nuget.exe"
    if (-not (Test-Path $nuget)) {
        New-Item -ItemType Directory -Force -Path "$root\build\tools" | Out-Null
        Write-Host "Downloading NuGet..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nuget
    }
}
Write-Host "Found NuGet: $nuget" -ForegroundColor Green

# Restore NuGet packages (packages/ is not committed, so a fresh CI checkout
# has none; a no-op when they are already restored)
Write-Host "Restoring NuGet packages..." -ForegroundColor Cyan
& $nuget restore "$root\AgentRedactor.vcxproj" -PackagesDirectory "$root\packages"
if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed" }

$makepri = Find-Tool "makepri.exe" @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\makepri.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64\makepri.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22000.0\x64\makepri.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.19041.0\x64\makepri.exe"
)
if (-not $makepri) {
    $makepri = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits" -Filter "makepri.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $makepri) { throw "Could not find makepri.exe" }
Write-Host "Found MakePRI: $makepri" -ForegroundColor Green

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building AgentRedactor ($Configuration|$Platform)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
$msbuildArgs = @("$root\AgentRedactor.vcxproj", "-p:Configuration=$Configuration", "-p:Platform=$Platform", "-p:RestorePackages=false", "-m", "-verbosity:minimal")
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Engine process (agentredactor.exe): the GUI spawns it from the same output
# folder, so local dev/test runs need it built too.
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building AgentRedactorEngine ($Configuration|$Platform)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
& $msbuild "$root\AgentRedactorEngine.vcxproj" "-p:Configuration=$Configuration" "-p:Platform=$Platform" "-m" "-verbosity:minimal"
if ($LASTEXITCODE -ne 0) { throw "Engine build failed" }

# Copy model files required at runtime
if (Test-Path "$root\models") {
    Write-Host "Copying model files..." -ForegroundColor Cyan
    if (-not (Test-Path "$outDir\models")) {
        New-Item -ItemType Directory -Force -Path "$outDir\models" | Out-Null
    }
    robocopy "$root\models" "$outDir\models" /E /R:3 /W:1 | Out-Null
}

# Copy icon resources required at runtime
$icoFiles = @("app.ico", "fox_grey.ico", "fox_warning.ico")
foreach ($ico in $icoFiles) {
    if (Test-Path "$root\resources\$ico") {
        Copy-Item "$root\resources\$ico" "$outDir\$ico" -Force
    }
}

# Generate resources.pri so the EXE can resolve localized strings when run directly
Write-Host ""
Write-Host "Generating resources.pri..." -ForegroundColor Cyan
if (-not (Test-Path "$outDir\Strings")) {
    New-Item -ItemType Directory -Force -Path "$outDir\Strings" | Out-Null
}
robocopy "$root\Strings" "$outDir\Strings" /MIR /R:3 /W:1 | Out-Null
$priStage = "$outDir\pri_staging"
if (Test-Path $priStage) { Remove-Item -Recurse -Force $priStage }
New-Item -ItemType Directory -Force -Path "$priStage\Strings" | Out-Null
robocopy "$outDir\Strings" "$priStage\Strings" /E /R:3 /W:1 | Out-Null
Copy-Item "$root\Package.appxmanifest" "$priStage\AppxManifest.xml" -Force
$priConfig = "$priStage\priconfig.xml"
$langs = @('en','de','es','fr','pt','it','da','nl','sv','lb','nb','fi','ru','hr','el','sl','sr-Latn','uk','sq','lv','hy','cs','et','sk','bg','ka','hu','pl','ro','lt','is','zh-CN','zh-TW','ja','ko','mt','hi','ta','vi','sw','af','he','id','fil','ig-NG','th','tr','ur','ar','ms','az-Latn','kk','ha-Latn')
& $makepri createconfig /cf $priConfig /dq ($langs -join '_') /pv 10.0.0 /o
if ($LASTEXITCODE -ne 0) { throw "makepri createconfig failed" }
& $makepri new /pr $priStage /cf $priConfig /of "$outDir\resources.pri" /o
if ($LASTEXITCODE -ne 0) { throw "makepri new failed" }
Remove-Item -Path $priStage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$outDir\resources.pri.xml" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$outDir\resources.language-*.pri.xml" -Force -ErrorAction SilentlyContinue
Write-Host "Generated resources.pri" -ForegroundColor Green

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Quick build complete!" -ForegroundColor Green
Write-Host "EXE output: $outDir\AgentRedactorUI.exe (+ agentredactor.exe engine/CLI)" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
