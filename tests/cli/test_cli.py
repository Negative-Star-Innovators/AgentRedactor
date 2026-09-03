"""End-to-end tests for `agentredactor <subcommand>` (the CLI surface).

These run the real engine against an isolated config dir and drive the CLI
as a subprocess, asserting on stdout and exit codes. The tests run in file
order against one shared engine; mutations restore themselves where needed,
and the password test (which restarts the engine into a locked state) runs
last but one.
"""

from __future__ import annotations

import sys
import time

import pytest

from conftest import CliEngine, TEST_API_KEY


def test_status(engine: CliEngine) -> None:
    r = engine.run_cli("status")
    assert r.returncode == 0, r.stderr + r.stdout
    assert "engine:" in r.stdout
    assert "password enabled: false" in r.stdout
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


def test_languages_command_and_app_language_validation(engine: CliEngine) -> None:
    """`languages` lists the supported BCP-47 codes; `set app-language` accepts
    only exact supported codes (a partial or unknown tag is rejected instead of
    silently falling back to English)."""
    r = engine.run_cli("languages")
    assert r.returncode == 0, r.stdout
    codes = r.stdout.split()
    for expected in ("en", "de", "es", "zh-CN", "zh-TW", "sr-Latn"):
        assert expected in codes, (expected, codes)

    # Unknown and partial tags are rejected.
    for bad in ("zz", "zh", "en-US"):
        r = engine.run_cli("set", "app-language", bad)
        assert r.returncode == 2, (bad, r.stdout)
        assert "unsupported language" in r.stdout, (bad, r.stdout)

    # An exact supported code applies (case-insensitive input is canonicalized).
    r = engine.run_cli("set", "app-language", "zh-CN")
    assert r.returncode == 0, r.stdout
    r = engine.run_cli("get", "app-language")
    assert r.returncode == 0 and r.stdout.strip() == "zh-CN", r.stdout
    r = engine.run_cli("set", "app-language", "de")
    assert r.returncode == 0, r.stdout
    r = engine.run_cli("get", "app-language")
    assert r.returncode == 0 and r.stdout.strip() == "de", r.stdout


def test_show_sensitive_rejected_while_logging_disabled(engine: CliEngine) -> None:
    """Mirrors the GUI: sensitive logging may only be switched on while logging
    is enabled; arming it silently would activate it the moment logging is
    turned on, without the GUI's warning dialog."""
    try:
        r = engine.run_cli("set", "logging", "false")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("set", "show-sensitive", "true")
        assert r.returncode == 1, r.stdout
        assert "failed to update setting" in r.stdout, r.stdout
        r = engine.run_cli("get", "show-sensitive")
        assert r.returncode == 0 and r.stdout.strip() == "false", r.stdout

        # With logging on, the same set succeeds.
        r = engine.run_cli("set", "logging", "true")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("set", "show-sensitive", "true")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("get", "show-sensitive")
        assert r.returncode == 0 and r.stdout.strip() == "true", r.stdout
    finally:
        engine.run_cli("set", "logging", "true")
        engine.run_cli("set", "show-sensitive", "false")


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


def test_removed_cli_keys_are_unknown(engine: CliEngine) -> None:
    """onnx-provider, the profile `enabled` key, the old use-openai-model
    name, and pii-types (owned by the `pii-types` command) are not get/set
    keys and must be rejected as unknown keys."""
    for args in (
        ("get", "onnx-provider"),
        ("set", "onnx-provider", "cpu"),
        ("get", "enabled"),
        ("set", "enabled", "true"),
        ("get", "use-openai-model"),
        ("set", "use-openai-model", "true"),
        ("get", "pii-types"),
        ("set", "pii-types", "secret"),
    ):
        r = engine.run_cli(*args)
        assert r.returncode == 2, (args, r.stdout)
        assert "unknown key" in r.stdout, (args, r.stdout)


def test_use_ai_model_roundtrip(engine: CliEngine) -> None:
    try:
        r = engine.run_cli("set", "use-ai-model", "false")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("get", "use-ai-model")
        assert r.returncode == 0, r.stdout
        assert r.stdout.strip() == "false"
    finally:
        engine.run_cli("set", "use-ai-model", "true")


