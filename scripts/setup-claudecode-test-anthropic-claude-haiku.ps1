<#
.SYNOPSIS
    Resets Claude Code to a fresh-test state and writes the Anthropic-compatible
    configuration used by the AgentRedactor proxy tests.
.DESCRIPTION
    This script replaces the manual config editing for Claude Code testing.
    By default it keeps the Claude Code CLI installed and only resets config,
    then writes the exact model settings:

      - Base URL:  http://localhost:8081/
      - Auth:      ANTHROPIC_AUTH_TOKEN = dummy
      - Model:     anthropic/claude-haiku-4.5

    The existing ~/.claude directory (if any) is backed up to a timestamped
    directory under $env:USERPROFILE\.claude.bak.*.

    If you really want a full reinstall, pass -Reinstall and the npm package
    will be reinstalled.
.NOTES
    Runs in the current user context. No admin rights are required.

    Usage:
      .\scripts\setup-claudecode-test-anthropic-claude-haiku.ps1
      .\scripts\setup-claudecode-test-anthropic-claude-haiku.ps1 -Reinstall
      .\scripts\setup-claudecode-test-anthropic-claude-haiku.ps1 -BaseUrl "http://127.0.0.1:8081/"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Reinstall,

    [string]$BaseUrl = "http://localhost:8081/",
    [string]$ApiKey = "dummy",
    [string]$Model = "anthropic/claude-haiku-4.5",
    [string]$Version = "2.1.81",

    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

# Prevent the CLI from auto-updating to a newer native build while this script
# is running. Newer Windows native builds have a headless stdout regression.
$env:DISABLE_AUTOUPDATER = '1'

function Sync-EnvPath {
    # Re-read User+Machine PATH from the registry so a just-installed claude is
    # discoverable in the current shell without requiring a manual restart.
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")
}

function Test-ClaudeInstalled {
    Sync-EnvPath
    return [bool](Get-Command claude -ErrorAction SilentlyContinue)
}

function Stop-ClaudeProcesses {
    param([switch]$WhatIf)

    $targets = @()

    # Don't kill the process that launched this script (e.g. pytest).
    $currentPid = $PID
    $excludedPids = @($currentPid)
    try {
        $parentPid = (Get-CimInstance Win32_Process -Filter "ProcessId = $currentPid" -ErrorAction SilentlyContinue).ParentProcessId
        if ($parentPid) { $excludedPids += $parentPid }
    } catch {}

    # Processes whose image name contains claude (this will also match the
    # Claude Desktop app; the test setup intentionally stops CLI instances).
    $targets += Get-Process -Name 'claude*' -ErrorAction SilentlyContinue | Where-Object {
        $excludedPids -notcontains $_.Id
    }

    # node processes running claude (e.g. the npm shim wrapper)
    $targets += Get-Process -Name 'node' -ErrorAction SilentlyContinue | Where-Object {
        if ($excludedPids -contains $_.Id) { return $false }
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -match 'claude'
    }

    $targets = $targets | Sort-Object Id -Unique

    if (-not $targets) { return }

    if ($WhatIf) {
        Write-Host "WHATIF: Would stop $($targets.Count) running Claude process(es)" -ForegroundColor Yellow
        return
    }

    $targets | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Host "Stopped $($targets.Count) running Claude process(es)" -ForegroundColor Green
}

function Install-Claude {
    param([switch]$WhatIf)

    $npm = Get-Command npm -ErrorAction SilentlyContinue
    if (-not $npm) {
        throw "npm was not found in PATH. Claude Code is installed via npm; please install Node.js/npm first."
    }

    Write-Host "Installing Claude Code via npm (version $Version) ..." -ForegroundColor Cyan

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'npm install -g @anthropic-ai/claude-code@$Version'" -ForegroundColor Yellow
        return
    }

    & npm install -g @anthropic-ai/claude-code@$Version
    if ($LASTEXITCODE -ne 0) {
        throw "npm install -g @anthropic-ai/claude-code@$Version exited with code $LASTEXITCODE"
    }

    Sync-EnvPath

    if (-not (Test-ClaudeInstalled)) {
        throw "Claude Code installation completed but 'claude' is still not on PATH. Restart this terminal and try again."
    }

    Write-Host "Claude Code installed successfully." -ForegroundColor Green
}

function Get-ClaudeConfigDir {
    return Join-Path $env:USERPROFILE ".claude"
}

function Get-ClaudeSettingsPath {
    return Join-Path (Get-ClaudeConfigDir) "settings.json"
}

function Backup-ClaudeState {
    param([switch]$WhatIf)

    $configDir = Get-ClaudeConfigDir
    if (-not (Test-Path $configDir)) { return $null }

    $backupDir = Join-Path $env:USERPROFILE ".claude.bak.$(Get-Date -Format yyyyMMddHHmmss)"
    if ($WhatIf) {
        Write-Host "WHATIF: Would backup '$configDir' -> '$backupDir'" -ForegroundColor Yellow
        return $backupDir
    }

    Copy-Item -Recurse -Force -Path $configDir -Destination $backupDir
    Write-Host "Backed up existing Claude state to:" -ForegroundColor Green
    Write-Host "  $backupDir" -ForegroundColor DarkGray
    return $backupDir
}

