"""Start and stop AgentRedactorUI.exe (and its agentredactor.exe engine) for GUI-driven tests.

This runs the normal Release build without --tray-only, so the app uses the
real %APPDATA%/%LOCALAPPDATA% locations. The caller is responsible for backing
up and restoring those dirs.
"""

from __future__ import annotations

import platform
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import psutil


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
# The vcxproj outputs to build/<Platform>/Release where <Platform> is the
# MSBuild platform name (x64 / ARM64). Match the host architecture.
_BUILD_PLATFORM = "ARM64" if platform.machine().upper() == "ARM64" else "x64"
DEFAULT_EXE = PROJECT_ROOT / "windows" / "build" / _BUILD_PLATFORM / "Release" / "AgentRedactorUI.exe"

# Process image names to clean up between tests: the engine owns the proxy
# ports and the control API, so a stale one would leak settings state and
# ports into the next test. On Linux the GUI test harness (tests/linux/)
# owns its own agentredactor-gui lifecycle; killing it here between regular
# tests races the AT-SPI accessibility tree and can leave rows missing.
_PROCESS_NAMES = (
    ("agentredactorui.exe", "agentredactor.exe")
    if sys.platform == "win32"
    else ("agentredactor",)
)


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _kill_existing_agent_redactor(
    timeout: float = 10.0,
    grace_period: float = 2.0,
) -> list[int]:
    """Terminate any running GUI/engine processes and return killed PIDs."""
    killed: list[int] = []
    targets = []
    for proc in psutil.process_iter(["name"]):
        try:
            if proc.info["name"] and proc.info["name"].lower() in _PROCESS_NAMES:
                targets.append(proc)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    if not targets:
        return killed

    # Graceful termination first.
    for proc in targets:
        try:
            proc.terminate()
        except psutil.NoSuchProcess:
            pass

    gone, alive = psutil.wait_procs(targets, timeout=grace_period)

    # Force-kill anything still alive.
    for proc in alive:
        try:
            proc.kill()
        except psutil.NoSuchProcess:
            pass

    gone2, alive2 = psutil.wait_procs(alive, timeout=timeout)

    for proc in gone + gone2:
        try:
            killed.append(proc.pid)
        except Exception:
            pass

    if alive2:
        print(
            f"Warning: could not terminate AgentRedactor PIDs: {[p.pid for p in alive2]}",
            file=sys.stderr,
        )

    return killed


def _wait_for_port(port: int, timeout: float = 90.0) -> bool:
    """Wait longer than the backend helper: the GUI app must load models."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


class GuiAppProcess:
    """Manages an AgentRedactorUI.exe process for GUI tests."""

    def __init__(
        self,
        proxy_port: int,
        exe_path: Path | None = None,
    ) -> None:
        self.proxy_port = proxy_port
        self.exe_path = exe_path or DEFAULT_EXE
        self.process: subprocess.Popen[Any] | None = None
        self._stdout_file: Any | None = None
        self._stderr_file: Any | None = None

    def start(self) -> None:
        if not self.exe_path.exists():
            raise FileNotFoundError(f"AgentRedactorUI.exe not found at {self.exe_path}")

        _kill_existing_agent_redactor()

        cmd = [str(self.exe_path)]
        # Start without a console window on Windows
        creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        # Redirect output to a temp location under LOCALAPPDATA
        import os
        local_app_data = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        log_dir = local_app_data / "AgentRedactor" / "sessions"
        log_dir.mkdir(parents=True, exist_ok=True)
        stdout_path = log_dir / "gui_test_stdout.txt"
        stderr_path = log_dir / "gui_test_stderr.txt"
        self._stdout_file = open(stdout_path, "w", encoding="utf-8")
        self._stderr_file = open(stderr_path, "w", encoding="utf-8")
        self.process = subprocess.Popen(
            cmd,
            stdout=self._stdout_file,
            stderr=self._stderr_file,
            creationflags=creationflags,
        )

        if not _wait_for_port(self.proxy_port, timeout=90.0):
            details = []
            returncode = self.process.poll()
            if returncode is not None:
                details.append(f"process exited early with code {returncode} (0x{returncode & 0xFFFFFFFF:08X})")
            # The app writes milestone traces to debug.log next to the exe and
            # its real log to %APPDATA%\AgentRedactor\agent_redactor.log.
            debug_log = self.exe_path.parent / "debug.log"
            for path in (debug_log,):
                if path.exists():
                    try:
                        tail = path.read_text(encoding="utf-8", errors="ignore")[-2000:]
                        details.append(f"--- tail of {path} ---\n{tail}")
                    except Exception:
                        pass
            self.stop()
            msg = f"AgentRedactor did not start listening on port {self.proxy_port} (GUI or engine failed to start)"
            if details:
                msg += "\n" + "\n".join(details)
            raise RuntimeError(msg)

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
            if self._stdout_file:
                try:
                    self._stdout_file.close()
                except Exception:
                    pass
                self._stdout_file = None
            if self._stderr_file:
                try:
                    self._stderr_file.close()
                except Exception:
                    pass
                self._stderr_file = None
            # Ensure no GUI/engine processes linger; this avoids
            # file-handle locks on the log/session files during fixture teardown.
            _kill_existing_agent_redactor()

    def __enter__(self) -> "GuiAppProcess":
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.stop()

    @property
    def proxy_url(self) -> str:
        return f"http://127.0.0.1:{self.proxy_port}"