def test_profiles_list_has_no_enabled_column(engine: CliEngine) -> None:
    r = engine.run_cli("profiles", "list")
    assert r.returncode == 0, r.stdout
    header = r.stdout.splitlines()[0]
    assert "enabled" not in header


def test_pii_types_list_marks_defaults(engine: CliEngine) -> None:
    """The CLI deals only in single PII types (no categories), like the GUI."""
    r = engine.run_cli("pii-types", "list")
    assert r.returncode == 0, r.stdout
    assert "FINANCIAL:" not in r.stdout
    assert "CONTACT:" not in r.stdout
    assert "[x] account_number" in r.stdout
    assert "[x] private_email" in r.stdout
    for t in ("secret", "private_address", "private_date", "private_person",
              "private_phone", "private_url"):
        assert ("[x] " + t) in r.stdout


def test_pii_types_categories_are_unknown(engine: CliEngine) -> None:
    """A category name (FINANCIAL/CONTACT/...) is not a PII type and must be
    rejected — the CLI supports only individual types."""
    r = engine.run_cli("pii-types", "disable", "CONTACT")
    assert r.returncode == 2
    assert "unknown PII type" in r.stdout


def test_pii_types_disable_enable_type(engine: CliEngine) -> None:
    try:
        r = engine.run_cli("pii-types", "disable", "secret")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("pii-types", "list")
        assert "[ ] secret" in r.stdout

        r = engine.run_cli("pii-types", "enable", "secret")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("pii-types", "list")
        assert "[x] secret" in r.stdout
    finally:
        engine.run_cli("pii-types", "enable", "secret")


def test_pii_types_bad_usage(engine: CliEngine) -> None:
    r = engine.run_cli("pii-types", "enable", "BOGUS")
    assert r.returncode == 2
    assert "unknown PII type" in r.stdout

    r = engine.run_cli("pii-types")
    assert r.returncode == 2
    assert "usage:" in r.stdout


def test_password_surface_usage(engine: CliEngine) -> None:
    """There is no typed password at all: no --password option anywhere, and
    the old `password change` / `password hello` subcommands are gone. The
    only subcommands are enable and disable, both Windows-Hello-only."""
    r = engine.run_cli("password")
    assert r.returncode == 2
    assert "usage: agentredactor password enable | disable" in r.stdout

    for args in (("password", "hello", "enable", "somepw"),
                 ("password", "hello", "disable"),
                 ("password", "change", "old", "new")):
        r = engine.run_cli(*args)
        assert r.returncode == 2, (args, r.stdout)
        assert "unknown password action" in r.stdout, (args, r.stdout)


def test_unlock_command_removed(engine: CliEngine) -> None:
    """`unlock` is no longer a CLI command: every gated command prompts for
    Windows Hello consent itself, so a dedicated unlock step is gone."""
    for args in (("unlock",), ("unlock", "--hello"), ("unlock", "--password", "x")):
        r = engine.run_cli(*args)
        assert r.returncode == 2, (args, r.stdout)
        assert "unknown command" in r.stdout or "unknown option" in r.stdout, (args, r.stdout)

    r = engine.run_cli("password", "disable")
    assert r.returncode == 0
    if sys.platform == "win32":
        assert "windows hello protection is not enabled" in r.stdout
    else:
        assert "master password protection is not enabled" in r.stdout

    r = engine.run_cli("status")
    assert r.returncode == 0
    assert "password enabled: false" in r.stdout


