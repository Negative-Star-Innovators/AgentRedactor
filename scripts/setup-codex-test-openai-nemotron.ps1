<#
.SYNOPSIS
    Resets OpenAI Codex to a fresh-test state and writes the custom-provider
    configuration used by the AgentRedactor proxy tests.
.DESCRIPTION
    This script replaces the manual config editing for Codex testing.
    By default it keeps the Codex CLI installed and only resets config,
    then writes the exact model settings:

      - Base URL:      http://localhost:8081/v1
      - API key env:   DUMMY_API_KEY
      - Wire API:      responses
      - Model ID:      nvidia/nemotron-3-nano-30b-a3b:free

    The existing ~/.codex directory (if any) is backed up to a timestamped
    directory under $env:USERPROFILE\.codex.bak.*.

    If you really want a full reinstall, pass -Reinstall and the official
    installer will be downloaded from https://chatgpt.com/codex/install.ps1.
.NOTES
    Runs in the current user context. No admin rights are required.

    Usage:
      .\scripts\setup-codex-test-openai-nemotron.ps1
      .\scripts\setup-codex-test-openai-nemotron.ps1 -Reinstall
      .\scripts\setup-codex-test-openai-nemotron.ps1 -BaseUrl "http://127.0.0.1:8081/v1"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Reinstall,

    [string]$BaseUrl = "http://localhost:8081/v1",
    [string]$ApiKeyEnv = "DUMMY_API_KEY",
    [string]$ModelId = "nvidia/nemotron-3-nano-30b-a3b:free",
    [string]$ProviderId = "agent_redactor",
    [string]$ProviderName = "Agent Redactor",
    [string]$WireApi = "responses",

    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

function Sync-EnvPath {
    # Re-read User+Machine PATH from the registry so a just-installed codex is discoverable
    # in the current shell without requiring a manual restart.
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")
}

function Test-CodexInstalled {
    Sync-EnvPath
    return [bool](Get-Command codex -ErrorAction SilentlyContinue)
}

function Stop-CodexProcesses {
    param([switch]$WhatIf)

    $targets = @()

    # Don't kill the process that launched this script (e.g. pytest).
    $currentPid = $PID
    $excludedPids = @($currentPid)
    try {
        $parentPid = (Get-CimInstance Win32_Process -Filter "ProcessId = $currentPid" -ErrorAction SilentlyContinue).ParentProcessId
        if ($parentPid) { $excludedPids += $parentPid }
    } catch {}

    # Processes whose image name contains codex
    $targets += Get-Process -Name 'codex*' -ErrorAction SilentlyContinue | Where-Object {
        $excludedPids -notcontains $_.Id
    }

    # node processes running codex
    $targets += Get-Process -Name 'node' -ErrorAction SilentlyContinue | Where-Object {
        if ($excludedPids -contains $_.Id) { return $false }
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -match 'codex'
    }

    $targets = $targets | Sort-Object Id -Unique

    if (-not $targets) { return }

    if ($WhatIf) {
        Write-Host "WHATIF: Would stop $($targets.Count) running Codex process(es)" -ForegroundColor Yellow
        return
    }

    $targets | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Host "Stopped $($targets.Count) running Codex process(es)" -ForegroundColor Green
}

function Install-Codex {
    param([switch]$WhatIf)

    $installUrl = "https://chatgpt.com/codex/install.ps1"
    Write-Host "Downloading Codex installer from $installUrl ..." -ForegroundColor Cyan

    if ($WhatIf) {
        Write-Host "WHATIF: Would run installer" -ForegroundColor Yellow
        return
    }

    & ([scriptblock]::Create((Invoke-RestMethod -Uri $installUrl -UseBasicParsing)))

    Sync-EnvPath

    if (-not (Test-CodexInstalled)) {
        throw "Codex installation completed but 'codex' is still not on PATH. Restart this terminal and try again."
    }

    Write-Host "Codex installed successfully." -ForegroundColor Green
}

function Get-CodexConfigDir {
    return Join-Path $env:USERPROFILE ".codex"
}

function Get-CodexConfigPath {
    return Join-Path (Get-CodexConfigDir) "config.toml"
}

