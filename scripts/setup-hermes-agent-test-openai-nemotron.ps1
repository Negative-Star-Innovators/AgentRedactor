<#
.SYNOPSIS
    Resets Hermes Agent to a fresh-test state and applies the custom-provider
    settings used by the AgentRedactor proxy tests.
.DESCRIPTION
    This script replaces the manual uninstall/reinstall dance for Hermes Agent testing.
    By default it keeps the Hermes CLI installed and only resets config + state,
    then re-applies the exact model settings:

      - Provider:      Custom (OpenAI-compatible endpoint)
      - Base URL:      http://localhost:8081/
      - API key:       test
      - API mode:      chat_completions
      - Model ID:      nvidia/nemotron-3-nano-30b-a3b:free

    Everything else (gateway, tools, skills, TTS, messaging) is left untouched.

    If you really want a full reinstall, pass -Reinstall and the official installer
    will be downloaded from https://hermes-agent.nousresearch.com/install.ps1 with
    the setup wizard disabled.
.NOTES
    Runs in the current user context. No admin rights are required.

    Usage:
      .\scripts\setup-hermes-agent-test-openai-nemotron.ps1
      .\scripts\setup-hermes-agent-test-openai-nemotron.ps1 -Reinstall
      .\scripts\setup-hermes-agent-test-openai-nemotron.ps1 -BaseUrl "http://127.0.0.1:8081/" -ApiKey "my-key"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Reinstall,

    [ValidateSet("config", "config+creds+sessions", "full")]
    [string]$ResetScope = "config+creds+sessions",

    [string]$BaseUrl = "http://localhost:8081/",
    [string]$ApiKey = "test",
    [string]$ModelId = "nvidia/nemotron-3-nano-30b-a3b:free",
    [ValidateSet("chat_completions", "anthropic_messages")]
    [string]$ApiMode = "chat_completions",

    [switch]$RunHealthCheck,
    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

function Sync-EnvPath {
    # Re-read User+Machine PATH from the registry so a just-installed hermes is discoverable
    # in the current shell without requiring a manual restart.
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")
}

function Test-HermesInstalled {
    Sync-EnvPath
    return [bool](Get-Command hermes -ErrorAction SilentlyContinue)
}

function Stop-HermesProcesses {
    param([switch]$WhatIf)

    $targets = @()

    # Don't kill the process that launched this script (e.g. pytest).
    $currentPid = $PID
    $excludedPids = @($currentPid)
    try {
        $parentPid = (Get-CimInstance Win32_Process -Filter "ProcessId = $currentPid" -ErrorAction SilentlyContinue).ParentProcessId
        if ($parentPid) { $excludedPids += $parentPid }
    } catch {}

    # Processes whose image name contains hermes
    $targets += Get-Process -Name 'hermes*' -ErrorAction SilentlyContinue | Where-Object {
        $excludedPids -notcontains $_.Id
    }

    # node processes running hermes-agent scripts
    $targets += Get-Process -Name 'node' -ErrorAction SilentlyContinue | Where-Object {
        if ($excludedPids -contains $_.Id) { return $false }
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -match 'hermes-agent|bin/hermes|\\hermes '
    }

    # python processes running hermes
    $targets += Get-Process -Name 'python*' -ErrorAction SilentlyContinue | Where-Object {
        if ($excludedPids -contains $_.Id) { return $false }
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -match 'hermes'
    }

    $targets = $targets | Sort-Object Id -Unique

    if (-not $targets) { return }

    if ($WhatIf) {
        Write-Host "WHATIF: Would stop $($targets.Count) running Hermes process(es)" -ForegroundColor Yellow
        return
    }

    $targets | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Host "Stopped $($targets.Count) running Hermes process(es)" -ForegroundColor Green
}

function Install-Hermes {
    param([switch]$WhatIf)

    $installUrl = "https://hermes-agent.nousresearch.com/install.ps1"
    Write-Host "Downloading Hermes Agent installer from $installUrl ..." -ForegroundColor Cyan

    if ($WhatIf) {
        Write-Host "WHATIF: Would run installer with -SkipSetup -NonInteractive" -ForegroundColor Yellow
        return
    }

    # Invoke the official installer but suppress its interactive setup wizard.
    & ([scriptblock]::Create((Invoke-RestMethod -Uri $installUrl -UseBasicParsing))) -SkipSetup -NonInteractive

    Sync-EnvPath

    if (-not (Test-HermesInstalled)) {
        throw "Hermes Agent installation completed but 'hermes' is still not on PATH. Restart this terminal and try again."
    }

    Write-Host "Hermes Agent installed successfully." -ForegroundColor Green
}

