from __future__ import annotations

import os
import sys

import pytest

from app_process import MacAppProcess
import gui_driver

pytestmark = [
    pytest.mark.macos_gui,
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS Accessibility GUI test"),
    pytest.mark.skipif(os.environ.get("AGENTREDACTOR_GUI_TEST") != "1",
                       reason="opt-in: set AGENTREDACTOR_GUI_TEST=1"),
]


def test_profile_lifecycle_and_close_reopen(app: MacAppProcess) -> None:
    gui_driver.click(app.pid, "profiles.add")
    gui_driver.wait_until("profile added", lambda: app.api("GET", "/profiles"),
                          lambda profiles: len(profiles) == 2)
    gui_driver.click(app.pid, "profiles.remove")
    gui_driver.click_title(app.pid, "Remove Profile")
    gui_driver.wait_until("profile removed", lambda: app.api("GET", "/profiles"),
                          lambda profiles: len(profiles) == 1)

    gui_driver.close_window(app.pid)
    gui_driver.wait_until("window hidden", lambda: gui_driver.window_count(app.pid),
                          lambda count: count == 0)
    assert gui_driver.auth_count() == 0
    app.reopen()
    gui_driver.wait_window(app.pid)
    assert gui_driver.window_count(app.pid) > 0
