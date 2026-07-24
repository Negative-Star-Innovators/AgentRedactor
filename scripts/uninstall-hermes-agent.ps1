<#
.SYNOPSIS
    Uninstalls the Hermes Agent (NousResearch) and removes residual files from Windows.
.DESCRIPTION
    This script stops related processes, removes the npm wrapper package, removes the
    Python package installed by the wrapper, and deletes residual configuration/cache
    directories.
.NOTES
    This script will automatically request Administrator privileges if not already elevated.
    Usage: .\scripts\uninstall-hermes-agent.ps1
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$SelfElevated
)

# Self-elevate if not running as Administrator
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Requesting Administrator privileges..." -ForegroundColor Yellow
    Write-Host "(Output will appear in the new elevated PowerShell window.)" -ForegroundColor DarkGray
    $scriptPath = $MyInvocation.MyCommand.Path
    $argList = "-ExecutionPolicy Bypass -NoExit -File `"$scriptPath`" -SelfElevated"
    if ($args) {
        $argList += " " + ($args -join " ")
    }
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    exit
}

try {

$ErrorActionPreference = 'Continue'

function Remove-ScheduledTaskIfExists {
    param([string]$TaskName)
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($task) {
        if ($PSCmdlet.ShouldProcess($TaskName, 'Delete scheduled task')) {
            schtasks /Delete /F /TN "$TaskName" 2>$null | Out-Null
            Write-Host "Deleted scheduled task: $TaskName" -ForegroundColor Green
        }
    }
    else {
        Write-Host "Scheduled task not found: $TaskName" -ForegroundColor DarkGray
    }
}

Write-Host "=== Hermes Agent (NousResearch) Uninstall Script ===" -ForegroundColor Cyan
Write-Host "Running as: $env:USERNAME" -ForegroundColor Cyan
Write-Host ""

# 1. Clear Services and Scheduled Tasks
Write-Host "[1/5] Stopping processes and removing scheduled tasks..." -ForegroundColor Cyan

Remove-ScheduledTaskIfExists -TaskName 'Hermes Agent'
Remove-ScheduledTaskIfExists -TaskName 'HermesAgent'
Remove-ScheduledTaskIfExists -TaskName 'Hermes Gateway'

$processes = Get-Process -Name 'hermes*' -ErrorAction SilentlyContinue
foreach ($proc in $processes) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped process: $($proc.Name) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# Also catch node processes that are running Hermes Agent scripts but whose node.exe path is generic
$nodeProcesses = Get-Process -Name 'node' -ErrorAction SilentlyContinue | ForEach-Object {
    $proc = $_
    $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
    if ($proc.Path -match 'hermes' -or $cmdLine -match 'hermes-agent|bin/hermes|\\hermes ') {
        $proc
    }
}
foreach ($proc in $nodeProcesses) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop node process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped node process: $($proc.Path) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# Also stop Python processes that may be running the Hermes Agent backend
$pythonProcesses = Get-Process -Name 'python*' -ErrorAction SilentlyContinue | ForEach-Object {
    $proc = $_
    $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
    if ($cmdLine -match 'hermes') {
        $proc
    }
}
foreach ($proc in $pythonProcesses) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop Python process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped Python process: $($proc.Path) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# 2. Run the Official CLI Uninstaller (if available)
Write-Host ""
Write-Host "[2/5] Running Hermes CLI uninstaller (if available)..." -ForegroundColor Cyan
$hermesExe = Get-Command hermes -ErrorAction SilentlyContinue
if ($hermesExe) {
    $hasUninstall = $false
    try {
        $helpText = & hermes --help 2>&1 | Out-String
        if ($helpText -match 'uninstall') {
            $hasUninstall = $true
        }
    }
    catch {
        $hasUninstall = $false
    }

    if ($hasUninstall) {
        if ($PSCmdlet.ShouldProcess('hermes uninstall --all --yes --non-interactive', 'Run CLI uninstaller')) {
            & hermes uninstall --all --yes --non-interactive
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "Hermes CLI uninstaller exited with code $LASTEXITCODE. Continuing anyway..."
            }
        }
    }
    else {
        Write-Host "Hermes CLI does not expose an uninstall command; skipping CLI uninstaller step." -ForegroundColor Yellow
    }
}
else {
    Write-Host "Hermes CLI not found in PATH; skipping CLI uninstaller step." -ForegroundColor Yellow
}

# 3. Remove the npm wrapper package
Write-Host ""
Write-Host "[3/5] Removing npm wrapper package..." -ForegroundColor Cyan

$npm = Get-Command npm -ErrorAction SilentlyContinue
if ($npm) {
    $npmPackages = @('hermes-agent', 'hermesagent')
    foreach ($pkg in $npmPackages) {
        $listOutput = & npm list -g --depth=0 $pkg 2>&1 | Out-String
        if ($listOutput -match $pkg) {
            if ($PSCmdlet.ShouldProcess("npm uninstall -g $pkg", 'Run npm uninstall')) {
                & npm uninstall -g $pkg
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "npm uninstall -g $pkg completed." -ForegroundColor Green
                }
                else {
                    Write-Warning "npm uninstall -g $pkg returned exit code $LASTEXITCODE."
                }
            }
        }
        else {
            Write-Host "npm package not installed: $pkg" -ForegroundColor DarkGray
        }
    }

    # Fallback: remove npm global shims and package directory directly if npm left them behind
    $npmPrefix = & npm prefix -g 2>$null
    $npmBinCandidates = @()
    $npmModuleCandidates = @()
    if ($npmPrefix) {
        $npmBinCandidates += $npmPrefix
        $npmBinCandidates += (Join-Path $npmPrefix 'node_modules\.bin')
        $npmModuleCandidates += (Join-Path $npmPrefix 'node_modules\hermes-agent')
        $npmModuleCandidates += (Join-Path $npmPrefix 'node_modules\hermesagent')
    }
    foreach ($npmBinDir in $npmBinCandidates) {
        if (-not $npmBinDir) { continue }
        $shims = @(
            (Join-Path $npmBinDir 'hermes.ps1'),
            (Join-Path $npmBinDir 'hermes.cmd'),
            (Join-Path $npmBinDir 'hermes'),
            (Join-Path $npmBinDir 'hermes-agent.ps1'),
            (Join-Path $npmBinDir 'hermes-agent.cmd'),
            (Join-Path $npmBinDir 'hermes-agent')
        )
        foreach ($shim in $shims) {
            if (Test-Path $shim) {
                if ($PSCmdlet.ShouldProcess($shim, 'Remove npm shim')) {
                    Remove-Item -Force $shim -ErrorAction SilentlyContinue
                    if (-not (Test-Path $shim)) {
                        Write-Host "Removed npm shim: $shim" -ForegroundColor Green
                    }
                }
            }
        }
    }

    foreach ($modDir in $npmModuleCandidates) {
        if (-not $modDir) { continue }
        if (Test-Path $modDir) {
            if ($PSCmdlet.ShouldProcess($modDir, 'Remove npm module directory')) {
                Remove-Item -Recurse -Force $modDir -ErrorAction SilentlyContinue
                if (-not (Test-Path $modDir)) {
                    Write-Host "Removed npm module directory: $modDir" -ForegroundColor Green
                }
                else {
                    Write-Warning "Could not remove npm module directory: $modDir"
                }
            }
        }
    }
}
else {
    Write-Host "npm not found in PATH; skipping npm uninstall step." -ForegroundColor Yellow
}

# 4. Remove the Python package installed by the npm wrapper
Write-Host ""
Write-Host "[4/5] Removing Python package installed by the wrapper..." -ForegroundColor Cyan

$pythonCmds = @('python', 'python3', 'py')
$pythonFound = $false
foreach ($py in $pythonCmds) {
    $pyCmd = Get-Command $py -ErrorAction SilentlyContinue
    if (-not $pyCmd) { continue }
    $pythonFound = $true

    # Check whether the hermes-agent Python package is installed
    $pipList = & $py -m pip list 2>$null | Out-String
    if ($pipList -match 'hermes-agent') {
        if ($PSCmdlet.ShouldProcess("$py -m pip uninstall -y hermes-agent", 'Run pip uninstall')) {
            & $py -m pip uninstall -y hermes-agent
            if ($LASTEXITCODE -eq 0) {
                Write-Host "Removed Python package via $py -m pip uninstall." -ForegroundColor Green
            }
            else {
                Write-Warning "$py -m pip uninstall returned exit code $LASTEXITCODE."
            }
        }
    }
    else {
        Write-Host "Python package 'hermes-agent' not found for $py; skipping." -ForegroundColor DarkGray
    }
}

if (-not $pythonFound) {
    Write-Host "No Python interpreter (python/python3/py) found in PATH; skipping pip uninstall step." -ForegroundColor Yellow
}

# Also try uv if present (development/path installs)
$uv = Get-Command uv -ErrorAction SilentlyContinue
if ($uv) {
    $uvTools = & uv tool list 2>$null | Out-String
    if ($uvTools -match 'hermes-agent') {
        if ($PSCmdlet.ShouldProcess('uv tool uninstall hermes-agent', 'Run uv tool uninstall')) {
            & uv tool uninstall hermes-agent
            if ($LASTEXITCODE -eq 0) {
                Write-Host "Removed uv tool 'hermes-agent'." -ForegroundColor Green
            }
            else {
                Write-Warning "uv tool uninstall returned exit code $LASTEXITCODE."
            }
        }
    }
}

# 5. Delete Residual Directories
Write-Host ""
Write-Host "[5/5] Deleting residual directories..." -ForegroundColor Cyan

$residualPaths = @(
    "$env:USERPROFILE\.hermes",
    "$env:USERPROFILE\.hermes-agent",
    "$env:APPDATA\Hermes",
    "$env:APPDATA\HermesAgent",
    "$env:LOCALAPPDATA\Hermes",
    "$env:LOCALAPPDATA\HermesAgent",
    "$env:LOCALAPPDATA\Programs\Hermes",
    "$env:LOCALAPPDATA\Programs\HermesAgent"
)

foreach ($path in $residualPaths) {
    if (Test-Path $path) {
        if ($PSCmdlet.ShouldProcess($path, 'Remove residual directory')) {
            Remove-Item -Recurse -Force $path -ErrorAction SilentlyContinue
            if (Test-Path $path) {
                Write-Warning "Could not remove: $path (it may still be in use)"
            }
            else {
                Write-Host "Removed: $path" -ForegroundColor Green
            }
        }
    }
    else {
        Write-Host "Not found, skipping: $path" -ForegroundColor DarkGray
    }
}

# Final verification
Write-Host ""
Write-Host "=== Verifying uninstall ===" -ForegroundColor Cyan

$stillInPath = Get-Command hermes -ErrorAction SilentlyContinue
if ($stillInPath) {
    Write-Warning "hermes is still in PATH: $($stillInPath.Source)"
}
else {
    Write-Host "hermes no longer found in PATH." -ForegroundColor Green
}

$stillInPath2 = Get-Command hermes-agent -ErrorAction SilentlyContinue
if ($stillInPath2) {
    Write-Warning "hermes-agent is still in PATH: $($stillInPath2.Source)"
}
else {
    Write-Host "hermes-agent no longer found in PATH." -ForegroundColor Green
}

foreach ($path in $residualPaths) {
    if (Test-Path $path) {
        Write-Warning "Residual directory still exists: $path"
    }
}

Write-Host ""
Write-Host "=== Hermes Agent uninstall script finished ===" -ForegroundColor Cyan
Write-Host "You may need to restart your computer to fully clear any lingering background services." -ForegroundColor Yellow

}
finally {
    # Keep the elevated window open so the user can read the output
    if ($SelfElevated) {
        Write-Host ""
        Read-Host "Press Enter to close this window"
    }
}
