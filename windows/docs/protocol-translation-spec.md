# Protocol Translation Feature Specification

**Status:** Removed — only `none` mode remains  
**Scope:** AgentRedactor local proxy  
**Goal:** The proxy no longer performs protocol translation. It forwards all requests unchanged (`none` mode). This document is retained for historical context; the `anthropic_to_openai` and `openai_to_anthropic` modes were removed.

---

## 1. Background

The current implementation has a single boolean checkbox, **“Translate Anthropic API to OpenAI (for Claude Code + OpenRouter)”**. It:

- Converts incoming Anthropic `POST /v1/messages` requests into OpenAI `POST /v1/chat/completions` requests.
- Converts OpenAI responses back into Anthropic responses.
- Hardcodes `openrouter/` model prefix stripping and re-adding.

This design is misleading because Claude Code + OpenRouter does **not** require translation — OpenRouter already exposes an Anthropic-compatible endpoint. It is also incomplete because it only supports one direction.

This specification replaces the checkbox with a clearer protocol negotiation model.

---

## 2. High-level design

Each API key profile exposes a single **Protocol Mode** setting. The setting is stored in the profile and applies to all traffic on that profile’s port.

### 2.1 Protocol modes

| Mode | Client protocol | Upstream protocol | Translation direction |
|---|---|---|---|
| `none` | Same as upstream | Same as upstream | No path or body translation (default and only mode) |

The `anthropic_to_openai` and `openai_to_anthropic` modes were removed. The proxy always forwards requests unchanged.

> **Note on path translation:** Different API formats live at different endpoints (e.g. Anthropic `POST /v1/messages` vs. OpenAI `POST /v1/chat/completions`). Path rewriting is therefore a required side effect of body-format translation. It is not a separate feature.

---

## 3. UI/UX

### 3.1 Control

Protocol bridging is no longer configurable. The proxy always operates in `none` mode and forwards requests unchanged.

### 3.2 Validation

Not applicable; `none` is the only supported mode.

---

## 4. Supported endpoints

### 4.1 Anthropic endpoints consumed by clients

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/v1/messages` | Chat/completions |
| `GET` | `/v1/models` | List models |

The implementation must tolerate path variations such as `/v1/messages/v1/messages` caused by misconfigured `ANTHROPIC_BASE_URL` values.

### 4.2 OpenAI endpoints consumed by clients

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/v1/chat/completions` | Chat/completions |
| `GET` | `/v1/models` | List models |

`POST /v1/responses` (OpenAI Responses API) is **out of scope** for v1.

### 4.3 Upstream endpoints

Because each API format has its own endpoint path, the proxy must rewrite the incoming path to the equivalent path for the upstream format. The body is translated to match at the same time.

| Client path | In `anthropic_to_openai` mode | In `openai_to_anthropic` mode |
|---|---|---|
| `POST /v1/messages` | `POST /v1/chat/completions` | N/A |
| `GET /v1/models` | `GET /v1/models` | N/A |
| `POST /v1/chat/completions` | N/A | `POST /v1/messages` |
| `GET /v1/models` | N/A | `GET /v1/models` |

In `none` mode, the client path is forwarded unchanged. This works when the upstream already exposes the same format, for example:

- Anthropic client → OpenRouter: set upstream URL to `https://openrouter.ai/api` and leave mode as `none`.
- OpenAI client → OpenRouter: set upstream URL to `https://openrouter.ai/api/v1` and leave mode as `none`.

Translation is required when the upstream only supports the opposite format, for example Claude Code → Groq. The client sends `POST /v1/messages`, but Groq only accepts `POST /v1/chat/completions`, so both path and body must be rewritten.

---

## 5. Request translation

Request translation has two parts that always happen together:

1. **Path rewrite:** map the incoming endpoint to the equivalent endpoint for the upstream format.
2. **Body rewrite:** map the JSON request payload to the upstream format.

### 5.1 Anthropic → OpenAI request mapping

Existing logic should be refactored into a clean mapping function. Fields:

| Anthropic field | OpenAI field | Notes |
|---|---|---|
| `model` | `model` | Pass through. Do **not** strip any prefix by default. |
| `messages` | `messages` | Copy `user`/`assistant` messages. |
| `system` | first `system` message | Anthropic top-level `system` becomes a message with `role: "system"`. |
| `max_tokens` | `max_tokens` | Pass through if present. |
| `temperature` | `temperature` | Pass through if present. |
| `top_p` | `top_p` | Pass through if present. |
| `stop_sequences` | `stop` | Rename. |
| `stream` | `stream` | Pass through. |
| `tools` | `tools` | Convert each Anthropic tool to `{ type: "function", function: { name, description?, parameters } }`. |
| `tool_choice` | `tool_choice` | `any` → `required`; `auto`/`none` → same; object form `{type:"tool",name}` → `{type:"function",function:{name}}`. |

