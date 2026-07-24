<#
.SYNOPSIS
    Resets OpenClaw to a fresh-test state and runs the non-interactive onboarding
    wizard with the custom-provider settings used by the AgentRedactor proxy tests.
.DESCRIPTION
    This script replaces the manual uninstall/reinstall dance for OpenClaw testing.
    By default it keeps the OpenClaw CLI installed and only resets config + state,
    then re-runs onboarding with the exact choices you normally type interactively:

      - Setup mode:    QuickStart
      - Provider:      Custom Provider
      - Base URL:      http://localhost:8081/
      - API key:       test
      - Compatibility: OpenAI-compatible (/chat/completions)
      - Model ID:      nvidia/nemotron-3-nano-30b-a3b:free
      - Provider ID:   auto-derived (e.g. custom-localhost-8081)

    Everything else (channels, daemon, skills, search, hooks, bootstrap) is skipped.

    If you really want a full reinstall, pass -Reinstall and the official installer
    will be downloaded from https://openclaw.ai/install.ps1 with onboarding disabled.

    The gateway is started automatically as a hidden background process after setup
    unless you pass -NoGatewayStart. Logs are written to ~\.openclaw\logs\gateway.*.log.
.NOTES
    Runs in the current user context. No admin rights are required unless you also
    install the gateway daemon (which this script skips by default).

    Usage:
      .\scripts\setup-openclaw-test-openai-nemotron.ps1
      .\scripts\setup-openclaw-test-openai-nemotron.ps1 -Reinstall
      .\scripts\setup-openclaw-test-openai-nemotron.ps1 -BaseUrl "http://127.0.0.1:8081/" -ApiKey "my-key"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Reinstall,

    [ValidateSet("config", "config+creds+sessions", "full")]
    [string]$ResetScope = "config+creds+sessions",

    [string]$BaseUrl = "http://localhost:8081/",
    [string]$ApiKey = "test",
    [string]$ModelId = "nvidia/nemotron-3-nano-30b-a3b:free",

    [int]$GatewayPort = 18789,
    [string]$GatewayBind = "loopback",

    [switch]$NoGatewayStart,
    [switch]$RunHealthCheck,
    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

function Test-OpenClawInstalled {
    return [bool](Get-Command openclaw -ErrorAction SilentlyContinue)
}

function Stop-OpenClawProcesses {
    param([switch]$WhatIf)

    $targets = @()
    $targets += Get-Process -Name "node" -ErrorAction SilentlyContinue | Where-Object {
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -like "*openclaw*"
    }
    $targets += Get-Process -Name "cmd" -ErrorAction SilentlyContinue | Where-Object {
        $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($_.Id)" -ErrorAction SilentlyContinue).CommandLine
        $cmd -like "*openclaw*"
    }
    $targets = $targets | Sort-Object Id -Unique

    if (-not $targets) { return }

    if ($WhatIf) {
        Write-Host "WHATIF: Would stop $($targets.Count) running openclaw process(es)" -ForegroundColor Yellow
        return
    }

    $targets | Stop-Process -Force
    Start-Sleep -Seconds 2
    Write-Host "Stopped $($targets.Count) running openclaw process(es)" -ForegroundColor Green
}

function Start-OpenClawGateway {
    param([switch]$WhatIf)

    $logDir = Join-Path $env:USERPROFILE ".openclaw\logs"
    $outLog = Join-Path $logDir "gateway.out.log"
    $errLog = Join-Path $logDir "gateway.err.log"

    if ($WhatIf) {
        Write-Host "WHATIF: Would start 'openclaw gateway run' as a hidden background process" -ForegroundColor Yellow
        Write-Host "WHATIF: Logs would go to: $outLog and $errLog" -ForegroundColor Yellow
        return
    }

    if (-not (Test-Path $logDir)) {
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    }

    Write-Host ""
    Write-Host "Starting OpenClaw gateway as a hidden background process ..." -ForegroundColor Cyan
    Write-Host "  stdout: $outLog" -ForegroundColor DarkGray
    Write-Host "  stderr: $errLog" -ForegroundColor DarkGray

    # Use cmd.exe so the openclaw .cmd shim runs without being blocked by
    # PowerShell execution policy and without spawning a visible window.
    # Redirection is done inside cmd so the caller's console is not held open.
    Start-Process -FilePath "cmd.exe" `
        -ArgumentList "/c", "openclaw gateway run > `"$outLog`" 2> `"$errLog`"" `
        -WindowStyle Hidden

    Start-Sleep -Seconds 4

    # Quick status probe
    $status = & openclaw status --deep 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Gateway appears reachable." -ForegroundColor Green
    } else {
        Write-Host "Gateway status probe returned an error (this is normal if it is still starting):" -ForegroundColor Yellow
        Write-Host $status -ForegroundColor DarkGray
    }

    Write-Host ""
    Write-Host "To stop the gateway later, run:" -ForegroundColor DarkGray
    Write-Host "  Get-Process node | Where-Object {`$_.CommandLine -like '*openclaw*'} | Stop-Process -Force" -ForegroundColor DarkGray
}

function Install-OpenClaw {
    param([switch]$WhatIf)

    $installUrl = "https://openclaw.ai/install.ps1"
    Write-Host "Downloading OpenClaw installer from $installUrl ..." -ForegroundColor Cyan

    if ($WhatIf) {
        Write-Host "WHATIF: Would run installer with -NoOnboard" -ForegroundColor Yellow
        return
    }

    # Invoke the official installer but suppress its interactive onboarding.
    # This is the equivalent of: irm https://openclaw.ai/install.ps1 | iex -NoOnboard
    & ([scriptblock]::Create((Invoke-RestMethod -Uri $installUrl -UseBasicParsing))) -NoOnboard

    if (-not (Test-OpenClawInstalled)) {
        throw "OpenClaw installation completed but 'openclaw' is still not on PATH. Restart this terminal and try again."
    }

    Write-Host "OpenClaw installed successfully." -ForegroundColor Green
}