@pytest.mark.skipif(sys.platform != "win32", reason="Windows Hello consent flow is Windows-only; Linux uses the typed master password (test_cli_linux_password.py)")
def test_password_hello_consent_gate(engine: CliEngine) -> None:
    """Windows-Hello-only protection: `password enable` (no password anywhere),
    a fresh engine session starts locked, and EVERY gated command demands a
    fresh Windows Hello consent on the spot (the AGENTREDACTOR_HELLO_TIMEOUT_MS
    harness override makes the prompt fail fast when no user answers). Only
    status/help stay open. `password disable` is gated the same way: the
    engine runs the consent inside the disable call, so without a Verified
    result the protection stays on (tests strip it via settings surgery)."""
    r = engine.run_cli("password", "enable")
    assert r.returncode == 0, r.stdout
    assert "windows hello protection enabled" in r.stdout

    # Enabling twice is an error.
    r = engine.run_cli("password", "enable")
    assert r.returncode == 1
    assert "already enabled" in r.stdout

    r = engine.run_cli("status")
    assert r.returncode == 0
    assert "password enabled: true" in r.stdout

    # Restart the engine: a fresh session starts locked, like a real app open.
    engine.stop()
    engine.start()
    try:
        # status stays ungated; the locked session is verified through the
        # gated-command failures below.
        r = engine.run_cli("status")
        assert r.returncode == 0, r.stdout
        assert "password enabled: true" in r.stdout

        # Reads and writes are gated behind a fresh Hello consent; with no one
        # answering the prompt the command fails fast with a hello error.
        for args in (("get", "logging"), ("get", "api-key"), ("set", "logging", "false"),
                     ("regex", "list"), ("keywords", "list"), ("pii-types", "list"),
                     ("profiles", "list"), ("get", "confidence-threshold")):
            r = engine.run_cli(*args)
            assert r.returncode == 1, (args, r.stdout)
            assert "windows hello" in r.stdout, (args, r.stdout)

        # `password disable` is gated like every other protected action: the
        # engine runs the Windows Hello consent inside the disable call and
        # only disables on Verified. Under the suppress hook the consent
        # cancels, so disable fails and protection stays on.
        r = engine.run_cli("password", "disable")
        assert r.returncode == 1, r.stdout
        assert "windows hello consent canceled" in r.stdout, r.stdout
        r = engine.run_cli("status")
        assert r.returncode == 0
        assert "password enabled: true" in r.stdout
    finally:
        engine.strip_protection()
        r = engine.run_cli("status")
        assert r.returncode == 0
        assert "password enabled: false" in r.stdout


@pytest.mark.skipif(sys.platform != "win32", reason="Windows Hello suppress-prompt invariant is Windows-only; Linux uses the typed master password (test_cli_linux_password.py)")
def test_hello_suppress_prompt_flag_never_grants_access(engine: CliEngine) -> None:
    """SECURITY INVARIANT for the AGENTREDACTOR_HELLO_SUPPRESS_PROMPT test
    hook (set in conftest for the whole suite): the flag must behave exactly
    like the user pressing cancel — it never shows the prompt, never verifies,
    never unlocks, never serves the API key, and never disables protection. If
    this test ever fails, the flag has become a bypass and must be treated as
    a critical vulnerability.

    The session below is locked and the flag is set: every gated command must
    fail with a Windows Hello error, the session must stay locked, the API key
    must never be returned, and `password disable` must be rejected (the
    engine runs the consent inside the disable call)."""
    r = engine.run_cli("password", "enable")
    assert r.returncode == 0, r.stdout
    try:
        # A fresh engine session starts locked (like a real app open); the
        # gated-command failures below verify the locked state.
        engine.stop()
        engine.start()
        r = engine.run_cli("status")
        assert r.returncode == 0, r.stdout

        # Every gated command fails; none succeeds via the flag.
        for args in (("get", "api-key"), ("get", "logging"), ("set", "logging", "false"),
                     ("regex", "list"), ("keywords", "list"), ("pii-types", "list"),
                     ("profiles", "list"), ("get", "confidence-threshold")):
            r = engine.run_cli(*args)
            assert r.returncode == 1, (args, r.stdout)
            assert "windows hello" in r.stdout, (args, r.stdout)

        # The API key was never served.
        r = engine.run_cli("get", "api-key")
        assert r.stdout.strip() != TEST_API_KEY, r.stdout

        # Disabling protection is gated too: the engine's consent inside the
        # disable call can never be passed by the flag, so protection stays on.
        r = engine.run_cli("password", "disable")
        assert r.returncode == 1, r.stdout
        assert "windows hello consent canceled" in r.stdout, r.stdout
        r = engine.run_cli("status")
        assert "password enabled: true" in r.stdout

        # status/help stay open (ungated by design).
        r = engine.run_cli("status")
        assert r.returncode == 0
        r = engine.run_cli("help")
        assert r.returncode == 0
    finally:
        # Tests cannot pass Windows Hello; strip protection via settings.
        engine.strip_protection()


