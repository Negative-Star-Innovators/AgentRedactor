"""Helpers for configuring and driving OpenClaw from the third-party tests."""

from __future__ import annotations

import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Optional


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = PROJECT_ROOT / "scripts"
TEST_LOG_DIR = PROJECT_ROOT / "third_party_tests" / ".logs"

# The OpenClaw setup scripts default to this gateway HTTP port.
GATEWAY_HTTP_PORT = 18789


def _powershell(cmd: list[str], timeout: float = 180.0) -> subprocess.CompletedProcess[str]:
    """Run a PowerShell command and return the completed process."""
    full_cmd = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        *cmd,
    ]
    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    return subprocess.run(
        full_cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="ignore",
        timeout=timeout,
        creationflags=creationflags,
        check=False,
    )


def run_setup_script(script_name: str, base_url: str = "http://localhost:8081/") -> str:
    """Run an OpenClaw setup script and return its stdout.

    The gateway is not started by the script; the caller starts it afterwards.
    """
    script_path = SCRIPTS_DIR / script_name
    if not script_path.exists():
        raise FileNotFoundError(f"Setup script not found: {script_path}")

    result = _powershell(
        [
            "-File",
            str(script_path),
            "-BaseUrl",
            base_url,
            "-NoGatewayStart",
        ]
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Setup script {script_name} failed with rc={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result.stdout


def _find_openclaw_cmd() -> str:
    """Find the openclaw command executable (.cmd shim preferred)."""
    for name in ("openclaw.cmd", "openclaw"):
        path = shutil.which(name)
        if path:
            return path
    raise FileNotFoundError(
        "openclaw not found in PATH. "
        "Make sure OpenClaw is installed and its npm global bin folder is on PATH."
    )


def _openclaw_command() -> list[str]:
    """Return the OpenClaw CLI invocation as a list.

    We bypass the npm ``.cmd`` shim and invoke Node directly with the OpenClaw
    ESM entry point. This avoids intermittent "'node' is not recognized" errors
    from the shim when the subprocess environment differs from the parent shell.
    """
    node_path = shutil.which("node")
    if not node_path:
        raise FileNotFoundError(
            "node not found in PATH. Make sure Node.js is installed and on PATH."
        )

    # The .cmd shim lives next to node_modules/openclaw/openclaw.mjs.
    shim_path = Path(_find_openclaw_cmd())
    mjs_path = shim_path.parent / "node_modules" / "openclaw" / "openclaw.mjs"
    if not mjs_path.exists():
        # Fallback to a global node_modules location if the shim is not in npm.
        for candidate in (
            Path(node_path).parent.parent / "node_modules" / "openclaw" / "openclaw.mjs",
            Path.home() / "AppData" / "Roaming" / "npm" / "node_modules" / "openclaw" / "openclaw.mjs",
        ):
            if candidate.exists():
                mjs_path = candidate
                break
        else:
            raise FileNotFoundError(
                f"openclaw.mjs not found next to {shim_path} or in known global locations."
            )

    return [node_path, str(mjs_path)]


def _log_paths() -> tuple[Path, Path]:
    TEST_LOG_DIR.mkdir(parents=True, exist_ok=True)
    return TEST_LOG_DIR / "gateway.out.log", TEST_LOG_DIR / "gateway.err.log"


def start_gateway() -> subprocess.Popen:
    """Start `openclaw gateway run` as a hidden background process."""
    cmd = _openclaw_command() + ["gateway", "run"]
    out_log, err_log = _log_paths()

    # Rotate old logs so each test starts with fresh output.
    for log in (out_log, err_log):
        if log.exists():
            backup = log.with_suffix(log.suffix + ".prev")
            try:
                shutil.move(str(log), str(backup))
            except PermissionError:
                # A previous gateway may still hold the log file open. Leave it
                # in place; the new process will append, which is still usable.
                pass

    out_handle = open(out_log, "w", encoding="utf-8", errors="ignore")
    err_handle = open(err_log, "w", encoding="utf-8", errors="ignore")

    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    proc = subprocess.Popen(
        cmd,
        stdout=out_handle,
        stderr=err_handle,
        creationflags=creationflags,
    )

    # Ensure handles are closed in the parent after the child inherits them.
    # Popen does not close them automatically on the parent side.
    # We keep references so we can close them when stopping the gateway.
    proc._test_out_handle = out_handle  # type: ignore[attr-defined]
    proc._test_err_handle = err_handle  # type: ignore[attr-defined]

    return proc


def stop_gateway() -> None:
    """Stop any running OpenClaw gateway processes."""
    try:
        import psutil
    except ImportError:
        # Fallback to taskkill on Windows if psutil is missing.
        subprocess.run(
            ["taskkill", "/F", "/IM", "node.exe", "/FI", "COMMANDLINE eq *openclaw*"],
            capture_output=True,
            check=False,
        )
        subprocess.run(
            ["taskkill", "/F", "/IM", "cmd.exe", "/FI", "COMMANDLINE eq *openclaw*"],
            capture_output=True,
            check=False,
        )
        return

    targets = []
    for proc in psutil.process_iter(["pid", "name"]):
        try:
            name = proc.info["name"]
            if name not in ("node.exe", "cmd.exe"):
                continue
            cmdline = " ".join(proc.cmdline() or [])
            if "openclaw" in cmdline.lower():
                targets.append(proc)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    for proc in targets:
        try:
            proc.terminate()
        except psutil.NoSuchProcess:
            pass

    gone, alive = psutil.wait_procs(targets, timeout=5)
    for proc in alive:
        try:
            proc.kill()
        except psutil.NoSuchProcess:
            pass

    # Give the port a moment to free up.
    time.sleep(1.0)


def wait_for_gateway(timeout: float = 30.0) -> bool:
    """Wait until the OpenClaw gateway HTTP health endpoint responds.

    We poll ``http://127.0.0.1:<GATEWAY_HTTP_PORT>/health`` instead of relying
    on ``openclaw status``. The CLI status command hangs or times out when its
    stdout/stderr are redirected on Windows, so a direct HTTP probe is more
    reliable in the pytest fixture context.
    """
    url = f"http://127.0.0.1:{GATEWAY_HTTP_PORT}/health"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=2.0) as response:
                if response.status == 200:
                    return True
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError):
            pass
        time.sleep(1.0)
    return False


def send_message(message: str, timeout: float = 120.0) -> tuple[int, str, str]:
    """Send a message through OpenClaw via the CLI and return (rc, stdout, stderr)."""
    cmd = _openclaw_command() + ["agent", "--agent", "main", "--message", message]
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="ignore",
        timeout=timeout,
        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
        check=False,
    )
    return result.returncode, result.stdout, result.stderr


def gateway_logs() -> tuple[str, str]:
    """Return the current gateway stdout and stderr log contents."""
    out_log = TEST_LOG_DIR / "gateway.out.log"
    err_log = TEST_LOG_DIR / "gateway.err.log"
    out_text = out_log.read_text(encoding="utf-8", errors="ignore") if out_log.exists() else ""
    err_text = err_log.read_text(encoding="utf-8", errors="ignore") if err_log.exists() else ""
    return out_text, err_text