### 5.2 OpenAI → Anthropic request mapping (new)

| OpenAI field | Anthropic field | Notes |
|---|---|---|
| `model` | `model` | Pass through. |
| `messages` | `messages` | Copy `user`/`assistant` messages. Remove any `system` messages. |
| first `system` message | `system` | Extract the first `role: "system"` message to Anthropic’s top-level `system` field. If multiple system messages exist, concatenate or warn. |
| `max_tokens` | `max_tokens` | Required in Anthropic; if missing, inject a sensible default (e.g. 4096) and log a warning. |
| `temperature` | `temperature` | Pass through if present. |
| `top_p` | `top_p` | Pass through if present. |
| `stop` | `stop_sequences` | Rename. |
| `stream` | `stream` | Pass through. |
| `tools` | `tools` | Convert each OpenAI function tool to Anthropic tool shape `{ name, description?, input_schema }`. |
| `tool_choice` | `tool_choice` | `required` → `any`; `auto`/`none` → same; object form `{type:"function",function:{name}}` → `{type:"tool",name}`. |

### 5.3 Request body validation

If translation is enabled but the incoming request does not match the expected client protocol:

- Return HTTP 400 with a clear JSON error:  
  `{ "error": "Expected Anthropic Messages API request but received OpenAI Chat Completions format" }`  
  or the reverse.
- Do not forward the request to the upstream.

---

## 6. Response translation

### 6.1 General rules

- Only translate successful upstream responses (HTTP 2xx).
- Pass through error responses unchanged so the client sees the real upstream error.
- Detect streaming vs non-streaming by the upstream `Content-Type` header (`text/event-stream` indicates SSE).

### 6.2 OpenAI → Anthropic response mapping

Existing logic, generalized:

| OpenAI response | Anthropic response | Notes |
|---|---|---|
| `id` | `id` | Generate `msg_...` ID if missing. |
| `choices[0].message.content` | `content` array with `type: "text"` block | Concatenate if content is an array. |
| `choices[0].message.tool_calls` | `content` array with `type: "tool_use"` blocks | Parse `function.arguments` JSON. |
| `choices[0].finish_reason` | `stop_reason` | Map `stop` → `end_turn`, `length` → `max_tokens`, `tool_calls` → `tool_use`, `content_filter` → `content_filter`. |
| `usage.prompt_tokens` | `usage.input_tokens` | |
| `usage.completion_tokens` | `usage.output_tokens` | |
| `model` | `model` | Use the original client-facing model name. |

For streaming responses, reconstruct Anthropic SSE events:

1. `message_start`
2. `content_block_start` for each content block
3. `content_block_delta` events as content streams in
4. `content_block_stop`
5. `message_delta` with `stop_reason` and output token usage
6. `message_stop`

### 6.3 Anthropic → OpenAI response mapping (new)

| Anthropic response | OpenAI response | Notes |
|---|---|---|
| `id` | `id` | |
| `content` text blocks | `choices[0].message.content` | Concatenate text blocks. |
| `content` tool_use blocks | `choices[0].message.tool_calls` | Build `{ id, type: "function", function: { name, arguments } }`. |
| `stop_reason` | `choices[0].finish_reason` | Map `end_turn` → `stop`, `max_tokens` → `length`, `tool_use` → `tool_calls`, `content_filter` → `content_filter`. |
| `usage.input_tokens` | `usage.prompt_tokens` | |
| `usage.output_tokens` | `usage.completion_tokens` | |
| `model` | `model` | |

For streaming responses, convert Anthropic SSE events into OpenAI `chat.completion.chunk` SSE events with `delta` objects.

### 6.4 Models list translation

When translation is enabled and the client calls `GET /v1/models`:

- Forward the request to the upstream models endpoint.
- Translate the response shape to match the client’s expected format.
- Do **not** add or remove provider-specific prefixes by default.

The current `openrouter/` prefix behaviour must be removed from the default implementation.

---

## 7. Model ID handling

### 7.1 Default behaviour

Model IDs must be passed through unchanged in both directions.

### 7.2 Optional model prefix rules (future)

If provider-specific prefix stripping/adding is needed (e.g. `openrouter/` or `anthropic/`), it must be implemented as a separate, optional per-profile setting, not tied to the protocol mode.

For v1, omit this entirely. Users configure the exact model ID their upstream expects.

---

## 8. Configuration and persistence

### 8.1 Storage

In `ApiKeyProfile`, replace:

