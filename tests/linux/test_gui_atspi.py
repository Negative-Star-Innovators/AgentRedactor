"""Linux GUI E2E tests driven through AT-SPI accessibility (Qt6, xcb).

The Linux mirror of tests/gui/ (Windows FlaUI suite): these tests run the real
agentredactor-gui binary on the accessibility bus and drive the actual widget
tree — profiles, keywords, regexes, PII controls, logging, clear buttons,
statistics, the typed-master-password lock overlay, API-key masking, and live
language switching — while asserting end-to-end redaction through the proxy
against the mock upstream LLM (tests/mock_llm.py).

Coverage notes vs. the Windows suite:
- The Windows "port status indicator" (green/red text next to the Port box)
  has no Linux counterpart; port validation is covered here through the
  Save-time conflict dialog instead (test_port_conflict_rejected_with_dialog).
- Language switching is driven through the real CLI (`set app-language`), as
  allowed for this port, and asserted on the retranslated a11y strings.
- Row text edits (keyword/regex) must be persisted with the profile card's
  Save button: AT-SPI text replacement never fires QLineEdit::editingFinished.

Requires a display (real X session, XWayland, or Xvfb) plus gi/Atspi; the
whole module skips cleanly otherwise. Each test boots a fresh GUI+engine pair
against an isolated config dir (model weights are shared from
~/.local/share/agentredactor).
"""

from __future__ import annotations

import asyncio
import os
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import aiohttp
import psutil
import pytest
import pytest_asyncio

_tests_root = Path(__file__).resolve().parent.parent
for _p in (str(_tests_root), str(_tests_root / "gui"), str(_tests_root / "linux")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from config_factory import create_settings  # noqa: E402
from gui_process import _find_free_port, _kill_existing_agent_redactor, _wait_for_port  # noqa: E402
from gui_atspi import ENGINE_BIN, AtspiGui, require_atspi, wait_until  # noqa: E402
from mock_llm import MockLLM  # noqa: E402

pytestmark = [
    pytest.mark.atspi,
    pytest.mark.skipif(sys.platform != "linux", reason="Linux AT-SPI GUI tests"),
]

OPENAI_PATH = "/v1/chat/completions"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _extract_last_user_message(request_body: dict) -> str:
    messages = request_body.get("messages", [])
    if not messages:
        return ""
    return str(messages[-1].get("content", ""))


def _extract_assistant_content(response_json: dict) -> str:
    choices = response_json.get("choices", [])
    if not choices:
        return ""
    return choices[0].get("message", {}).get("content", "")


def _wait_for_port_closed(port: int, timeout: float = 15.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                time.sleep(0.1)
        except OSError:
            return
    raise RuntimeError(f"Port {port} is still listening after removal")


@dataclass
class GuiContext:
    app: AtspiGui
    config_dir: Path
    proxy_port: int

    @property
    def proxy_url(self) -> str:
        return f"http://127.0.0.1:{self.proxy_port}"

    def cli(self, *args: str, input: str | None = None) -> subprocess.CompletedProcess:
        """Run the engine CLI against this test's engine/config dir."""
        env = dict(os.environ)
        env["AGENTREDACTOR_CONFIG_DIR"] = str(self.config_dir)
        # Same rationale as AtspiGui: no keyring on the test session bus.
        env["AGENTREDACTOR_DISABLE_KEYRING"] = "1"
        return subprocess.run(
            [str(ENGINE_BIN), *args], env=env, input=input,
            capture_output=True, text=True, timeout=60,
        )

    def log_text(self, name: str = "agent_redactor.log", max_chars: int = 50_000) -> str:
        path = self.config_dir / name
        if not path.exists():
            return ""
        text = path.read_text(encoding="utf-8", errors="ignore")
        return text[-max_chars:] if len(text) > max_chars else text


async def _send_chat(
    ctx: GuiContext,
    client: aiohttp.ClientSession,
    content: str,
    *,
    port: int | None = None,
) -> dict:
    """POST through the proxy, retrying while listeners restart.

    UI mutations (add/delete keyword, save profile) make the engine restart
    its proxy listeners; the port is briefly unreachable or answers 502. Poll
    instead of sleeping a fixed delay.
    """
    payload = _chat_request(content)
    url = f"http://127.0.0.1:{port or ctx.proxy_port}{OPENAI_PATH}"
    deadline = time.monotonic() + 60
    last: str = ""
    while time.monotonic() < deadline:
        try:
            async with client.post(url, json=payload) as resp:
                if resp.status == 200:
                    return await resp.json()
                last = f"HTTP {resp.status}: {(await resp.text())[:200]}"
        except (aiohttp.ClientError, asyncio.TimeoutError) as exc:
            last = repr(exc)
        await asyncio.sleep(0.5)
    raise RuntimeError(f"proxy never answered 200 for {url}; last: {last}")


def _assert_keyword_redacted(upstream_body: dict, matched_text: str) -> None:
    content = _extract_last_user_message(upstream_body)
    assert matched_text not in content
    assert "<<REDACTED_KEYWORD_" in content


def _assert_not_redacted(upstream_body: dict, expected_text: str) -> None:
    content = _extract_last_user_message(upstream_body)
    assert expected_text in content
    assert "<<REDACTED_" not in content


@pytest_asyncio.fixture
async def gui(tmp_path: Path, mock_llm: MockLLM) -> Any:
    """Fresh GUI+engine with one seeded profile pointing at the mock LLM."""
    require_atspi()
    _kill_existing_agent_redactor()
    config_dir = tmp_path / "config"
    xdg_home = tmp_path / "xdg"
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],  # added via the UI in tests
        regex_patterns=[],
    )
    app = AtspiGui(config_dir, xdg_home)
    app.start()
    try:
        app.app()
        app.wait_ready()
        if not _wait_for_port(proxy_port, timeout=90.0):
            raise RuntimeError(f"proxy port {proxy_port} never opened")
        yield GuiContext(app=app, config_dir=config_dir, proxy_port=proxy_port)
    finally:
        app.stop()
        _kill_existing_agent_redactor()


