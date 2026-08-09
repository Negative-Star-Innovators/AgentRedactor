# Agent Redactor - Reset All Settings
# Deletes settings, logs, registry startup entry, and cached MSIX data
# Run as normal user (no admin required for HKCU and AppData)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Agent Redactor - Reset All Settings" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# -----------------------------------------------------------------------------
# 1. Stop any running instance
# -----------------------------------------------------------------------------
$proc = Get-Process -Name "AgentRedactor" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Stopping running AgentRedactor.exe..." -ForegroundColor Yellow
    Stop-Process -Name "AgentRedactor" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Write-Host "Process stopped." -ForegroundColor Green
} else {
    Write-Host "AgentRedactor is not running." -ForegroundColor Gray
}

# -----------------------------------------------------------------------------
# 2. Remove standalone settings & logs (%APPDATA%\AgentRedactor)
# -----------------------------------------------------------------------------
$appDataPath = Join-Path $env:APPDATA "AgentRedactor"
if (Test-Path $appDataPath) {
    Write-Host "Removing standalone data: $appDataPath" -ForegroundColor Yellow
    Remove-Item -Path $appDataPath -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Removed." -ForegroundColor Green
} else {
    Write-Host "No standalone data found at $appDataPath" -ForegroundColor Gray
}

# -----------------------------------------------------------------------------
# 3. Remove MSIX packaged app data (%LOCALAPPDATA%\Packages\AgentRedactor_*)
# -----------------------------------------------------------------------------
$packagesPath = Join-Path $env:LOCALAPPDATA "Packages"
$msixFolders = Get-ChildItem -Path $packagesPath -Filter "AgentRedactor_*" -Directory -ErrorAction SilentlyContinue
if ($msixFolders) {
    foreach ($folder in $msixFolders) {
        Write-Host "Removing MSIX data: $($folder.FullName)" -ForegroundColor Yellow
        Remove-Item -Path $folder.FullName -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Removed." -ForegroundColor Green
    }
} else {
    Write-Host "No MSIX package data found." -ForegroundColor Gray
}

# -----------------------------------------------------------------------------
# 4. Remove registry startup entry
# -----------------------------------------------------------------------------
$regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$regValue = "Agent Redactor"
$existing = Get-ItemProperty -Path $regPath -Name $regValue -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Removing registry startup entry..." -ForegroundColor Yellow
    Remove-ItemProperty -Path $regPath -Name $regValue -Force -ErrorAction SilentlyContinue
    Write-Host "Removed." -ForegroundColor Green
} else {
    Write-Host "No registry startup entry found." -ForegroundColor Gray
}

# -----------------------------------------------------------------------------
# 5. Remove any stray log file in temp / working dirs
# -----------------------------------------------------------------------------
$tempLog = Join-Path $env:TEMP "agent_redactor.log"
if (Test-Path $tempLog) {
    Remove-Item -Path $tempLog -Force -ErrorAction SilentlyContinue
    Write-Host "Removed temp log file." -ForegroundColor Green
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Reset complete. All settings cleared." -ForegroundColor Green
Write-Host "Start Agent Redactor to begin fresh." -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
