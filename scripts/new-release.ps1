<#
.SYNOPSIS
    Prepare and publish a Velopack self-release of AgentRedactor.

.DESCRIPTION
    Bumps AgentRedactor/version.txt to the given version, commits it, creates
    tag v<Version>, and pushes the commit + tag to origin. Pushing the tag
    triggers the release-selfrelease.yml workflow, which builds x64+arm64,
    runs the full test suite (incl. GUI tests) and publishes both Velopack
    channels to R2 (served via the Cloudflare worker).

    Use -DryRun to see exactly what would happen without changing anything
    (no file edits, no git mutations).

.EXAMPLE
    .\scripts\new-release.ps1 -Version 1.1.0

.EXAMPLE
    .\scripts\new-release.ps1 -Version 1.1.0 -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step([string]$Message) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Fail([string]$Message) { Write-Host "`nERROR: $Message" -ForegroundColor Red; exit 1 }

$tag = "v$Version"

# Resolve repo paths relative to this script (scripts/ lives at repo root).
$repoRoot = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $repoRoot 'AgentRedactor/version.txt'

Push-Location $repoRoot
try {
    Write-Step "Checking repository state"

    # Must be on the default branch (the branch origin/HEAD points at).
    $defaultBranch = (git symbolic-ref --short refs/remotes/origin/HEAD 2>$null) -replace '^origin/', ''
    if (-not $defaultBranch) { $defaultBranch = 'main' }
    $currentBranch = git rev-parse --abbrev-ref HEAD
    if ($currentBranch -ne $defaultBranch) {
        Fail "You are on branch '$currentBranch', not the default branch '$defaultBranch'. Switch to '$defaultBranch' first."
    }
    Write-Host "On default branch: $defaultBranch"

    # Working tree must have no tracked modifications so the release commit
    # contains only the bump. Untracked working files (-uno) are fine — they
    # can never enter a commit or a push.
    $status = git status --porcelain -uno
    if ($status) {
        git status --short -uno | Write-Host
        Fail "Working tree has uncommitted changes. Commit or stash them first."
    }
    Write-Host "Working tree is clean (ignoring untracked files)."

    # Local branch should not be behind origin, or the push will be rejected.
    git fetch origin $defaultBranch --quiet
    $behind = git rev-list --count "HEAD..origin/$defaultBranch"
    if ($behind -ne '0') {
        Fail "Your local '$defaultBranch' is $behind commit(s) behind origin. Pull first."
    }

    Write-Step "Checking tag $tag does not already exist"

    $localTag = git tag --list $tag
    if ($localTag) { Fail "Tag '$tag' already exists locally. Pick a different version." }

    $remoteTags = git ls-remote --tags origin $tag
    if ($remoteTags) { Fail "Tag '$tag' already exists on origin. Pick a different version." }
    Write-Host "Tag $tag is free locally and on origin."

    Write-Step "Updating AgentRedactor/version.txt"

    $oldVersion = if (Test-Path $versionFile) { (Get-Content -Raw $versionFile).Trim() } else { '(file missing)' }
    Write-Host "Version: $oldVersion -> $Version"
    if (-not $DryRun) {
        Set-Content -Path $versionFile -Value $Version -NoNewline -Encoding utf8
    }

    # Best-effort: snapshot the default settings.json the app writes for a
    # fresh config dir as a per-version migration-test fixture
    # (tests/migration/fixtures/settings/v<Version>.json). Never fails the
    # release — warns and continues when the exe or the hook is unavailable.
    $fixturePath = $null
    $fixturePreExisted = $false
    $fixtureOriginal = $null
    if (-not $DryRun) {
        Write-Step "Snapshotting default settings fixture (best effort)"
        $selftestExe = Join-Path $repoRoot 'AgentRedactor/build/x64/Release/AgentRedactor.exe'
        $fixtureRelPath = "tests/migration/fixtures/settings/v$Version.json"
        if (Test-Path $selftestExe) {
            $tempConfigDir = Join-Path ([System.IO.Path]::GetTempPath()) ("agentredactor-fixture-" + [guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Force -Path $tempConfigDir | Out-Null
            $candidate = Join-Path $repoRoot $fixtureRelPath
            $fixturePreExisted = Test-Path $candidate
            if ($fixturePreExisted) { $fixtureOriginal = [System.IO.File]::ReadAllBytes($candidate) }
            try {
                $env:AGENTREDACTOR_CONFIG_DIR = $tempConfigDir
                $proc = Start-Process -FilePath $selftestExe -ArgumentList '--selftest-migrate-settings' -PassThru -WindowStyle Hidden
                if (-not $proc.WaitForExit(30000)) {
                    $proc.Kill()
                    Write-Host "WARNING: --selftest-migrate-settings timed out; add $fixtureRelPath manually (see tests/migration/README.md)." -ForegroundColor Yellow
                } elseif ($proc.ExitCode -ne 0) {
                    Write-Host "WARNING: --selftest-migrate-settings exited with code $($proc.ExitCode); add $fixtureRelPath manually (see tests/migration/README.md)." -ForegroundColor Yellow
                } else {
                    $generated = Join-Path $tempConfigDir 'settings.json'
                    if (Test-Path $generated) {
                        New-Item -ItemType Directory -Force -Path (Split-Path $candidate) | Out-Null
                        Copy-Item $generated $candidate -Force
                        $fixturePath = $candidate
                        Write-Host "Fixture snapshot written: $fixtureRelPath"
                    } else {
                        Write-Host "WARNING: selftest produced no settings.json; add $fixtureRelPath manually (see tests/migration/README.md)." -ForegroundColor Yellow
                    }
                }
            } finally {
                Remove-Item Env:AGENTREDACTOR_CONFIG_DIR -ErrorAction SilentlyContinue
                Remove-Item -Recurse -Force $tempConfigDir -ErrorAction SilentlyContinue
            }
        } else {
            Write-Host "WARNING: $selftestExe not found; add $fixtureRelPath manually (see tests/migration/README.md)." -ForegroundColor Yellow
        }
    }

    Write-Step "Summary"
    Write-Host "  Version : $Version"
    Write-Host "  Tag     : $tag"
    Write-Host "  Branch  : $defaultBranch"
    Write-Host "  File    : AgentRedactor/version.txt ($oldVersion -> $Version)"
    if ($fixturePath) {
        Write-Host "  Fixture : tests/migration/fixtures/settings/v$Version.json (refreshed)"
    }
    Write-Host ""
    Write-Host "This will run:"
    Write-Host "  git add AgentRedactor/version.txt"
    if ($fixturePath) {
        Write-Host "  git add tests/migration/fixtures/settings/v$Version.json"
    }
    Write-Host "  git commit -m `"chore: bump self-release version to $Version`""
    Write-Host "  git tag $tag"
    Write-Host "  git push origin HEAD --tags"
    Write-Host ""
    Write-Host "Pushing tag $tag triggers the 'Self-Release (Velopack)' GitHub Actions"
    Write-Host "workflow, which builds x64+arm64, runs the full test suite (incl. GUI"
    Write-Host "tests), and publishes both channels to R2. Installed instances will"
    Write-Host "then auto-update to $Version."

    if ($DryRun) {
        Write-Host "`nDry run: no changes were made. Re-run without -DryRun to release." -ForegroundColor Yellow
        exit 0
    }

    $answer = Read-Host "`nType 'yes' to create and push tag $tag"
    if ($answer -ne 'yes') {
        # version.txt was already updated above; restore it so an aborted run
        # leaves the tree exactly as it found it.
        git checkout -- AgentRedactor/version.txt 2>$null
        if ($fixturePath) {
            if ($fixturePreExisted) {
                [System.IO.File]::WriteAllBytes($fixturePath, $fixtureOriginal)
            } else {
                Remove-Item $fixturePath -Force -ErrorAction SilentlyContinue
            }
        }
        Write-Host "Aborted. No commit, tag or push was created." -ForegroundColor Yellow
        exit 1
    }

    Write-Step "Committing and pushing"
    git add AgentRedactor/version.txt
    if ($fixturePath) { git add $fixturePath }
    git commit -m "chore: bump self-release version to $Version"
    git tag $tag
    git push origin HEAD --tags

    Write-Host "`nDone! Tag $tag pushed." -ForegroundColor Green
    Write-Host "Watch the release at: https://github.com/Negative-Star-Innovators/AgentRedactor/actions/workflows/release-selfrelease.yml"
}
finally {
    Pop-Location
}