@pytest_asyncio.fixture
async def gui_fresh(tmp_path: Path) -> Any:
    """GUI with a completely fresh config dir (no seeded settings.json)."""
    require_atspi()
    _kill_existing_agent_redactor()
    config_dir = tmp_path / "config"
    xdg_home = tmp_path / "xdg"
    config_dir.mkdir(parents=True)
    app = AtspiGui(config_dir, xdg_home)
    app.start()
    try:
        app.app()
        app.wait_ready()
        yield GuiContext(app=app, config_dir=config_dir, proxy_port=0)
    finally:
        app.stop()
        _kill_existing_agent_redactor()


# ---------------------------------------------------------------------------
# 12. Default profile seeding
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_fresh_config_seeds_default_profile(gui_fresh: GuiContext) -> None:
    """A fresh config dir lands on a GUI-seeded "Default" profile."""
    names = wait_until(
        "default profile seeded", gui_fresh.app.profiles, lambda ns: "Default" in ns
    )
    assert names == ["Default"]


# ---------------------------------------------------------------------------
# 1. Profiles
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_add_save_and_remove_profile(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """Add a profile via the sidebar, configure + save it through the form,
    verify its proxy port opens and serves redaction-free traffic, then remove
    it through the UI (confirm dialog) and verify the port closes."""
    app = gui.app
    second_port = _find_free_port()

    app.add_profile()
    wait_until("second profile listed", app.profiles, lambda ns: len(ns) == 2)
    app.set_form(url=mock_llm.base_url, api_key="test-api-key-second",
                 port=second_port, alias="second-profile")
    app.save_profile()

    wait_until("alias saved", app.profiles, lambda ns: "second-profile" in ns)
    assert _wait_for_port(second_port, timeout=30.0)

    response = await _send_chat(gui, client, "hello from second", port=second_port)
    assert "hello from second" in _extract_assistant_content(response)

    app.remove_profile("second-profile")
    _wait_for_port_closed(second_port)

    # The default profile keeps working.
    response = await _send_chat(gui, client, "default still works")
    assert "default still works" in _extract_assistant_content(response)


@pytest.mark.asyncio
async def test_two_profile_isolation_redaction(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """Two profiles on two ports: a keyword added to the second profile only
    redacts traffic through the second port, and the upstream mock sees the
    redacted text."""
    app = gui.app
    second_port = _find_free_port()

    app.add_profile()
    app.set_form(url=mock_llm.base_url, api_key="test-api-key-second",
                 port=second_port, alias="second-profile")
    app.save_profile()
    assert _wait_for_port(second_port, timeout=30.0)

    # Keyword on the second profile only (it is the selected one).
    app.add_keyword("SecondSecret")

    # First profile: no keyword configured, text passes through untouched.
    await _send_chat(gui, client, "SecondSecret via first profile")
    upstream = mock_llm.last_request
    assert upstream is not None
    assert "SecondSecret" in _extract_last_user_message(upstream)

    mock_llm.reset()

    # Second profile: upstream must see the redacted placeholder; the client
    # gets the original text reconstructed.
    response = await _send_chat(gui, client, "SecondSecret via second", port=second_port)
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, "SecondSecret")
    assert "SecondSecret" in _extract_assistant_content(response)


@pytest.mark.asyncio
async def test_port_conflict_rejected_with_dialog(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """Saving a profile onto another profile's port shows the validation
    dialog and leaves both profiles on their original ports."""
    app = gui.app
    second_port = _find_free_port()

    app.add_profile()
    app.set_form(url=mock_llm.base_url, api_key="test-api-key-second",
                 port=second_port, alias="second-profile")
    app.save_profile()
    assert _wait_for_port(second_port, timeout=30.0)

    app.select_profile("second-profile")
    app.set_form(port=gui.proxy_port)
    card = app.panel("Profile")
    app.press_named("Save", within=card)

    lines = app.dialog_text("Validation Error")
    assert any("already used by profile" in line for line in lines), lines
    app.dismiss_dialog("OK", title="Validation Error")

    # The dialog rejected the save: the form reverted to the original port.
    wait_until(
        "form reverted to original port",
        lambda: app.field_text("Proxy port"),
        lambda text: text == str(second_port),
    )


# ---------------------------------------------------------------------------
# 2. Keywords
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_keyword_add_and_redact(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    secret = "Project Chimera"
    gui.app.add_keyword(secret, case_sensitive=False)

    keywords = gui.app.keywords()
    assert secret in keywords
    assert keywords[secret]["enabled"] is True
    assert keywords[secret]["case_sensitive"] is False

    # Case-insensitive: lowercase in the request is redacted too; the client
    # gets the keyword's canonical casing back (Windows-suite parity).
    request_text = "The plan is project chimera."
    response = await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, "project chimera")
    assert secret in _extract_assistant_content(response)


@pytest.mark.asyncio
async def test_keyword_toggle_enable_disable(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    secret = "Project Chimera"
    gui.app.add_keyword(secret)

    gui.app.toggle_keyword(secret)
    wait_until("keyword disabled", gui.app.keywords,
               lambda ks: ks.get(secret, {}).get("enabled") is False)

    response = await _send_chat(gui, client, f"The plan is {secret} once.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, secret)
    assert _extract_assistant_content(response) == f"The plan is {secret} once."

    gui.app.toggle_keyword(secret)
    wait_until("keyword re-enabled", gui.app.keywords,
               lambda ks: ks.get(secret, {}).get("enabled") is True)

    response = await _send_chat(gui, client, f"The plan is {secret} twice.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, secret)


@pytest.mark.asyncio
async def test_keyword_edit_text(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    old_secret = "Project Chimera"
    new_secret = "Alpha Bravo"
    gui.app.add_keyword(old_secret)

    gui.app.set_keyword_text(old_secret, new_secret)

    response = await _send_chat(gui, client, f"The plan is {new_secret}.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, new_secret)
    assert _extract_assistant_content(response) == f"The plan is {new_secret}."

    response = await _send_chat(gui, client, f"The plan is {old_secret}.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, old_secret)


@pytest.mark.asyncio
async def test_keyword_case_button(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    secret = "Project Chimera"
    gui.app.add_keyword(secret, case_sensitive=False)

    # Lowercase redacted while case-insensitive.
    await _send_chat(gui, client, "The plan is project chimera.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, "project chimera")

    # Flip to case-sensitive via the row's Case button (persists immediately).
    gui.app.toggle_keyword_case(secret)
    wait_until("case sensitive on", gui.app.keywords,
               lambda ks: ks.get(secret, {}).get("case_sensitive") is True)

    await _send_chat(gui, client, "The plan is project chimera again.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, "project chimera")

    await _send_chat(gui, client, "The plan is Project Chimera exactly.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, "Project Chimera")


@pytest.mark.asyncio
async def test_keyword_delete(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    secret = "Project Chimera"
    gui.app.add_keyword(secret)

    await _send_chat(gui, client, f"The plan is {secret}.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_keyword_redacted(upstream, secret)

    gui.app.delete_keyword(secret)

    response = await _send_chat(gui, client, f"Mission {secret} today.")
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, secret)
    assert _extract_assistant_content(response) == f"Mission {secret} today."


# ---------------------------------------------------------------------------
# 3. Regex patterns
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_regex_add_and_redact(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    gui.app.add_regex(pattern)
    wait_until("regex row added", gui.app.regexes,
               lambda rs: rs.get(pattern) is True)

    request_text = f"The part number is {secret}."
    response = await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    content = _extract_last_user_message(upstream)
    assert secret not in content
    assert "<<REDACTED_REGEX_" in content
    assert _extract_assistant_content(response) == request_text


@pytest.mark.asyncio
async def test_regex_invalid_pattern_dialog(gui: GuiContext) -> None:
    """Adding an invalid pattern shows the validation dialog and adds no row."""
    gui.app.add_regex("[")
    lines = gui.app.dialog_text("Validation Error")
    assert any("Invalid regex syntax." in line for line in lines), lines
    gui.app.dismiss_dialog("OK", title="Validation Error")
    assert "[" not in gui.app.regexes()


@pytest.mark.asyncio
async def test_regex_toggle_and_delete(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    gui.app.add_regex(pattern)
    wait_until("regex row added", gui.app.regexes, lambda rs: pattern in rs)

    request_text = f"The part number is {secret}."
    await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    assert "<<REDACTED_REGEX_" in _extract_last_user_message(upstream)

    # Disable via the row checkbox: the secret passes through again.
    gui.app.toggle_regex(pattern)
    wait_until("regex disabled", gui.app.regexes,
               lambda rs: rs.get(pattern) is False)
    response = await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, secret)
    assert _extract_assistant_content(response) == request_text

    # Re-enable, then delete via the row Delete button.
    gui.app.toggle_regex(pattern)
    wait_until("regex re-enabled", gui.app.regexes,
               lambda rs: rs.get(pattern) is True)
    gui.app.delete_regex(pattern)
    response = await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, secret)


# ---------------------------------------------------------------------------
# 4. PII controls
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_pii_type_toggle(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """Unchecking the Person type (and saving) stops person-name redaction;
    re-enabling restores it."""
    app = gui.app
    app.set_pii_type("private_person", False)
    app.save_profile()

    request_text = "My name is Bob Marley."
    response = await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, "Bob Marley")
    assert _extract_assistant_content(response) == request_text

    app.set_pii_type("private_person", True)
    app.save_profile()

    # Different name so the proxy cannot reuse a cached fragment.
    request_text2 = "My name is Alice Smith."
    response = await _send_chat(gui, client, request_text2)
    upstream = mock_llm.last_request
    assert upstream is not None
    content = _extract_last_user_message(upstream)
    assert "Alice Smith" not in content
    assert "<<REDACTED_PII_" in content
    assert _extract_assistant_content(response) == request_text2


@pytest.mark.asyncio
async def test_pii_master_switch(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """The master 'Use AI model' switch gates all PII detection."""
    app = gui.app
    assert app.pii_master_state() is True

    app.set_pii_master(False)
    app.save_profile()
    assert app.pii_master_state() is False

    request_text = "Contact me at bob.marley@example.com."
    response = await _send_chat(gui, client, request_text)
    upstream = mock_llm.last_request
    assert upstream is not None
    _assert_not_redacted(upstream, "bob.marley@example.com")
    assert _extract_assistant_content(response) == request_text


# ---------------------------------------------------------------------------
# 5. Logging
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_logging_toggle_controls_request_log(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """Enable logging on: requests are written to agent_redactor.log.
    Off: no traffic lines (and the log file is not created on a fresh dir)."""
    app = gui.app
    log_path = gui.config_dir / "agent_redactor.log"

    # Seeded with logging enabled.
    assert app.logging_checked() is True
    await _send_chat(gui, client, "First logged request.")
    wait_until(
        "request logged",
        lambda: gui.log_text(),
        lambda text: "[Upstream]" in text,
    )

    app.set_logging(False)
    wait_until("logging off", app.logging_checked, lambda on: on is False)
    size_after_disable = log_path.stat().st_size

    await _send_chat(gui, client, "Second request must not be logged.")
    time.sleep(2)  # give any (unexpected) log write a chance to land
    assert "Second request must not be logged" not in gui.log_text()
    assert log_path.stat().st_size <= size_after_disable + 4096  # lifecycle lines only

    app.set_logging(True)
    wait_until("logging on", app.logging_checked, lambda on: on is True)
    await _send_chat(gui, client, "Third request logged again.")
    wait_until(
        "logging resumed",
        lambda: gui.log_text(),
        lambda text: "Third request logged again." in text
        or text.count("[Upstream]") >= 2,
    )


@pytest.mark.asyncio
async def test_clear_logs_button(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """'Delete all logs' (with confirmation) removes the request log file."""
    await _send_chat(gui, client, "Log me, then delete me.")
    wait_until("request logged", lambda: gui.log_text(),
               lambda text: "[Upstream]" in text)

    gui.app.clear_logs()

    assert not (gui.config_dir / "agent_redactor.log").exists()
    sessions = gui.config_dir / "sessions"
    assert not sessions.exists() or not any(sessions.iterdir())


# ---------------------------------------------------------------------------
# 6./7. Clear buttons + statistics panel
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_statistics_and_clear_buttons(
    gui: GuiContext, client: aiohttp.ClientSession, mock_llm: MockLLM
) -> None:
    """After proxied traffic the Statistics panel counts the request and the
    keyword match, the Session Redactions list shows the entry, and both Clear
    buttons reset their panels."""
    app = gui.app
    secret = "Project Chimera"
    app.add_keyword(secret)

    await _send_chat(gui, client, f"The plan is {secret}.")

    stats = wait_until(
        "statistics after traffic",
        app.statistics,
        lambda s: "Requests: 1" in s and "Keywords: 1" in s,
    )
    entries = wait_until(
        "session redaction listed",
        app.session_redactions,
        lambda es: any("REDACTED_KEYWORD" in e or secret in e for e in es),
    )

    app.clear_statistics()
    wait_until(
        "statistics cleared",
        app.statistics,
        lambda s: "Requests: 0" in s and "Keywords: 0" in s,
    )

    app.clear_session_redactions()
    wait_until(
        "session redactions cleared",
        app.session_redactions,
        lambda es: not any("REDACTED_KEYWORD" in e or secret in e for e in es),
    )


# ---------------------------------------------------------------------------
# 9. Master password: lock overlay + typed unlock
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_master_password_lock_overlay_and_unlock(gui: GuiContext) -> None:
    """With the typed master password enabled, a fresh GUI start shows only the
    lock overlay; a wrong password is rejected inline; the correct one typed
    into the overlay unlocks the full UI."""
    app = gui.app
    password = "atspi-test-pw"

    r = gui.cli("password", "enable", input=f"{password}\n{password}\n")
    assert r.returncode == 0, r.stdout + r.stderr

    # Restart the GUI: the fresh engine session starts locked. The GUI-owned
    # engine stops asynchronously (fire-and-forget /engine/stop), so wait for
    # it to actually exit — otherwise the new GUI would attach to the old,
    # still-unlocked engine session.
    app.stop()
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if not [p for p in psutil.process_iter(["name"]) if p.info["name"] == "agentredactor"]:
            break
        time.sleep(0.3)
    app.start()
    app.app()

    wait_until("lock overlay shown", app.locked, lambda locked: locked, timeout=60)
    # The content page is hidden while locked (nothing leaks behind it).
    assert not app.find_all("panel", "Profile")
    assert not app.find_all("list", "Profiles")

    # Wrong password: inline error, overlay stays.
    app.unlock("wrong-password")
    wait_until("wrong-password error", app.unlock_error,
               lambda err: err is not None)
    assert app.locked()

    # Correct password: overlay lifts, content appears.
    app.unlock(password)
    app.wait_ready()
    assert not app.locked()
    assert "test-profile" in app.profiles()


# ---------------------------------------------------------------------------
# 10. API key field visibility/masking
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_api_key_masked_and_revealed(gui: GuiContext) -> None:
    """The API key box is a password-echo field by default; the 'Show API key'
    checkbox swaps it to a plain text field and back."""
    app = gui.app
    assert app.api_key_role() == "password text"

    app.toggle_show_api_key()
    wait_until("api key revealed", app.api_key_role, lambda role: role == "text")
    assert app.api_key_text() == "test-api-key"

    app.toggle_show_api_key()
    wait_until("api key masked again", app.api_key_role,
               lambda role: role == "password text")


# ---------------------------------------------------------------------------
# 11. Language switching (CLI-driven, asserted on retranslated a11y strings)
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_language_switch_retranslates_ui(gui: GuiContext) -> None:
    """`set app-language` retranslates the running GUI without a restart:
    German, then Arabic (RTL), then back to English."""
    app = gui.app

    r = gui.cli("set", "app-language", "de")
    assert r.returncode == 0, r.stdout + r.stderr
    wait_until("German UI", lambda: app.find_all("panel", "Profil"),
               lambda nodes: len(nodes) > 0, timeout=20)
    assert app.find_all("panel", "Schlüsselwörter")

    r = gui.cli("set", "app-language", "ar")
    assert r.returncode == 0, r.stdout + r.stderr
    wait_until("Arabic UI", lambda: app.find_all("panel", "حساب تعريفي"),
               lambda nodes: len(nodes) > 0, timeout=20)

    r = gui.cli("set", "app-language", "en")
    assert r.returncode == 0, r.stdout + r.stderr
    wait_until("English UI restored", lambda: app.find_all("panel", "Profile"),
               lambda nodes: len(nodes) > 0, timeout=20)
    assert "test-profile" in app.profiles()