function Get-HermesProfileDir {
    <#
    Returns the directory that contains Hermes config.yaml and .env.
    On a managed Windows install this is usually $env:LOCALAPPDATA\hermes,
    not $env:USERPROFILE\.hermes.
    #>
    Sync-EnvPath
    $hermesCmd = Get-Command hermes -ErrorAction SilentlyContinue
    if ($hermesCmd) {
        try {
            $configFile = & hermes config path 2>$null
            if ($configFile -and (Test-Path $configFile)) {
                return Split-Path -Parent $configFile
            }
        } catch {
            # fall through to defaults
        }
    }

    $default = Join-Path $env:LOCALAPPDATA "hermes"
    if (Test-Path $default) { return $default }

    return Join-Path $env:USERPROFILE ".hermes"
}

function Get-HermesStateItemNames {
    <#
    Returns the relative names of files/directories inside the Hermes profile
    that should be backed up / reset for the chosen $ResetScope.
    We deliberately avoid install directories such as bin/, hermes-agent/,
    node/, venvs/ etc. so the script clears settings without forcing a reinstall.
    #>
    $items = @()

    # Always clear config + credentials
    $items += "config.yaml", ".env", "auth.json", "auth.lock"

    # config.yaml.bak.* files created by Hermes itself
    $items += "config.yaml.bak.*"

    if ($ResetScope -in @("config+creds+sessions", "full")) {
        $items += "sessions", "logs", "state.db", "state.db-shm", "state.db-wal", ".hermes_history"
    }

    if ($ResetScope -eq "full") {
        $items += "memories", "skills", "cron", "pairing", "hooks", "sandboxes"
        $items += "audio_cache", "image_cache", "cache", "SOUL.md"
        $items += "models_dev_cache.json", "ollama_cloud_models_cache.json"
        $items += ".skills_prompt_snapshot.json", ".update_check"
    }

    return $items | Select-Object -Unique
}

function Backup-HermesState {
    param([switch]$WhatIf)

    $profileDir = Get-HermesProfileDir
    $stateItems = Get-HermesStateItemNames

    $existing = $stateItems | ForEach-Object {
        $pattern = $_
        $candidate = Join-Path $profileDir $pattern
        if ($pattern -match '\*\?\[') {
            Get-ChildItem -Path $profileDir -Name $pattern -ErrorAction SilentlyContinue
        } elseif (Test-Path $candidate) {
            $pattern
        }
    } | Where-Object { $_ } | Select-Object -Unique

    if (-not $existing) { return $null }

    $backupDir = Join-Path $env:USERPROFILE ".hermes.bak.$(Get-Date -Format yyyyMMddHHmmss)"
    if ($WhatIf) {
        Write-Host "WHATIF: Would backup Hermes state from '$profileDir' -> '$backupDir'" -ForegroundColor Yellow
        return $backupDir
    }

    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    foreach ($item in $existing) {
        $src = Join-Path $profileDir $item
        $dst = Join-Path $backupDir $item
        if (Test-Path $src -PathType Container) {
            Copy-Item -Recurse -Force -Path $src -Destination $dst
        } else {
            $parent = Split-Path -Parent $dst
            if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            Copy-Item -Force -Path $src -Destination $dst
        }
    }

    Write-Host "Backed up existing Hermes state to:" -ForegroundColor Green
    Write-Host "  $backupDir" -ForegroundColor DarkGray
    return $backupDir
}

function Reset-HermesState {
    param([switch]$WhatIf)

    $profileDir = Get-HermesProfileDir
    $stateItems = Get-HermesStateItemNames

    if ($WhatIf) {
        Write-Host "WHATIF: Would clear the following items under '$profileDir':" -ForegroundColor Yellow
        foreach ($item in $stateItems) {
            $candidate = Join-Path $profileDir $item
            if (Test-Path $candidate) {
                Write-Host "    $item" -ForegroundColor DarkGray
            }
        }
        return
    }

    foreach ($item in $stateItems) {
        $paths = @()
        $candidate = Join-Path $profileDir $item
        if ($item -match '[\*\?\[]') {
            $paths += Get-ChildItem -Path $profileDir -Name $item -ErrorAction SilentlyContinue | ForEach-Object { Join-Path $profileDir $_ }
        } elseif (Test-Path $candidate) {
            $paths += $candidate
        }

        foreach ($p in $paths) {
            Remove-Item -Recurse -Force -Path $p -ErrorAction SilentlyContinue
            if (Test-Path $p) {
                Write-Warning "Could not remove: $p (it may still be in use)"
            } else {
                Write-Host "Cleared Hermes state: $p" -ForegroundColor Green
            }
        }
    }
}

