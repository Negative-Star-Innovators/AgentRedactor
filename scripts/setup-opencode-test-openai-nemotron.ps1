<#
.SYNOPSIS
    Resets OpenCode to a fresh-test state and writes the custom-provider
    configuration used by the AgentRedactor proxy tests.
.DESCRIPTION
    This script replaces the manual config editing for OpenCode testing.
    By default it keeps the OpenCode CLI installed and only resets config,
    then writes the exact model settings:

      - Provider npm:  @ai-sdk/openai-compatible
      - Base URL:      http://localhost:8081/
      - Model ID:      nvidia/nemotron-3-nano-30b-a3b:free

    The existing opencode.jsonc (if any) is backed up to a timestamped file
    under $env:USERPROFILE\.config\opencode.bak.*.

    If you really want a full reinstall, pass -Reinstall and the official
    installer will be downloaded from https://opencode.ai/install.ps1.
.NOTES
    Runs in the current user context. No admin rights are required.

    Usage:
      .\scripts\setup-opencode-test-openai-nemotron.ps1
      .\scripts\setup-opencode-test-openai-nemotron.ps1 -Reinstall
      .\scripts\setup-opencode-test-openai-nemotron.ps1 -BaseUrl "http://127.0.0.1:8081/"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Reinstall,

    [string]$BaseUrl = "http://localhost:8081/",
    [string]$ApiKey = "test",
    [string]$ModelId = "nvidia/nemotron-3-nano-30b-a3b:free",
    [string]$ModelName = "",
    [string]$ProviderId = "agentredactor",
    [string]$ProviderName = "Agent Redactor",

    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

function Sync-EnvPath {
    # Re-read User+Machine PATH from the registry so a just-installed opencode is discoverable
    # in the current shell without requiring a manual restart.
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")
}

function Test-OpenCodeInstalled {
    Sync-EnvPath
    return [bool](Get-Command opencode -ErrorAction SilentlyContinue)
}

function Stop-OpenCodeProcesses {
    param([switch]$WhatIf)

    $targets = @()

    # Don't kill the process that launched this script (e.g. pytest).
    $currentPid = $PID
    $excludedPids = @($currentPid)
    try {
        $parentPid = (Get-CimInstance Win32_Process -Filter "ProcessId = $currentPid" -ErrorAction SilentlyContinue).ParentProcessId
        if ($parentPid) { $excludedPids += $parentPid }
    } catch {}

    # Processes whose image name contains opencode
    $targets += Get-Process -Name 'opencode*' -ErrorAction SilentlyContinue | Where-Object {
        $excludedPids -notcontains $_.Id
    }

    # node processes running opencode
    $targets += Get-Process -Name 'node' -ErrorAction SilentlyContinue | Where-Object {
        if ($excludedPids -contains $_.Id) { return $false }
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -match 'opencode'
    }

    $targets = $targets | Sort-Object Id -Unique

    if (-not $targets) { return }

    if ($WhatIf) {
        Write-Host "WHATIF: Would stop $($targets.Count) running OpenCode process(es)" -ForegroundColor Yellow
        return
    }

    $targets | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Host "Stopped $($targets.Count) running OpenCode process(es)" -ForegroundColor Green
}

function Install-OpenCode {
    param([switch]$WhatIf)

    $installUrl = "https://opencode.ai/install.ps1"
    Write-Host "Downloading OpenCode installer from $installUrl ..." -ForegroundColor Cyan

    if ($WhatIf) {
        Write-Host "WHATIF: Would run installer" -ForegroundColor Yellow
        return
    }

    & ([scriptblock]::Create((Invoke-RestMethod -Uri $installUrl -UseBasicParsing)))

    Sync-EnvPath

    if (-not (Test-OpenCodeInstalled)) {
        throw "OpenCode installation completed but 'opencode' is still not on PATH. Restart this terminal and try again."
    }

    Write-Host "OpenCode installed successfully." -ForegroundColor Green
}

function Get-OpenCodeConfigDir {
    return Join-Path $env:USERPROFILE ".config\opencode"
}

function Get-OpenCodeConfigPath {
    return Join-Path (Get-OpenCodeConfigDir) "opencode.jsonc"
}

function Backup-OpenCodeState {
    param([switch]$WhatIf)

    $configDir = Get-OpenCodeConfigDir
    if (-not (Test-Path $configDir)) { return $null }

    $backupDir = Join-Path $env:USERPROFILE ".config.opencode.bak.$(Get-Date -Format yyyyMMddHHmmss)"
    if ($WhatIf) {
        Write-Host "WHATIF: Would backup '$configDir' -> '$backupDir'" -ForegroundColor Yellow
        return $backupDir
    }

    Copy-Item -Recurse -Force -Path $configDir -Destination $backupDir
    Write-Host "Backed up existing OpenCode state to:" -ForegroundColor Green
    Write-Host "  $backupDir" -ForegroundColor DarkGray
    return $backupDir
}

