"""End-to-end tests for OpenCode talking through AgentRedactor."""

from __future__ import annotations

from typing import Any

import pytest

from gui.windows.gui_driver import save_profile
from redaction_helpers import (
    assert_keyword_reconstructed,
    assert_keyword_redacted,
)

from .opencode_runner import (
    run_setup_script,
    send_message,
    stop_opencode,
)


SETUP_SCRIPTS = {
    "openai-nemotron": "setup-opencode-test-openai-nemotron.ps1",
    "anthropic-claude-haiku": "setup-opencode-test-anthropic-claude-haiku.ps1",
}

# Maps setup key -> the provider/model string passed to `opencode run -m`.
PROVIDER_MODELS = {
    "openai-nemotron": "agentredactor/nvidia/nemotron-3-nano-30b-a3b:free",
    "anthropic-claude-haiku": "agentredactor/anthropic/claude-haiku-4.5",
}


def _failure_indicators(output: str) -> list[str]:
    """Return any failure-related substrings found in the combined output."""
    indicators = [
        "error",
        "timeout",
        "fetch failed",
        "not found",
        "invalid",
        "failed",
        "unreachable",
        "refused",
    ]
    lower = output.lower()
    return [indicator for indicator in indicators if indicator in lower]


def _send_opencode_message(
    setup_key: str,
    message: str | None = None,
) -> tuple[int, str, str]:
    """Configure OpenCode, send a one-shot message, and return output."""
    script = SETUP_SCRIPTS[setup_key]
    provider_model = PROVIDER_MODELS[setup_key]

    stop_opencode()
    run_setup_script(script)

    rc, stdout, stderr = send_message(
        provider_model=provider_model,
        message=(
            message
            if message is not None
            else "my name is hugh. What is my name? What is your name?"
        ),
        timeout=180,
    )
    return rc, stdout, stderr


def _assert_opencode_success(rc: int, stdout: str, stderr: str) -> str:
    """Assert OpenCode returned a successful reply and return the combined output."""
    output = stdout + stderr
    failures = _failure_indicators(output)

    assert rc == 0, (
        f"Expected OpenCode success but got rc={rc}.\n"
        f"stdout:\n{stdout}\n"
        f"stderr:\n{stderr}"
    )
    assert "hugh" in output.lower(), (
        f"Expected reply to mention 'hugh' but got:\n{output}"
    )
    assert not failures, (
        f"Expected clean output but found failure indicators: {failures}\n"
        f"output:\n{output}"
    )
    return output


@pytest.mark.asyncio
async def test_opencode_openai_nemotron_no_bridge(agentredactor_app) -> None:
    """OpenAI-formatted OpenCode + no protocol translation should succeed."""
    save_profile()
    rc, stdout, stderr = _send_opencode_message("openai-nemotron")
    _assert_opencode_success(rc, stdout, stderr)


@pytest.mark.xfail(
    reason="OpenCode custom Anthropic provider via @ai-sdk/anthropic is unverified",
    strict=False,
)
@pytest.mark.asyncio
async def test_opencode_anthropic_claude_haiku_no_bridge(agentredactor_app) -> None:
    """Anthropic-formatted OpenCode with Claude Haiku + no translation should succeed."""
    save_profile()
    rc, stdout, stderr = _send_opencode_message(
        "anthropic-claude-haiku",
        message="My name is hugh. Please respond with exactly the sentence: 'Your name is hugh.'",
    )
    _assert_opencode_success(rc, stdout, stderr)


@pytest.mark.asyncio
async def test_opencode_openai_nemotron_keyword_redaction(
    agentredactor_app_redacting: tuple[Any, Any],
) -> None:
    """OpenCode + keyword 'hugh' should be redacted upstream and reconstructed downstream."""
    save_profile()
    _app, mock_llm = agentredactor_app_redacting

    rc, stdout, stderr = _send_opencode_message("openai-nemotron")
    output = _assert_opencode_success(rc, stdout, stderr)

    assert_keyword_redacted(mock_llm, keyword="hugh")
    assert_keyword_reconstructed(output, keyword="hugh")
