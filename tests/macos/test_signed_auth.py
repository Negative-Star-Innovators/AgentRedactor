from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

MAC_GUI = Path(__file__).parent.parent / "gui" / "macos"
if str(MAC_GUI) not in sys.path:
    sys.path.insert(0, str(MAC_GUI))

from app_process import MacAppProcess
import gui_driver

pytestmark = [
    pytest.mark.macos_gui,
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS signed authentication E2E"),
    pytest.mark.skipif(os.environ.get("AGENTREDACTOR_SIGNED_AUTH_TEST") != "1",
                       reason="opt-in signed release authentication test"),
]


def wait_unlocked(app: MacAppProcess, unlocked: bool) -> None:
    gui_driver.wait_until(
        f"engine unlocked={unlocked}",
        lambda: app.api("GET", "/status"),
        lambda status: status.get("unlocked") is unlocked)


def authenticate() -> None:
    password = os.environ.get("AGENTREDACTOR_TEST_MAC_PASSWORD")
    if not password:
        raise RuntimeError("AGENTREDACTOR_TEST_MAC_PASSWORD is required")
    gui_driver.wait_until("Apple authentication prompt", gui_driver.auth_count,
                          lambda count: count > 0)
    gui_driver.submit_auth(password)
    gui_driver.wait_until("authentication completed", gui_driver.auth_count,
                          lambda count: count == 0)


def test_signed_password_lifecycle(tmp_path: Path) -> None:
    app_path = Path(os.environ.get("AGENTREDACTOR_APP", ""))
    if not app_path.is_dir():
        pytest.fail("AGENTREDACTOR_APP must point to the signed app under test")
    subprocess.run(["bash", str(MAC_GUI / "AccessibilityHelper" / "build.sh")],
                   check=True, capture_output=True, timeout=120)

    config_dir = Path(os.environ.get("AGENTREDACTOR_TEST_CONFIG_DIR",
                                     str(tmp_path / "config")))
    app = MacAppProcess(app_path, config_dir)
    try:
        app.start()
        app.wait_for_engine()
        gui_driver.wait_window(app.pid)
        gui_driver.click(app.pid, "settings.master-password.enable")
        gui_driver.wait_until(
            "protection enabled",
            lambda: app.api("GET", "/settings"),
            lambda settings: settings.get("masterPasswordEnabled") is True)
        wait_unlocked(app, True)
        assert gui_driver.auth_count() == 0

        gui_driver.close_window(app.pid)
        wait_unlocked(app, False)
        assert gui_driver.auth_count() == 0
        app.reopen()
        gui_driver.wait_window(app.pid)
        authenticate()
        wait_unlocked(app, True)

        app.stop()
        app = MacAppProcess(app_path, config_dir)
        app.start()
        app.wait_for_engine()
        gui_driver.wait_window(app.pid)
        authenticate()
        wait_unlocked(app, True)

        gui_driver.click(app.pid, "settings.master-password.disable")
        authenticate()
        gui_driver.wait_until(
            "protection disabled",
            lambda: app.api("GET", "/settings"),
            lambda settings: settings.get("masterPasswordEnabled") is False)
    finally:
        if gui_driver.auth_count() > 0:
            gui_driver.cancel_auth()
        app.stop()