function Backup-OpenClawState {
    param([switch]$WhatIf)

    $openclawDir = Join-Path $env:USERPROFILE ".openclaw"
    if (-not (Test-Path $openclawDir)) { return $null }

    $backupDir = Join-Path $env:USERPROFILE ".openclaw.bak.$(Get-Date -Format yyyyMMddHHmmss)"
    if ($WhatIf) {
        Write-Host "WHATIF: Would backup '$openclawDir' -> '$backupDir'" -ForegroundColor Yellow
        return $backupDir
    }

    Copy-Item -Recurse -Force -Path $openclawDir -Destination $backupDir
    Write-Host "Backed up existing OpenClaw state to:" -ForegroundColor Green
    Write-Host "  $backupDir" -ForegroundColor DarkGray
    return $backupDir
}

function Invoke-OpenClawSetup {
    param([switch]$WhatIf)

    Write-Host ""
    Write-Host "Running OpenClaw non-interactive onboarding ..." -ForegroundColor Cyan

    $argList = @(
        "onboard",
        "--non-interactive",
        "--accept-risk",
        "--flow", "quickstart",
        "--mode", "local",
        "--auth-choice", "custom-api-key",
        "--custom-base-url", $BaseUrl,
        "--custom-api-key", $ApiKey,
        "--custom-model-id", $ModelId,
        "--custom-compatibility", "openai",
        "--gateway-port", $GatewayPort,
        "--gateway-bind", $GatewayBind,
        "--reset",
        "--reset-scope", $ResetScope,
        "--skip-channels",
        "--skip-daemon",
        "--skip-skills",
        "--skip-search",
        "--skip-hooks",
        "--skip-bootstrap"
    )

    if (-not $RunHealthCheck) {
        $argList += "--skip-health"
    }

    if ($WhatIf) {
        Write-Host "WHATIF: Would execute:" -ForegroundColor Yellow
        Write-Host "  openclaw $($argList -join ' ')" -ForegroundColor DarkGray
        return
    }

    & openclaw @argList
    if ($LASTEXITCODE -ne 0) {
        throw "openclaw onboard exited with code $LASTEXITCODE"
    }
}

function Test-OpenClawConfig {
    param([switch]$WhatIf)

    if ($WhatIf) {
        Write-Host "WHATIF: Would run 'openclaw config validate'" -ForegroundColor Yellow
        return
    }

    Write-Host ""
    Write-Host "Validating openclaw.json ..." -ForegroundColor Cyan
    & openclaw config validate
    if ($LASTEXITCODE -ne 0) {
        throw "openclaw config validate exited with code $LASTEXITCODE"
    }
    Write-Host "Config is valid." -ForegroundColor Green
}

# ----------------------------- main -----------------------------

Write-Host ""
Write-Host "=== OpenClaw Test Setup ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Reinstall:       $Reinstall"
Write-Host "Reset scope:     $ResetScope"
Write-Host "Base URL:        $BaseUrl"
Write-Host "API key:         $ApiKey"
Write-Host "Model ID:        $ModelId"
Write-Host "Gateway port:    $GatewayPort"
Write-Host "Gateway bind:    $GatewayBind"
Write-Host "Run health:      $RunHealthCheck"
Write-Host "No gateway start:$NoGatewayStart"
Write-Host ""

# 1. Make sure openclaw is installed
$installed = Test-OpenClawInstalled
if (-not $installed -or $Reinstall) {
    if ($installed -and $Reinstall) {
        Write-Host "Reinstall requested: running the official installer ..." -ForegroundColor Cyan
    } else {
        Write-Host "openclaw not found on PATH: running the official installer ..." -ForegroundColor Cyan
    }
    Install-OpenClaw -WhatIf:$WhatIfPreference
} else {
    Write-Host "openclaw is already installed; skipping install step." -ForegroundColor Green
}

# 2. Optionally backup existing state before we reset it
$backupPath = $null
if (-not $SkipBackup) {
    $backupPath = Backup-OpenClawState -WhatIf:$WhatIfPreference
}

# 3. Stop any running gateway so the reset can move locked state files
Stop-OpenClawProcesses -WhatIf:$WhatIfPreference

# 4. Reset + re-run onboarding with the desired custom provider
Invoke-OpenClawSetup -WhatIf:$WhatIfPreference

# 5. Validate the resulting config
Test-OpenClawConfig -WhatIf:$WhatIfPreference

# 6. Start the gateway as a hidden background process by default
if (-not $NoGatewayStart) {
    Start-OpenClawGateway -WhatIf:$WhatIfPreference
} else {
    Write-Host ""
    Write-Host "Skipping gateway start (-NoGatewayStart)." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== OpenClaw test setup complete ===" -ForegroundColor Green

$configFile = & openclaw config file
$primaryModel = $null
try {
    $ErrorActionPreference = 'SilentlyContinue'
    $primaryModel = & openclaw config get agents.defaults.model.primary 2>$null
    $ErrorActionPreference = 'Stop'
} catch {
    $ErrorActionPreference = 'Stop'
}
Write-Host "Config file: $configFile" -ForegroundColor Cyan
if ($primaryModel) {
    Write-Host "Primary model: $primaryModel" -ForegroundColor Cyan
} else {
    Write-Host "Primary model: <auto-derived custom provider>/$ModelId" -ForegroundColor Cyan
}
if ($backupPath) {
    Write-Host "Backup of previous state: $backupPath" -ForegroundColor DarkGray
}
Write-Host ""