function Backup-CodexState {
    param([switch]$WhatIf)

    $configDir = Get-CodexConfigDir
    if (-not (Test-Path $configDir)) { return $null }

    $backupDir = Join-Path $env:USERPROFILE ".codex.bak.$(Get-Date -Format yyyyMMddHHmmss)"
    if ($WhatIf) {
        Write-Host "WHATIF: Would backup '$configDir' -> '$backupDir'" -ForegroundColor Yellow
        return $backupDir
    }

    Copy-Item -Recurse -Force -Path $configDir -Destination $backupDir
    Write-Host "Backed up existing Codex state to:" -ForegroundColor Green
    Write-Host "  $backupDir" -ForegroundColor DarkGray
    return $backupDir
}

function Invoke-CodexConfigSetup {
    param([switch]$WhatIf)

    $configDir = Get-CodexConfigDir
    $configPath = Get-CodexConfigPath

    if (-not (Test-Path $configDir)) {
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    }

    $config = @"
model = "$ModelId"
model_provider = "$ProviderId"

[model_providers.$ProviderId]
name = "$ProviderName"
base_url = "$BaseUrl"
env_key = "$ApiKeyEnv"
wire_api = "$WireApi"
"@

    if ($WhatIf) {
        Write-Host "WHATIF: Would write config to $configPath" -ForegroundColor Yellow
        Write-Host $config -ForegroundColor DarkGray
        return
    }

    # Write BOM-less UTF-8 so the TOML parser is not confused by a leading BOM.
    [System.IO.File]::WriteAllText($configPath, $config, [System.Text.Encoding]::UTF8)
    Write-Host "Wrote Codex config to:" -ForegroundColor Green
    Write-Host "  $configPath" -ForegroundColor DarkGray
}

function Test-CodexConfig {
    param([switch]$WhatIf)

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'codex --version'" -ForegroundColor Yellow
        return
    }

    Write-Host ""
    Write-Host "Validating Codex installation ..." -ForegroundColor Cyan
    $versionOutput = & codex --version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "codex --version exited with code $LASTEXITCODE`n$versionOutput"
    }
    Write-Host "Codex is reachable: $versionOutput" -ForegroundColor Green
}

# ----------------------------- main -----------------------------

Write-Host ""
Write-Host "=== Codex Test Setup ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Reinstall:     $Reinstall"
Write-Host "Base URL:      $BaseUrl"
Write-Host "API key env:   $ApiKeyEnv"
Write-Host "Model ID:      $ModelId"
Write-Host "Provider ID:   $ProviderId"
Write-Host "Provider name: $ProviderName"
Write-Host "Wire API:      $WireApi"
Write-Host ""

# 1. Make sure Codex is installed
$installed = Test-CodexInstalled
if (-not $installed -or $Reinstall) {
    if ($installed -and $Reinstall) {
        Write-Host "Reinstall requested: running the official installer ..." -ForegroundColor Cyan
    } else {
        Write-Host "codex not found on PATH: running the official installer ..." -ForegroundColor Cyan
    }
    Install-Codex -WhatIf:$WhatIfPreference
} else {
    Write-Host "codex is already installed; skipping install step." -ForegroundColor Green
}

# 2. Optionally backup existing state before we reset it
$backupPath = $null
if (-not $SkipBackup) {
    $backupPath = Backup-CodexState -WhatIf:$WhatIfPreference
}

# 3. Stop any running Codex processes so the reset can move locked state files
Stop-CodexProcesses -WhatIf:$WhatIfPreference

# 4. Write the custom provider configuration
Invoke-CodexConfigSetup -WhatIf:$WhatIfPreference

# 5. Validate the resulting config
Test-CodexConfig -WhatIf:$WhatIfPreference

Write-Host ""
Write-Host "=== Codex test setup complete ===" -ForegroundColor Green

Write-Host "Config file: $(Get-CodexConfigPath)" -ForegroundColor Cyan
Write-Host "Provider:    $ProviderId" -ForegroundColor Cyan
Write-Host "Base URL:    $BaseUrl" -ForegroundColor Cyan
Write-Host "Model:       $ModelId" -ForegroundColor Cyan
Write-Host "Wire API:    $WireApi" -ForegroundColor Cyan
if ($backupPath) {
    Write-Host "Backup of previous state: $backupPath" -ForegroundColor DarkGray
}
Write-Host ""
Write-Host "NOTE: Make sure the environment variable '$ApiKeyEnv' is set before running codex." -ForegroundColor Yellow
Write-Host "      Example: `$env:$ApiKeyEnv = 'dummy'" -ForegroundColor Yellow
Write-Host ""
