"""GUI tests for profile management through the UI."""

from __future__ import annotations

import asyncio
import socket
import time

import aiohttp
import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from gui_process import _find_free_port as find_free_port
from windows.gui_driver import (
    add_profile,
    get_profiles,
    remove_profile,
    save_profile,
    select_profile,
    set_api_key,
    set_forward_url,
    set_port,
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


def _extract_last_user_content(request_body: dict) -> str:
    messages = request_body.get("messages", [])
    if not messages:
        return ""
    return messages[-1].get("content", "")


def _wait_for_port(port: int, timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"Port {port} did not start listening")


def _wait_for_port_closed(port: int, timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                time.sleep(0.1)
        except OSError:
            return
    raise RuntimeError(f"Port {port} is still listening after removal")


@pytest.mark.asyncio
async def test_gui_add_second_profile(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    second_port = find_free_port()

    add_profile("second-profile")
    set_forward_url(mock_llm.base_url)
    set_api_key("test-api-key-second")
    set_port(second_port)
    save_profile()

    _wait_for_port(second_port)

    secret = "Alpha"
    payload = _chat_request(f"Keyword {secret} should pass through.")
    async with client.post(f"http://127.0.0.1:{second_port}{OPENAI_PATH}", json=payload) as resp:
        assert resp.status == 200
        response_json = await resp.json()

    assert secret in _extract_assistant_content(response_json)


@pytest.mark.asyncio
async def test_gui_second_profile_request_isolation(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    second_port = find_free_port()

    add_profile("second-profile")
    set_forward_url(mock_llm.base_url)
    set_api_key("test-api-key-second")
    set_port(second_port)
    save_profile()

    _wait_for_port(second_port)

    # Send a request through the default (first) profile.
    first_secret = "FirstProfileSecret"
    async with client.post(
        f"{gui_app.proxy_url}{OPENAI_PATH}",
        json=_chat_request(first_secret),
    ) as resp:
        assert resp.status == 200
        await resp.json()

    # Reset the mock so only the second-profile request will be recorded.
    mock_llm.reset()

    # Send a request through the second profile.
    second_secret = "SecondProfileSecret"
    async with client.post(
        f"http://127.0.0.1:{second_port}{OPENAI_PATH}",
        json=_chat_request(second_secret),
    ) as resp:
        assert resp.status == 200
        await resp.json()

    # The mock should have received exactly one request, and it must be from
    # the second profile.
    assert len(mock_llm.requests) == 1
    assert len(mock_llm.paths) == 1
    upstream_body = mock_llm.last_request
    assert upstream_body is not None
    assert first_secret not in _extract_last_user_content(upstream_body)
    assert second_secret in _extract_last_user_content(upstream_body)


@pytest.mark.asyncio
async def test_gui_two_profiles_concurrent_requests(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    second_port = find_free_port()

    add_profile("second-profile")
    set_forward_url(mock_llm.base_url)
    set_api_key("test-api-key-second")
    set_port(second_port)
    save_profile()

    _wait_for_port(second_port)

    first_secret = "ConcurrentFirst"
    second_secret = "ConcurrentSecond"

    async def _send_to_port(port: int, secret: str) -> dict:
        async with client.post(
            f"http://127.0.0.1:{port}{OPENAI_PATH}",
            json=_chat_request(secret),
        ) as resp:
            assert resp.status == 200
            return await resp.json()

    results = await asyncio.gather(
        _send_to_port(gui_app.proxy_port, first_secret),
        _send_to_port(second_port, second_secret),
    )

    # Both clients should see their original secrets reconstructed.
    assert first_secret in _extract_assistant_content(results[0])
    assert second_secret in _extract_assistant_content(results[1])

    # Both requests should have reached the upstream mock.
    assert len(mock_llm.requests) == 2
    upstream_contents = [_extract_last_user_content(req) for req in mock_llm.requests]
    assert all(secret in upstream_contents for secret in (first_secret, second_secret))


@pytest.mark.asyncio
async def test_gui_remove_profile(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    second_port = find_free_port()

    add_profile("second-profile")
    set_forward_url(mock_llm.base_url)
    set_api_key("test-api-key-second")
    set_port(second_port)
    save_profile()

    _wait_for_port(second_port)

    # Sanity-check that the second profile works before removing it.
    async with client.post(
        f"http://127.0.0.1:{second_port}{OPENAI_PATH}",
        json=_chat_request("pre-remove"),
    ) as resp:
        assert resp.status == 200

    # Remove the second profile through the UI.
    remove_profile("second-profile")

    # The profile list should now contain only the default profile.
    profiles = get_profiles()
    assert "second-profile" not in profiles

    # The second port should stop listening.
    _wait_for_port_closed(second_port)

    # The default profile should still work.
    async with client.post(
        f"{gui_app.proxy_url}{OPENAI_PATH}",
        json=_chat_request("default still works"),
    ) as resp:
        assert resp.status == 200
        response_json = await resp.json()
    assert "default still works" in _extract_assistant_content(response_json)
