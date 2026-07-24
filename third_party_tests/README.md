# Third-Party Integration Tests

This folder contains end-to-end tests for third-party clients that talk to the AgentRedactor proxy. The current targets are **OpenClaw**, **Hermes Agent**, **OpenCode**, **OpenAI Codex**, and **Claude Code**.

## Prerequisites

1. Build AgentRedactor in Release:
   ```powershell
   cd ..\AgentRedactor
   .\buildquick.ps1 -Configuration Release
   ```

2. Build the FlaUI helper used by the existing GUI tests:
   ```powershell
   cd ..\tests\gui\windows
   .\build.ps1
   ```

3. Install Python dependencies:
   ```powershell
   cd third_party_tests
   pip install -r requirements.txt
   ```

4. Create a `.env` file with your OpenRouter API key:
   ```powershell
   copy .env.example .env
   notepad .env
   ```
   Replace `your-key-here` with your real OpenRouter API key.

5. Install the third-party client(s) you want to test and ensure they are on `PATH`:
   - OpenClaw: `openclaw --version`
   - Hermes Agent: `hermes --version`
   - OpenCode: `opencode --version`
   - OpenAI Codex: `codex --version`
   - Claude Code: `claude --version`

## Running the OpenClaw tests

```powershell
cd third_party_tests
pytest -v openclaw/
```

### What is tested (OpenClaw)

The tests start AgentRedactor (which always runs with no protocol translation), then run one of the `scripts/setup-openclaw-test-*.ps1` scripts to configure OpenClaw, start the OpenClaw gateway in the background, and send a message via the OpenClaw CLI. The test passes if OpenClaw returns a successful reply and fails if it returns an error or times out.

| OpenClaw setup script | AgentRedactor protocol mode | Reason |
|---|---|---|
| `setup-openclaw-test-openai-nemotron.ps1` | `none` | OpenAI client ↔ OpenAI upstream (Nemotron model) |
| `setup-openclaw-test-anthropic-claude-haiku.ps1` | `none` | Anthropic client ↔ Anthropic upstream (Claude Haiku model) |

## Running the Hermes Agent tests

```powershell
cd third_party_tests
pytest -v hermes/
```

### What is tested (Hermes Agent)

The tests start AgentRedactor (which always runs with no protocol translation), then run one of the `scripts/setup-hermes-agent-test-*.ps1` scripts to configure Hermes Agent. Hermes is then invoked in one-shot mode (`hermes chat -q`) through the proxy. The test passes if Hermes returns a successful reply and fails if it returns an error or times out.

| Hermes setup script | AgentRedactor protocol mode | Reason |
|---|---|---|
| `setup-hermes-agent-test-openai-nemotron.ps1` | `none` | OpenAI client ↔ OpenAI upstream (Nemotron model) |
| `setup-hermes-agent-test-anthropic-claude-haiku.ps1` | `none` | Anthropic client ↔ Anthropic upstream (Claude Haiku model) |

## Running the OpenCode tests

```powershell
cd third_party_tests
pytest -v opencode/
```

### What is tested (OpenCode)

The tests start AgentRedactor (which always runs with no protocol translation), then run one of the `scripts/setup-opencode-test-*.ps1` scripts to configure OpenCode. OpenCode is then invoked in one-shot mode (`opencode run "..." -m provider/model`) through the proxy. The test passes if OpenCode returns a successful reply and fails if it returns an error or times out.

| OpenCode setup script | AgentRedactor protocol mode | Reason |
|---|---|---|
| `setup-opencode-test-openai-nemotron.ps1` | `none` | OpenAI-compatible client ↔ OpenAI upstream (Nemotron model) |
| `setup-opencode-test-anthropic-claude-haiku.ps1` | `none` | Anthropic-compatible client ↔ Anthropic upstream (Claude Haiku model) |

The Anthropic test is marked as `xfail` because OpenCode's support for custom Anthropic-compatible endpoints via `@ai-sdk/anthropic` is experimental and may not work in all versions. The OpenAI test is expected to pass reliably.

## Running the Codex tests

```powershell
cd third_party_tests
pytest -v codex/
```

### What is tested (OpenAI Codex)

The tests start AgentRedactor (which always runs with no protocol translation), then run `scripts/setup-codex-test-openai-nemotron.ps1` to configure Codex. The setup script writes `~/.codex/config.toml` with a custom `agent_redactor` provider pointing at `http://localhost:8081/v1` and using the OpenAI Responses API (`wire_api = "responses"`).

Codex is then invoked in one-shot mode inside a temporary project directory with `DUMMY_API_KEY=dummy` set in the environment. The test passes if Codex returns a successful reply and fails if it returns an error or times out.

| Codex setup script | AgentRedactor protocol mode | Reason |
|---|---|---|
| `setup-codex-test-openai-nemotron.ps1` | `none` | Responses API client ↔ OpenAI upstream (Nemotron model) |

Codex does not support the Anthropic API format, so only the OpenAI/Responses test is provided.

## Running the Claude Code tests

```powershell
cd third_party_tests
pytest -v claudecode/
```

### What is tested (Claude Code)

The tests start AgentRedactor (which always runs with no protocol translation), then run `scripts/setup-claudecode-test-anthropic-claude-haiku.ps1` to configure Claude Code. The setup script installs the Claude Code CLI if necessary, backs up the existing `~/.claude` directory, and writes `~/.claude/settings.json` with the Anthropic-compatible env block:

```json
{
  "env": {
    "ANTHROPIC_BASE_URL": "http://localhost:8081/",
    "ANTHROPIC_AUTH_TOKEN": "dummy",
    "ANTHROPIC_MODEL": "anthropic/claude-haiku-4.5"
  }
}
```

Claude Code is then invoked in one-shot mode (`claude -p "..."`) inside a temporary project directory. The test passes if Claude Code returns a successful reply and fails if it returns an error or times out.

| Claude Code setup script | AgentRedactor protocol mode | Reason |
|---|---|---|
| `setup-claudecode-test-anthropic-claude-haiku.ps1` | `none` | Anthropic client ↔ Anthropic upstream (Claude Haiku model) |

Claude Code only supports the Anthropic API format, so only the Anthropic test is provided.

## Keyword redaction tests

Most third-party clients also have a `*_keyword_redaction` test that uses a local **mock LLM** instead of OpenRouter. The fixture `agentredactor_app_redacting` starts AgentRedactor on port 8081 with the keyword **"hugh"** configured and points it at the mock upstream. The mock LLM echoes the (redacted) request content back, so the tests can verify both sides of redaction:

1. **Redaction:** the upstream request received by the mock LLM contains `<<REDACTED_KEYWORD_0>>` instead of "hugh".
2. **Reconstruction:** the final client output contains "hugh" because AgentRedactor unredacted the mock's echoed response.

These tests do **not** require an OpenRouter API key or network connection.

```powershell
cd third_party_tests
pytest -v openclaw/ hermes/ opencode/ codex/ claudecode/ -k keyword_redaction
```

## Notes

- Tests use the real OpenRouter upstream (`https://openrouter.ai/api/v1`), so they require an API key and a network connection.
- Tests assume the AgentRedactor proxy listens on `http://localhost:8081/`, matching the setup scripts.
- Each test resets the third-party client's config/state and stops any running processes, so they can be run independently.
- Protocol bridging is no longer available; all traffic is forwarded unchanged.
