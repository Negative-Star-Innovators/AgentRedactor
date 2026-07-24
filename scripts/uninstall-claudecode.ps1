<#
.SYNOPSIS
    Uninstalls Claude Code (CLI) and removes residual files/configuration from Windows.
.DESCRIPTION
    This script stops Claude Code processes, removes the npm global package,
    deletes npm global shims, and removes the user-level configuration/state
    directories (including ~/.claude and ~/.claude.json).
.NOTES
    This script will automatically request Administrator privileges if not already elevated.
    Usage: .\scripts\uninstall-claudecode.ps1
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

Write-Host "=== Claude Code Uninstall Script ===" -ForegroundColor Cyan
Write-Host "Running as: $env:USERNAME" -ForegroundColor Cyan
Write-Host ""

# 1. Stop processes
Write-Host "[1/4] Stopping Claude Code processes..." -ForegroundColor Cyan

Remove-ScheduledTaskIfExists -TaskName 'Claude Code'
Remove-ScheduledTaskIfExists -TaskName 'ClaudeCode'

$processes = Get-Process -Name 'claude*' -ErrorAction SilentlyContinue
foreach ($proc in $processes) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped process: $($proc.Name) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

$nodeProcesses = Get-Process -Name 'node' -ErrorAction SilentlyContinue | ForEach-Object {
    $proc = $_
    $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
    if ($proc.Path -match 'claude' -or $cmdLine -match 'claude') {
        $proc
    }
}
foreach ($proc in $nodeProcesses) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop node process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped node process: $($proc.Path) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# 2. Remove the npm package
Write-Host ""
Write-Host "[2/4] Removing Claude Code npm package..." -ForegroundColor Cyan

$npm = Get-Command npm -ErrorAction SilentlyContinue
if ($npm) {
    $npmPackages = @('@anthropic-ai/claude-code', 'claude-code')
    foreach ($pkg in $npmPackages) {
        $listOutput = & npm list -g --depth=0 $pkg 2>&1 | Out-String
        if ($listOutput -match [regex]::Escape($pkg)) {
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

    # Fallback: remove npm global shims directly if npm left them behind
    $npmPrefix = & npm prefix -g 2>$null
    $npmBinCandidates = @()
    if ($npmPrefix) {
        $npmBinCandidates += $npmPrefix
        $npmBinCandidates += (Join-Path $npmPrefix 'node_modules\.bin')
    }
    foreach ($npmBinDir in $npmBinCandidates) {
        if (-not $npmBinDir) { continue }
        $shims = @(
            (Join-Path $npmBinDir 'claude.ps1'),
            (Join-Path $npmBinDir 'claude.cmd'),
            (Join-Path $npmBinDir 'claude')
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
}
else {
    Write-Host "npm not found in PATH; skipping npm uninstall step." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "If Claude Code was installed via the native installer (claude.ai/install.ps1), also remove:" -ForegroundColor Yellow
Write-Host "  $env:USERPROFILE\.local\bin\claude.exe" -ForegroundColor Yellow
Write-Host "  $env:USERPROFILE\.local\share\claude" -ForegroundColor Yellow

# 3. Delete residual directories and config files
Write-Host ""
Write-Host "[3/4] Deleting Claude Code configuration and local state..." -ForegroundColor Cyan

$residualPaths = @(
    "$env:USERPROFILE\.claude",
    "$env:USERPROFILE\.claude.json",
    "$env:USERPROFILE\.local\bin\claude.exe",
    "$env:USERPROFILE\.local\share\claude",
    "$env:LOCALAPPDATA\.claude-code-cache",
    "$env:APPDATA\.claude-code"
)

foreach ($path in $residualPaths) {
    if (Test-Path $path) {
        if ($PSCmdlet.ShouldProcess($path, 'Remove residual path')) {
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

# 4. Clean up temp files
Write-Host ""
Write-Host "[4/4] Cleaning up temporary files..." -ForegroundColor Cyan

$tempPaths = @(
    "$env:TEMP\claude*"
)

foreach ($pattern in $tempPaths) {
    $items = Get-Item -Path $pattern -ErrorAction SilentlyContinue
    foreach ($item in $items) {
        if ($PSCmdlet.ShouldProcess($item.FullName, 'Remove temporary item')) {
            Remove-Item -Recurse -Force $item.FullName -ErrorAction SilentlyContinue
            if (-not (Test-Path $item.FullName)) {
                Write-Host "Removed temp item: $($item.FullName)" -ForegroundColor Green
            }
        }
    }
}

# Final verification
Write-Host ""
Write-Host "=== Verifying uninstall ===" -ForegroundColor Cyan
$stillInPath = Get-Command claude -ErrorAction SilentlyContinue
if ($stillInPath) {
    Write-Warning "claude is still in PATH: $($stillInPath.Source)"
}
else {
    Write-Host "claude no longer found in PATH." -ForegroundColor Green
}

foreach ($path in $residualPaths) {
    if (Test-Path $path) {
        Write-Warning "Residual path still exists: $path"
    }
}

Write-Host ""
Write-Host "=== Claude Code uninstall script finished ===" -ForegroundColor Cyan
Write-Host "You may need to restart your computer to fully clear any lingering background services." -ForegroundColor Yellow

}
finally {
    # Keep the elevated window open so the user can read the output
    if ($SelfElevated) {
        Write-Host ""
        Read-Host "Press Enter to close this window"
    }
}
