"""Threaded HTTP mock LLM for third-party redaction tests.

This implementation uses Python's stdlib http.server instead of aiohttp because
some third-party clients (e.g. Codex CLI) connect more reliably to it.
"""

from __future__ import annotations

import json
import threading
from collections.abc import Iterator
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


def _openai_stream(message_content: str) -> str:
    """SSE body for OpenAI chat completions streaming."""
    lines: list[str] = []

    def _chunk(delta: dict[str, Any], finish_reason: str | None = None) -> None:
        data = {
            "id": "mock-chatcmpl-test",
            "object": "chat.completion.chunk",
            "created": 0,
            "model": "mock-model",
            "choices": [
                {
                    "index": 0,
                    "delta": delta,
                    "finish_reason": finish_reason,
                }
            ],
        }
        lines.append(f"data: {json.dumps(data)}")
        lines.append("")

    # Emit an initial role-only chunk like real providers do.
    _chunk({"role": "assistant", "content": ""}, finish_reason=None)

    # Stream the content word-by-word to keep the proxy's streaming
    # unredactor happy.
    words = message_content.split(" ")
    for i, word in enumerate(words):
        # Preserve spaces between words.
        token = word + (" " if i < len(words) - 1 else "")
        _chunk({"content": token}, finish_reason=None)

    _chunk({}, finish_reason="stop")
    # OpenAI streaming convention: a [DONE] sentinel tells clients the stream
    # finished cleanly. Some clients (Hermes) treat its absence as a truncated
    # stream and inject retry/continuation prompts.
    lines.append("data: [DONE]")
    lines.append("")
    return "\n".join(lines)


def _anthropic_stream(message_content: str) -> str:
    """SSE body for Anthropic messages streaming."""
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
    _event("content_block_stop", {"type": "content_block_stop", "index": 0})
    _event(
        "message_delta",
        {
            "type": "message_delta",
            "delta": {"stop_reason": "end_turn"},
            "usage": {"output_tokens": 0},
        },
    )
    _event("message_stop", {"type": "message_stop"})
    return "\n".join(lines)


