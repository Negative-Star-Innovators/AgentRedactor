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
    # Invoke-WebRequest follows the 302 redirect to the GitHub release asset.
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

    # Run the installer and wait for it to finish.
    Write-Host 'Running installer...'
    Start-Process -FilePath $setupPath -Wait
}
finally {
    # Clean up the downloaded installer regardless of outcome.
    if (Test-Path $setupPath) { Remove-Item $setupPath -Force }
}

Write-Host 'AgentRedactor installed successfully. You can launch it from the Start menu.'
`;
