# Agent Redactor Build Script
# Builds WinUI 3 AgentRedactor and packages as MSIX (Microsoft Store)

param(
    [string]$Version = "1.0.0",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = "$root\build"
$outDir = "$buildDir\x64\Release"

# Stop any running instance
$proc = Get-Process -Name "AgentRedactor" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Stopping running AgentRedactor.exe..." -ForegroundColor Yellow
    Stop-Process -Name "AgentRedactor" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

# ============================================================================
# FIND TOOLS
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
    # Download nuget if not found
    $nuget = "$root\build\tools\nuget.exe"
    if (-not (Test-Path $nuget)) {
        New-Item -ItemType Directory -Force -Path "$root\build\tools" | Out-Null
        Write-Host "Downloading NuGet..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nuget
    }
}
Write-Host "Found NuGet: $nuget" -ForegroundColor Green

$makeappx = Find-Tool "MakeAppx.exe" @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\MakeAppx.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64\MakeAppx.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22000.0\x64\MakeAppx.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.19041.0\x64\MakeAppx.exe"
)
if (-not $makeappx) {
    # Search for any MakeAppx.exe
    $makeappx = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits" -Filter "MakeAppx.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $makeappx) { throw "Could not find MakeAppx.exe" }
Write-Host "Found MakeAppx: $makeappx" -ForegroundColor Green

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

# ============================================================================
# CLEAN
# ============================================================================
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    $proc = Get-Process -Name "AgentRedactor" -ErrorAction SilentlyContinue
    if ($proc) {
        Stop-Process -Name "AgentRedactor" -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
    try {
        $empty = "$env:TEMP\empty_agentredactor"
        New-Item -ItemType Directory -Force -Path $empty | Out-Null
        robocopy $empty $buildDir /MIR /R:3 /W:1 | Out-Null
        Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force $empty -ErrorAction SilentlyContinue
    } catch {
        Write-Warning "Could not fully clean build directory. Continuing anyway..."
    }
}

# ============================================================================
# BUILD
# ============================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Restoring NuGet packages..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
& $nuget restore "$root\AgentRedactor.vcxproj" -PackagesDirectory "$root\packages"
if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed" }

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building AgentRedactor (Release|x64)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
& $msbuild "$root\AgentRedactor.vcxproj" -p:Configuration=Release -p:Platform=x64 -p:RestorePackages=false
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Copy models and resources
if (Test-Path "$root\models") {
    Write-Host "Copying model files..." -ForegroundColor Cyan
    if (-not (Test-Path "$outDir\models")) {
        New-Item -ItemType Directory -Force -Path "$outDir\models" | Out-Null
    }
    robocopy "$root\models" "$outDir\models" /E /R:3 /W:1 | Out-Null
}

$icoFiles = @("app.ico", "fox_grey.ico", "fox_warning.ico")
foreach ($ico in $icoFiles) {
    if (Test-Path "$root\resources\$ico") {
        Copy-Item "$root\resources\$ico" "$outDir\$ico" -Force
    }
}

# ============================================================================
# GENERATE RESOURCES.PRI
# ============================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Generating resources.pri..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Mirror Strings folder to output so renamed/removed locales don't linger
# (MSBuild Content copy may be skipped in BuildTools)
Write-Host "Copying Strings to output..." -ForegroundColor DarkGray
New-Item -ItemType Directory -Force -Path "$outDir\Strings" | Out-Null
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

Write-Host "Generated resources.pri" -ForegroundColor Green

# Remove temporary staging directory and any dumped PRI XML files not needed in the installer/package
Remove-Item -Path $priStage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$outDir\resources.pri.xml" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$outDir\resources.language-*.pri.xml" -Force -ErrorAction SilentlyContinue

# ============================================================================
# MSIX
# ============================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Creating MSIX package..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$msixStage = "$buildDir\msix_stage"
if (Test-Path $msixStage) { Remove-Item -Recurse -Force $msixStage }
New-Item -ItemType Directory -Force -Path $msixStage | Out-Null

# Copy build output
robocopy $outDir $msixStage /E /R:3 /W:1 | Out-Null

# Never ship stale runtime logs inside the package (a packaged debug.log in the
# read-only install folder crashes wWinMain on startup)
Remove-Item "$msixStage\debug.log", "$msixStage\startup_debug.log" -Force -ErrorAction SilentlyContinue

# Copy manifest and assets
# Replace x-generate with actual languages because MakeAppx.exe does not expand it.
$manifestXml = [xml](Get-Content "$root\Package.appxmanifest")
$ns = $manifestXml.Package.NamespaceURI
$resourcesNode = $manifestXml.Package.Resources
$resourcesNode.RemoveAll()
foreach ($lang in $langs) {
    $resource = $manifestXml.CreateElement('Resource', $ns)
    $resource.SetAttribute('Language', $lang)
    $resourcesNode.AppendChild($resource) | Out-Null
}
$manifestXml.Save("$msixStage\AppxManifest.xml")

if (Test-Path "$root\resources\assets") {
    if (-not (Test-Path "$msixStage\resources\assets")) {
        New-Item -ItemType Directory -Force -Path "$msixStage\resources\assets" | Out-Null
    }
    robocopy "$root\resources\assets" "$msixStage\resources\assets" /E /R:3 /W:1 | Out-Null
}

$msixPath = "$buildDir\AgentRedactor.msix"
if (-not (Test-Path "$msixStage\resources.pri")) {
    Write-Warning "resources.pri not found in MSIX stage. Localization may not work."
}
& $makeappx pack /d $msixStage /p $msixPath /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed" }
Write-Host "MSIX created: $msixPath" -ForegroundColor Green

# ============================================================================
# DONE
# ============================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Build complete!" -ForegroundColor Green
Write-Host "EXE output: $outDir\AgentRedactor.exe" -ForegroundColor Green
Write-Host "MSIX output: $msixPath" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