def test_engine_commands_removed(engine: CliEngine) -> None:
    """The `engine` subcommand is not part of the CLI surface at all — the
    GUI owns engine lifecycle. `engine stop` / `engine run` must be rejected
    as unknown commands (and the engine must keep running)."""
    for args in (("engine",), ("engine", "stop"), ("engine", "run"), ("engine", "run", "--console")):
        r = engine.run_cli(*args)
        assert r.returncode == 2, (args, r.stdout, r.stderr)
        assert "unknown command" in r.stdout or "unknown option" in r.stdout, (args, r.stdout)
    # The engine is untouched: status still works and the process is alive.
    r = engine.run_cli("status")
    assert r.returncode == 0 and "engine:" in r.stdout
    if engine.process:
        assert engine.process.poll() is None


def test_set_port_range_validation(engine: CliEngine) -> None:
    """Port range parity with the GUI: 1024..65535."""
    for bad in ("0", "1023", "65536", "999999999"):
        r = engine.run_cli("set", "port", bad)
        assert r.returncode == 2, (bad, r.stdout)
        assert "between 1024 and 65535" in r.stdout, (bad, r.stdout)
    # The valid existing port still saves fine.
    r = engine.run_cli("set", "port", str(engine.proxy_port))
    assert r.returncode == 0, r.stdout


def test_set_upstream_url_validation(engine: CliEngine) -> None:
    """Upstream URL validation parity with the GUI."""
    for bad, msg in (
        ("", "must not be empty"),
        ("ftp://host", "must start with http:// or https://"),
        ("http://", "has no host"),
        ("https:///path", "has no host"),
    ):
        r = engine.run_cli("set", "upstream-url", bad)
        assert r.returncode == 2, (bad, r.stdout)
        assert msg in r.stdout, (bad, r.stdout)


def test_regex_add_invalid_pattern_rejected(engine: CliEngine) -> None:
    """An unparseable regex is rejected before it reaches the engine (the GUI
    does the same via ValidateRegex)."""
    r = engine.run_cli("regex", "add", r"([unclosed")
    assert r.returncode == 2, r.stdout
    assert "invalid regex" in r.stdout

    r = engine.run_cli("regex", "add", "")
    assert r.returncode == 2, r.stdout
    assert "must not be empty" in r.stdout


def test_regex_add_accepts_brace_shorthand(engine: CliEngine) -> None:
    """The "{,N}" / "{,}" quantifier shorthand is accepted and normalized to
    "{0,N}" / "{0,}" (valid in .NET/PCRE/JS Annex-B; std::regex rejects the
    raw form) — matching the GUI's validation."""
    for pattern in ("a[a-z]{,10}", "a[a-z]{,}", "a{1,3}"):
        r = engine.run_cli("regex", "add", pattern)
        assert r.returncode == 0, (pattern, r.stdout)

    r = engine.run_cli("regex", "list")
    assert r.returncode == 0, r.stdout
    assert "a[a-z]{0,10}" in r.stdout, r.stdout
    assert "a[a-z]{0,}" in r.stdout, r.stdout
    assert "a{1,3}" in r.stdout, r.stdout



