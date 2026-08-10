"""pytest fixtures for the agentredactor.exe CLI tests.

Unlike the GUI tests (which use the real %APPDATA% dirs), the CLI tests run
the engine against an isolated config dir via AGENTREDACTOR_CONFIG_DIR and
drive `agentredactor <subcommand>` as a subprocess, capturing its stdout —
the same way a script or AI agent would.
"""

from __future__ import annotations

import os
import platform
import subprocess
import sys
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

# Allow importing shared test utilities from tests/ and tests/gui/
_tests_root = Path(__file__).resolve().parent.parent
_gui_dir = _tests_root / "gui"
for _p in (str(_tests_root), str(_gui_dir)):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from config_factory import create_settings  # noqa: E402
from gui_process import _find_free_port, _kill_existing_agent_redactor, _wait_for_port  # noqa: E402

PROJECT_ROOT = _tests_root.parent
_BUILD_PLATFORM = "ARM64" if platform.machine().upper() == "ARM64" else "x64"
ENGINE_EXE = PROJECT_ROOT / "windows" / "build" / _BUILD_PLATFORM / "Release" / "agentredactor.exe"

TEST_API_KEY = "sk-cli-test-key"


def _wait_for_control_json(config_dir: Path, timeout: float = 90.0) -> None:
    deadline = time.monotonic() + timeout
    path = config_dir / "control.json"
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return
        time.sleep(0.1)
    raise RuntimeError(f"control.json did not appear in {config_dir}")


class CliEngine:
    """Manages a standalone agentredactor.exe engine for CLI tests."""

    def __init__(self, config_dir: Path, proxy_port: int) -> None:
        self.config_dir = config_dir
        self.proxy_port = proxy_port
        self.process: subprocess.Popen | None = None

    def _env(self) -> dict[str, str]:
        env = dict(os.environ)
        env["AGENTREDACTOR_CONFIG_DIR"] = str(self.config_dir)
        return env

    def start(self) -> None:
        _kill_existing_agent_redactor()
        # Drop any stale control.json so the wait below cannot return on a
        # previous engine's file (the token would not match the new engine).
        stale = self.config_dir / "control.json"
        if stale.exists():
            stale.unlink()
        creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        self.process = subprocess.Popen(
            [str(ENGINE_EXE)],
            env=self._env(),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=creationflags,
        )
        _wait_for_control_json(self.config_dir)
        if not _wait_for_port(self.proxy_port, timeout=90.0):
            self.stop()
            raise RuntimeError(f"engine did not start listening on port {self.proxy_port}")

    def run_cli(self, *args: str) -> subprocess.CompletedProcess:
        # stdin=DEVNULL: no interactive console, so a locked engine must
        # reject commands instead of prompting (and hanging the test).
        return subprocess.run(
            [str(ENGINE_EXE), *args],
            env=self._env(),
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def stop(self) -> None:
        try:
            if self.process and self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    try:
                        self.process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        pass
        finally:
            self.process = None
            _kill_existing_agent_redactor()


@pytest.fixture(scope="module")
def engine(tmp_path_factory: pytest.TempPathFactory) -> Iterator[CliEngine]:
    """A running engine with one seeded profile, shared by the module's tests."""
    if not ENGINE_EXE.exists():
        pytest.skip(f"engine exe not built: {ENGINE_EXE}")
    config_dir = tmp_path_factory.mktemp("cli-config")
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url="http://127.0.0.1:9",  # unreachable on purpose; no traffic sent
        api_key=TEST_API_KEY,
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )
    eng = CliEngine(config_dir, proxy_port)
    eng.start()
    yield eng
    eng.stop()
