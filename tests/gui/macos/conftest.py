from __future__ import annotations

import plistlib
import shutil
import subprocess
import uuid
from pathlib import Path

import pytest

from app_process import MacAppProcess
import gui_driver

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


@pytest.fixture()
def app(tmp_path: Path):
    if not SOURCE_APP.is_dir():
        pytest.skip(f"development app not built: {SOURCE_APP}")
    subprocess.run(["bash", str(HELPER_BUILD)], check=True, capture_output=True, timeout=120)
    process = MacAppProcess(prepare_isolated_app(tmp_path), tmp_path / "config")
    try:
        process.start()
        process.wait_for_engine()
        gui_driver.wait_window(process.pid)
        gui_driver.wait_until("default profile", lambda: process.api("GET", "/profiles"),
                              lambda profiles: len(profiles) == 1, timeout=45)
        yield process
    finally:
        if gui_driver.auth_count() > 0:
            gui_driver.cancel_auth()
        process.stop()
