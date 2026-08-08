"""GUI tests for the OpenAI Responses API (/v1/responses) and upstream auth headers."""

from __future__ import annotations

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import add_keyword


RESPONSES_PATH = "/v1/responses"


def _responses_request(content: str, stream: bool) -> dict:
    return {
        "model": "mock-model",
        "input": [
            {
                "role": "user",
                "content": [{"type": "input_text", "text": content}],
            }
        ],
        "stream": stream,
    }


def _extract_input_text(request_body: dict) -> str:
    input_value = request_body.get("input", [])
    if isinstance(input_value, list) and input_value:
        content = input_value[-1].get("content", [])
        if isinstance(content, list):
            return "".join(
                block.get("text", "")
                for block in content
                if isinstance(block, dict) and block.get("type") == "input_text"
            )
    return ""


@pytest.mark.asyncio
async def test_gui_responses_streaming_keyword_reconstructed(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    """Responses API SSE: redacted upstream, reconstructed in every streamed field."""
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)

    request_text = "The plan is project chimera."
    async with client.post(
        f"{gui_app.proxy_url}{RESPONSES_PATH}",
        json=_responses_request(request_text, stream=True),
    ) as resp:
        assert resp.status == 200
        body = await resp.text()

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    upstream_text = _extract_input_text(upstream_body)
    assert "project chimera" not in upstream_text
    assert "<<REDACTED_KEYWORD_0>>" in upstream_text

    # No label may leak through any streamed event.
    assert "<<REDACTED" not in body
    # The top-level string "delta" field of response.output_text.delta events
    # and the nested "text" fields of done/completed snapshot events are both
    # reconstructed.
    assert f'"delta":"The plan is {secret}."' in body
    assert f'"text":"The plan is {secret}."' in body


@pytest.mark.asyncio
async def test_gui_responses_non_streaming_keyword_reconstructed(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    """Responses API JSON: redacted upstream, reconstructed in the response body."""
    secret = "Project Chimera"
    add_keyword(secret, case_sensitive=False)

    async with client.post(
        f"{gui_app.proxy_url}{RESPONSES_PATH}",
        json=_responses_request("The plan is project chimera.", stream=False),
    ) as resp:
        assert resp.status == 200
        response_json = await resp.json()

    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    assert "<<REDACTED_KEYWORD_0>>" in _extract_input_text(upstream_body)

    assert response_json["output_text"] == f"The plan is {secret}."
    content_text = response_json["output"][0]["content"][0]["text"]
    assert content_text == f"The plan is {secret}."


@pytest.mark.asyncio
async def test_gui_client_credential_headers_get_real_key(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    """Placeholder credentials sent by the client are replaced with the profile key.

    Anthropic-style agents send x-api-key (e.g. Claude Code's dummy
    ANTHROPIC_AUTH_TOKEN); OpenAI-style agents send Authorization: Bearer. Both
    styles must reach the upstream with the real key, in the client's style.
    """
    headers = {
        "Authorization": "Bearer dummy-key-to-bypass-login",
        "x-api-key": "dummy-key-to-bypass-login",
    }
    async with client.post(
        f"{gui_app.proxy_url}{RESPONSES_PATH}",
        json=_responses_request("hello", stream=False),
        headers=headers,
    ) as resp:
        assert resp.status == 200

    sent = mock_llm.last_request_headers
    assert sent is not None
    assert sent.get("Authorization") == "Bearer test-api-key"
    assert sent.get("x-api-key") == "test-api-key"


@pytest.mark.asyncio
async def test_gui_no_client_credentials_defaults_to_bearer(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    """Without client credential headers the proxy adds Authorization: Bearer."""
    async with client.post(
        f"{gui_app.proxy_url}{RESPONSES_PATH}",
        json=_responses_request("hello", stream=False),
    ) as resp:
        assert resp.status == 200

    sent = mock_llm.last_request_headers
    assert sent is not None
    assert sent.get("Authorization") == "Bearer test-api-key"
    assert "x-api-key" not in sent
