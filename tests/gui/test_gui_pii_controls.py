"""GUI tests for PII redaction controls via the UI."""

from __future__ import annotations

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import (
    get_debug_log_text,
    get_log_text,
    get_pii_master_state,
    get_session_redactions,
    get_statistics,
    save_profile,
    set_pii_master,
    set_pii_type,
    set_show_sensitive,
)


OPENAI_PATH = "/v1/chat/completions"
PII_TYPES_ALL = [
    "account_number",
    "private_address",
    "private_date",
    "private_email",
    "private_person",
    "private_phone",
    "private_url",
    "secret",
]


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


def _assert_redacted_pii(upstream_body: dict, matched_text: str) -> None:
    upstream_content = _extract_last_user_message(upstream_body)
    assert matched_text not in upstream_content
    assert "<<REDACTED_PII_" in upstream_content


def _assert_not_redacted_pii(upstream_body: dict, expected_text: str) -> None:
    upstream_content = _extract_last_user_message(upstream_body)
    assert expected_text in upstream_content
    assert "<<REDACTED_PII_" not in upstream_content


def _assert_reconstructed(response_json: dict, expected_content: str) -> None:
    final_content = _extract_assistant_content(response_json)
    assert expected_content == final_content


def _assert_pii_model_bypassed(log_text: str) -> None:
    assert "AFTER_PII_MODEL" not in log_text
    assert "pii_model=0us" in log_text


def _assert_pii_model_ran(log_text: str) -> None:
    assert "AFTER_PII_MODEL" in log_text


@pytest.mark.asyncio
async def test_gui_pii_person_redaction(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Bob Marley"
    for pii_type in PII_TYPES_ALL:
        if pii_type != "private_person":
            set_pii_type(pii_type, enabled=False)
    save_profile()

    request_text = f"My name is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_pii(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)


@pytest.mark.asyncio
async def test_gui_pii_email_redaction(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "bob.marley@example.com"
    for pii_type in PII_TYPES_ALL:
        if pii_type != "private_email":
            set_pii_type(pii_type, enabled=False)
    save_profile()

    request_text = f"Contact me at {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_pii(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)


@pytest.mark.asyncio
async def test_gui_pii_disabled(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    # Categories remain enabled, but turn the master AI/PII switch off.
    set_pii_master(False)
    save_profile()
    assert get_pii_master_state() is False

    secret = "Bob Marley"
    request_text = f"My name is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_not_redacted_pii(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    log_text = get_log_text()
    _assert_pii_model_bypassed(log_text)


@pytest.mark.asyncio
async def test_gui_pii_all_unselected(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    # Master switch stays on, but no individual PII category is selected.
    assert get_pii_master_state() is True
    for pii_type in PII_TYPES_ALL:
        set_pii_type(pii_type, enabled=False)
    save_profile()

    secret = "Bob Marley"
    request_text = f"My name is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_not_redacted_pii(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    log_text = get_log_text()
    _assert_pii_model_bypassed(log_text)


@pytest.mark.asyncio
async def test_gui_pii_toggle_via_ui(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Bob Marley"

    # Toggle Person off and verify no redaction.
    set_pii_type("private_person", enabled=False)
    save_profile()

    mock_llm.reset()
    request_text = f"My name is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    assert secret in _extract_last_user_message(upstream_body)

    # Toggle Person back on and verify redaction occurs.
    # Use different text so the proxy does not reuse the cached fragment from
    # the first request.
    set_pii_type("private_person", enabled=True)
    save_profile()

    mock_llm.reset()
    secret2 = "Alice Smith"
    request_text2 = f"My name is {secret2}."
    response_json2 = await _send_chat_request(gui_app, client, request_text2)

    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_redacted_pii(upstream_body2, secret2)
    _assert_reconstructed(response_json2, request_text2)

    # Session redactions list should contain the match.
    session = get_session_redactions()
    assert any("private_person" in entry or secret2 in entry for entry in session)


@pytest.mark.asyncio
async def test_gui_pii_all_enabled(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    # Default profile has the master switch on and all categories enabled.
    assert get_pii_master_state() is True

    # Pipeline-stage details are only written in show-sensitive mode.
    set_show_sensitive(True)

    secrets = {
        "private_person": "Bob Marley",
        "private_email": "bob.marley@example.com",
        "private_phone": "(555) 123-4567",
        "private_address": "123 Main Street, New York, NY 10001",
        "private_date": "January 1, 1990",
        "private_url": "https://example.com",
        "account_number": "1234567890",
        "secret": "password12345",
    }

    request_text = (
        f"My name is {secrets['private_person']}. "
        f"My email is {secrets['private_email']}. "
        f"My phone is {secrets['private_phone']}. "
        f"I live at {secrets['private_address']}. "
        f"I was born on {secrets['private_date']}. "
        f"My account number is {secrets['account_number']}. "
        f"My website is {secrets['private_url']}. "
        f"My password is {secrets['secret']}."
    )

    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    upstream_content = _extract_last_user_message(upstream_body)
    for secret in secrets.values():
        assert secret not in upstream_content
    assert "<<REDACTED_PII_" in upstream_content

    final_content = _extract_assistant_content(response_json)
    for secret in secrets.values():
        assert secret in final_content

    log_text = get_log_text()
    _assert_pii_model_ran(log_text)


@pytest.mark.asyncio
async def test_gui_pii_stats_and_session(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Bob Marley"

    request_text = f"My name is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_pii(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    stats = get_statistics()
    assert "Requests: 1" in stats
    assert "PII: 1" in stats

    session = get_session_redactions()
    assert any(secret in entry or "private_person" in entry for entry in session)


@pytest.mark.asyncio
async def test_gui_pii_logs(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Bob Marley"

    request_text = f"My name is {secret}."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted_pii(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    log_text = get_log_text()
    assert "<<REDACTED_PII_0>>" in log_text
    debug_text = get_debug_log_text()
    assert "<<REDACTED_PII_0>>" in debug_text
