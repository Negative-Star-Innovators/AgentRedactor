"""GUI tests for regex redaction through the Home page Regex section."""

from __future__ import annotations

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import (
    add_regex,
    delete_regex,
    get_debug_log_text,
    get_log_text,
    get_regexes,
    get_session_redactions,
    get_statistics,
    save_profile,
    set_regex_text,
    toggle_regex,
)


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
    return messages[-1].get("content", "")


def _extract_assistant_content(response_json: dict) -> str:
    choices = response_json.get("choices", [])
    if not choices:
        return ""
    return choices[0].get("message", {}).get("content", "")


def _find_regex(regexes: list[dict[str, str | bool]], pattern: str) -> dict[str, str | bool] | None:
    for r in regexes:
        if r["pattern"] == pattern:
            return r
    return None


def _assert_redacted_regex(upstream_body: dict, matched_text: str) -> None:
    upstream_content = _extract_last_user_message(upstream_body)
    assert matched_text not in upstream_content
    assert "<<REDACTED_REGEX_0>>" in upstream_content


def _assert_not_redacted_regex(upstream_body: dict, expected_text: str) -> None:
    upstream_content = _extract_last_user_message(upstream_body)
    assert expected_text in upstream_content
    assert "<<REDACTED_REGEX" not in upstream_content


def _assert_reconstructed(response_json: dict, expected_content: str) -> None:
    final_content = _extract_assistant_content(response_json)
    assert expected_content == final_content


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
async def test_gui_regex_redaction(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    add_regex(pattern)

    regexes = get_regexes()
    r = _find_regex(regexes, pattern)
    assert r is not None
    assert r["enabled"] is True

    request_text = f"The part number is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_regex(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)


@pytest.mark.asyncio
async def test_gui_regex_disabled(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    add_regex(pattern)

    request_text = f"The part number is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_regex(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    toggle_regex(pattern)
    save_profile()
    r = _find_regex(get_regexes(), pattern)
    assert r is not None
    assert r["enabled"] is False

    response_json2 = await _send_chat_request(gui_app, client, request_text)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_not_redacted_regex(upstream_body2, secret)
    _assert_reconstructed(response_json2, request_text)

    toggle_regex(pattern)
    save_profile()
    assert _find_regex(get_regexes(), pattern)["enabled"] is True

    response_json3 = await _send_chat_request(gui_app, client, request_text)
    upstream_body3 = mock_llm.last_request
    assert upstream_body3 is not None
    _assert_redacted_regex(upstream_body3, secret)
    _assert_reconstructed(response_json3, request_text)


@pytest.mark.asyncio
async def test_gui_regex_modify(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    old_secret = "PN-12345"
    old_pattern = r"\bPN-\d{5}\b"
    new_secret = "token-abcd"
    new_pattern = r"\btoken-[a-z]{4}\b"
    add_regex(old_pattern)

    old_request = f"The part number is {old_secret}."
    response_json = await _send_chat_request(gui_app, client, old_request)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_regex(upstream_body, old_secret)
    _assert_reconstructed(response_json, old_request)

    set_regex_text(old_pattern, new_pattern)
    save_profile()

    assert _find_regex(get_regexes(), new_pattern) is not None
    assert _find_regex(get_regexes(), old_pattern) is None

    new_request = f"The serial is {new_secret}."
    response_json2 = await _send_chat_request(gui_app, client, new_request)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_redacted_regex(upstream_body2, new_secret)
    _assert_reconstructed(response_json2, new_request)

    response_json3 = await _send_chat_request(gui_app, client, old_request)
    upstream_body3 = mock_llm.last_request
    assert upstream_body3 is not None
    _assert_not_redacted_regex(upstream_body3, old_secret)
    _assert_reconstructed(response_json3, old_request)


@pytest.mark.asyncio
async def test_gui_regex_delete(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    add_regex(pattern)

    request_text = f"The part number is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_regex(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    delete_regex(pattern)
    assert _find_regex(get_regexes(), pattern) is None

    response_json2 = await _send_chat_request(gui_app, client, request_text)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_not_redacted_regex(upstream_body2, secret)
    _assert_reconstructed(response_json2, request_text)


@pytest.mark.asyncio
async def test_gui_regex_stats_and_session(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    add_regex(pattern)

    request_text = f"The part number is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_regex(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    stats = get_statistics()
    assert "Requests: 1" in stats
    assert "Regex: 1" in stats

    redactions = get_session_redactions()
    assert any(secret in entry or "REDACTED_REGEX" in entry for entry in redactions)


@pytest.mark.asyncio
async def test_gui_regex_logs(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "PN-12345"
    pattern = r"\bPN-\d{5}\b"
    add_regex(pattern)

    request_text = f"The part number is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_regex(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    log_text = get_log_text()
    assert "<<REDACTED_REGEX_0>>" in log_text
    debug_text = get_debug_log_text()
    assert "<<REDACTED_REGEX_0>>" in debug_text
