"""Shared fixtures/helpers for the headless migration and upgrade tests."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
if sys.platform == "win32":
    DEFAULT_EXE = REPO_ROOT / "windows" / "build" / "x64" / "Release" / "AgentRedactorUI.exe"
else:
    DEFAULT_EXE = REPO_ROOT / "linux" / "build" / "engine" / "agentredactor"


def resolve_exe() -> Path | None:
    """AGENTREDACTOR_EXE wins; otherwise fall back to the local build."""
    env_path = os.environ.get("AGENTREDACTOR_EXE")
    if env_path:
        return Path(env_path)
    return DEFAULT_EXE if DEFAULT_EXE.is_file() else None


@pytest.fixture(scope="session")
def agentredactor_exe() -> Path:
    exe = resolve_exe()
    if exe is None or not exe.is_file():
        pytest.skip(
            "agentredactor binary not found (looked at "
            f"{DEFAULT_EXE}). Build the Release configuration (Windows) or "
            "the linux/build tree, or point AGENTREDACTOR_EXE at a built binary."
        )
    return exe


@pytest.fixture(scope="session")
def run_selftest(agentredactor_exe):
    """Run '<exe> --selftest-migrate-settings' against an isolated config dir."""

    def _run(config_dir: Path, timeout: int = 60) -> subprocess.CompletedProcess:
        env = os.environ.copy()
        env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
        return subprocess.run(
            [str(agentredactor_exe), "--selftest-migrate-settings"],
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )

    return _run