def test_profiles_add_returns_id_and_delete(engine: CliEngine) -> None:
    """profiles add prints the created profile's id; delete accepts ONLY the
    id (an alias or list number is rejected — it could point at a different
    profile later)."""
    r = engine.run_cli("profiles", "add", "second")
    assert r.returncode == 0, r.stdout
    assert "created profile" in r.stdout
    new_id = r.stdout.split("created profile ", 1)[1].split(" ", 1)[0].strip()
    assert new_id

    try:
        r = engine.run_cli("profiles", "list")
        assert r.returncode == 0, r.stdout
        assert "second" in r.stdout
        assert new_id in r.stdout

        # Delete by id.
        r = engine.run_cli("profiles", "delete", new_id)
        assert r.returncode == 0, r.stdout
        assert "deleted profile" in r.stdout
        r = engine.run_cli("profiles", "list")
        assert "second" not in r.stdout

        # An alias must NOT work as a delete selector.
        r = engine.run_cli("profiles", "add", "third")
        assert r.returncode == 0, r.stdout
        r = engine.run_cli("profiles", "delete", "third")
        assert r.returncode == 1, r.stdout
        assert "profile not found" in r.stdout
        # The profile is still there (and cleanup uses the id).
        r = engine.run_cli("profiles", "list")
        assert "third" in r.stdout
    finally:
        engine.run_cli("profiles", "delete", new_id)


def test_profiles_add_flags_and_validation(engine: CliEngine) -> None:
    r = engine.run_cli("profiles", "add", "second", "--port", "18080", "--upstream-url", "https://llm.example/v1")
    assert r.returncode == 0, r.stdout
    second_id = r.stdout.split("created profile ", 1)[1].split(" ", 1)[0].strip()

    # Alias validation.
    r = engine.run_cli("profiles", "add", "  ")
    assert r.returncode == 2
    assert "alias must not be empty" in r.stdout

    # Port validation mirrors set port.
    r = engine.run_cli("profiles", "add", "third", "--port", "80")
    assert r.returncode == 2
    assert "between 1024 and 65535" in r.stdout

    # Port used by another profile (the automated coverage the user asked for).
    r = engine.run_cli("profiles", "add", "third", "--port", "18080")
    assert r.returncode == 2, r.stdout
    assert "already used by profile" in r.stdout

    # Upstream URL validation.
    r = engine.run_cli("profiles", "add", "third", "--upstream-url", "not-a-url")
    assert r.returncode == 2
    assert "must start with http:// or https://" in r.stdout

    r = engine.run_cli("profiles", "delete", second_id)
    assert r.returncode == 0, r.stdout


def test_set_port_conflict_across_profiles(engine: CliEngine) -> None:
    """`set port` rejects a port already used by another profile."""
    r = engine.run_cli("profiles", "add", "second", "--port", "18081")
    assert r.returncode == 0, r.stdout
    second_id = r.stdout.split("created profile ", 1)[1].split(" ", 1)[0].strip()
    try:
        r = engine.run_cli("set", "port", "18081", "--profile", "test-profile")
        assert r.returncode == 2, r.stdout
        assert "already used by profile 'second'" in r.stdout
        # The value was not applied.
        r = engine.run_cli("get", "port", "--profile", "test-profile")
        assert r.returncode == 0
        assert r.stdout.strip() == str(engine.proxy_port)
    finally:
        engine.run_cli("profiles", "delete", second_id)


def test_profiles_delete_guards(engine: CliEngine) -> None:
    # Defensive cleanup: a failed earlier test may have left extra profiles.
    r = engine.run_cli("profiles", "list")
    assert r.returncode == 0
    for line in r.stdout.splitlines():
        if "second" in line or "third" in line:
            engine.run_cli("profiles", "delete", line.split()[2])

    r = engine.run_cli("profiles", "delete", "nope")
    assert r.returncode == 1
    assert "profile not found" in r.stdout

    # An alias is not a valid delete selector.
    r = engine.run_cli("profiles", "delete", "test-profile")
    assert r.returncode == 1
    assert "profile not found" in r.stdout

    # Deleting the last (only) profile by its real id is refused.
    r = engine.run_cli("profiles", "list")
    assert r.returncode == 0
    id_line = next(line for line in r.stdout.splitlines() if "test-profile" in line)
    test_id = id_line.split()[2]
    r = engine.run_cli("profiles", "delete", test_id)
    assert r.returncode == 2
    assert "cannot delete the last profile" in r.stdout

    r = engine.run_cli("profiles", "list")
    assert r.returncode == 0
    assert "test-profile" in r.stdout
