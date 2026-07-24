"""GUI tests for the Clear Statistics / Clear Session / Clear Logs buttons."""

from __future__ import annotations

import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import (
    add_keyword,
    clear_logs,
    clear_session_redactions,
    clear_statistics,
    get_log_text,
    get_session_redactions,
    get_statistics,
    save_profile,
    set_enable_logging,
)


OPENAI_PATH = "/v1/chat/completions"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _extract_stat(stats_text: str, label: str) -> int:
    import re

    match = re.search(rf"{label}:\s*(\d+)", stats_text)
    if not match:
        raise AssertionError(f"Could not find '{label}' in stats: {stats_text}")
    return int(match.group(1))


@pytest.mark.asyncio
async def test_gui_clear_statistics(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()

    async with client.post(f"{gui_app.proxy_url}{OPENAI_PATH}", json=_chat_request(f"The plan is {secret}.")) as resp:
        assert resp.status == 200
        await resp.json()

    stats = get_statistics()
    assert _extract_stat(stats, "Requests") >= 1
    assert _extract_stat(stats, "Keywords") >= 1

    clear_statistics()

    stats = get_statistics()
    assert _extract_stat(stats, "Requests") == 0
    assert _extract_stat(stats, "Keywords") == 0
    assert _extract_stat(stats, "PII") == 0
    assert _extract_stat(stats, "Regex") == 0


@pytest.mark.asyncio
async def test_gui_clear_session_redactions(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)
    save_profile()

    async with client.post(f"{gui_app.proxy_url}{OPENAI_PATH}", json=_chat_request(f"The plan is {secret}.")) as resp:
        assert resp.status == 200
        await resp.json()

    session = get_session_redactions()
    assert any(secret in entry or "REDACTED_KEYWORD" in entry for entry in session)

    clear_session_redactions()

    session = get_session_redactions()
    assert not any(
        secret in entry or "REDACTED_KEYWORD" in entry for entry in session
    )


@pytest.mark.asyncio
async def test_gui_clear_logs(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    # Ensure logging is enabled and clear any pre-existing log.
    set_enable_logging(False)
    set_enable_logging(True)

    async with client.post(f"{gui_app.proxy_url}{OPENAI_PATH}", json=_chat_request("Hello, logs.")) as resp:
        assert resp.status == 200
        await resp.json()

    log_text = get_log_text()
    assert "[Upstream] Request" in log_text

    clear_logs()

    log_text = get_log_text()
    assert "[Upstream] Request" not in log_text
    assert len(log_text.strip()) == 0
