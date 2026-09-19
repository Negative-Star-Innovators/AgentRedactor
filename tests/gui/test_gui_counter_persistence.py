"""Placeholder label counters must survive an app restart (Windows GUI suite).

The same keyword redacted before and after a full app restart must get
<<REDACTED_KEYWORD_1>> the second time — never a reissued
<<REDACTED_KEYWORD_0>> — otherwise a label still referenced by provider-side
conversation state could be silently re-mapped to different PII.
"""

from __future__ import annotations

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import add_keyword

OPENAI_PATH = "/v1/chat/completions"
SECRET = "Counter Persistence Canary Seven"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _last_user_content(request_body: dict) -> str:
    messages = request_body.get("messages", [])
    if not messages:
        return ""
    return messages[-1].get("content", "")


def _assistant_content(response_json: dict) -> str:
    choices = response_json.get("choices", [])
    if not choices:
        return ""
    return choices[0].get("message", {}).get("content", "")


async def _send_chat_request(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    content: str,
) -> dict:
    async with client.post(f"{gui_app.proxy_url}{OPENAI_PATH}", json=_chat_request(content)) as resp:
        assert resp.status == 200
        return await resp.json()


@pytest.mark.asyncio
async def test_label_counter_persists_across_app_restart(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    add_keyword(SECRET, case_sensitive=False)

    response = await _send_chat_request(gui_app, client, SECRET)
    upstream = mock_llm.last_request
    assert upstream is not None
    content = _last_user_content(upstream)
    assert "<<REDACTED_KEYWORD_0>>" in content
    assert SECRET not in content
    # The echoed label is restored to the original for the client.
    assert _assistant_content(response) == SECRET

    # Full app restart (GUI + engine). The keyword config persists in
    # settings.json; the in-memory label maps do not.
    gui_app.stop()
    gui_app.start()

    response = await _send_chat_request(gui_app, client, SECRET)
    upstream = mock_llm.last_request
    assert upstream is not None
    content = _last_user_content(upstream)
    assert "<<REDACTED_KEYWORD_1>>" in content
    assert "<<REDACTED_KEYWORD_0>>" not in content
    assert SECRET not in content
    assert _assistant_content(response) == SECRET
