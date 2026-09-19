"""Tool machinery must survive redaction byte-exact; tool data must not.

Regression for the Claude Code 400: the NER model / keywords / regexes can
match tool names ("Bash"), and providers validate tool names, ids, and
schemas — a placeholder there rejects the whole request. Declarations and
tool identifiers are vendor code and are exempted; everything tools carry
(content, arguments, input, output) stays redacted.
"""
import http.server
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux engine test")

_tests = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_tests))
sys.path.insert(0, str(_tests / "gui"))
from config_factory import create_settings
from gui_process import _find_free_port, _wait_for_port

ENGINE = os.environ.get("AGENTREDACTOR_ENGINE_BIN",
    str(_tests.parent / "linux/build/engine/agentredactor"))

NAME_PATTERN = re.compile(r"^[a-zA-Z0-9_-]{1,128}$")

captured = []


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        captured.append(json.loads(self.rfile.read(length)))
        payload = json.dumps({"choices": [{"message": {"role": "assistant", "content": "ok"}}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *a):
        pass


def _chat_payload() -> dict:
    """OpenAI-shaped body exercising every tool-related exemption."""
    return {
        "model": "claude-sonnet-4-20250514",
        "tool_choice": {"type": "function", "function": {"name": "Task"}},
        "tools": [
            {"name": "Bash", "description": "Run a Bash command via Task",
             "input_schema": {"type": "object", "properties": {
                 "command": {"type": "string", "description": "command to run"}}}},
            {"name": "Task", "description": "Launch a Task agent",
             "input_schema": {"type": "object", "properties": {
                 "prompt": {"type": "string", "enum": ["Bash", "pear"]}}}},
        ],
        "messages": [
            {"role": "user", "content": "Run the sk-abc123 cleanup for Pear please."},
            {"role": "assistant", "content": None,
             "tool_calls": [{"id": "call_1", "type": "function",
                             "function": {"name": "Bash",
                                          "arguments": "{\"command\": \"cat /home/jack/pear.txt\"}"}}]},
            {"role": "tool", "tool_call_id": "call_1", "name": "Bash",
             "content": "Pear report for Jack: contents of sk-def456"},
        ],
    }


def _anthropic_payload() -> dict:
    return {
        "model": "claude-sonnet-4-20250514",
        "tool_choice": {"type": "tool", "name": "Task"},
        "tools": [{"name": "Task", "description": "Launch a Task agent",
                   "input_schema": {"type": "object"}}],
        "messages": [
            {"role": "user", "content": "Handle the Pear order from Jack."},
            {"role": "assistant", "content": [
                {"type": "tool_use", "id": "toolu_1", "name": "Bash",
                 "input": {"command": "ls /home/jack/pear-reports", "note": "Pear sk-ghi789"}},
            ]},
            {"role": "user", "content": [
                {"type": "tool_result", "tool_use_id": "toolu_1",
                 "content": [{"type": "text", "text": "Pear totals for Jack, ref sk-jkl012"}]},
            ]},
        ],
    }


def _responses_payload() -> dict:
    return {
        "model": "claude-sonnet-4-20250514",
        "previous_response_id": "resp_pear123",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Pear for Jack"}]},
            {"type": "function_call", "call_id": "call_9", "name": "Bash",
             "arguments": "{\"command\": \"rm sk-mno345\"}"},
            {"type": "function_call_output", "call_id": "call_9",
             "output": "Pear Jack sk-pqr678"},
        ],
    }


def test_tool_machinery_exempt_tool_data_redacted():
    if not Path(ENGINE).exists():
        pytest.skip(f"engine exe not built: {ENGINE}")

    upstream = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=upstream.serve_forever, daemon=True).start()

    config_dir = Path(tempfile.mkdtemp(prefix="ar-tools-exempt-"))
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url=f"http://127.0.0.1:{upstream.server_address[1]}",
        api_key="sk-tools-exempt",
        proxy_port=proxy_port,
        logging_enabled=False,
        keywords=[{"text": "Pear", "case_sensitive": False, "enabled": True},
                  {"text": "Task", "case_sensitive": False, "enabled": True}],
        regex_patterns=[{"pattern": "sk-[a-zA-Z0-9]{3,}", "enabled": True},
                        {"pattern": "Bash", "enabled": True}],
    )
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    proc = subprocess.Popen([ENGINE], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        assert _wait_for_port(proxy_port, timeout=300), "engine never started"
        time.sleep(2)

        for payload in (_chat_payload(), _anthropic_payload(), _responses_payload()):
            req = urllib.request.Request(
                f"http://127.0.0.1:{proxy_port}/v1/chat/completions",
                data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"})
            urllib.request.urlopen(req, timeout=120).read()
        assert len(captured) == 3, "upstream saw no request"

        chat, anthropic, responses = captured

        # --- OpenAI chat shape ---
        tools = {t["name"]: t for t in chat["tools"]}
        assert set(tools) == {"Bash", "Task"}, f"declaration names redacted: {set(tools)}"
        assert tools["Task"]["input_schema"]["properties"]["prompt"]["enum"] == ["Bash", "pear"], \
            "declaration enum values redacted"
        assert chat["tool_choice"]["function"]["name"] == "Task"
        hist_call = chat["messages"][1]["tool_calls"][0]
        assert hist_call["function"]["name"] == "Bash", "history function.name redacted"
        args = hist_call["function"]["arguments"].lower()
        assert "pear" not in args, f"keyword leaked inside arguments: {args!r}"
        tool_msg = chat["messages"][2]
        assert tool_msg["name"] == "Bash", "tool message name redacted"
        assert "Pear" not in tool_msg["content"] and "Jack" not in tool_msg["content"], \
            f"tool result content leaked: {tool_msg['content']!r}"
        user = chat["messages"][0]["content"]
        assert "Pear" not in user and "sk-abc123" not in user, f"user content leaked: {user!r}"

        # --- Anthropic shape ---
        a_tools = {t["name"]: t for t in anthropic["tools"]}
        assert set(a_tools) == {"Task"}
        assert anthropic["tool_choice"]["name"] == "Task"
        tool_use = anthropic["messages"][1]["content"][0]
        assert tool_use["name"] == "Bash", "tool_use name redacted"
        assert tool_use["id"] == "toolu_1"
        tool_use_input = json.dumps(tool_use["input"]).lower()
        assert "pear" not in tool_use_input, f"keyword leaked inside tool_use input: {tool_use_input!r}"
        assert "sk-ghi789" not in json.dumps(tool_use["input"])
        tool_result = anthropic["messages"][2]["content"][0]
        assert tool_result["tool_use_id"] == "toolu_1"
        assert "Pear" not in json.dumps(tool_result["content"]), "tool_result content leaked"

        # --- Responses API shape ---
        assert responses["previous_response_id"] == "resp_pear123"
        fn_call = responses["input"][1]
        assert fn_call["name"] == "Bash", "function_call name redacted"
        assert fn_call["call_id"] == "call_9"
        assert "sk-mno345" not in fn_call["arguments"], "function_call arguments leaked"
        fn_out = responses["input"][2]
        assert fn_out["call_id"] == "call_9"
        assert "Pear" not in fn_out["output"] and "sk-pqr678" not in fn_out["output"], \
            f"function_call_output leaked: {fn_out['output']!r}"
        # Sanity: every name we assert on really would fail provider validation
        # if it were a placeholder.
        for name in ("Bash", "Task"):
            assert NAME_PATTERN.match(name)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        upstream.shutdown()