function Invoke-HermesModelSetup {
    param([switch]$WhatIf)

    Write-Host ""
    Write-Host "Applying Hermes custom-provider model configuration ..." -ForegroundColor Cyan

    $settings = @(
        @("model.provider", "custom"),
        @("model.base_url", $BaseUrl),
        @("model.api_key", $ApiKey),
        @("model.default", $ModelId),
        @("model.api_mode", $ApiMode),
        @("auxiliary.title_generation.provider", "main"),
        @("auxiliary.title_generation.model", $ModelId)
    )

    if ($WhatIf) {
        Write-Host "WHATIF: Would execute:" -ForegroundColor Yellow
        foreach ($pair in $settings) {
            Write-Host "  hermes config set $($pair[0]) $($pair[1])" -ForegroundColor DarkGray
        }
        return
    }

    foreach ($pair in $settings) {
        $key = $pair[0]
        $value = $pair[1]
        & hermes config set $key $value
        if ($LASTEXITCODE -ne 0) {
            throw "hermes config set $key exited with code $LASTEXITCODE"
        }
    }
}

function Test-HermesConfig {
    param([switch]$WhatIf)

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'hermes config check'" -ForegroundColor Yellow
        return
    }

    Write-Host ""
    Write-Host "Validating Hermes config ..." -ForegroundColor Cyan
    $checkOutput = & hermes config check 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "hermes config check exited with code $LASTEXITCODE`n$checkOutput"
    }
    Write-Host "Config looks valid." -ForegroundColor Green
}

function Invoke-HermesHealthCheck {
    param([switch]$WhatIf)

    if (-not $RunHealthCheck) { return }

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'hermes doctor'" -ForegroundColor Yellow
        return
    }

    Write-Host ""
    Write-Host "Running Hermes doctor (health check) ..." -ForegroundColor Cyan
    & hermes doctor
    # doctor may return non-zero for unreachable endpoints; warn only
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "hermes doctor exited with code $LASTEXITCODE (this is normal if the proxy endpoint is not yet running)"
    }
}

# ----------------------------- main -----------------------------

Write-Host ""
Write-Host "=== Hermes Agent Test Setup ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Reinstall:    $Reinstall"
Write-Host "Reset scope:  $ResetScope"
Write-Host "Base URL:     $BaseUrl"
Write-Host "API key:      $ApiKey"
Write-Host "Model ID:     $ModelId"
Write-Host "API mode:     $ApiMode"
Write-Host "Health check: $RunHealthCheck"
Write-Host ""

# 1. Make sure Hermes is installed
$installed = Test-HermesInstalled
if (-not $installed -or $Reinstall) {
    if ($installed -and $Reinstall) {
        Write-Host "Reinstall requested: running the official installer ..." -ForegroundColor Cyan
    } else {
        Write-Host "hermes not found on PATH: running the official installer ..." -ForegroundColor Cyan
    }
    Install-Hermes -WhatIf:$WhatIfPreference
} else {
    Write-Host "hermes is already installed; skipping install step." -ForegroundColor Green
}

# 2. Optionally backup existing state before we reset it
$backupPath = $null
if (-not $SkipBackup) {
    $backupPath = Backup-HermesState -WhatIf:$WhatIfPreference
}

# 3. Stop any running Hermes processes so the reset can move locked state files
Stop-HermesProcesses -WhatIf:$WhatIfPreference

# 4. Reset Hermes settings without removing the installed CLI
Reset-HermesState -WhatIf:$WhatIfPreference

# 5. Apply the custom provider model configuration
Invoke-HermesModelSetup -WhatIf:$WhatIfPreference

# 6. Validate the resulting config
Test-HermesConfig -WhatIf:$WhatIfPreference

# 7. Optional health check
Invoke-HermesHealthCheck -WhatIf:$WhatIfPreference

Write-Host ""
Write-Host "=== Hermes Agent test setup complete ===" -ForegroundColor Green

$configFile = & hermes config path
Write-Host "Config file: $configFile" -ForegroundColor Cyan
Write-Host "Provider:    custom" -ForegroundColor Cyan
Write-Host "Base URL:    $BaseUrl" -ForegroundColor Cyan
Write-Host "API mode:    $ApiMode" -ForegroundColor Cyan
Write-Host "Model:       $ModelId" -ForegroundColor Cyan
if ($backupPath) {
    Write-Host "Backup of previous state: $backupPath" -ForegroundColor DarkGray
}
Write-Host ""
