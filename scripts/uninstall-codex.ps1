<#
.SYNOPSIS
    Uninstalls OpenAI Codex (CLI, Desktop runtime, and local state) from Windows.
.DESCRIPTION
    This script stops Codex processes, logs out to clear cached credentials, removes
    the npm global package, removes the standalone CLI and Desktop runtime directories,
    and deletes the user-level .codex configuration/state folder.
.NOTES
    This script will automatically request Administrator privileges if not already elevated.
    Usage: .\scripts\uninstall-codex.ps1
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

function Remove-CredentialManagerEntries {
    param([string]$TargetName)
    $creds = cmdkey /list 2>$null | Select-String -Pattern $TargetName
    if ($creds) {
        foreach ($line in $creds) {
            $target = ($line -split ':', 2)[1].Trim()
            if ($target) {
                if ($PSCmdlet.ShouldProcess($target, 'Remove credential')) {
                    cmdkey /delete:$target 2>$null | Out-Null
                    Write-Host "Removed credential: $target" -ForegroundColor Green
                }
            }
        }
    }
}

Write-Host "=== OpenAI Codex Uninstall Script ===" -ForegroundColor Cyan
Write-Host "Running as: $env:USERNAME" -ForegroundColor Cyan
Write-Host ""

# 1. Stop processes
Write-Host "[1/4] Stopping Codex processes..." -ForegroundColor Cyan

Remove-ScheduledTaskIfExists -TaskName 'Codex'
Remove-ScheduledTaskIfExists -TaskName 'OpenAI Codex'

$processes = Get-Process -Name 'codex*' -ErrorAction SilentlyContinue
foreach ($proc in $processes) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped process: $($proc.Name) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

$nodeProcesses = Get-Process -Name 'node' -ErrorAction SilentlyContinue | ForEach-Object {
    $proc = $_
    $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)" -ErrorAction SilentlyContinue).CommandLine
    if ($proc.Path -match 'codex' -or $cmdLine -match 'codex') {
        $proc
    }
}
foreach ($proc in $nodeProcesses) {
    if ($PSCmdlet.ShouldProcess($proc.Name, 'Stop node process')) {
        Stop-Process -Id $proc.Id -Force
        Write-Host "Stopped node process: $($proc.Path) (PID: $($proc.Id))" -ForegroundColor Green
    }
}

# 2. Log out / clear credentials
Write-Host ""
Write-Host "[2/4] Clearing Codex credentials..." -ForegroundColor Cyan

$codexExe = Get-Command codex -ErrorAction SilentlyContinue
if ($codexExe) {
    if ($PSCmdlet.ShouldProcess('codex auth logout', 'Run CLI logout')) {
        & codex auth logout 2>$null
        Write-Host "Ran 'codex auth logout'." -ForegroundColor Green
    }
}
else {
    Write-Host "codex not found in PATH; skipping CLI logout." -ForegroundColor Yellow
}

# Also try to remove any OpenAI/Codex entries from Windows Credential Manager
Remove-CredentialManagerEntries -TargetName 'codex'
Remove-CredentialManagerEntries -TargetName 'openai'

# 3. Remove npm package and standalone install directories
Write-Host ""
Write-Host "[3/4] Removing Codex package installations..." -ForegroundColor Cyan

$npm = Get-Command npm -ErrorAction SilentlyContinue
if ($npm) {
    $npmPackages = @('@openai/codex', 'codex')
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
            (Join-Path $npmBinDir 'codex.ps1'),
            (Join-Path $npmBinDir 'codex.cmd'),
            (Join-Path $npmBinDir 'codex')
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

# Remove standalone CLI and Desktop runtime directories
$installPaths = @(
    "$env:LOCALAPPDATA\Programs\OpenAI\Codex",
    "$env:LOCALAPPDATA\OpenAI\Codex",
    "$env:APPDATA\OpenAI\Codex"
)

foreach ($path in $installPaths) {
    if (Test-Path $path) {
        if ($PSCmdlet.ShouldProcess($path, 'Remove installation directory')) {
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

# 4. Delete user config/state
Write-Host ""
Write-Host "[4/4] Deleting Codex configuration and local state..." -ForegroundColor Cyan

$residualPaths = @(
    "$env:USERPROFILE\.codex"
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

Write-Host ""
Write-Host "If Codex was installed via the Windows Store or a standalone .exe installer, please remove it manually:" -ForegroundColor Yellow
Write-Host "  Windows Settings > Apps > Installed Apps > OpenAI Codex > Uninstall" -ForegroundColor Yellow

# Final verification
Write-Host ""
Write-Host "=== Verifying uninstall ===" -ForegroundColor Cyan
$stillInPath = Get-Command codex -ErrorAction SilentlyContinue
if ($stillInPath) {
    Write-Warning "codex is still in PATH: $($stillInPath.Source)"
}
else {
    Write-Host "codex no longer found in PATH." -ForegroundColor Green
}

foreach ($path in ($installPaths + $residualPaths)) {
    if (Test-Path $path) {
        Write-Warning "Residual directory still exists: $path"
    }
}

Write-Host ""
Write-Host "=== OpenAI Codex uninstall script finished ===" -ForegroundColor Cyan
Write-Host "You may need to restart your computer to fully clear any lingering background services." -ForegroundColor Yellow

}
finally {
    # Keep the elevated window open so the user can read the output
    if ($SelfElevated) {
        Write-Host ""
        Read-Host "Press Enter to close this window"
    }
}
