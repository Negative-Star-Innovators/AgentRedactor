# Self-release (Velopack) build wrapper.
# Reads the version from version.txt (the single version source of truth for
# the self-release channel) and invokes build.ps1 -SelfRelease.
#
# Output lands in build\velopack\ (Setup.exe, *-full.nupkg, releases.win.json,
# portable zip). Upload with: vpk upload github --repoUrl <repo> --publish

param(
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$versionFile = "$root\version.txt"
if (-not (Test-Path $versionFile)) { throw "version.txt not found at $versionFile" }
$version = (Get-Content $versionFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($version)) { throw "version.txt is empty" }

Write-Host "Building self-release v$version ($Platform)..." -ForegroundColor Cyan

$buildArgs = @{
    SelfRelease = $true
    Version     = $version
    Platform    = $Platform
}
if ($Clean) { $buildArgs.Clean = $true }

& "$root\build.ps1" @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build.ps1 -SelfRelease failed" }
