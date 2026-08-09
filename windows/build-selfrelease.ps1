# Self-release (Velopack) build wrapper.
# Reads the version from version.txt (the single version source of truth for
# the self-release channel) and invokes build.ps1 -SelfRelease.
#
# Output lands in build\velopack\ (x64) or build\velopack-arm64\ (ARM64):
# Setup.exe, *-full.nupkg, releases.<channel>.json, portable zip. Upload with:
# vpk upload github --repoUrl <repo> --publish [--merge] [-c win-arm64]

param(
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    # Optional override; defaults to version.txt. CI uses this to stamp PR
    # builds one patch above the live version so the upgrade E2E has
    # something to upgrade to (PR artifacts are never published).
    [string]$Version,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if (-not $Version) {
    $versionFile = "$root\version.txt"
    if (-not (Test-Path $versionFile)) { throw "version.txt not found at $versionFile" }
    $Version = (Get-Content $versionFile -Raw).Trim()
    if ([string]::IsNullOrWhiteSpace($Version)) { throw "version.txt is empty" }
}

Write-Host "Building self-release v$Version ($Platform)..." -ForegroundColor Cyan

$buildArgs = @{
    SelfRelease = $true
    Version     = $Version
    Platform    = $Platform
}
if ($Clean) { $buildArgs.Clean = $true }

& "$root\build.ps1" @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build.ps1 -SelfRelease failed" }
