<#
.SYNOPSIS
    Self-signs the AgentRedactor MSIX/MSIX bundle package.

.DESCRIPTION
    Creates (or reuses) a self-signed code-signing certificate whose subject
    matches the Publisher in windows\Package.appxmanifest, then signs
    build\AgentRedactor.msixbundle with signtool.exe from the Windows SDK
    (signtool signs .msix and .msixbundle identically).

    Because the certificate is self-signed, it must also be installed into the
    machine's "Trusted People" store before the signed MSIX can be installed.
    Use -TrustCert to do this automatically (requires elevation).

.EXAMPLE
    .\sign-agentredactor-msix.ps1 -TrustCert

.EXAMPLE
    .\sign-agentredactor-msix.ps1 -MsixPath "..\windows\build\AgentRedactor-x64.msix"
#>
[CmdletBinding()]
param(
    # Path to the package to sign. Defaults to the bundle produced by
    # windows\build.ps1 (per arch) + buildbundle.ps1.
    [string]$MsixPath = (Join-Path $PSScriptRoot '..\windows\build\AgentRedactor.msixbundle'),

    # Certificate subject. Defaults to the Publisher from Package.appxmanifest.
    [string]$CertSubject,

    # Install the certificate into LocalMachine\TrustedPeople (and Root) so the
    # signed MSIX can be installed. Requires an elevated shell.
    [switch]$TrustCert
)

$ErrorActionPreference = 'Stop'

# --- Resolve MSIX -----------------------------------------------------------
$MsixPath = (Resolve-Path $MsixPath).Path
Write-Host "MSIX: $MsixPath"

# --- Determine certificate subject (must match the package Publisher) -------
if (-not $CertSubject) {
    $manifestPath = Join-Path $PSScriptRoot '..\windows\Package.appxmanifest'
    [xml]$manifest = Get-Content $manifestPath
    $CertSubject = $manifest.Package.Identity.Publisher
    if (-not $CertSubject) {
        throw "Could not read Publisher from $manifestPath"
    }
}
Write-Host "Certificate subject: $CertSubject"

# --- Find or create the self-signed certificate -----------------------------
$cert = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $CertSubject -and $_.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3' } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    Write-Host "Creating new self-signed code-signing certificate..."
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $CertSubject `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(3) `
        -CertStoreLocation 'Cert:\CurrentUser\My'
} else {
    Write-Host "Reusing existing certificate (thumbprint $($cert.Thumbprint))."
}

# --- Locate signtool.exe ----------------------------------------------------
$sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$signtool = Get-ChildItem $sdkBin -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

if (-not $signtool) {
    $signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
}
if (-not $signtool) {
    throw "signtool.exe not found. Install the Windows 10/11 SDK."
}
Write-Host "Using signtool: $signtool"

# --- Sign -------------------------------------------------------------------
& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint $MsixPath
if ($LASTEXITCODE -ne 0) {
    throw "signtool failed with exit code $LASTEXITCODE"
}
Write-Host "Signed successfully: $MsixPath"

# --- Trust the certificate so the MSIX can be installed ---------------------
if ($TrustCert) {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        throw "-TrustCert requires an elevated (Administrator) PowerShell."
    }
    foreach ($store in 'TrustedPeople', 'Root') {
        $target = "Cert:\LocalMachine\$store"
        if (-not (Get-ChildItem $target | Where-Object Thumbprint -eq $cert.Thumbprint)) {
            $tmp = Join-Path $env:TEMP "AgentRedactor-$($cert.Thumbprint).cer"
            Export-Certificate -Cert $cert -FilePath $tmp -Force | Out-Null
            Import-Certificate -FilePath $tmp -CertStoreLocation $target | Out-Null
            Remove-Item $tmp -Force
            Write-Host "Installed certificate into $target"
        } else {
            Write-Host "Certificate already in $target"
        }
    }
    Write-Host "Done. You can now install the package with: Add-AppxPackage '$MsixPath'"
} else {
    Write-Host ""
    Write-Host "NOTE: the certificate is self-signed. Before installing the MSIX, either:"
    Write-Host "  - re-run this script with -TrustCert from an elevated PowerShell, or"
    Write-Host "  - manually import the certificate into 'LocalMachine\TrustedPeople'."
}
