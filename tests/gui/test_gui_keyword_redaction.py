"""GUI tests for keyword redaction through the Home page Keywords section."""

from __future__ import annotations

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import (
    add_keyword,
    delete_keyword,
    get_debug_log_text,
    get_keywords,
    get_log_text,
    get_session_redactions,
    get_statistics,
    save_profile,
    set_keyword_case,
    set_keyword_text,
    wait_until,
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


def _find_keyword(keywords: list[dict[str, str | bool]], text: str) -> dict[str, str | bool] | None:
    for kw in keywords:
        if kw["text"] == text:
            return kw
    return None


def _assert_redacted(upstream_body: dict, matched_text: str) -> None:
    upstream_content = _extract_last_user_message(upstream_body)
    assert matched_text not in upstream_content
    assert "<<REDACTED_KEYWORD_0>>" in upstream_content


def _assert_not_redacted(upstream_body: dict, expected_text: str) -> None:
    upstream_content = _extract_last_user_message(upstream_body)
    assert expected_text in upstream_content
    assert "<<REDACTED_KEYWORD" not in upstream_content


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
async def test_gui_keyword_not_case_sensitive(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)

    keywords = get_keywords()
    kw = _find_keyword(keywords, secret)
    assert kw is not None
    assert kw["case_sensitive"] is False
    assert kw["enabled"] is True

    request_text = "The plan is project chimera."
    response_json = await _send_chat_request(gui_app, client, request_text)

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, "project chimera")
    final_content = _extract_assistant_content(response_json)
    assert secret in final_content
    assert "<<REDACTED_KEYWORD" not in final_content


@pytest.mark.asyncio
async def test_gui_keyword_case_sensitive(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=True)

    keywords = get_keywords()
    kw = _find_keyword(keywords, secret)
    assert kw is not None
    assert kw["case_sensitive"] is True
    assert kw["enabled"] is True

    exact_text = "The plan is Project Chimera."
    response_json = await _send_chat_request(gui_app, client, exact_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, "Project Chimera")
    _assert_reconstructed(response_json, exact_text)

    different_case_text = "The plan is project chimera."
    response_json2 = await _send_chat_request(gui_app, client, different_case_text)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_not_redacted(upstream_body2, "project chimera")
    _assert_reconstructed(response_json2, different_case_text)


@pytest.mark.asyncio
async def test_gui_keyword_delete(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    assert _find_keyword(get_keywords(), secret) is not None

    before_text = "The plan is Project Chimera."
    response_json = await _send_chat_request(gui_app, client, before_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, secret)
    _assert_reconstructed(response_json, before_text)

    delete_keyword(secret)
    assert _find_keyword(get_keywords(), secret) is None

    after_text = "Mission Project Chimera today."
    response_json2 = await _send_chat_request(gui_app, client, after_text)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_not_redacted(upstream_body2, secret)
    _assert_reconstructed(response_json2, after_text)


@pytest.mark.asyncio
async def test_gui_keyword_modify_case(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    kw = _find_keyword(get_keywords(), secret)
    assert kw is not None
    assert kw["case_sensitive"] is False

    lower_text = "The plan is project chimera."
    response_json = await _send_chat_request(gui_app, client, lower_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, "project chimera")
    final_content = _extract_assistant_content(response_json)
    assert secret in final_content
    assert "<<REDACTED_KEYWORD" not in final_content

    set_keyword_case(secret, case_sensitive=True)
    save_profile()
    kw = _find_keyword(get_keywords(), secret)
    assert kw is not None
    assert kw["case_sensitive"] is True

    response_json2 = await _send_chat_request(gui_app, client, lower_text)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_not_redacted(upstream_body2, "project chimera")
    _assert_reconstructed(response_json2, lower_text)

    exact_text = "The plan is Project Chimera."
    response_json3 = await _send_chat_request(gui_app, client, exact_text)
    upstream_body3 = mock_llm.last_request
    assert upstream_body3 is not None
    _assert_redacted(upstream_body3, "Project Chimera")
    _assert_reconstructed(response_json3, exact_text)


@pytest.mark.asyncio
async def test_gui_keyword_modify_text(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    old_secret = "Project Chimera"
    new_secret = "Alpha Bravo"
    add_keyword(old_secret, case_sensitive=False)
    assert _find_keyword(get_keywords(), old_secret) is not None

    set_keyword_text(old_secret, new_secret)
    save_profile()

    assert _find_keyword(get_keywords(), new_secret) is not None
    assert _find_keyword(get_keywords(), old_secret) is None

    new_text = "The plan is Alpha Bravo."
    response_json = await _send_chat_request(gui_app, client, new_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, new_secret)
    _assert_reconstructed(response_json, new_text)

    old_text = "The plan is Project Chimera."
    response_json2 = await _send_chat_request(gui_app, client, old_text)
    upstream_body2 = mock_llm.last_request
    assert upstream_body2 is not None
    _assert_not_redacted(upstream_body2, old_secret)
    _assert_reconstructed(response_json2, old_text)


@pytest.mark.asyncio
async def test_gui_keyword_stats_and_session(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)

    request_text = "The plan is Project Chimera."
    response_json = await _send_chat_request(gui_app, client, request_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    stats = wait_until(
        "statistics after traffic",
        get_statistics,
        lambda s: "Requests: 1" in s and "Keywords: 1" in s,
    )

    redactions = wait_until(
        "session redactions after traffic",
        get_session_redactions,
        lambda entries: any(secret in entry or "REDACTED_KEYWORD" in entry for entry in entries),
    )


@pytest.mark.asyncio
async def test_gui_keyword_logs(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)

    request_text = "The plan is Project Chimera."
    response_json = await _send_chat_request(gui_app, client, request_text)
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    _assert_redacted(upstream_body, secret)
    _assert_reconstructed(response_json, request_text)

    log_text = get_log_text()
    assert "<<REDACTED_KEYWORD_0>>" in log_text
    debug_text = get_debug_log_text()
    assert "<<REDACTED_KEYWORD_0>>" in debug_text
