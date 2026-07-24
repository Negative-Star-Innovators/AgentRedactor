"""A tiny aiohttp mock LLM that echoes redacted text back to the proxy."""

from __future__ import annotations

import json
from collections.abc import AsyncIterator
from typing import Any

from aiohttp import web


def _openai_response(message_content: str) -> dict[str, Any]:
    return {
        "id": "mock-chatcmpl-test",
        "object": "chat.completion",
        "created": 0,
        "model": "mock-model",
        "choices": [
            {
                "index": 0,
                "message": {
                    "role": "assistant",
                    "content": message_content,
                },
                "finish_reason": "stop",
            }
        ],
        "usage": {
            "prompt_tokens": 0,
            "completion_tokens": 0,
            "total_tokens": 0,
        },
    }


def _openai_stream(message_content: str) -> str:
    """Return a text/event-stream body for OpenAI chat completions streaming."""
    lines: list[str] = []
    chunk = {
        "id": "mock-chatcmpl-test",
        "object": "chat.completion.chunk",
        "created": 0,
        "model": "mock-model",
        "choices": [
            {
                "index": 0,
                "delta": {"role": "assistant", "content": message_content},
                "finish_reason": None,
            }
        ],
    }
    lines.append(f"data: {json.dumps(chunk)}")
    done_chunk = {
        "id": "mock-chatcmpl-test",
        "object": "chat.completion.chunk",
        "created": 0,
        "model": "mock-model",
        "choices": [
            {
                "index": 0,
                "delta": {},
                "finish_reason": "stop",
            }
        ],
    }
    lines.append(f"data: {json.dumps(done_chunk)}")
    lines.append("data: [DONE]")
    lines.append("")
    return "\n".join(lines)


def _anthropic_response(message_content: str) -> dict[str, Any]:
    return {
        "id": "msg_0123456789abcdef01234567",
        "type": "message",
        "role": "assistant",
        "content": [
            {"type": "text", "text": message_content},
        ],
        "model": "mock-model",
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 0,
            "output_tokens": 0,
        },
    }


def _anthropic_stream(message_content: str) -> str:
    """Return a text/event-stream body for Anthropic messages streaming."""
    lines: list[str] = []

    def _event(event_type: str, data: dict[str, Any]) -> None:
        lines.append(f"event: {event_type}")
        lines.append(f"data: {json.dumps(data)}")
        lines.append("")

    _event(
        "message_start",
        {
            "type": "message_start",
            "message": {
                "id": "msg_mock_test",
                "type": "message",
                "role": "assistant",
                "content": [],
                "model": "mock-model",
                "stop_reason": None,
                "usage": {"input_tokens": 0, "output_tokens": 0},
            },
        },
    )
    _event(
        "content_block_start",
        {
            "type": "content_block_start",
            "index": 0,
            "content_block": {"type": "text", "text": ""},
        },
    )
    _event(
        "content_block_delta",
        {
            "type": "content_block_delta",
            "index": 0,
            "delta": {"type": "text_delta", "text": message_content},
        },
    )
    _event(
        "content_block_stop",
        {
            "type": "content_block_stop",
            "index": 0,
        },
    )
    _event(
        "message_delta",
        {
            "type": "message_delta",
            "delta": {"stop_reason": "end_turn"},
            "usage": {"output_tokens": 0},
        },
    )
    _event("message_stop", {"type": "message_stop"})
    lines.append("")
    return "\n".join(lines)


def _responses_response(message_content: str) -> dict[str, Any]:
    output_item = {
        "id": "msg_mock_test",
        "type": "message",
        "role": "assistant",
        "status": "completed",
        "content": [
            {"type": "output_text", "text": message_content, "annotations": []},
        ],
    }
    return {
        "id": "resp_mock_test",
        "object": "response",
        "created_at": 0,
        "status": "completed",
        "model": "mock-model",
        "output": [output_item],
        "output_text": message_content,
        "usage": {
            "input_tokens": 0,
            "output_tokens": 0,
            "total_tokens": 0,
        },
    }


def _responses_stream(message_content: str) -> str:
    """Return a text/event-stream body for OpenAI Responses API streaming."""
    lines: list[str] = []

    def _event(event_type: str, data: dict[str, Any]) -> None:
        lines.append(f"event: {event_type}")
        lines.append(f"data: {json.dumps(data)}")
        lines.append("")

    response_obj = _responses_response(message_content)
    output_item = response_obj["output"][0]
    empty_response_obj = {**response_obj, "output": []}

    _event(
        "response.created",
        {"type": "response.created", "response": empty_response_obj},
    )
    _event(
        "response.in_progress",
        {"type": "response.in_progress", "response": empty_response_obj},
    )
    _event(
        "response.output_item.added",
        {"type": "response.output_item.added", "output_index": 0, "item": output_item},
    )
    _event(
        "response.content_part.added",
        {
            "type": "response.content_part.added",
            "output_index": 0,
            "content_index": 0,
            "part": {"type": "output_text", "text": ""},
        },
    )
    _event(
        "response.output_text.delta",
        {
            "type": "response.output_text.delta",
            "output_index": 0,
            "content_index": 0,
            "delta": message_content,
        },
    )
    _event(
        "response.content_part.done",
        {
            "type": "response.content_part.done",
            "output_index": 0,
            "content_index": 0,
            "part": {"type": "output_text", "text": message_content, "annotations": []},
        },
    )
    _event(
        "response.output_item.done",
        {"type": "response.output_item.done", "output_index": 0, "item": output_item},
    )
    _event(
        "response.completed",
        {"type": "response.completed", "response": response_obj},
    )
    # Ensure the terminal SSE event is properly terminated.
    lines.append("")
    return "\n".join(lines)


