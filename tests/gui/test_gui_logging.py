"""GUI tests for the Enable logging / Show sensitive information controls."""

from __future__ import annotations

import json
import os
from pathlib import Path

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import (
    add_keyword,
    get_debug_log_text,
    get_is_checked,
    get_is_enabled,
    get_log_text,
    save_profile,
    set_api_key,
    set_enable_logging,
    set_show_sensitive,
)


OPENAI_PATH = "/v1/chat/completions"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _extract_assistant_content(response_json: dict) -> str:
    choices = response_json.get("choices", [])
    if not choices:
        return ""
    return choices[0].get("message", {}).get("content", "")


def _settings_path() -> Path:
    appdata = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    return appdata / "AgentRedactor" / "settings.json"


async def _send_chat_request(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    content: str,
) -> dict:
    payload = _chat_request(content)
    async with client.post(f"{gui_app.proxy_url}{OPENAI_PATH}", json=payload) as resp:
        assert resp.status == 200
        response_json = await resp.json()
    return response_json


@pytest.mark.asyncio
async def test_gui_enable_logging_toggle(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()

    # Turn logging off; the Show sensitive checkbox becomes disabled.
    set_enable_logging(False)
    assert get_is_enabled("ShowSensitiveCheck") is False

    request_text = f"The plan is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)
    assert mock_llm.last_request is not None
    assert _extract_assistant_content(response_json) == request_text

    # Turn logging back on; the sensitive checkbox is enabled again.
    set_enable_logging(True)
    assert get_is_enabled("ShowSensitiveCheck") is True

    response_json = await _send_chat_request(gui_app, client, request_text)
    assert mock_llm.last_request is not None
    assert _extract_assistant_content(response_json) == request_text


@pytest.mark.asyncio
async def test_gui_logging_api_key_not_leaked(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    distinctive_key = "sk-test-no-leak-98765"

    # Set a distinctive API key; logging on, sensitive off.
    set_api_key(distinctive_key)
    save_profile()
    set_enable_logging(True)

    await _send_chat_request(gui_app, client, "Check the logs for secrets.")

    log_text = get_log_text()
    debug_text = get_debug_log_text()
    # The Authorization header should be redacted in both log files.
    assert distinctive_key not in log_text
    assert distinctive_key not in debug_text
    assert "<REDACTED>" in log_text


@pytest.mark.asyncio
async def test_gui_sensitive_content_logged_when_enabled(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()
    set_enable_logging(True)
    set_show_sensitive(True)

    await _send_chat_request(gui_app, client, f"The plan is {secret}.")

    log_text = get_log_text()
    # Sensitive logging logs the raw text that was redacted.
    assert "[REDACTED: keyword]" in log_text
    assert secret in log_text


@pytest.mark.asyncio
async def test_gui_sensitive_content_not_logged_when_disabled(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()

    # Logging on, sensitive off (the default at app start).
    set_enable_logging(True)

    await _send_chat_request(gui_app, client, f"The plan is {secret}.")

    log_text = get_log_text()
    debug_text = get_debug_log_text()
    # Without sensitive mode, raw values and redaction details stay out of the logs.
    assert secret not in log_text
    assert "[REDACTED:" not in log_text
    assert secret not in debug_text
    assert "[REDACTED:" not in debug_text
    # The redacted body is still logged.
    assert "<<REDACTED_KEYWORD_0>>" in log_text


@pytest.mark.asyncio
async def test_gui_logging_disabled_writes_no_traffic(
    gui_app_logging_disabled: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    gui_app = gui_app_logging_disabled
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()

    await _send_chat_request(gui_app, client, f"The plan is {secret}.")
    assert mock_llm.last_request is not None

    log_text = get_log_text()
    # With logging off, no traffic or redaction labels are written...
    assert "[Upstream]" not in log_text
    assert "<<REDACTED" not in log_text
    # ...but lifecycle lines still are.
    assert "Started proxy on port" in log_text
    # The debug traffic log gets nothing (or does not exist).
    debug_text = get_debug_log_text()
    assert "CLIENT_IN" not in debug_text
    assert "UPSTREAM_" not in debug_text


@pytest.mark.asyncio
async def test_gui_logging_metadata_only_client_boundaries(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()

    # Logging on, sensitive off (the default at app start).
    set_enable_logging(True)

    await _send_chat_request(gui_app, client, f"The plan is {secret}.")

    debug_text = get_debug_log_text()
    # Client-boundary lines are metadata-only: no raw client body, no final
    # unredacted response body.
    client_in_lines = [line for line in debug_text.splitlines() if "[CLIENT_IN]" in line]
    assert client_in_lines
    assert any("bytes (body omitted)" in line for line in client_in_lines)
    client_out_lines = [line for line in debug_text.splitlines() if "[CLIENT_OUT]" in line]
    assert client_out_lines
    assert any("bytes (body omitted)" in line for line in client_out_lines)
    assert secret not in debug_text
    # The redacted body is present at UPSTREAM_OUT.
    upstream_out = debug_text.partition("[UPSTREAM_OUT]")[2]
    assert upstream_out
    assert "<<REDACTED_KEYWORD_0>>" in upstream_out


@pytest.mark.asyncio
async def test_gui_sensitive_resets_on_restart(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()
    set_enable_logging(True)
    set_show_sensitive(True)

    await _send_chat_request(gui_app, client, f"The plan is {secret}.")
    assert secret in get_log_text()

    # Restart the app with the same settings; show-sensitive is session-only.
    gui_app.stop()
    gui_app.start()

    assert get_is_checked("ShowSensitiveCheck") is False

    # The previous log was rotated away at startup, so the current log file
    # only contains lines written after the restart.
    await _send_chat_request(gui_app, client, f"The plan is {secret} again.")
    log_text = get_log_text()
    assert secret not in log_text
    assert "[REDACTED:" not in log_text


@pytest.mark.asyncio
async def test_gui_enable_logging_persists_on_restart(
    gui_app_logging_disabled: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    gui_app = gui_app_logging_disabled

    # Enable logging through the UI and verify traffic is logged.
    set_enable_logging(True)

    await _send_chat_request(gui_app, client, "First request.")
    assert "[Upstream] Request" in get_log_text()

    # Restart the app; Enable logging is persisted.
    gui_app.stop()
    gui_app.start()

    await _send_chat_request(gui_app, client, "Second request.")
    assert "[Upstream] Request" in get_log_text()

    settings = json.loads(_settings_path().read_text(encoding="utf-8"))
    assert settings.get("logging_enabled") is True


@pytest.mark.asyncio
async def test_gui_verbose_setting_migrates(
    gui_app_legacy_verbose_logging: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    gui_app = gui_app_legacy_verbose_logging

    # The legacy verbose_logging: true setting enables traffic logging.
    await _send_chat_request(gui_app, client, "Migration check.")
    assert "[Upstream] Request" in get_log_text()

    settings = json.loads(_settings_path().read_text(encoding="utf-8"))
    assert settings.get("logging_enabled") is True
    assert "verbose_logging" not in settings
