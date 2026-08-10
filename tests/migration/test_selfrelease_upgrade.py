"""Opt-in end-to-end test for the Velopack self-release upgrade path.

Installs the *previous* published release silently, points the installed app
at a local HTTP feed built from the vNext Velopack output, and waits for the
auto-apply update to swap in the new version. Afterwards it verifies that the
user's settings survived the upgrade and still migrate cleanly.

Skipped unless AGENTREDACTOR_UPGRADE_TEST=1. Required env vars:
  AGENTREDACTOR_PREV_SETUP   path to the previous release's Setup.exe for the
                             channel under test (e.g. AgentRedactor-win-Setup.exe
                             for x64, AgentRedactor-win-arm64-Setup.exe for ARM64)
  AGENTREDACTOR_FEED_DIR     folder with the vNext vpk output
                             (releases.<channel>.json + *-full.nupkg)
Optional:
  AGENTREDACTOR_CHANNEL      update channel: 'win' (x64, default) or 'win-arm64'.
                             Selects the releases.<channel>.json feed file.
  AGENTREDACTOR_EXPECT_VERSION  expected new version (default: newest version
                             in the feed's releases.<channel>.json)

See tests/migration/README.md for the full contract and CI wiring.
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import psutil
import pytest

pytestmark = [
    pytest.mark.upgrade,
    pytest.mark.skipif(
        sys.platform != "win32", reason="Velopack self-release is Windows-only"
    ),
    pytest.mark.skipif(
        os.environ.get("AGENTREDACTOR_UPGRADE_TEST") != "1",
        reason="opt-in E2E: set AGENTREDACTOR_UPGRADE_TEST=1 "
        "(plus AGENTREDACTOR_PREV_SETUP / AGENTREDACTOR_FEED_DIR)",
    ),
]

# Velopack pack id is "AgentRedactor" (build.ps1: vpk pack -u AgentRedactor),
# so the per-user install root is %LOCALAPPDATA%\AgentRedactor with Update.exe
# at the root and the app under current\.
INSTALL_DIR_NAME = "AgentRedactor"

POLL_TIMEOUT_S = 300  # ~5 min: feed fetch + nupkg download + Update.exe apply
POLL_INTERVAL_S = 5
SETUP_TIMEOUT_S = 300


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _version_key(text: str) -> tuple[int, int, int]:
    """Normalize 'v1.2.3[.0][-suffix]' to a comparable (1, 2, 3) tuple."""
    core = text.strip().lstrip("vV").split("-")[0].split("+")[0]
    parts = [int(p) for p in core.split(".") if p != ""]
    return tuple((parts + [0, 0, 0])[:3])


def _file_version(exe: Path) -> str:
    out = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            f"$v=(Get-Item '{exe}').VersionInfo; "
            "if ($v.FileVersion) { $v.FileVersion } else { $v.ProductVersion }",
        ],
        capture_output=True,
        text=True,
        timeout=30,
    )
    return out.stdout.strip()


def _latest_feed_version(feed_dir: Path, channel: str) -> str:
    feed = json.loads((feed_dir / f"releases.{channel}.json").read_text(encoding="utf-8"))
    versions = [
        asset["Version"]
        for asset in feed.get("Assets", [])
        if asset.get("Type") == 1 and asset.get("Version")  # Type 1 == Full nupkg
    ]
    if not versions:
        pytest.fail(f"no full-package assets in {feed_dir / f'releases.{channel}.json'}")
    return max(versions, key=_version_key)


def _kill_install_processes(install_root: Path) -> None:
    """Kill AgentRedactorUI.exe / agentredactor.exe / Update.exe instances running from the install."""
    root = str(install_root).lower()
    for proc in psutil.process_iter(["exe"]):
        try:
            exe = proc.info.get("exe")
            if exe and str(exe).lower().startswith(root):
                proc.kill()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    time.sleep(1)


def _layout_listing(install_root: Path) -> str:
    if not install_root.exists():
        return f"{install_root} does not exist"
    entries = [str(p.relative_to(install_root)) for p in install_root.rglob("*")]
    return "\n".join(sorted(entries)[:200])


def test_selfrelease_upgrade(tmp_path):
    prev_setup = os.environ.get("AGENTREDACTOR_PREV_SETUP")
    if not prev_setup or not Path(prev_setup).is_file():
        pytest.skip("AGENTREDACTOR_PREV_SETUP is not set or does not point at a Setup.exe")
    channel = os.environ.get("AGENTREDACTOR_CHANNEL", "win")
    feed_file = f"releases.{channel}.json"
    feed_dir = os.environ.get("AGENTREDACTOR_FEED_DIR")
    if not feed_dir or not (Path(feed_dir) / feed_file).is_file():
        pytest.skip(
            f"AGENTREDACTOR_FEED_DIR is not set or has no {feed_file} "
            "(point it at the vpk output folder, e.g. windows/build/velopack)"
        )
    feed_dir_path = Path(feed_dir)
    expected = os.environ.get("AGENTREDACTOR_EXPECT_VERSION") or _latest_feed_version(
        feed_dir_path, channel
    )

    install_root = Path(os.environ["LOCALAPPDATA"]) / INSTALL_DIR_NAME
    current_exe = install_root / "current" / "AgentRedactorUI.exe"
    update_exe = install_root / "Update.exe"
    # Previous live releases predate the AgentRedactor.exe -> AgentRedactorUI.exe
    # rename, so the freshly installed previous build may carry the old name.
    legacy_exe = install_root / "current" / "AgentRedactor.exe"

    server: subprocess.Popen | None = None
    try:
        _kill_install_processes(install_root)

        # 1. Silent-install the previous release (Velopack Setup supports
        #    --silent). Kill anything it may have launched afterwards.
        subprocess.run([prev_setup, "--silent"], check=True, timeout=SETUP_TIMEOUT_S)
        _kill_install_processes(install_root)

        # 2. Verify the installed layout matches what update_manager.cpp's
        #    FindUpdateExe expects (Update.exe at root, app under current\).
        installed_exe = current_exe if current_exe.is_file() else legacy_exe
        assert update_exe.is_file() and installed_exe.is_file(), (
            "Unexpected Velopack install layout; expected Update.exe and "
            f"current\\AgentRedactorUI.exe (or legacy AgentRedactor.exe) under {install_root}.\n"
            + _layout_listing(install_root)
        )

        prev_version = _file_version(installed_exe)
        assert _version_key(prev_version) < _version_key(expected), (
            f"installed previous version '{prev_version}' is not older than "
            f"the expected upgrade target '{expected}'"
        )

        # 3. Seed a settings file so we can prove settings survive the upgrade.
        config_dir = tmp_path / "config"
        config_dir.mkdir()
        (config_dir / "settings.json").write_text(
            json.dumps({"start_on_boot": False, "app_language": "en"}),
            encoding="utf-8",
        )

        # 4. Serve the vNext feed folder over HTTP (the feed override requires
        #    HTTP/HTTPS; a bare folder path is not accepted).
        port = _free_port()
        server = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "http.server",
                str(port),
                "--bind",
                "127.0.0.1",
                "--directory",
                str(feed_dir_path),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # 5. Launch the installed app with the feed override and auto-apply.
        env = os.environ.copy()
        env.update(
            {
                "AGENTREDACTOR_UPDATE_FEED": f"http://127.0.0.1:{port}",
                "AGENTREDACTOR_UPDATE_AUTOAPPLY": "1",
                "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
            }
        )
        subprocess.Popen([str(installed_exe)], env=env)

        # 6. Poll until the installed current\AgentRedactorUI.exe is the vNext
        #    build (Update.exe apply swaps it and restarts the app).
        deadline = time.monotonic() + POLL_TIMEOUT_S
        seen = prev_version
        upgraded = False
        while time.monotonic() < deadline:
            time.sleep(POLL_INTERVAL_S)
            try:
                if not current_exe.is_file():
                    continue  # mid-swap
                seen = _file_version(current_exe)
            except Exception:
                continue
            if seen and _version_key(seen) >= _version_key(expected):
                upgraded = True
                break
        if not upgraded:
            pytest.fail(
                f"upgrade did not complete within {POLL_TIMEOUT_S}s: "
                f"last seen file version '{seen}', expected >= '{expected}'"
            )

        # 7. Settings survived the upgrade and still migrate cleanly on the
        #    new build.
        result = subprocess.run(
            [str(current_exe), "--selftest-migrate-settings"],
            env={**os.environ, "AGENTREDACTOR_CONFIG_DIR": str(config_dir)},
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert result.returncode == 0, (
            f"selftest failed on upgraded install: {result.stdout} {result.stderr}"
        )
        assert "SETTINGS_MIGRATION_OK" in result.stdout

        settings = json.loads((config_dir / "settings.json").read_text(encoding="utf-8"))
        assert settings["settings_version"] == 2
        assert settings["start_on_boot"] is False
        assert settings["app_language"] == "en"
        assert "verbose_logging" not in settings
    finally:
        _kill_install_processes(install_root)
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
