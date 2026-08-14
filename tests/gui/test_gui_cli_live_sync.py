"""E2E test: CLI setting changes must reach the running GUI without a restart.

The GUI polls GET /settings every second, diffs it, and applies engine-side
changes live (WM_APP_NOTIFY_SETTINGS -> RefreshFromEngineSettings). This test
flips settings through the real `agentredactor.exe` CLI while the GUI is up
and asserts the UI updates in place.
"""

from __future__ import annotations

import platform
import subprocess
import time
from pathlib import Path

import pytest

from gui_process import GuiAppProcess
from windows.gui_driver import get_is_checked, get_is_enabled


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_BUILD_PLATFORM = "ARM64" if platform.machine().upper() == "ARM64" else "x64"
ENGINE_EXE = PROJECT_ROOT / "windows" / "build" / _BUILD_PLATFORM / "Release" / "agentredactor.exe"


def _run_cli(*args: str) -> subprocess.CompletedProcess:
    """Run the CLI against the engine the GUI spawned (real %APPDATA%)."""
    return subprocess.run(
        [str(ENGINE_EXE), *args],
        capture_output=True,
        text=True,
        timeout=30,
    )


def _wait_until(condition, timeout: float = 15.0, interval: float = 1.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if condition():
                return True
        except RuntimeError:
            pass  # helper race while the UI refreshes the control tree
        time.sleep(interval)
    return False


@pytest.mark.asyncio
async def test_gui_reflects_cli_setting_changes_without_restart(
    gui_app: GuiAppProcess,
) -> None:
    # The gui_app fixture starts with logging enabled (logging_enabled=True).
    assert get_is_checked("EnableLoggingCheck") is True

    # Flip the setting through the CLI. No --data-dir, no GUI restart: the
    # CLI talks to the same engine the GUI is already connected to.
    r = _run_cli("set", "logging", "false")
    assert r.returncode == 0, r.stdout + r.stderr

    # The GUI picks the change up within its 1s poll window.
    assert _wait_until(lambda: get_is_checked("EnableLoggingCheck") is False), (
        "Enable logging checkbox never reflected the CLI change"
    )
    # Show sensitive follows logging state and must become disabled too.
    assert _wait_until(lambda: get_is_enabled("ShowSensitiveCheck") is False), (
        "Show sensitive checkbox never became disabled"
    )

    # And back, still with the same GUI process running.
    r = _run_cli("set", "logging", "true")
    assert r.returncode == 0, r.stdout + r.stderr
    assert _wait_until(lambda: get_is_checked("EnableLoggingCheck") is True), (
        "Enable logging checkbox never returned to checked"
    )
    assert _wait_until(lambda: get_is_enabled("ShowSensitiveCheck") is True), (
        "Show sensitive checkbox never became enabled again"
    )