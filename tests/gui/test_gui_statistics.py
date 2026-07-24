"""GUI test: verify statistics and session redactions are reflected in the UI."""

from __future__ import annotations

import re
import time

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import add_keyword, get_session_redactions, get_statistics


OPENAI_PATH = "/v1/chat/completions"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _extract_stat(stats_text: str, label: str) -> int:
    match = re.search(rf"{label}:\s*(\d+)", stats_text)
    if not match:
        raise AssertionError(f"Could not find '{label}' in stats: {stats_text}")
    return int(match.group(1))


@pytest.mark.asyncio
async def test_gui_statistics_and_session_redactions(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    start = time.monotonic()

    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)

    payload = _chat_request(f"The plan is {secret}.")
    async with client.post(f"{gui_app.proxy_url}{OPENAI_PATH}", json=payload) as resp:
        assert resp.status == 200
        await resp.json()

    stats = get_statistics()
    print(f"\n[GUI TEST] statistics: {stats}")
    assert _extract_stat(stats, "Requests") >= 1
    assert _extract_stat(stats, "Keywords") >= 1

    session = get_session_redactions()
    print(f"[GUI TEST] session redactions: {session}")
    assert any(secret in entry or "REDACTED_KEYWORD" in entry for entry in session)

    elapsed = time.monotonic() - start
    print(f"\n[GUI TEST] test_gui_statistics_and_session_redactions took {elapsed:.2f}s")
    print("[GUI TEST] Test complete; pausing for observation before teardown...")
    time.sleep(2)
