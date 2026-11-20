"""Regression tests for the Windows fresh-install profile failures.

Two bugs shipped together and presented as "on a fresh install there are no
profiles and nothing can be saved", on BOTH the Microsoft Store (MSIX) and the
Velopack self-release channel:

1. No guard on the control-API port. The engine binds 127.0.0.1:8737 with
   SO_REUSEADDR. On Windows that lets a SECOND process bind the same
   address:port while the FIRST keeps receiving every connection. A second
   engine therefore "started" successfully, overwrote control.json with its own
   token and pid, and then never saw a request -- the GUI read the new token,
   talked to the OLD process, and got 401 for every call. The GUI maps any
   failed read to an empty profile list, so the symptom was an empty Profiles
   panel plus saves that silently did nothing.

2. The GUI seeded its default profile over the control API but discarded the
   result (SettingsFacade::AddProfile/UpdateProfile returned void), so a failed
   seed or save was indistinguishable from success.

Why CI missed it: tests/gui/conftest.py calls create_settings() for EVERY
fixture, which writes a settings.json that already contains a profile, and the
CLI suite starts from create_settings() too. No test ever exercised a config
dir with NO settings.json, and no test ever asserted that a second engine cannot
hijack the control port. The tests below close both gaps.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest

from conftest import ENGINE_EXE, _wait_for_control_json

IS_WINDOWS = sys.platform == "win32"


def _creation_flags() -> int:
    return subprocess.CREATE_NO_WINDOW if IS_WINDOWS else 0


def _engine_env(config_dir: Path) -> dict[str, str]:
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    # Cancel-equivalent Hello hook: never shows a prompt, never grants access.
    env["AGENTREDACTOR_HELLO_SUPPRESS_PROMPT"] = "1"
    env["AGENTREDACTOR_HELLO_TIMEOUT_MS"] = "1000"
    return env


def _api_call(port: int, token: str, method: str, path: str, body: dict | None = None):
    """Talk to the control API exactly as the GUIs do."""
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


class _Engine:
    """Starts the engine against a given config dir and exposes its control API."""

    def __init__(self, config_dir: Path) -> None:
        self.config_dir = config_dir
        self.process: subprocess.Popen | None = None

    def start(self, timeout: float = 180.0) -> None:
        stale = self.config_dir / "control.json"
        if stale.exists():
            stale.unlink()
        self.process = subprocess.Popen(
            [str(ENGINE_EXE)],
            env=_engine_env(self.config_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=_creation_flags(),
        )
        try:
            _wait_for_control_json(self.config_dir, timeout=timeout, process=self.process)
        except RuntimeError:
            # Distinguish "engine died early" (a real regression) from "engine
            # slow" (timeout): report the exit code in the failure message.
            rc = self.process.poll()
            if rc is not None:
                self.stop()
                raise RuntimeError(
                    f"engine exited with rc={rc} before writing control.json "
                    f"in {self.config_dir}"
                ) from None
            raise

    @property
    def control(self) -> dict:
        return json.loads((self.config_dir / "control.json").read_text(encoding="utf-8"))

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        self.process = None


@pytest.fixture
def fresh_engine(tmp_path: Path):
    """An engine over a config dir with NO settings.json (a true fresh install)."""
    if not ENGINE_EXE.exists():
        pytest.skip(f"engine exe not built: {ENGINE_EXE}")
    config_dir = tmp_path / "fresh-config"
    config_dir.mkdir(parents=True, exist_ok=True)
    assert not (config_dir / "settings.json").exists()
    eng = _Engine(config_dir)
    eng.start()
    try:
        yield eng
    finally:
        eng.stop()


# ---------------------------------------------------------------------------
# The fresh-install profile path
# ---------------------------------------------------------------------------


def test_fresh_install_control_api_accepts_the_advertised_token(fresh_engine: _Engine) -> None:
    """The headline bug: the token in control.json was rejected with 401.

    A 401 here means the port is served by a process that does not know the
    advertised token (the stale-engine collision), which is what made the GUI
    see no profiles and drop every save.
    """
    ctl = fresh_engine.control
    status, body = _api_call(ctl["port"], ctl["token"], "GET", "/status")
    assert status == 200, (
        f"control API rejected the token it just wrote to control.json "
        f"(HTTP {status}: {body}). A foreign process is serving port "
        f"{ctl['port']} -- the stale-engine collision regression."
    )


def test_fresh_install_starts_with_zero_profiles(fresh_engine: _Engine) -> None:
    """A config dir with no settings.json really has no profiles yet."""
    ctl = fresh_engine.control
    status, body = _api_call(ctl["port"], ctl["token"], "GET", "/profiles")
    assert status == 200, body
    assert json.loads(body) == [], (
        "a genuinely fresh install should report zero profiles before the GUI "
        "seeds its default"
    )


def test_fresh_install_seeds_and_saves_a_default_profile(fresh_engine: _Engine) -> None:
    """The exact sequence HomePage::LoadData() + Save perform.

    Seeds the default profile over the control API, then saves an edit, then
    asserts both survived to settings.json on disk.
    """
    ctl = fresh_engine.control
    port, token = ctl["port"], ctl["token"]
    profile = {
        "alias": "Default",
        "upstream_url": "https://api.example.com/v1",
        "api_key": "sk-fresh-install-test",
        "port": 8080,
        "use_openai_model": True,
        "enabled_pii_types": ["private_email"],
        "pii_confidence_threshold": 0.9,
        "regex_patterns": [],
        "keywords": [],
        "enabled": True,
    }

    status, body = _api_call(port, token, "POST", "/profiles", profile)
    assert status == 200, f"seeding the default profile failed: HTTP {status} {body}"

    status, body = _api_call(port, token, "GET", "/profiles")
    assert status == 200, body
    profiles = json.loads(body)
    assert len(profiles) == 1, f"expected the seeded profile, got {profiles}"

    # Now save an edit, the way the Save button does.
    profile_id = profiles[0]["id"]
    edited = dict(profiles[0])
    edited["alias"] = "Saved-Alias"
    status, body = _api_call(port, token, "PUT", f"/profiles/{profile_id}", edited)
    assert status == 200, f"saving the profile failed: HTTP {status} {body}"

    status, body = _api_call(port, token, "GET", "/profiles")
    assert status == 200, body
    saved = json.loads(body)
    assert saved[0]["alias"] == "Saved-Alias", "the save did not persist"

    # And it must be on disk, not just in memory.
    settings = json.loads((fresh_engine.config_dir / "settings.json").read_text(encoding="utf-8"))
    assert "profiles" in settings, "settings.json was never given a profiles array"
    assert len(settings["profiles"]) == 1
    assert settings["profiles"][0]["alias"] == "Saved-Alias"


def test_fresh_install_keeps_the_advertised_port_served_by_us(fresh_engine: _Engine) -> None:
    """Whoever answers the advertised port must accept the advertised token.

    Guards the general contract behind the collision bug: control.json must
    never point at a socket this engine does not serve.
    """
    ctl = fresh_engine.control
    assert ctl["pid"] == fresh_engine.process.pid, (
        "control.json was written by a different process than the engine we started"
    )
    status, _ = _api_call(ctl["port"], ctl["token"], "GET", "/status")
    assert status == 200


# ---------------------------------------------------------------------------
# The control-port collision / multi-user guarantee
# ---------------------------------------------------------------------------


def test_two_users_get_independent_control_ports(tmp_path: Path) -> None:
    """Two users on one machine must each get a working engine.

    The engine is a per-USER singleton (settings, the control token, and API
    keys all live in that user's own config dir), but the control port is
    machine-wide. Two users therefore race for 127.0.0.1:8737, and on Windows
    SO_REUSEADDR used to let the second engine bind the port the first was
    already serving: the newcomer overwrote ITS control.json with a token the
    serving process rejected, so the GUI saw 401 on everything and showed an
    empty profile list with saves that did nothing.

    Two config dirs stand in for two users (the engine keys its per-user state,
    including the single-instance guard, on the config dir). Both engines must
    come up, advertise a port THEY serve, and authenticate with their own token.
    """
    if not ENGINE_EXE.exists():
        pytest.skip(f"engine exe not built: {ENGINE_EXE}")

    user_one = tmp_path / "user-one"
    user_two = tmp_path / "user-two"
    user_one.mkdir(parents=True, exist_ok=True)
    user_two.mkdir(parents=True, exist_ok=True)

    eng_one = _Engine(user_one)
    eng_two = _Engine(user_two)
    try:
        eng_one.start()
        eng_two.start()

        ctl_one, ctl_two = eng_one.control, eng_two.control

        # Each user's token must work against the port that user's engine
        # advertised. A 401 here is the stale-engine/multi-user regression.
        status_one, body_one = _api_call(ctl_one["port"], ctl_one["token"], "GET", "/status")
        assert status_one == 200, (
            f"user one's own token was rejected by the port it advertised "
            f"(HTTP {status_one}: {body_one})"
        )
        status_two, body_two = _api_call(ctl_two["port"], ctl_two["token"], "GET", "/status")
        assert status_two == 200, (
            f"user two's own token was rejected by the port it advertised "
            f"(HTTP {status_two}: {body_two}). Two engines are sharing one "
            f"control port, so one user's GUI would see no profiles and be "
            f"unable to save."
        )

        # The engines must be distinct processes.
        assert ctl_one["pid"] != ctl_two["pid"], "both users ended up on one engine process"

        # And each must be able to seed and save its own profile independently.
        for label, ctl in (("user one", ctl_one), ("user two", ctl_two)):
            port, token = ctl["port"], ctl["token"]
            profile = {
                "alias": f"Default-{label.replace(' ', '-')}",
                "upstream_url": "https://api.example.com/v1",
                "api_key": "sk-per-user-test",
                "port": 8080,
                "use_openai_model": True,
                "enabled_pii_types": ["private_email"],
                "pii_confidence_threshold": 0.9,
                "regex_patterns": [],
                "keywords": [],
                "enabled": True,
            }
            status, body = _api_call(port, token, "POST", "/profiles", profile)
            assert status == 200, f"{label}: seeding the default profile failed: {body}"
            status, body = _api_call(port, token, "GET", "/profiles")
            assert status == 200, f"{label}: {body}"
            assert len(json.loads(body)) == 1, f"{label}: expected exactly one profile"
    finally:
        eng_two.stop()
        eng_one.stop()


def test_same_user_second_engine_exits_quietly(tmp_path: Path) -> None:
    """The per-user guard must still stop a SECOND engine for the SAME user.

    Scoping the guard per user (rather than machine-wide) is what lets two users
    each run an engine; it must not weaken the same-user protection, or two
    engines would contend for one control port again.
    """
    if not ENGINE_EXE.exists():
        pytest.skip(f"engine exe not built: {ENGINE_EXE}")

    config_dir = tmp_path / "same-user"
    config_dir.mkdir(parents=True, exist_ok=True)

    first = _Engine(config_dir)
    second = _Engine(config_dir)
    try:
        first.start()
        assert _api_call(first.control["port"], first.control["token"], "GET", "/status")[0] == 200

        # A second engine for the same user must refuse to run. It gets no
        # control.json of its own; the original keeps serving.
        second.process = subprocess.Popen(
            [str(ENGINE_EXE)],
            env=_engine_env(config_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=_creation_flags(),
        )
        try:
            returncode = second.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            second.stop()
            pytest.fail(
                "a second engine started for the same user; it would race the "
                "first one for the control port"
            )
        assert returncode == 0, (
            f"a second engine for the same user exited with {returncode} "
            f"instead of quietly standing down"
        )

        # The original engine must still be healthy and authoritative.
        status, body = _api_call(first.control["port"], first.control["token"], "GET", "/status")
        assert status == 200, f"the first engine was disturbed: HTTP {status} {body}"
    finally:
        second.stop()
        first.stop()
