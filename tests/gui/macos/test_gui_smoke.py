from __future__ import annotations

import os
import plistlib
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

import pytest

from app_process import MacAppProcess
import gui_driver

pytestmark = [
    pytest.mark.macos_gui,
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS Accessibility GUI test"),
    pytest.mark.skipif(os.environ.get("AGENTREDACTOR_GUI_TEST") != "1",
                       reason="opt-in: set AGENTREDACTOR_GUI_TEST=1"),
]

PROJECT_ROOT = Path(__file__).resolve().parents[3]
SOURCE_APP = PROJECT_ROOT / "macos" / "build" / "AgentRedactor.app"
HELPER_BUILD = Path(__file__).parent / "AccessibilityHelper" / "build.sh"


def prepare_isolated_app(tmp_path: Path) -> Path:
    app = tmp_path / "AgentRedactorGuiTest.app"
    shutil.copytree(SOURCE_APP, app)
    plist_path = app / "Contents" / "Info.plist"
    with plist_path.open("rb") as handle:
        plist = plistlib.load(handle)
    plist["CFBundleIdentifier"] = f"com.negativestarinnovators.agentredactor.guitest.{uuid.uuid4().hex}"
    with plist_path.open("wb") as handle:
        plistlib.dump(plist, handle)
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)],
                   check=True, capture_output=True, timeout=60)
    return app


def test_profile_lifecycle_and_close_reopen(tmp_path: Path) -> None:
    if not SOURCE_APP.is_dir():
        pytest.skip(f"development app not built: {SOURCE_APP}")
    subprocess.run(["bash", str(HELPER_BUILD)], check=True, capture_output=True, timeout=120)
    app = MacAppProcess(prepare_isolated_app(tmp_path), tmp_path / "config")
    try:
        app.start()
        app.wait_for_engine()
        gui_driver.wait_window(app.pid)
        gui_driver.wait_until("default profile", lambda: app.api("GET", "/profiles"),
                              lambda profiles: len(profiles) == 1, timeout=45)

        try:
            gui_driver.click(app.pid, "profiles.add")
        except RuntimeError:
            print(gui_driver.dump(app.pid))
            raise
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
    finally:
        if gui_driver.auth_count() > 0:
            gui_driver.cancel_auth()
        app.stop()
