"""Explorer restart must restore a working tray icon after a tooltip update."""

from __future__ import annotations

import platform
import subprocess
import time
from pathlib import Path

import pytest

from gui_process import GuiAppProcess
from windows.gui_driver import (
    assert_tray_icon,
    click_tray_icon,
    close_to_tray,
    get_log_text,
    get_pii_master_state,
    restore_taskbar,
    wait_until,
)


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_BUILD_PLATFORM = "ARM64" if platform.machine().upper() == "ARM64" else "x64"
ENGINE_EXE = PROJECT_ROOT / "windows" / "build" / _BUILD_PLATFORM / "Release" / "agentredactor.exe"


@pytest.mark.asyncio
async def test_tray_icon_recovers_after_tooltip_update_and_explorer_restart(
    gui_app: GuiAppProcess,
) -> None:
    get_pii_master_state()
    time.sleep(2)
    assert_tray_icon()

    result = subprocess.run(
        [str(ENGINE_EXE), "set", "app-language", "de"],
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    wait_until(
        "GUI tooltip update after CLI language change",
        get_log_text,
        lambda log: "[AppState] SetLanguage called: de" in log,
    )

    close_to_tray()
    restore_taskbar()
    click_tray_icon()
