"""End-to-end tests for `agentredactor <subcommand>` (the CLI surface).

These run the real engine against an isolated config dir and drive the CLI
as a subprocess, asserting on stdout and exit codes. The tests run in file
order against one shared engine; mutations restore themselves where needed,
and the password test (which restarts the engine into a locked state) runs
last but one.
"""

from __future__ import annotations

import time

from conftest import CliEngine, TEST_API_KEY


def test_status(engine: CliEngine) -> None:
    r = engine.run_cli("status")
    assert r.returncode == 0, r.stderr + r.stdout
    assert "engine:" in r.stdout
    assert "master password: false" in r.stdout
    assert "profiles:" in r.stdout
    assert "test-profile" in r.stdout
    assert f"port {engine.proxy_port}" in r.stdout
    assert "proxy running" in r.stdout


def test_help(engine: CliEngine) -> None:
    r = engine.run_cli("help")
    assert r.returncode == 0
    assert "usage: agentredactor" in r.stdout


def test_unknown_command(engine: CliEngine) -> None:
    r = engine.run_cli("frobnicate")
    assert r.returncode == 2
    assert "unknown command" in r.stdout


def test_get_global_setting(engine: CliEngine) -> None:
    r = engine.run_cli("get", "logging")
    assert r.returncode == 0, r.stdout
    assert r.stdout.strip() == "true"


def test_set_and_get_global_setting(engine: CliEngine) -> None:
    try:
        r = engine.run_cli("set", "logging", "false")
        assert r.returncode == 0, r.stdout
        assert r.stdout.strip() == "ok"
        r = engine.run_cli("get", "logging")
        assert r.stdout.strip() == "false"
    finally:
        engine.run_cli("set", "logging", "true")


def test_set_rejects_bad_bool(engine: CliEngine) -> None:
    r = engine.run_cli("set", "logging", "maybe")
    assert r.returncode == 2
    assert "expected a boolean" in r.stdout


def test_profiles_list(engine: CliEngine) -> None:
    r = engine.run_cli("profiles", "list")
    assert r.returncode == 0, r.stdout
    assert "alias" in r.stdout  # header
    assert "test-profile" in r.stdout
    assert str(engine.proxy_port) in r.stdout
    assert "true" in r.stdout  # enabled / running


def test_profiles_list_masks_api_key(engine: CliEngine) -> None:
    r = engine.run_cli("profiles", "list")
    assert r.returncode == 0
    assert TEST_API_KEY not in r.stdout


def test_get_api_key(engine: CliEngine) -> None:
    r = engine.run_cli("get", "api-key")
    assert r.returncode == 0, r.stdout
    assert r.stdout.strip() == TEST_API_KEY


def test_set_and_get_confidence_threshold(engine: CliEngine) -> None:
    try:
        r = engine.run_cli("set", "confidence-threshold", "0.75")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("get", "confidence-threshold")
        assert r.returncode == 0
        assert r.stdout.strip() == "0.75"
    finally:
        engine.run_cli("set", "confidence-threshold", "0.9")


def test_confidence_threshold_range(engine: CliEngine) -> None:
    r = engine.run_cli("set", "confidence-threshold", "1.5")
    assert r.returncode == 2
    assert "between 0 and 1" in r.stdout


def test_regex_add_list_remove(engine: CliEngine) -> None:
    pattern = r"\bZZ-\d{4}\b"
    try:
        r = engine.run_cli("regex", "add", pattern)
        assert r.returncode == 0, r.stdout

        r = engine.run_cli("regex", "list")
        assert r.returncode == 0
        assert pattern in r.stdout
        assert "[x]" in r.stdout

        r = engine.run_cli("regex", "remove", "1")
        assert r.returncode == 0, r.stdout

        r = engine.run_cli("regex", "list")
        assert "(none)" in r.stdout
    finally:
        engine.run_cli("regex", "remove", pattern)


def test_keywords_add_list_remove(engine: CliEngine) -> None:
    try:
        r = engine.run_cli("keywords", "add", "TopSecret", "--ignore-case")
        assert r.returncode == 0, r.stdout

        r = engine.run_cli("keywords", "list")
        assert r.returncode == 0
        assert "TopSecret" in r.stdout
        assert "(ignore case)" in r.stdout

        r = engine.run_cli("keywords", "remove", "TopSecret")
        assert r.returncode == 0, r.stdout

        r = engine.run_cli("keywords", "list")
        assert "(none)" in r.stdout
    finally:
        engine.run_cli("keywords", "remove", "TopSecret")


def test_remove_missing_entry(engine: CliEngine) -> None:
    r = engine.run_cli("regex", "remove", "99")
    assert r.returncode == 2
    assert "out of range" in r.stdout


def test_password_lock_gate(engine: CliEngine) -> None:
    """With a master password set and a locked engine, settings commands
    require --password; status does not."""
    password = "pw-test-123"
    r = engine.run_cli("password", "enable", password)
    assert r.returncode == 0, r.stdout
    assert "master password enabled" in r.stdout

    # Restart the engine: a fresh session starts locked.
    engine.stop()
    engine.start()
    try:
        # status stays ungated and reports the locked session.
        r = engine.run_cli("status")
        assert r.returncode == 0, r.stdout
        assert "master password: true" in r.stdout
        assert "session:" in r.stdout and "locked" in r.stdout

        # Reads and writes are gated (no interactive console -> hard error).
        r = engine.run_cli("get", "logging")
        assert r.returncode == 1
        assert "password required" in r.stdout

        r = engine.run_cli("get", "api-key")
        assert r.returncode == 1
        assert "password required" in r.stdout

        r = engine.run_cli("set", "logging", "false")
        assert r.returncode == 1
        assert "password required" in r.stdout

        # Wrong password is rejected; right password unlocks the session.
        r = engine.run_cli("get", "logging", "--password", "wrong")
        assert r.returncode == 1
        assert "wrong password" in r.stdout

        r = engine.run_cli("get", "api-key", "--password", password)
        assert r.returncode == 0, r.stdout
        assert r.stdout.strip() == TEST_API_KEY

        # The session is now unlocked for subsequent commands.
        r = engine.run_cli("get", "logging")
        assert r.returncode == 0
    finally:
        engine.run_cli("password", "disable", "--password", password)
        r = engine.run_cli("status")
        assert "master password: false" in r.stdout


def test_engine_stop(engine: CliEngine) -> None:
    """Last test: `engine stop` cleanly stops the shared engine."""
    r = engine.run_cli("engine", "stop")
    assert r.returncode == 0, r.stdout
    assert "engine stopping" in r.stdout
    if engine.process:
        engine.process.wait(timeout=15)
        assert engine.process.poll() is not None
