<#
.SYNOPSIS
    Uninstalls OpenClaw and removes residual files/services from Windows.
.DESCRIPTION
    This script stops related processes, removes scheduled tasks, runs the OpenClaw
    CLI uninstaller if available, removes npm/global package installations, and
    deletes residual configuration/cache directories.
.NOTES
    This script will automatically request Administrator privileges if not already elevated.
    Usage: .\scripts\uninstall-openclaw.ps1
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

Write-Host "=== OpenClaw Uninstall Script ===" -ForegroundColor Cyan
Write-Host "Running as: $env:USERNAME" -ForegroundColor Cyan
Write-Host ""

# 1. Clear Services and Scheduled Tasks
Write-Host "[1/4] Stopping processes and removing scheduled tasks..." -ForegroundColor Cyan

Remove-ScheduledTaskIfExists -TaskName 'OpenClaw Gateway'
Remove-ScheduledTaskIfExists -TaskName 'ClawdBot Gateway'
Remove-ScheduledTaskIfExists -TaskName 'MoltBot Gateway'

$processes = Get-Process -Name 'openclaw*' -ErrorAction SilentlyContinue
foreach ($proc in $processes) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped process: $($proc.Name) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# Also catch node processes that are running OpenClaw scripts but whose node.exe path is generic
$nodeProcesses = Get-Process -Name 'node' -ErrorAction SilentlyContinue | ForEach-Object {
    $proc = $_
    $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
    if ($proc.Path -match 'openclaw|clawdbot|moltbot' -or $cmdLine -match 'openclaw|clawdbot|moltbot') {
        $proc
    }
}
foreach ($proc in $nodeProcesses) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop node process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped node process: $($proc.Path) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# 2. Run the Official CLI Uninstaller
Write-Host ""
Write-Host "[2/4] Running OpenClaw CLI uninstaller (if available)..." -ForegroundColor Cyan
$openclawExe = Get-Command openclaw -ErrorAction SilentlyContinue
if ($openclawExe) {
    if ($PSCmdlet.ShouldProcess('openclaw uninstall --all --yes --non-interactive', 'Run CLI uninstaller')) {
        & openclaw uninstall --all --yes --non-interactive
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "OpenClaw CLI uninstaller exited with code $LASTEXITCODE. Continuing anyway..."
        }
    }
}
else {
    Write-Host "OpenClaw CLI not found in PATH; skipping CLI uninstaller step." -ForegroundColor Yellow
}

# 3. Remove the App and Package Directories
Write-Host ""
Write-Host "[3/4] Removing package installation..." -ForegroundColor Cyan

$npm = Get-Command npm -ErrorAction SilentlyContinue
if ($npm) {
    if ($PSCmdlet.ShouldProcess('npm uninstall -g openclaw', 'Run npm uninstall')) {
        & npm uninstall -g openclaw
        if ($LASTEXITCODE -eq 0) {
            Write-Host "npm uninstall -g openclaw completed." -ForegroundColor Green
        }
        else {
            Write-Warning "npm uninstall returned exit code $LASTEXITCODE."
        }
    }
}
else {
    Write-Host "npm not found in PATH; skipping npm uninstall step." -ForegroundColor Yellow
}

# Fallback: remove npm global shim directly if npm left it behind
$npmPrefix = & npm prefix -g 2>$null
$npmBinCandidates = @()
if ($npmPrefix) {
    $npmBinCandidates += $npmPrefix
    $npmBinCandidates += (Join-Path $npmPrefix 'node_modules\.bin')
}
foreach ($npmBinDir in $npmBinCandidates) {
    if (-not $npmBinDir) { continue }
    $shims = @(
        (Join-Path $npmBinDir 'openclaw.ps1'),
        (Join-Path $npmBinDir 'openclaw.cmd'),
        (Join-Path $npmBinDir 'openclaw')
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

Write-Host ""
Write-Host "If OpenClaw was installed via the Windows .exe installer, please remove it manually:" -ForegroundColor Yellow
Write-Host "  Windows Settings > Apps > Installed Apps > OpenClaw > Uninstall" -ForegroundColor Yellow

# 4. Delete Residual Directories
Write-Host ""
Write-Host "[4/4] Deleting residual directories..." -ForegroundColor Cyan

$residualPaths = @(
    "$env:USERPROFILE\.openclaw",
    "$env:USERPROFILE\.clawdbot",
    "$env:APPDATA\OpenClaw",
    "$env:LOCALAPPDATA\OpenClaw"
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
$stillInPath = Get-Command openclaw -ErrorAction SilentlyContinue
if ($stillInPath) {
    Write-Warning "openclaw is still in PATH: $($stillInPath.Source)"
}
else {
    Write-Host "openclaw no longer found in PATH." -ForegroundColor Green
}

foreach ($path in $residualPaths) {
    if (Test-Path $path) {
        Write-Warning "Residual directory still exists: $path"
    }
}

Write-Host ""
Write-Host "=== OpenClaw uninstall script finished ===" -ForegroundColor Cyan
Write-Host "You may need to restart your computer to fully clear any lingering background services." -ForegroundColor Yellow

}
finally {
    # Keep the elevated window open so the user can read the output
    if ($SelfElevated) {
        Write-Host ""
        Read-Host "Press Enter to close this window"
    }
}