function Merge-ClaudeSettingsJson {
    param(
        [string]$SettingsPath,
        [hashtable]$EnvBlock
    )

    $settings = [ordered]@{}
    if (Test-Path $SettingsPath) {
        try {
            $existing = Get-Content -Raw -Path $SettingsPath -ErrorAction Stop | ConvertFrom-Json
            foreach ($prop in $existing.PSObject.Properties) {
                $settings[$prop.Name] = $prop.Value
            }
        }
        catch {
            Write-Warning "Could not parse existing settings.json; it will be overwritten."
        }
    }

    $settings['env'] = $EnvBlock
    $json = $settings | ConvertTo-Json -Depth 10
    return $json
}

function Invoke-ClaudeConfigSetup {
    param([switch]$WhatIf)

    $configDir = Get-ClaudeConfigDir
    $settingsPath = Get-ClaudeSettingsPath

    if (-not (Test-Path $configDir)) {
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    }

    $envBlock = [ordered]@{
        ANTHROPIC_BASE_URL            = $BaseUrl
        ANTHROPIC_AUTH_TOKEN          = $ApiKey
        ANTHROPIC_MODEL               = $Model
        # Pin to the version installed by this script; newer native builds have a
        # Windows headless stdout regression that breaks ``claude -p`` output capture.
        DISABLE_AUTOUPDATER           = "1"
        # Claude Code defaults to 32K output tokens, which exceeds the test
        # OpenRouter key's per-request credit budget for Claude Haiku. Cap the
        # requested output tokens so the request stays within the available limit.
        CLAUDE_CODE_MAX_OUTPUT_TOKENS = "4096"
    }

    $config = Merge-ClaudeSettingsJson -SettingsPath $settingsPath -EnvBlock $envBlock

    if ($WhatIf) {
        Write-Host "WHATIF: Would write config to $settingsPath" -ForegroundColor Yellow
        Write-Host $config -ForegroundColor DarkGray
        return
    }

    # Write BOM-less UTF-8 so the JSON parser is not confused by a leading BOM.
    [System.IO.File]::WriteAllText($settingsPath, $config, [System.Text.Encoding]::UTF8)
    Write-Host "Wrote Claude Code config to:" -ForegroundColor Green
    Write-Host "  $settingsPath" -ForegroundColor DarkGray
}

function Test-ClaudeConfig {
    param([switch]$WhatIf)

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'claude --version'" -ForegroundColor Yellow
        return
    }

    Write-Host ""
    Write-Host "Validating Claude Code installation ..." -ForegroundColor Cyan
    $versionOutput = & claude --version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "claude --version exited with code $LASTEXITCODE`n$versionOutput"
    }
    Write-Host "Claude Code is reachable: $versionOutput" -ForegroundColor Green
}

# ----------------------------- main -----------------------------

Write-Host ""
Write-Host "=== Claude Code Test Setup (Anthropic) ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Reinstall:  $Reinstall"
Write-Host "Base URL:   $BaseUrl"
Write-Host "API key:    $ApiKey"
Write-Host "Model:      $Model"
Write-Host "Version:    $Version"
Write-Host ""

# 1. Make sure Claude Code is installed
$installed = Test-ClaudeInstalled
if (-not $installed -or $Reinstall) {
    if ($installed -and $Reinstall) {
        Write-Host "Reinstall requested: reinstalling via npm ..." -ForegroundColor Cyan
    } else {
        Write-Host "claude not found on PATH: installing via npm ..." -ForegroundColor Cyan
    }
    Install-Claude -WhatIf:$WhatIfPreference
} else {
    Write-Host "claude is already installed; skipping install step." -ForegroundColor Green
}

# 2. Optionally backup existing state before we reset it
$backupPath = $null
if (-not $SkipBackup) {
    $backupPath = Backup-ClaudeState -WhatIf:$WhatIfPreference
}

# 3. Stop any running Claude processes so the reset can move locked state files
Stop-ClaudeProcesses -WhatIf:$WhatIfPreference

# 4. Write the Anthropic-compatible env configuration
Invoke-ClaudeConfigSetup -WhatIf:$WhatIfPreference

# 5. Validate the resulting config
Test-ClaudeConfig -WhatIf:$WhatIfPreference

Write-Host ""
Write-Host "=== Claude Code test setup complete (Anthropic) ===" -ForegroundColor Green

Write-Host "Config file: $(Get-ClaudeSettingsPath)" -ForegroundColor Cyan
Write-Host "Base URL:    $BaseUrl" -ForegroundColor Cyan
Write-Host "Model:       $Model" -ForegroundColor Cyan
Write-Host "Version:     $Version" -ForegroundColor Cyan
if ($backupPath) {
    Write-Host "Backup of previous state: $backupPath" -ForegroundColor DarkGray
}
Write-Host ""
