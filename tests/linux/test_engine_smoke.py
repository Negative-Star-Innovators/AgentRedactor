"""Linux engine smoke tests: lifecycle, single-instance lock, file permissions.

These run the real Linux engine binary against an isolated config dir
(AGENTREDACTOR_CONFIG_DIR) and assert the platform contract that the Windows
suite gets implicitly: the engine starts and stops cleanly, control.json is
0600, the config dir is 0700, and a second engine instance is refused.
"""

from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
import time
from pathlib import Path

import pytest

_tests_root = Path(__file__).resolve().parent.parent
for _p in (str(_tests_root), str(_tests_root / "gui")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from config_factory import create_settings  # noqa: E402
from gui_process import _find_free_port, _kill_existing_agent_redactor, _wait_for_port  # noqa: E402

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux engine smoke tests")

PROJECT_ROOT = _tests_root.parent
ENGINE_BIN = Path(
    os.environ.get("AGENTREDACTOR_ENGINE_BIN")
    or PROJECT_ROOT / "linux" / "build" / "engine" / "agentredactor"
)


def _wait_for_control_json(config_dir: Path, timeout: float = 90.0) -> None:
    deadline = time.monotonic() + timeout
    path = config_dir / "control.json"
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return
        time.sleep(0.1)
    raise RuntimeError(f"control.json did not appear in {config_dir}")


@pytest.fixture()
def engine(tmp_path: Path):
    """A running engine with one seeded profile; yields (process, config_dir, port)."""
    if not ENGINE_BIN.is_file():
        pytest.skip(f"engine binary not built: {ENGINE_BIN}")
    config_dir = tmp_path / "config"
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url="http://127.0.0.1:9",  # unreachable on purpose; no traffic sent
        api_key="sk-linux-smoke",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    _kill_existing_agent_redactor()
    proc = subprocess.Popen(
        [str(ENGINE_BIN)],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        _wait_for_control_json(config_dir)
        if not _wait_for_port(proxy_port, timeout=90.0):
            raise RuntimeError(f"engine did not start listening on port {proxy_port}")
        yield proc, config_dir, proxy_port, env
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        _kill_existing_agent_redactor()


def test_engine_starts_and_stops_cleanly(engine) -> None:
    proc, config_dir, proxy_port, env = engine
    assert proc.poll() is None

    r = subprocess.run(
        [str(ENGINE_BIN), "status"],
        env=env,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    assert "engine:" in r.stdout
    assert "proxy running" in r.stdout

    proc.terminate()
    proc.wait(timeout=5)
    assert proc.returncode is not None


def test_control_json_and_config_dir_permissions(engine) -> None:
    _, config_dir, _, _ = engine
    control = config_dir / "control.json"
    assert stat.S_IMODE(control.stat().st_mode) == 0o600
    assert stat.S_IMODE(config_dir.stat().st_mode) == 0o700

    # control.json carries the bearer token the CLI authenticates with.
    data = json.loads(control.read_text(encoding="utf-8"))
    assert data.get("token")


def test_second_engine_instance_is_refused(engine) -> None:
    proc, config_dir, _, env = engine
    assert proc.poll() is None
    # The single-instance lock (flock on engine.lock, mirroring the Windows
    # named mutex) makes a second engine with the same config dir exit quietly
    # with code 0 without taking over: control.json must stay the first
    # engine's (its bearer token would otherwise no longer authenticate).
    control = config_dir / "control.json"
    before = control.read_bytes()
    r = subprocess.run(
        [str(ENGINE_BIN)],
        env=env,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert r.returncode == 0
    assert control.read_bytes() == before
    assert proc.poll() is None  # the first engine is unaffected
