# Build the FlaUI GUI automation helper.
# Uses the same MSBuild toolchain as AgentRedactor.
param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$projectDir = "$root\FlaUIHelper"
$project = "$projectDir\FlaUIHelper.csproj"
$nugetCandidates = @(
    "$root\..\..\..\AgentRedactor\build\tools\nuget.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\NuGet.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\NuGet.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\NuGet.exe",
    "${env:ProgramFiles}\NuGet\nuget.exe"
)
$nuget = $nugetCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $nuget) {
    $nuget = (Get-Command nuget.exe -ErrorAction SilentlyContinue).Source
}
if (-not $nuget) {
    # Last resort: download to the shared tools folder (same location
    # AgentRedactor\buildquick.ps1 uses)
    $nuget = "$root\..\..\..\AgentRedactor\build\tools\nuget.exe"
    New-Item -ItemType Directory -Force -Path (Split-Path $nuget) | Out-Null
    Write-Host "Downloading NuGet..." -ForegroundColor Green
    Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nuget
}
$msbuildCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

Write-Host "Using NuGet: $nuget" -ForegroundColor Green
Write-Host "Restoring NuGet packages..." -ForegroundColor Green
& $nuget restore $project -SolutionDirectory $projectDir

$msbuild = $null
foreach ($candidate in $msbuildCandidates) {
    if (Test-Path $candidate) {
        $msbuild = $candidate
        break
    }
}
if (-not $msbuild) {
    $msbuild = (Get-Command msbuild.exe -ErrorAction SilentlyContinue).Source
}
if (-not $msbuild) {
    # Fall back to vswhere (present on GitHub Actions runners where MSBuild
    # is not on PATH)
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    }
}
if (-not $msbuild) {
    throw "msbuild.exe not found"
}

Write-Host "Building with MSBuild: $msbuild" -ForegroundColor Green
& $msbuild $project -p:Configuration=$Configuration -p:Platform=$Platform -verbosity:minimal

$exe = "$projectDir\bin\$Platform\$Configuration\FlaUIHelper.exe"
if (Test-Path $exe) {
    Write-Host "Built $exe" -ForegroundColor Green
} else {
    throw "Build did not produce $exe"
}
