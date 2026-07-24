"""Shared helpers for third-party keyword-redaction tests."""

from __future__ import annotations

from typing import Any


def extract_upstream_text(body: dict[str, Any]) -> str:
    """Extract the last user text from an upstream request body.

    Handles OpenAI chat completions, Anthropic messages, and OpenAI Responses API.
    """
    # OpenAI Responses API
    if "input" in body:
        input_value = body["input"]
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

    # OpenAI chat completions / Anthropic messages
    messages = body.get("messages", [])
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


def assert_keyword_redacted(mock_llm: Any, keyword: str = "hugh") -> None:
    """Assert the last upstream request redacted the keyword."""
    last_request = mock_llm.last_request
    assert last_request is not None, "No request was captured by the mock LLM"
    upstream_text = extract_upstream_text(last_request)
    assert keyword not in upstream_text.lower(), (
        f"Expected keyword '{keyword}' to be redacted in upstream request, "
        f"but it was present:\n{upstream_text}"
    )
    assert "<<REDACTED_KEYWORD_0>>" in upstream_text, (
        f"Expected redaction label in upstream request, but got:\n{upstream_text}"
    )


def assert_keyword_reconstructed(output: str, keyword: str = "hugh") -> None:
    """Assert the final client output contains the reconstructed keyword."""
    assert keyword in output.lower(), (
        f"Expected reconstructed keyword '{keyword}' in client output, but got:\n{output}"
    )
