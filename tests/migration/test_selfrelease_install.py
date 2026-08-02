"""Opt-in end-to-end test for the Velopack self-release *fresh install* path.

Silent-installs a just-built ``*-Setup.exe``, verifies the installed layout
and version, checks the PE machine type matches the build architecture, and
runs the headless ``--selftest-migrate-settings`` hook on the *installed* exe
(so the test exercises exactly the bits a user gets, not the build folder).
Uninstalls again afterwards (``Update.exe uninstall``) unless told to keep it.

Skipped unless AGENTREDACTOR_INSTALL_TEST=1. Required env vars:
  AGENTREDACTOR_SETUP        path to the *-Setup.exe under test
Optional:
  AGENTREDACTOR_EXPECT_VERSION  expected file version of the installed exe
                                (e.g. "1.1.0"); skipped when unset
  AGENTREDACTOR_EXPECT_MACHINE  expected PE machine type: 'AMD64' or 'ARM64';
                                skipped when unset
  AGENTREDACTOR_KEEP_INSTALL=1  do not uninstall afterwards

See tests/migration/README.md for the full contract and CI wiring.
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import psutil
import pytest

pytestmark = [
    pytest.mark.install,
    pytest.mark.skipif(
        sys.platform != "win32", reason="Velopack self-release is Windows-only"
    ),
    pytest.mark.skipif(
        os.environ.get("AGENTREDACTOR_INSTALL_TEST") != "1",
        reason="opt-in E2E: set AGENTREDACTOR_INSTALL_TEST=1 "
        "(plus AGENTREDACTOR_SETUP)",
    ),
]

# Velopack pack id is "AgentRedactor" (build.ps1: vpk pack -u AgentRedactor),
# so the per-user install root is %LOCALAPPDATA%\AgentRedactor with Update.exe
# at the root and the app under current\.
INSTALL_DIR_NAME = "AgentRedactor"

SETUP_TIMEOUT_S = 300

# PE header machine types (IMAGE_FILE_MACHINE_*).
MACHINE_NAMES = {0x014C: "I386", 0x8664: "AMD64", 0xAA64: "ARM64"}


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


def _pe_machine(exe: Path) -> str:
    """Read the PE header's machine field ('AMD64' / 'ARM64' / ...)."""
    data = exe.read_bytes()
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        pytest.fail(f"{exe} is not a PE file")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    return MACHINE_NAMES.get(machine, f"unknown(0x{machine:04X})")


def _kill_install_processes(install_root: Path) -> None:
    """Kill AgentRedactor.exe / Update.exe instances running from the install."""
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


def test_selfrelease_fresh_install(tmp_path):
    setup = os.environ.get("AGENTREDACTOR_SETUP")
    if not setup or not Path(setup).is_file():
        pytest.skip("AGENTREDACTOR_SETUP is not set or does not point at a Setup.exe")

    install_root = Path(os.environ["LOCALAPPDATA"]) / INSTALL_DIR_NAME
    current_exe = install_root / "current" / "AgentRedactor.exe"
    update_exe = install_root / "Update.exe"

    try:
        _kill_install_processes(install_root)

        # 1. Silent-install the build under test (Velopack Setup supports
        #    --silent). Kill anything it may have launched afterwards.
        subprocess.run([setup, "--silent"], check=True, timeout=SETUP_TIMEOUT_S)
        _kill_install_processes(install_root)

        # 2. Verify the installed layout (Update.exe at root, app under
        #    current\) — this is also what update_manager.cpp::FindUpdateExe
        #    expects at runtime.
        assert update_exe.is_file() and current_exe.is_file(), (
            "Unexpected Velopack install layout; expected Update.exe and "
            f"current\\AgentRedactor.exe under {install_root}.\n"
            + _layout_listing(install_root)
        )

        # 3. Version assertion (when the caller knows what it built).
        expected_version = os.environ.get("AGENTREDACTOR_EXPECT_VERSION")
        if expected_version:
            installed = _file_version(current_exe)
            assert _version_key(installed) == _version_key(expected_version), (
                f"installed version '{installed}' does not match expected "
                f"'{expected_version}'"
            )

        # 4. Architecture assertion: the installed exe must be the native
        #    build for the channel under test (guards against packing the
        #    wrong binaries into a channel).
        expected_machine = os.environ.get("AGENTREDACTOR_EXPECT_MACHINE")
        if expected_machine:
            machine = _pe_machine(current_exe)
            assert machine == expected_machine, (
                f"installed exe is {machine}, expected {expected_machine}"
            )

        # 5. The installed exe's headless migration hook works against an
        #    isolated config dir (proves the packaged binary actually runs).
        config_dir = tmp_path / "config"
        config_dir.mkdir()
        result = subprocess.run(
            [str(current_exe), "--selftest-migrate-settings"],
            env={**os.environ, "AGENTREDACTOR_CONFIG_DIR": str(config_dir)},
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert result.returncode == 0, (
            f"selftest failed on fresh install: {result.stdout} {result.stderr}"
        )
        assert "SETTINGS_MIGRATION_OK" in result.stdout
    finally:
        _kill_install_processes(install_root)
        if os.environ.get("AGENTREDACTOR_KEEP_INSTALL") != "1" and update_exe.is_file():
            # Velopack's own uninstaller (same path scripts/remove-agentredactor-full.ps1
            # uses); best-effort, the install root may linger if a file is locked.
            subprocess.run(
                [str(update_exe), "uninstall"],
                capture_output=True,
                timeout=SETUP_TIMEOUT_S,
            )
