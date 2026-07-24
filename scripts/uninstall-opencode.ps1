<#
.SYNOPSIS
    Uninstalls OpenCode and removes residual files from Windows.
.DESCRIPTION
    This script stops related processes, removes the npm package, deletes npm
    global shims, and removes residual configuration/cache/state directories.
.NOTES
    This script will automatically request Administrator privileges if not already elevated.
    Usage: .\scripts\uninstall-opencode.ps1
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

Write-Host "=== OpenCode Uninstall Script ===" -ForegroundColor Cyan
Write-Host "Running as: $env:USERNAME" -ForegroundColor Cyan
Write-Host ""

# 1. Clear Services and Scheduled Tasks
Write-Host "[1/3] Stopping processes and removing scheduled tasks..." -ForegroundColor Cyan

Remove-ScheduledTaskIfExists -TaskName 'OpenCode'
Remove-ScheduledTaskIfExists -TaskName 'OpenCode Agent'

$processes = Get-Process -Name 'opencode*' -ErrorAction SilentlyContinue
foreach ($proc in $processes) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped process: $($proc.Name) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# Also catch node processes that are running OpenCode scripts but whose node.exe path is generic
$nodeProcesses = Get-Process -Name 'node' -ErrorAction SilentlyContinue | ForEach-Object {
    $proc = $_
    $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
    if ($proc.Path -match 'opencode' -or $cmdLine -match 'opencode') {
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
Write-Host "[2/3] Removing npm package..." -ForegroundColor Cyan

$npm = Get-Command npm -ErrorAction SilentlyContinue
if ($npm) {
    $npmPackages = @('opencode', 'opencode-ai')
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
        $npmModuleCandidates += (Join-Path $npmPrefix 'node_modules\opencode')
        $npmModuleCandidates += (Join-Path $npmPrefix 'node_modules\opencode-ai')
    }
    foreach ($npmBinDir in $npmBinCandidates) {
        if (-not $npmBinDir) { continue }
        $shims = @(
            (Join-Path $npmBinDir 'opencode.ps1'),
            (Join-Path $npmBinDir 'opencode.cmd'),
            (Join-Path $npmBinDir 'opencode')
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

Write-Host ""
Write-Host "If OpenCode was installed via a Windows .exe installer, please remove it manually:" -ForegroundColor Yellow
Write-Host "  Windows Settings > Apps > Installed Apps > OpenCode > Uninstall" -ForegroundColor Yellow

# 3. Delete Residual Directories
Write-Host ""
Write-Host "[3/3] Deleting residual directories..." -ForegroundColor Cyan

$residualPaths = @(
    "$env:USERPROFILE\.config\opencode",
    "$env:USERPROFILE\.local\share\opencode",
    "$env:USERPROFILE\.local\state\opencode",
    "$env:USERPROFILE\.cache\opencode",
    "$env:LOCALAPPDATA\Temp\opencode"
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
$stillInPath = Get-Command opencode -ErrorAction SilentlyContinue
if ($stillInPath) {
    Write-Warning "opencode is still in PATH: $($stillInPath.Source)"
}
else {
    Write-Host "opencode no longer found in PATH." -ForegroundColor Green
}

foreach ($path in $residualPaths) {
    if (Test-Path $path) {
        Write-Warning "Residual directory still exists: $path"
    }
}

Write-Host ""
Write-Host "=== OpenCode uninstall script finished ===" -ForegroundColor Cyan
Write-Host "You may need to restart your computer to fully clear any lingering background services." -ForegroundColor Yellow

}
finally {
    # Keep the elevated window open so the user can read the output
    if ($SelfElevated) {
        Write-Host ""
        Read-Host "Press Enter to close this window"
    }
}
