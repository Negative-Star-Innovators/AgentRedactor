"""End-to-end tests for OpenAI Codex talking through AgentRedactor."""

from __future__ import annotations

import tempfile
from pathlib import Path
from typing import Any

import pytest

from gui.windows.gui_driver import save_profile
from redaction_helpers import (
    assert_keyword_reconstructed,
    assert_keyword_redacted,
)

from .codex_runner import (
    run_setup_script,
    send_message,
    stop_codex,
)


SETUP_SCRIPT = "setup-codex-test-openai-nemotron.ps1"


def _failure_indicators(output: str) -> list[str]:
    """Return any failure-related substrings found in the combined output."""
    # NOTE: We intentionally omit "not found" because Codex prints a benign
    # warning when it has no metadata for the custom model:
    #   "Model metadata for `...` not found. Defaulting to fallback metadata..."
    indicators = [
        "error",
        "timeout",
        "fetch failed",
        "invalid",
        "failed",
        "unreachable",
        "refused",
        "unauthorized",
        "authentication",
    ]
    lower = output.lower()
    return [indicator for indicator in indicators if indicator in lower]


def _send_codex_message(message: str | None = None) -> tuple[int, str, str]:
    """Configure Codex, send a one-shot message in a temp project, and return output."""
    stop_codex()
    run_setup_script(SETUP_SCRIPT)

    with tempfile.TemporaryDirectory(prefix="codex_third_party_test_", ignore_cleanup_errors=True) as tmp_dir:
        project_dir = Path(tmp_dir)
        (project_dir / "README.md").write_text(
            "# Test project\n\nThis is a dummy project for Codex third-party testing.\n",
            encoding="utf-8",
        )

        rc, stdout, stderr = send_message(
            message=(
                message
                if message is not None
                else "my name is hugh. What is my name? What is your name?"
            ),
            project_dir=project_dir,
            timeout=300,
        )
        return rc, stdout, stderr


def _assert_codex_success(rc: int, stdout: str, stderr: str) -> str:
    """Assert Codex returned a successful reply and return the combined output."""
    output = stdout + stderr
    failures = _failure_indicators(output)

    assert rc == 0, (
        f"Expected Codex success but got rc={rc}.\n"
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
async def test_codex_openai_nemotron_no_bridge(agentredactor_app) -> None:
    """OpenAI-formatted Codex (Responses API) + no protocol translation should succeed."""
    save_profile()
    rc, stdout, stderr = _send_codex_message()
    _assert_codex_success(rc, stdout, stderr)


@pytest.mark.asyncio
async def test_codex_openai_nemotron_keyword_redaction(
    agentredactor_app_redacting: tuple[Any, Any],
) -> None:
    """Codex + keyword 'hugh' should be redacted upstream and reconstructed downstream."""
    save_profile()
    _app, mock_llm = agentredactor_app_redacting

    rc, stdout, stderr = _send_codex_message()
    output = _assert_codex_success(rc, stdout, stderr)

    assert_keyword_redacted(mock_llm, keyword="hugh")
    assert_keyword_reconstructed(output, keyword="hugh")
