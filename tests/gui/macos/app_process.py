from __future__ import annotations

import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path


class MacAppProcess:
    def __init__(self, app: Path, config_dir: Path, home_dir: Path | None = None) -> None:
        self.app = app
        self.config_dir = config_dir
        self.home_dir = home_dir
        self.process: subprocess.Popen | None = None
        self._log = None

    def _environment(self) -> dict[str, str]:
        env = dict(os.environ)
        env["AGENTREDACTOR_CONFIG_DIR"] = str(self.config_dir)
        env["AGENTREDACTOR_DISABLE_KEYRING"] = "1"
        if self.home_dir:
            env["HOME"] = str(self.home_dir)
        return env

    def start(self) -> None:
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self._log = open(self.config_dir / "gui-stdout.log", "a")
        self.process = subprocess.Popen(
            [str(self.app / "Contents" / "MacOS" / "AgentRedactor")],
            env=self._environment(), stdout=self._log, stderr=self._log)

    @property
    def pid(self) -> int:
        assert self.process is not None
        return self.process.pid

    def reopen(self) -> None:
        subprocess.run(
            [str(self.app / "Contents" / "MacOS" / "AgentRedactor")],
            env=self._environment(), stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, timeout=15, check=True)

    def _control(self) -> dict:
        return json.loads((self.config_dir / "control.json").read_text())

    def api(self, method: str, path: str, body: dict | None = None) -> dict:
        control = self._control()
        request = urllib.request.Request(
            f"http://127.0.0.1:{control['port']}{path}",
            data=None if body is None else json.dumps(body).encode(),
            headers={"Authorization": f"Bearer {control['token']}",
                     "Content-Type": "application/json"}, method=method)
        with urllib.request.urlopen(request, timeout=15) as response:
            return json.load(response)

    def wait_for_engine(self, timeout: float = 45) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process and self.process.poll() is not None:
                raise AssertionError(f"GUI exited during startup: {self.process.returncode}")
            if (self.config_dir / "control.json").is_file():
                try:
                    self.api("GET", "/status")
                    return
                except Exception:
                    pass
            time.sleep(0.2)
        raise AssertionError("engine did not become ready")

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if (self.config_dir / "control.json").is_file():
            try:
                self.api("POST", "/engine/stop")
            except Exception:
                pass
        if self._log:
            self._log.close()
            self._log = None
