"""Headless settings-schema migration tests.

Each test drives the real agentredactor binary (AgentRedactorUI.exe on
Windows, the Linux engine binary elsewhere) with --selftest-migrate-settings
against an isolated config dir (AGENTREDACTOR_CONFIG_DIR) — no GUI session is
needed. The exe path comes from AGENTREDACTOR_EXE or the local build;
the whole module skips when no binary is available. See tests/migration/README.md.
"""

from __future__ import annotations

import json
import shutil
from pathlib import Path

import pytest

FIXTURES = Path(__file__).parent / "fixtures" / "settings"

# Defaults the app fills in for keys missing from a settings file
# (SettingsManager ctor in src/settings_manager.cpp).
APP_DEFAULTS = {
    "app_language": "",
    "logging_enabled": False,
    "settings_version": 2,
    "start_on_boot": True,
}


def _v1_fixtures() -> list[Path]:
    return sorted(
        p
        for p in FIXTURES.glob("v1_*.json")
        if not p.name.endswith(".expected.json")
    )


def _expected_path(fixture: Path) -> Path:
    return fixture.with_name(fixture.name.replace(".json", ".expected.json"))


@pytest.mark.parametrize("fixture", _v1_fixtures(), ids=lambda p: p.name)
def test_v1_fixture_migrates_to_v2(fixture: Path, run_selftest, tmp_path):
    expected_path = _expected_path(fixture)
    assert expected_path.is_file(), f"missing expected-output fixture {expected_path}"

    shutil.copy(fixture, tmp_path / "settings.json")
    result = run_selftest(tmp_path)

    assert result.returncode == 0, f"selftest failed: {result.stdout} {result.stderr}"
    assert "SETTINGS_MIGRATION_OK" in result.stdout

    migrated = json.loads((tmp_path / "settings.json").read_text(encoding="utf-8"))
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    assert migrated == expected
    assert migrated["settings_version"] == 2
    assert "verbose_logging" not in migrated


def test_v2_fixture_is_left_untouched(run_selftest, tmp_path):
    fixture = FIXTURES / "v2_defaults.json"
    original_bytes = fixture.read_bytes()
    shutil.copy(fixture, tmp_path / "settings.json")

    result = run_selftest(tmp_path)

    assert result.returncode == 0, f"selftest failed: {result.stdout} {result.stderr}"
    assert "SETTINGS_MIGRATION_OK" in result.stdout
    # Already current-schema with all defaults present: the app must not
    # rewrite the file at all.
    assert (tmp_path / "settings.json").read_bytes() == original_bytes


def test_corrupt_json_is_backed_up_and_reset(run_selftest, tmp_path):
    corrupt = b'{"start_on_boot": true, "verbose_log'
    (tmp_path / "settings.json").write_bytes(corrupt)

    result = run_selftest(tmp_path)

    assert result.returncode == 0, f"selftest failed: {result.stdout} {result.stderr}"
    assert "SETTINGS_MIGRATION_OK" in result.stdout

    # A fresh, valid, schema-current settings file is produced.
    repaired = json.loads((tmp_path / "settings.json").read_text(encoding="utf-8"))
    assert repaired == APP_DEFAULTS

    # The unreadable original is preserved as a timestamped backup.
    backups = list(tmp_path.glob("settings.json.corrupt-*.bak"))
    assert len(backups) == 1, f"expected one .corrupt-*.bak, found {backups}"
    assert backups[0].read_bytes() == corrupt


def test_wrong_typed_values_do_not_crash(run_selftest, tmp_path):
    # Valid JSON, wrong types for known keys. The app only fills in *missing*
    # keys — it does not re-type existing ones — so "start_on_boot" keeps its
    # (bogus) value; the contract under test is: no crash, file stays valid
    # JSON, schema version is stamped, missing defaults are applied.
    (tmp_path / "settings.json").write_text(
        '{"start_on_boot": "yes"}', encoding="utf-8"
    )

    result = run_selftest(tmp_path)

    assert result.returncode == 0, f"selftest failed: {result.stdout} {result.stderr}"
    assert "SETTINGS_MIGRATION_OK" in result.stdout

    repaired = json.loads((tmp_path / "settings.json").read_text(encoding="utf-8"))
    assert repaired["settings_version"] == 2
    assert repaired["logging_enabled"] is False
    assert repaired["app_language"] == ""
