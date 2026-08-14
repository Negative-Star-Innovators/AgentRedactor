"""GUI tests for the Windows-Hello-only protection model.

There is no typed password anywhere in the product: the "Lock with Windows
Hello" checkbox toggles protection. Enabling is direct (no dialog, no
consent - the engine generates a random key wrapped in DPAPI); disabling
keeps the Windows Hello consent prompt, but that prompt is system UI (the
Windows Security PIN/biometrics dialog) and cannot be automated, so these
tests drive the UI up to the consent point, then use the engine CLI against
the running app's engine to enable/disable protection for the restart
scenarios.

When the session is locked the window shows ONLY the padlock overlay (lock
icon + app name + Unlock button). There is no retry/exit dialog, no message
on failure, and nothing of the app content is in the UI tree while locked
(the content frame is collapsed), so the tests assert those properties
instead of driving a dialog.
"""

from __future__ import annotations

import os
import platform
import subprocess
import time
from pathlib import Path

import pytest

from mock_llm import MockLLM

from config_factory import create_settings
from gui_process import GuiAppProcess, _find_free_port
from windows.gui_driver import (
    get_content_dialog_text,
    get_require_password_state,
    toggle_require_password,
    wait_until,
)

# The GUI's lock overlay auto-runs the Windows Hello prompt when the window
# appears. AGENTREDACTOR_HELLO_SUPPRESS_PROMPT (cancel-equivalent test hook)
# makes that prompt behave exactly like a user pressing cancel: no system
# "Windows Security" dialog ever appears, so the harness is never blocked by
# it on Hello-enabled machines, and the lock flow is deterministic anywhere.
# The hook can never verify or unlock (asserted by
# test_hello_suppress_prompt_flag_never_grants_access in tests/cli); the real
# unlock path requires the actual Windows Hello authentication. Inherited by
# every GUI/engine process these tests spawn.
os.environ["AGENTREDACTOR_HELLO_SUPPRESS_PROMPT"] = "1"
# Belt-and-braces: any non-suppressed consent path fails fast instead of
# hanging 60 s per call.
os.environ["AGENTREDACTOR_HELLO_TIMEOUT_MS"] = "1500"

# The vcxproj outputs to build/<Platform>/Release where <Platform> is the
# MSBuild platform name (x64 / ARM64). Match the host architecture, like
# gui_process.py and tests/cli/conftest.py do — the ARM64 CI leg only has
# windows/build/ARM64/Release/, so a hardcoded x64 path would not exist there.
_BUILD_PLATFORM = "ARM64" if platform.machine().upper() == "ARM64" else "x64"
ENGINE_EXE = (
    Path(__file__).resolve().parent.parent.parent
    / "windows"
    / "build"
    / _BUILD_PLATFORM
    / "Release"
    / "agentredactor.exe"
)


def _run_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(ENGINE_EXE), *args],
        capture_output=True,
        text=True,
        timeout=60,
    )


def _page_text() -> str:
    """All text in the app window; the helper failure (window not ready) is
    surfaced as an empty string so callers can keep polling."""
    try:
        return "\n".join(get_content_dialog_text())
    except RuntimeError:
        return ""


def _overlay_visible() -> bool:
    """True while the padlock overlay is up. Its app-name title is an exact
    "Agent Redactor" line; the unlocked page only contains the longer
    "How to use Agent Redactor" text, so an exact line match is unambiguous."""
    return any(line.strip() == "Agent Redactor" for line in _page_text().splitlines())


def _wait_for_locked_overlay() -> str:
    """Wait (bounded) for the padlock overlay to be on screen."""
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        text = _page_text()
        if any(line.strip() == "Agent Redactor" for line in text.splitlines()):
            return text
        time.sleep(0.5)
    raise AssertionError("lock overlay did not appear in time")


def _start_app(
    data_dir: Path, proxy_port: int, mock_llm: MockLLM, *, create: bool = True
) -> GuiAppProcess:
    if create:
        create_settings(
            data_dir=data_dir,
            upstream_url=mock_llm.base_url,
            api_key="test-api-key",
            proxy_port=proxy_port,
            logging_enabled=True,
            keywords=[],
            regex_patterns=[],
        )
    app = GuiAppProcess(proxy_port=proxy_port)
    app.start()
    return app


