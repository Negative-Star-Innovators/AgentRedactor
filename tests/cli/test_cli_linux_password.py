"""Linux typed-master-password tests for the CLI surface.

On Linux there is no Windows Hello; protection is a typed master password
(PBKDF2-wrapped session key). Gated commands prompt on stdin (no echo) and
verify through POST /unlock. These tests mirror the Windows Hello consent
gate in test_cli.py (skipped there on non-Windows) using piped stdin.

The tests run in file order against the shared module-scoped engine; the
final test strips protection again so the suite ends unlocked.
"""

from __future__ import annotations

import sys

import pytest

from conftest import CliEngine, TEST_API_KEY

pytestmark = pytest.mark.skipif(
    sys.platform == "win32",
    reason="typed master password is the Linux protection mode; Windows uses Hello",
)

PASSWORD = "cli-test-pw"


def test_password_enable_requires_matching_entries(engine: CliEngine) -> None:
    # Mismatched confirmation is rejected and protection stays off.
    r = engine.run_cli("password", "enable", input=f"{PASSWORD}\nother\n")
    assert r.returncode == 1, r.stdout
    assert "passwords do not match" in r.stdout
    r = engine.run_cli("status")
    assert "password enabled: false" in r.stdout

    # Empty password is rejected.
    r = engine.run_cli("password", "enable", input="\n\n")
    assert r.returncode == 1, r.stdout
    assert "password must not be empty" in r.stdout


def test_password_enable_and_locked_session_gate(engine: CliEngine) -> None:
    r = engine.run_cli("password", "enable", input=f"{PASSWORD}\n{PASSWORD}\n")
    assert r.returncode == 0, r.stdout
    assert "master password protection enabled" in r.stdout

    # Enabling twice is an error.
    r = engine.run_cli("password", "enable", input=f"{PASSWORD}\n{PASSWORD}\n")
    assert r.returncode == 1
    assert "already enabled" in r.stdout

    r = engine.run_cli("status")
    assert r.returncode == 0
    assert "password enabled: true" in r.stdout

    # Restart the engine: a fresh session starts locked, like a real app open.
    engine.stop()
    engine.start()
    try:
        # status/help stay open (ungated by design).
        r = engine.run_cli("status")
        assert r.returncode == 0, r.stdout
        assert "password enabled: true" in r.stdout
        r = engine.run_cli("help")
        assert r.returncode == 0

        # With stdin=DEVNULL the prompt hits EOF and every gated command fails
        # fast instead of hanging.
        for args in (("get", "logging"), ("get", "api-key"), ("set", "logging", "false"),
                     ("regex", "list"), ("keywords", "list"), ("pii-types", "list"),
                     ("profiles", "list"), ("get", "confidence-threshold")):
            r = engine.run_cli(*args)
            assert r.returncode == 1, (args, r.stdout)
            assert "master password required" in r.stdout, (args, r.stdout)

        # A wrong piped password is rejected.
        r = engine.run_cli("get", "logging", input="wrong-pw\n")
        assert r.returncode == 1, r.stdout
        assert "wrong master password" in r.stdout

        # The API key was never served.
        r = engine.run_cli("get", "api-key")
        assert r.stdout.strip() != TEST_API_KEY, r.stdout

        # The correct piped password unlocks the session for the command.
        r = engine.run_cli("get", "logging", input=f"{PASSWORD}\n")
        assert r.returncode == 0, r.stdout
        assert r.stdout.strip() == "true"

        # Disabling protection is gated like every other protected action.
        r = engine.run_cli("password", "disable")
        assert r.returncode == 1, r.stdout
        r = engine.run_cli("status")
        assert "password enabled: true" in r.stdout
    finally:
        # Safety net: if any assertion above failed mid-lock, drop protection
        # via settings surgery so later modules start unlocked.
        engine.strip_protection()
        r = engine.run_cli("status")
        assert r.returncode == 0
        assert "password enabled: false" in r.stdout


def test_password_disable_with_password(engine: CliEngine) -> None:
    r = engine.run_cli("password", "enable", input=f"{PASSWORD}\n{PASSWORD}\n")
    assert r.returncode == 0, r.stdout
    try:
        engine.stop()
        engine.start()
        r = engine.run_cli("password", "disable", input=f"{PASSWORD}\n")
        assert r.returncode == 0, r.stdout
        assert "master password protection disabled" in r.stdout
        r = engine.run_cli("status")
        assert "password enabled: false" in r.stdout
        # Ungated again.
        r = engine.run_cli("get", "logging")
        assert r.returncode == 0, r.stdout
    finally:
        engine.strip_protection()
