# Agent Redactor Bundle Script
# Combines the per-architecture MSIX packages (built by build.ps1 -Platform x64
# and -Platform ARM64) into a single AgentRedactor.msixbundle for the Store.

param(
    [string]$BundlePath = "$PSScriptRoot\build\AgentRedactor.msixbundle"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = "$root\build"

# ============================================================================
# FIND MAKEAPPX
# ============================================================================
function Find-Tool {
    param([string]$Name, [string[]]$Paths)
    foreach ($p in $Paths) {
        if (Test-Path $p) { return $p }
    }
    $inPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }
    return $null
}

$makeappx = Find-Tool "MakeAppx.exe" @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\MakeAppx.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64\MakeAppx.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22000.0\x64\MakeAppx.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.19041.0\x64\MakeAppx.exe"
)
if (-not $makeappx) {
    $makeappx = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits" -Filter "MakeAppx.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $makeappx) { throw "Could not find MakeAppx.exe" }
Write-Host "Found MakeAppx: $makeappx" -ForegroundColor Green

# ============================================================================
# COLLECT PER-ARCH PACKAGES
# ============================================================================
$bundleStage = "$buildDir\bundle_stage"
if (Test-Path $bundleStage) { Remove-Item -Recurse -Force $bundleStage }
New-Item -ItemType Directory -Force -Path $bundleStage | Out-Null

$packages = Get-ChildItem -Path $buildDir -Filter "AgentRedactor-*.msix" -File
if (-not $packages) {
    throw "No per-architecture MSIX packages found in $buildDir. Run build.ps1 -Platform x64 and/or -Platform ARM64 first."
}
foreach ($pkg in $packages) {
    Write-Host "Bundling: $($pkg.Name)" -ForegroundColor Cyan
    Copy-Item $pkg.FullName $bundleStage -Force
}

# ============================================================================
# BUNDLE
# ============================================================================
& $makeappx bundle /d $bundleStage /p $BundlePath /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx bundle failed" }

Remove-Item -Recurse -Force $bundleStage -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Bundle created: $BundlePath" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