@pytest.mark.asyncio
async def test_gui_hello_enable_direct(
    gui_app: GuiAppProcess,
    mock_llm: MockLLM,
) -> None:
    """Checking the Lock checkbox enables protection directly: no confirm
    dialog and no consent prompt, and no lock overlay appears (there is
    nothing to consent to - the engine just wraps a random key in DPAPI)."""
    assert not get_require_password_state()

    toggle_require_password()
    assert get_require_password_state()
    # No lock overlay: the page content is visible (not covered/collapsed).
    text = _page_text()
    assert "test-profile" in text, text
    assert "Unlock" not in text, text

    # Restore: protection off via the engine CLI (the GUI-side disable
    # consent prompt cannot be automated). The checkbox follows the engine
    # through the 1-second settings poll, so wait for the change.
    r = _run_cli("password", "disable")
    assert r.returncode == 0, r.stdout
    wait_until(
        "checkbox unchecks after CLI disable",
        get_require_password_state,
        lambda state: state is False,
    )


@pytest.mark.asyncio
async def test_gui_hello_startup_lock_dialog(
    user_data_backup: Path,
    mock_llm: MockLLM,
) -> None:
    """With protection enabled, a fresh app open starts locked: the window
    shows ONLY the padlock overlay (lock icon + app name + Unlock button),
    the app content is NOT in the UI tree at all (the frame is collapsed, so
    profiles/regex/keywords cannot leak behind the Windows Security dialog),
    the Hello prompt runs immediately, and a failed/cancelled verification
    simply stays on the padlock. There is no retry/exit dialog and the app
    keeps running until the user unlocks."""
    port = _find_free_port()
    app = _start_app(user_data_backup, port, mock_llm)
    try:
        # Enabling via the engine CLI while the GUI runs is inherently racy
        # (the app may or may not lock itself before we stop it); the restart
        # below is the deterministic part that verifies the lock flow.
        r = _run_cli("password", "enable")
        assert r.returncode == 0, r.stdout
    finally:
        app.stop()

    app = _start_app(user_data_backup, port, mock_llm, create=False)
    try:
        print("DIAG restart status:", _run_cli("status").stdout)
        text = _wait_for_locked_overlay()
        # Padlock overlay elements are on screen (app name)...
        assert "Agent Redactor" in text, text
        # ...and the app content is NOT (frame collapsed while locked).
        assert "test-profile" not in text, text
        # The app is still running: there is no exit path on the padlock.
        assert app.process.poll() is None
    finally:
        app.stop()


@pytest.mark.asyncio
async def test_gui_hello_disable_via_cli_while_locked(
    user_data_backup: Path,
    mock_llm: MockLLM,
) -> None:
    """`password disable` stays safe on a locked session: protection can be
    removed from the command line while the padlock is up, and the overlay
    lifts through the 1-second settings poll - no dialog, no exit, the app
    keeps running."""
    port = _find_free_port()
    app = _start_app(user_data_backup, port, mock_llm)
    try:
        r = _run_cli("password", "enable")
        assert r.returncode == 0, r.stdout
    finally:
        app.stop()

    app = _start_app(user_data_backup, port, mock_llm, create=False)
    try:
        _wait_for_locked_overlay()
        assert app.process.poll() is None
        r = _run_cli("password", "disable")
        assert r.returncode == 0, r.stdout
        # The settings poll lifts the padlock overlay...
        wait_until(
            "overlay lifts after CLI disable",
            lambda: not _overlay_visible(),
            lambda lifted: lifted,
        )
        # ...and the app content (incl. the Lock checkbox) is back.
        wait_until(
            "checkbox unchecks after CLI disable",
            get_require_password_state,
            lambda state: state is False,
        )
        assert app.process.poll() is None
    finally:
        app.stop()

    app = _start_app(user_data_backup, port, mock_llm, create=False)
    try:
        assert not get_require_password_state()
    finally:
        app.stop()
