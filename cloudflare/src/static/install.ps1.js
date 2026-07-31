// The PowerShell bootstrapper served at /install.ps1.
// Users run it via:
//   iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"
// so it must work under Invoke-Expression: no param blocks, no $PSScriptRoot.

export const INSTALL_PS1 = `# AgentRedactor one-line installer (Windows)
# Run with:
#   iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"

$ErrorActionPreference = 'Stop'

$setupUrl  = 'https://api.agentredactor.negativestarinnovators.com/updates/win/AgentRedactor-win-Setup.exe'
$setupPath = Join-Path $env:TEMP 'AgentRedactor-Setup.exe'

try {
    # Invoke-WebRequest follows the 302 redirect to the GitHub release asset.
    Write-Host 'Downloading AgentRedactor setup...'
    Invoke-WebRequest -Uri $setupUrl -OutFile $setupPath -UseBasicParsing

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