function Install-ProviderPackage {
    param(
        [string]$PackageName,
        [switch]$WhatIf
    )

    $configDir = Get-OpenCodeConfigDir
    if (-not (Test-Path $configDir)) {
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    }

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'npm install $PackageName' in $configDir" -ForegroundColor Yellow
        return
    }

    $packageJson = Join-Path $configDir "package.json"
    if (-not (Test-Path $packageJson)) {
        Set-Content -Path $packageJson -Value '{"dependencies":{}}' -Encoding UTF8
    }

    Push-Location $configDir
    try {
        Write-Host "Installing provider package $PackageName in $configDir ..." -ForegroundColor Cyan
        & npm install $PackageName
        if ($LASTEXITCODE -ne 0) {
            throw "npm install $PackageName exited with code $LASTEXITCODE"
        }
        Write-Host "Provider package installed." -ForegroundColor Green
    } finally {
        Pop-Location
    }
}

function Invoke-OpenCodeConfigSetup {
    param([switch]$WhatIf)

    $configDir = Get-OpenCodeConfigDir
    $configPath = Get-OpenCodeConfigPath

    if (-not (Test-Path $configDir)) {
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    }

    $derivedModelName = $ModelName
    if ([string]::IsNullOrWhiteSpace($derivedModelName)) {
        $derivedModelName = ($ModelId -split '/')[-1] -replace ':free$', ''
    }

    $config = @{
        '$schema' = "https://opencode.ai/config.json"
        provider   = @{
            $ProviderId = @{
                name   = $ProviderName
                npm    = "@ai-sdk/openai-compatible"
                options = @{
                    baseURL = $BaseUrl
                }
                models = @{
                    $ModelId = @{
                        name = $derivedModelName
                    }
                }
            }
        }
    } | ConvertTo-Json -Depth 10

    if ($WhatIf) {
        Write-Host "WHATIF: Would write config to $configPath" -ForegroundColor Yellow
        Write-Host $config -ForegroundColor DarkGray
        return
    }

    # Write BOM-less UTF-8 so the JSON parser is not confused by a leading BOM.
    [System.IO.File]::WriteAllText($configPath, $config, [System.Text.Encoding]::UTF8)
    Write-Host "Wrote OpenCode config to:" -ForegroundColor Green
    Write-Host "  $configPath" -ForegroundColor DarkGray
}

function Test-OpenCodeConfig {
    param([switch]$WhatIf)

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'opencode debug config' and 'opencode models $ProviderId'" -ForegroundColor Yellow
        return
    }

    Write-Host ""
    Write-Host "Validating OpenCode config ..." -ForegroundColor Cyan
    $debugOutput = & opencode debug config 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "opencode debug config exited with code $LASTEXITCODE`n$debugOutput"
    }

    $modelsOutput = & opencode models $ProviderId 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "opencode models $ProviderId exited with code $LASTEXITCODE`n$modelsOutput"
    }
    if (-not ($modelsOutput -match [regex]::Escape($ModelId))) {
        throw "Expected model $ModelId in 'opencode models $ProviderId' output but got:`n$modelsOutput"
    }
    Write-Host "Config is valid and model is listed." -ForegroundColor Green
}

# ----------------------------- main -----------------------------

Write-Host ""
Write-Host "=== OpenCode Test Setup ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Reinstall:     $Reinstall"
Write-Host "Base URL:      $BaseUrl"
Write-Host "API key:       $ApiKey"
Write-Host "Model ID:      $ModelId"
Write-Host "Provider ID:   $ProviderId"
Write-Host "Provider name: $ProviderName"
Write-Host ""

# 1. Make sure OpenCode is installed
$installed = Test-OpenCodeInstalled
if (-not $installed -or $Reinstall) {
    if ($installed -and $Reinstall) {
        Write-Host "Reinstall requested: running the official installer ..." -ForegroundColor Cyan
    } else {
        Write-Host "opencode not found on PATH: running the official installer ..." -ForegroundColor Cyan
    }
    Install-OpenCode -WhatIf:$WhatIfPreference
} else {
    Write-Host "opencode is already installed; skipping install step." -ForegroundColor Green
}

# 2. Optionally backup existing state before we reset it
$backupPath = $null
if (-not $SkipBackup) {
    $backupPath = Backup-OpenCodeState -WhatIf:$WhatIfPreference
}

# 3. Stop any running OpenCode processes so the reset can move locked state files
Stop-OpenCodeProcesses -WhatIf:$WhatIfPreference

# 4. Install the AI SDK provider package
Install-ProviderPackage -PackageName "@ai-sdk/openai-compatible" -WhatIf:$WhatIfPreference

# 5. Write the custom provider configuration
Invoke-OpenCodeConfigSetup -WhatIf:$WhatIfPreference

# 6. Validate the resulting config
Test-OpenCodeConfig -WhatIf:$WhatIfPreference

Write-Host ""
Write-Host "=== OpenCode test setup complete ===" -ForegroundColor Green

Write-Host "Config file: $(Get-OpenCodeConfigPath)" -ForegroundColor Cyan
Write-Host "Provider:    $ProviderId" -ForegroundColor Cyan
Write-Host "Base URL:    $BaseUrl" -ForegroundColor Cyan
Write-Host "Model:       $ProviderId/$ModelId" -ForegroundColor Cyan
if ($backupPath) {
    Write-Host "Backup of previous state: $backupPath" -ForegroundColor DarkGray
}
Write-Host ""