def _extract_last_user_content(body: dict[str, Any]) -> str:
    messages = body.get("messages", [])
    if not messages:
        return ""
    content = messages[-1].get("content", "")
    if isinstance(content, list) and content:
        # Anthropic-style content blocks or OpenAI Responses input blocks
        texts = [
            block.get("text", "")
            for block in content
            if isinstance(block, dict) and block.get("type") in ("text", "input_text")
        ]
        return "".join(texts)
    return str(content)


def _extract_responses_input_text(body: dict[str, Any]) -> str:
    """Extract the last user text from an OpenAI Responses API request body."""
    input_value = body.get("input", "")
    if isinstance(input_value, list) and input_value:
        last = input_value[-1]
        if isinstance(last, dict):
            content = last.get("content", "")
            if isinstance(content, list) and content:
                texts = [
                    block.get("text", "")
                    for block in content
                    if isinstance(block, dict)
                    and block.get("type") in ("text", "input_text")
                ]
                return "".join(texts)
            return str(content)
        return str(last)
    return str(input_value)


class MockLLM:
    """Records incoming requests and echoes the redacted body back."""

    def __init__(self) -> None:
        self.requests: list[dict[str, Any]] = []
        self.paths: list[str] = []
        self.app = web.Application()
        self.app.router.add_post("/v1/chat/completions", self._handle_chat_completions)
        self.app.router.add_post("/v1/messages", self._handle_anthropic_messages)
        self.app.router.add_post("/v1/responses", self._handle_responses)

    @property
    def last_request(self) -> dict[str, Any] | None:
        return self.requests[-1] if self.requests else None

    @property
    def last_path(self) -> str | None:
        return self.paths[-1] if self.paths else None

    def reset(self) -> None:
        self.requests.clear()
        self.paths.clear()

    async def _handle_chat_completions(self, request: web.Request) -> web.Response:
        body = await request.json()
        self.requests.append(body)
        self.paths.append("/v1/chat/completions")

        content = _extract_last_user_content(body)
        if body.get("stream"):
            stream_body = _openai_stream(content)
            return web.Response(
                status=200,
                content_type="text/event-stream",
                body=stream_body,
                headers={"Content-Length": str(len(stream_body.encode("utf-8")))},
            )
        response = _openai_response(content)
        return web.Response(
            status=200,
            content_type="application/json",
            body=json.dumps(response),
        )

    async def _handle_anthropic_messages(self, request: web.Request) -> web.Response:
        body = await request.json()
        self.requests.append(body)
        self.paths.append("/v1/messages")

        content = _extract_last_user_content(body)
        if body.get("stream"):
            stream_body = _anthropic_stream(content)
            return web.Response(
                status=200,
                content_type="text/event-stream",
                body=stream_body,
                headers={"Content-Length": str(len(stream_body.encode("utf-8")))},
            )
        response = _anthropic_response(content)
        return web.Response(
            status=200,
            content_type="application/json",
            body=json.dumps(response),
        )

    async def _handle_responses(self, request: web.Request) -> web.Response:
        body = await request.json()
        self.requests.append(body)
        self.paths.append("/v1/responses")

        content = _extract_responses_input_text(body)
        if body.get("stream"):
            stream_body = _responses_stream(content)
            return web.Response(
                status=200,
                content_type="text/event-stream",
                body=stream_body,
                headers={"Content-Length": str(len(stream_body.encode("utf-8")))},
            )
        response = _responses_response(content)
        return web.Response(
            status=200,
            content_type="application/json",
            body=json.dumps(response),
        )

    async def __aenter__(self) -> "MockLLM":
        self.runner = web.AppRunner(self.app)
        await self.runner.setup()
        # Bind to 127.0.0.1 on an OS-assigned port
        self.site = web.TCPSite(self.runner, "127.0.0.1", 0)
        await self.site.start()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.runner.cleanup()

    @property
    def base_url(self) -> str:
        """Return the base URL of the running mock server."""
        # site.name is like 'http://127.0.0.1:54321'
        return self.site.name


async def mock_llm_context() -> AsyncIterator[MockLLM]:
    """Async context manager usable as a pytest fixture."""
    async with MockLLM() as llm:
        yield llm