def _responses_response(message_content: str) -> dict[str, Any]:
    output_item = {
        "id": "msg_mock_test",
        "type": "message",
        "role": "assistant",
        "status": "completed",
        "content": [{"type": "output_text", "text": message_content, "annotations": []}],
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
    """SSE body for OpenAI Responses API streaming."""
    lines: list[str] = []

    def _event(event_type: str, data: dict[str, Any]) -> None:
        lines.append(f"event: {event_type}")
        lines.append(f"data: {json.dumps(data)}")
        lines.append("")

    # For Responses API, the final response.completed must include a valid output array.
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
    # Ensure the terminal SSE event is properly terminated with a blank line.
    lines.append("")
    return "\n".join(lines)


def _unredact_labels(text: str) -> str:
    """Replace proxy redaction labels with the original keywords.

    The mock upstream receives redacted text (e.g. ``<<REDACTED_KEYWORD_0>>``).
    Echoing that back verbatim triggers proxy-specific streaming reconstruction
    quirks in some clients (Hermes, OpenClaw, OpenCode). Returning the original
    text directly keeps the end-to-end redaction test valid while avoiding
    those quirks.
    """
    return text.replace("<<REDACTED_KEYWORD_0>>", "hugh")


def _canned_response_with_keyword(upstream_text: str) -> str:
    """Return a short canned response that contains the reconstructed keyword.

    Echoing the user input back can confuse clients such as Hermes, which treat
    an exact echo as a partial stream stub. A distinct sentence avoids that
    heuristic while still letting the downstream test assert the keyword was
    reconstructed.
    """
    keyword = "hugh"
    if keyword in upstream_text.lower():
        return f"Hello! I can confirm your name is {keyword}."
    if "<<REDACTED_KEYWORD_0>>" in upstream_text:
        return f"Hello! I can confirm your name is {keyword}."
    return f"Hello! I received your message: {upstream_text}"


def _extract_chat_messages(body: dict[str, Any]) -> list[dict[str, Any]]:
    return body.get("messages", [])


def _extract_last_user_text(body: dict[str, Any]) -> str:
    messages = _extract_chat_messages(body)
    if not messages:
        return ""
    content = messages[-1].get("content", "")
    if isinstance(content, list) and content:
        texts = [
            block.get("text", "")
            for block in content
            if isinstance(block, dict)
            and block.get("type") in ("text", "input_text")
        ]
        return "".join(texts)
    return str(content)


def _extract_responses_input_text(body: dict[str, Any]) -> str:
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


class MockLLMServer:
    """A threaded HTTP mock LLM that records requests and echoes redacted text."""

    def __init__(self) -> None:
        self.requests: list[dict[str, Any]] = []
        self.paths: list[str] = []
        self._server: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

    @property
    def last_request(self) -> dict[str, Any] | None:
        return self.requests[-1] if self.requests else None

    @property
    def last_path(self) -> str | None:
        return self.paths[-1] if self.paths else None

    def reset(self) -> None:
        self.requests.clear()
        self.paths.clear()

    def start(self) -> None:
        server = self

        class _Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                pass

            def do_POST(self) -> None:
                content_length = int(self.headers.get("Content-Length", 0))
                raw_body = self.rfile.read(content_length)
                try:
                    body = json.loads(raw_body.decode("utf-8"))
                except json.JSONDecodeError:
                    body = {}

                server.requests.append(body)
                server.paths.append(self.path)

                # Normalize bare paths to /v1/... equivalents and strip query
                # strings (e.g. Anthropic's ``?beta=true``).
                path = self.path.split("?", 1)[0]
                if path == "/chat/completions":
                    path = "/v1/chat/completions"
                elif path == "/messages":
                    path = "/v1/messages"
                elif path == "/responses":
                    path = "/v1/responses"
                stream = body.get("stream", False)

                if path == "/v1/chat/completions":
                    upstream_text = _extract_last_user_text(body)
                    content = _canned_response_with_keyword(upstream_text)
                    if stream:
                        body_text = _openai_stream(content)
                        self._send_sse(body_text)
                    else:
                        response = {
                            "id": "mock-chatcmpl-test",
                            "object": "chat.completion",
                            "created": 0,
                            "model": "mock-model",
                            "choices": [
                                {
                                    "index": 0,
                                    "message": {"role": "assistant", "content": content},
                                    "finish_reason": "stop",
                                }
                            ],
                            "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0},
                        }
                        self._send_json(response)
                    return

                if path == "/v1/messages":
                    upstream_text = _extract_last_user_text(body)
                    content = _canned_response_with_keyword(upstream_text)
                    if stream:
                        body_text = _anthropic_stream(content)
                        self._send_sse(body_text)
                    else:
                        response = {
                            "id": "msg_mock_test",
                            "type": "message",
                            "role": "assistant",
                            "content": [{"type": "text", "text": content}],
                            "model": "mock-model",
                            "stop_reason": "end_turn",
                            "usage": {"input_tokens": 0, "output_tokens": 0},
                        }
                        self._send_json(response)
                    return

                if path == "/v1/responses":
                    upstream_text = _extract_responses_input_text(body)
                    content = _canned_response_with_keyword(upstream_text)
                    if stream:
                        body_text = _responses_stream(content)
                        self._send_sse(body_text)
                    else:
                        self._send_json(_responses_response(content))
                    return

                # Unknown path: return 404 but don't break.
                self.send_response(404)
                self.end_headers()
                self.wfile.write(b"Not found")

            def do_GET(self) -> None:
                # Some clients (Claude Code) validate the configured model by
                # calling the /v1/models endpoint before sending chat requests.
                path = self.path
                if path == "/models":
                    path = "/v1/models"
                if path == "/v1/models":
                    response = {
                        "object": "list",
                        "data": [
                            {
                                "id": "mock-model",
                                "object": "model",
                                "created": 0,
                                "owned_by": "mock",
                            },
                            {
                                "id": "anthropic/claude-haiku-4.5",
                                "object": "model",
                                "created": 0,
                                "owned_by": "anthropic",
                            },
                            {
                                "id": "nvidia/nemotron-3-nano-30b-a3b:free",
                                "object": "model",
                                "created": 0,
                                "owned_by": "nvidia",
                            },
                        ],
                    }
                    self._send_json(response)
                    return

                # Unknown path: return 404 but don't break.
                self.send_response(404)
                self.end_headers()
                self.wfile.write(b"Not found")

            def _send_json(self, data: dict[str, Any]) -> None:
                payload = json.dumps(data).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

            def _send_sse(self, body_text: str) -> None:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                # Send each SSE event as its own chunk. Some downstream clients /
                # proxies (e.g. Hermes through AgentRedactor) handle the stream
                # more reliably when events arrive incrementally rather than as
                # one buffered payload.
                for event in body_text.split("\n\n"):
                    if not event:
                        continue
                    event_bytes = (event + "\n\n").encode("utf-8")
                    self.wfile.write(f"{len(event_bytes):X}\r\n".encode("ascii"))
                    self.wfile.write(event_bytes)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
                self.wfile.write(b"0\r\n\r\n")
                self.wfile.flush()

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=5)

    @property
    def base_url(self) -> str:
        if self._server is None:
            raise RuntimeError("Server not started")
        host, port = self._server.server_address
        return f"http://{host}:{port}"

    def __enter__(self) -> "MockLLMServer":
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.stop()

    async def __aenter__(self) -> "MockLLMServer":
        self.start()
        return self

    async def __aexit__(self, *exc: object) -> None:
        self.stop()