```cpp
bool translateAnthropicToOpenAI = false;
```

with:

```cpp
enum class ProtocolMode {
    None,
    AnthropicToOpenAI,
    OpenAIToAnthropic
};
ProtocolMode protocolMode = ProtocolMode::None;
```

In `settings.json`, store as a string:

```json
{
  "protocol_mode": "none"
}
```

Maintain backward compatibility when loading old profiles that contain `translate_anthropic_to_openai: true`:

- Any legacy value is ignored and the profile loads as `protocol_mode: "none"`.

### 8.2 Default

Default mode for new profiles is `none`.

---

## 9. Auto-detection (future, not v1)

### 9.1 Client protocol detection

The proxy can detect the client protocol from the first request:

- `/v1/messages` + Anthropic body schema → Anthropic client.
- `/v1/chat/completions` + OpenAI body schema → OpenAI client.

This could be used to warn the user if their selected protocol mode is mismatched, but should not silently override the explicit setting in v1.

### 9.2 Upstream protocol detection

Upstream detection is intentionally **not** part of v1. It requires probing `/v1/models` or sending test requests, which introduces latency, cost, and failure modes. The user must explicitly select the mode.

---

## 10. Error handling

| Scenario | Behaviour |
|---|---|
| Translation enabled but request body is invalid JSON | Return 400 with parse error. Do not forward. |
| Translation enabled but request is wrong protocol | Return 400 with mismatch error. Do not forward. |
| Translation succeeds but upstream returns error | Pass through upstream status and body unchanged. |
| Response translation fails on a successful upstream response | Log error; optionally return 502 with translation failure message. |
| Upstream `/v1/models` returns unexpected shape | Pass through unchanged; log a warning. |

---

## 11. Logging

Add per-request log entries when translation is active:

- Direction and endpoint mapping.
- Original model ID and translated model ID (if changed).
- Whether streaming was detected.
- Any dropped or transformed fields (e.g. system prompt extraction).

---

## 12. Testing scenarios

### 12.1 Anthropic → OpenAI mode

1. Claude Code sends `POST /v1/messages` with `model: claude-sonnet-4` → upstream receives `POST /v1/chat/completions` with the same model.
2. Claude Code sends `tools` in Anthropic format → upstream receives OpenAI function-tool format.
3. Upstream returns OpenAI streaming response → client receives Anthropic SSE events.
4. Upstream returns 401 error → client sees the raw 401 error body.

### 12.2 OpenAI → Anthropic mode

1. Codex sends `POST /v1/chat/completions` → upstream receives `POST /v1/messages`.
2. OpenAI request with `role: "system"` message → upstream receives top-level `system` field.
3. Anthropic upstream returns `tool_use` blocks → client receives OpenAI `tool_calls`.
4. Anthropic upstream returns streaming response → client receives OpenAI SSE chunks.

### 12.3 None mode

1. All requests and responses pass through unchanged regardless of path.

---

## 13. Out of scope for v1

- OpenAI Responses API (`/v1/responses`).
- Google Gemini / Vertex AI formats.
- Automatic upstream protocol probing.
- Provider-specific model prefix rules.
- Multiple simultaneous client protocols on the same port.

---

## 14. Migration from current implementation

1. Replace the boolean profile field with the enum.
2. Update the UI from checkbox to radio buttons.
3. Remove the hardcoded `openrouter/` prefix stripping and re-adding.
4. Refactor existing Anthropic→OpenAI code into a clear adapter class.
5. Implement the reverse OpenAI→Anthropic adapter.
6. Add response-path translation for both directions.
7. Update settings serialization and backward-compatibility loading.
8. Update user documentation and in-app help text.

---

## 15. Decisions

1. **Upstream URL validation:** Not implemented in v1. The proxy does not validate that the upstream URL path aligns with the selected mode. Users are responsible for configuring a matching upstream URL.
2. **Streaming translation:** The proxy buffers the entire upstream SSE body and then translates it in one pass. This keeps the implementation simple and consistent with the existing Anthropic→OpenAI streaming path.
3. **Missing `max_tokens` in OpenAI→Anthropic:** The proxy injects a default value of `4096` and logs a warning. This matches Anthropic's API requirement and avoids rejecting otherwise-valid OpenAI client requests.

## 16. Notes

- The hardcoded `openrouter/` model prefix stripping and re-adding has been removed. Model IDs are passed through unchanged in both directions.
- The UI control has been changed from a single checkbox to a mutually exclusive radio button group labelled **Protocol bridging**.
- Existing profiles that contain `translate_anthropic_to_openai: true` are migrated to `protocol_mode: "none"` on load.
