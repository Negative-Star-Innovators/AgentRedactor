// The PowerShell bootstrapper served at /install.ps1.
// Users run it via:
//   iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"
// so it must work under Invoke-Expression: no param blocks, no $PSScriptRoot.

export const INSTALL_PS1 = `# AgentRedactor one-line installer (Windows)
# Run with:
#   iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"

$ErrorActionPreference = 'Stop'

# Pick the update channel by CPU architecture: ARM64 devices get the native
# win-arm64 build, everything else the x64 build.
$baseUrl = 'https://api.agentredactor.negativestarinnovators.com/updates'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
    $channel   = 'win-arm64'
    $setupName = 'AgentRedactor-win-arm64-Setup.exe'
} else {
    $channel   = 'win'
    $setupName = 'AgentRedactor-win-Setup.exe'
}
$setupPath = Join-Path $env:TEMP 'AgentRedactor-Setup.exe'

try {
    # The updates endpoint serves the installer directly from R2.
    Write-Host "Downloading AgentRedactor setup ($channel)..."
    try {
        Invoke-WebRequest -Uri "$baseUrl/$channel/$setupName" -OutFile $setupPath -UseBasicParsing
    }
    catch {
        # Extract the HTTP status (PS5.1 WebException vs PS7 HttpResponseException).
        $status = $null
        if ($null -ne $_.Exception.Response -and $null -ne $_.Exception.Response.StatusCode) {
            $status = [int]$_.Exception.Response.StatusCode
        } elseif ($null -ne $_.Exception.StatusCode) {
            $status = [int]$_.Exception.StatusCode
        }
        # If there is no ARM64 build in the latest release yet (404) or the
        # feed could not reach it (502), fall back to the x64 installer —
        # it runs fine under emulation on Windows on ARM.
        if ($channel -eq 'win-arm64' -and ($status -eq 404 -or $status -eq 502)) {
            Write-Warning 'No ARM64 build available yet; installing the x64 build instead (runs under emulation).'
            Invoke-WebRequest -Uri "$baseUrl/win/AgentRedactor-win-Setup.exe" -OutFile $setupPath -UseBasicParsing
        } else {
            throw
        }
    }

    # Strip the Mark-of-the-Web so SmartScreen doesn't block the downloaded
    # (not-yet-reputation) installer on dev machines.
    Unblock-File -Path $setupPath

    # Run the installer. Velopack's Setup.exe stays alive for as long as the
    # installed app runs, so don't -Wait on it: poll until the app is
    # installed and launched (or Setup exits on its own), then hand the
    # terminal back.
    Write-Host 'Running installer...'
    $proc = Start-Process -FilePath $setupPath -PassThru
    $appExe = Join-Path $env:LOCALAPPDATA 'AgentRedactor\current\AgentRedactor.exe'
    $deadline = (Get-Date).AddMinutes(5)
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        if ((Test-Path $appExe) -and (Get-Process -Name 'AgentRedactor' -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Seconds 2
    }
    if ($proc.HasExited -and $proc.ExitCode -ne 0) {
        Write-Warning "The installer exited with code $($proc.ExitCode). Agent Redactor may not be installed."
    } elseif (Test-Path $appExe) {
        Write-Host 'AgentRedactor installed successfully. The app is starting — you can launch it anytime from the Start menu.'
    } else {
        Write-Warning 'Timed out waiting for the installer. Check the Start menu for Agent Redactor.'
    }
}
finally {
    # Clean up the downloaded installer. Setup.exe may still be running (it
    # stays alive with the app), so a locked file is fine — it is in %TEMP%.
    if (Test-Path $setupPath) { Remove-Item $setupPath -Force -ErrorAction SilentlyContinue }
}
`;
