"""Regression test: a CLI `set api-key` must reach the open GUI's API key box
within the ~1s poll even when the new key shares its first 3 characters with
the old one ("apple" -> "applesarebadforyou").

GET /profiles masks every api key to "abc...****", and the Windows GUI's
diff-aware refresh (ProfilesMatch) compared that mask — so a same-prefix key
change read as "no change" and the box stayed stale until the user switched
profiles or reopened the window. HomePage::RefreshSelectedProfileApiKey()
re-fetches the real key whenever the diff reports no visible change; this test
locks that behavior in on the real GUI binary.
"""

from __future__ import annotations

import json
import os
import platform
import shutil
import subprocess
import time
from pathlib import Path

import pytest

from gui_process import GuiAppProcess
from windows.gui_driver import get_api_key_visibility, toggle_show_api_key


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_BUILD_PLATFORM = "ARM64" if platform.machine().upper() == "ARM64" else "x64"
ENGINE_EXE = PROJECT_ROOT / "windows" / "build" / _BUILD_PLATFORM / "Release" / "agentredactor.exe"

_DBG_COUNTER = 0


def _dump_log(tag: str) -> None:
    """Copy the current app log for diagnosis (also on success paths)."""
    global _DBG_COUNTER
    _DBG_COUNTER += 1
    app_log = Path(os.environ.get("APPDATA", "")) / "AgentRedactor" / "agent_redactor.log"
    if not app_log.exists():
        print(f"\n[DBG:{tag}] no app log at {app_log}")
        return
    dst = PROJECT_ROOT / "debug_out" / f"gui_api_key_sync_step{_DBG_COUNTER:02d}_{tag}.log"
    dst.parent.mkdir(exist_ok=True)
    shutil.copy2(app_log, dst)
    print(f"\n[DBG:{tag}] app log ({app_log.stat().st_size} bytes) copied to {dst}")


def _run_cli(*args: str) -> subprocess.CompletedProcess:
    """Run the CLI against the engine the GUI spawned (real %APPDATA%)."""
    return subprocess.run(
        [str(ENGINE_EXE), *args],
        capture_output=True,
        text=True,
        timeout=30,
    )


def _profile_id() -> str:
    """The id of the single seeded profile (the user's --profile argument)."""
    appdata = Path(os.environ["APPDATA"]) / "AgentRedactor"
    data = json.loads((appdata / "settings.json").read_text(encoding="utf-8"))
    return data["profiles"][0]["id"]


def _revealed_api_key_text() -> str:
    """Return the real text in the ApiKeyBox.

    A hidden WinUI PasswordBox does not expose its value through UIA, so
    reveal the key first (toggles are user-only, no autosave side effect).
    """
    checked, text = get_api_key_visibility()
    if not checked:
        toggle_show_api_key()
        _, text = get_api_key_visibility()
    return text


def _wait_until(condition, timeout: float = 15.0, interval: float = 0.5) -> bool:
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
async def test_gui_api_key_reflects_cli_set_with_same_prefix(
    gui_app_no_profile_config: GuiAppProcess,
) -> None:
    try:
        _run_scenario()
    except BaseException:
        # Save the app log for diagnosis BEFORE the fixture restores the real
        # %APPDATA% (the fixture deletes this test's fresh data dir).
        app_log = Path(os.environ["APPDATA"]) / "AgentRedactor" / "agent_redactor.log"
        if app_log.exists():
            dst = PROJECT_ROOT / "debug_out" / "gui_api_key_sync_debug.log"
            dst.parent.mkdir(exist_ok=True)
            shutil.copy2(app_log, dst)
            print(f"\n[DBG] app log copied to {dst}")
        raise


def _run_scenario() -> None:
    # The fixture profile starts with an EMPTY api key (the user's first-time
    # case: no key set yet).
    _dump_log("startup")
    assert _revealed_api_key_text() == ""

    profile_arg = ["--profile", _profile_id()]

    # 1) Empty -> "apple": the mask changes ("" -> "app...****"), so the
    #    diff-aware refresh reloads and the box updates on every build.
    r = _run_cli("set", "api-key", "apple", *profile_arg)
    assert r.returncode == 0, r.stdout + r.stderr
    assert _wait_until(lambda: _revealed_api_key_text() == "apple"), (
        "box never showed the first CLI-set key ('apple')"
    )
    _dump_log("after_apple")

    # 2) "apple" -> "applesarebadforyou": same "app" 3-char prefix, so the
    #    masked values are identical and the diff alone can never see it.
    #    This is the reported regression — the box must still update without
    #    switching profiles or reopening the window.
    r = _run_cli("set", "api-key", "applesarebadforyou", *profile_arg)
    assert r.returncode == 0, r.stdout + r.stderr
    assert _wait_until(lambda: _revealed_api_key_text() == "applesarebadforyou"), (
        "box never reflected the same-prefix CLI key change "
        "('apple' -> 'applesarebadforyou') without a profile switch"
    )
    _dump_log("after_applesarebadforyou")

    # Sanity: the engine really stores the new key, and a third CLI change
    # with a *different* prefix still lands (diff path still works).
    r = _run_cli("set", "api-key", "x-key-333", *profile_arg)
    assert r.returncode == 0, r.stdout + r.stderr
    assert _wait_until(lambda: _revealed_api_key_text() == "x-key-333"), (
        "box never reflected the different-prefix CLI key change"
    )
    _dump_log("after_xkey333")