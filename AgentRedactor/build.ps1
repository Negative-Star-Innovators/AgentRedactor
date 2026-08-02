# Agent Redactor Build Script
# Builds WinUI 3 AgentRedactor and packages as MSIX (Microsoft Store)
# or, with -SelfRelease, as a Velopack installer (self-release channel).

param(
    [string]$Version = "1.0.0",
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$SelfRelease
)

$ErrorActionPreference = "Stop"
$Platform = if ($Platform -ieq "ARM64") { "ARM64" } else { "x64" }
$root = $PSScriptRoot
$buildDir = "$root\build"
$outDir = "$buildDir\$Platform\Release"
$archLower = $Platform.ToLower()

# Self-release builds are versioned from version.txt unless -Version is given
if ($SelfRelease -and -not $PSBoundParameters.ContainsKey('Version')) {
    $versionFile = "$root\version.txt"
    if (Test-Path $versionFile) {
        $Version = (Get-Content $versionFile -Raw).Trim()
        Write-Host "Self-release version from version.txt: $Version" -ForegroundColor Cyan
    }
}

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
    # The clean wiped build\tools\nuget.exe (our fallback NuGet location)
    # after it was already resolved above — re-download if it's gone.
    if (-not (Test-Path $nuget)) {
        New-Item -ItemType Directory -Force -Path "$root\build\tools" | Out-Null
        Write-Host "Re-downloading NuGet (clean removed it)..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nuget
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
Write-Host "Building AgentRedactor (Release|$Platform)..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
$msbuildArgs = @(
    "$root\AgentRedactor.vcxproj",
    "-p:Configuration=Release",
    "-p:Platform=$Platform",
    "-p:RestorePackages=false"
)
if ($SelfRelease) {
    # Stamps AGENTREDACTOR_SELFRELEASE + AR_VERSION_STRING (see vcxproj)
    $msbuildArgs += "-p:SelfRelease=true"
    $msbuildArgs += "-p:AppVersion=$Version"
    # 4-part comma form for the VERSIONINFO resource (AR_VERSION_QUAD); the rc
    # preprocessor cannot split strings, so the quad is computed here.
    $versionCore = ($Version -split '[-+]')[0]
    $appVersionQuad = ($versionCore -replace '\.', ',') + ',0'
    # Embedded quotes: without them PowerShell's native-argument passing
    # splits the comma-separated value into separate msbuild arguments
    # (MSB1006 "Property is not valid").
    $msbuildArgs += "-p:AppVersionQuad=`"$appVersionQuad`""
}
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Copy models and resources
if (Test-Path "$root\models") {
    Write-Host "Copying model files..." -ForegroundColor Cyan
    if (-not (Test-Path "$outDir\models")) {
        New-Item -ItemType Directory -Force -Path "$outDir\models" | Out-Null
    }
    if ($SelfRelease) {
        # The 1.6 GB weight file (model_quantized.onnx_data) is NOT shipped in
        # the self-release installer; the app downloads it on first run from
        # the Cloudflare R2 endpoint (see src/model_downloader.cpp). The tiny
        # model graph (.onnx) and the companion files still ship.
        robocopy "$root\models" "$outDir\models" /E /XF *.onnx_data /R:3 /W:1 | Out-Null
        # robocopy /E does not purge: remove weight files left behind by
        # earlier non-SelfRelease (MSIX) builds of the same output folder,
        # which would otherwise bloat the installer to ~1.8 GB.
        Remove-Item "$outDir\models\*.onnx_data", "$outDir\models\onnx\*.onnx_data" -Force -ErrorAction SilentlyContinue
    } else {
        robocopy "$root\models" "$outDir\models" /E /R:3 /W:1 | Out-Null
    }
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
# MSIX (Store channel only)
# ============================================================================
if ($SelfRelease) {
    # ========================================================================
    # VELOPACK PACK (self-release channel)
    # ========================================================================
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Packing Velopack release v$Version..." -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    # vpk resolution: PATH first, then the dotnet global-tools shim dir.
    # `dotnet tool install -g vpk` drops vpk.exe in ~/.dotnet/tools, which is
    # NOT on PATH in shells started before the install — so check the shim
    # path directly both before installing (a previous shell may have
    # installed it) and after, instead of relying on PATH alone.
    $dotnetToolsVpk = "$env:USERPROFILE\.dotnet\tools\vpk.exe"
    $vpkCmd = Get-Command vpk -ErrorAction SilentlyContinue
    $vpk = if ($vpkCmd) { $vpkCmd.Source } elseif (Test-Path $dotnetToolsVpk) { $dotnetToolsVpk } else { $null }
    if (-not $vpk) {
        Write-Host "vpk (Velopack CLI) not found; installing via dotnet tool..." -ForegroundColor Yellow
        & dotnet tool install -g vpk
        if ($LASTEXITCODE -ne 0) { throw "Failed to install vpk (is the .NET SDK installed?)" }
        $vpkCmd = Get-Command vpk -ErrorAction SilentlyContinue
        $vpk = if ($vpkCmd) { $vpkCmd.Source } elseif (Test-Path $dotnetToolsVpk) { $dotnetToolsVpk } else { $null }
        if (-not $vpk) { throw "vpk is still missing after install (expected at $dotnetToolsVpk)" }
    }
    Write-Host "Found vpk: $vpk" -ForegroundColor Green

    # Channel/runtime per arch (vpk pack -r/-c, vpk 1.2.0): x64 keeps the
    # original 'win' channel (feed URLs already deployed), ARM64 gets its own
    # 'win-arm64' channel. ARM64 packs into a SEPARATE output folder —
    # two channels in one folder would mix their releases.<channel>.json feeds.
    $rid = if ($Platform -eq "ARM64") { "win-arm64" } else { "win-x64" }
    $channel = if ($Platform -eq "ARM64") { "win-arm64" } else { "win" }
    $velopackOut = if ($Platform -eq "ARM64") { "$buildDir\velopack-arm64" } else { "$buildDir\velopack" }
    # -s: language-neutral splash shown by Setup.exe during install (fox logo
    # on dark background, no text — Velopack's own Setup UI text is English).
    & $vpk pack -u AgentRedactor -v $Version -p $outDir -e AgentRedactor.exe `
        -r $rid -c $channel `
        --packTitle "Agent Redactor" -i "$root\resources\app.ico" `
        -s "$root\resources\splash.png" -o $velopackOut
    if ($LASTEXITCODE -ne 0) { throw "vpk pack failed" }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Self-release build complete!" -ForegroundColor Green
    Write-Host "EXE output: $outDir\AgentRedactor.exe" -ForegroundColor Green
    Write-Host "Velopack output: $velopackOut" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    return
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Creating MSIX package..." -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$msixStage = "$buildDir\msix_stage_$archLower"
if (Test-Path $msixStage) { Remove-Item -Recurse -Force $msixStage }
New-Item -ItemType Directory -Force -Path $msixStage | Out-Null

# Copy build output
robocopy $outDir $msixStage /E /R:3 /W:1 | Out-Null

# Never ship stale runtime logs inside the package (a packaged debug.log in the
# read-only install folder crashes wWinMain on startup)
Remove-Item "$msixStage\debug.log", "$msixStage\startup_debug.log" -Force -ErrorAction SilentlyContinue

# Copy manifest and assets
# Replace x-generate with actual languages because MakeAppx.exe does not expand it.
# Also stamp the package architecture for this build (source manifest stays x64).
$manifestXml = [xml](Get-Content "$root\Package.appxmanifest")
$ns = $manifestXml.Package.NamespaceURI
$manifestXml.Package.Identity.SetAttribute('ProcessorArchitecture', $archLower)
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

$msixPath = "$buildDir\AgentRedactor-$archLower.msix"
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
