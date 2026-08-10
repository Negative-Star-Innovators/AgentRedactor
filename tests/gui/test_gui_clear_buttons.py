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
    wait_until,
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

    stats = wait_until(
        "statistics after traffic",
        get_statistics,
        lambda s: _extract_stat(s, "Requests") >= 1 and _extract_stat(s, "Keywords") >= 1,
    )

    clear_statistics()

    stats = wait_until(
        "statistics cleared",
        get_statistics,
        lambda s: _extract_stat(s, "Requests") == 0
        and _extract_stat(s, "Keywords") == 0
        and _extract_stat(s, "PII") == 0
        and _extract_stat(s, "Regex") == 0,
    )


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

    session = wait_until(
        "session redactions after traffic",
        get_session_redactions,
        lambda entries: any(secret in entry or "REDACTED_KEYWORD" in entry for entry in entries),
    )

    clear_session_redactions()

    session = wait_until(
        "session redactions cleared",
        get_session_redactions,
        lambda entries: not any(
            secret in entry or "REDACTED_KEYWORD" in entry for entry in entries
        ),
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

    log_text = wait_until(
        "request logged",
        get_log_text,
        lambda text: "[Upstream] Request" in text,
    )

    clear_logs()

    log_text = wait_until(
        "logs cleared",
        get_log_text,
        lambda text: "[Upstream] Request" not in text and len(text.strip()) == 0,
    )
