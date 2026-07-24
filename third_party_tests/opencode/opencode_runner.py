"""Helpers for configuring and driving OpenCode from the third-party tests."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = PROJECT_ROOT / "scripts"


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
    """Run an OpenCode setup script and return its stdout.

    The script resets OpenCode state, writes a fresh opencode.jsonc, installs the
    required AI SDK provider package, and validates the configuration.
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
        ]
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Setup script {script_name} failed with rc={result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result.stdout


def _refresh_path_from_registry() -> None:
    """Re-read User+Machine PATH from the registry on Windows.

    This lets a just-installed opencode be discoverable in the current process
    without requiring a shell restart.
    """
    if sys.platform != "win32":
        return
    try:
        import winreg

        user_path = ""
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
                user_path, _ = winreg.QueryValueEx(key, "Path")
        except OSError:
            pass

        machine_path = ""
        try:
            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment",
            ) as key:
                machine_path, _ = winreg.QueryValueEx(key, "Path")
        except OSError:
            pass

        paths = [p for p in (user_path, machine_path, os.environ.get("PATH", "")) if p]
        os.environ["PATH"] = ";".join(paths)
    except Exception:
        pass


def _find_opencode_cmd() -> str:
    """Find the opencode command executable."""
    _refresh_path_from_registry()
    for name in ("opencode", "opencode.cmd"):
        path = shutil.which(name)
        if path:
            return path
    raise FileNotFoundError(
        "opencode not found in PATH. "
        "Make sure OpenCode is installed and its npm global bin folder is on PATH."
    )


def stop_opencode() -> None:
    """Stop any running OpenCode processes."""
    own_pid = os.getpid()

    try:
        import psutil
    except ImportError:
        # Fallback to taskkill on Windows if psutil is missing.
        subprocess.run(
            ["taskkill", "/F", "/IM", "opencode.exe"],
            capture_output=True,
            check=False,
        )
        subprocess.run(
            [
                "taskkill",
                "/F",
                "/IM",
                "node.exe",
                "/FI",
                "COMMANDLINE eq *opencode*",
            ],
            capture_output=True,
            check=False,
        )
        return

    targets = []
    for proc in psutil.process_iter(["pid", "name"]):
        try:
            pid = proc.info["pid"]
            if pid == own_pid:
                continue
            name = proc.info["name"]
            if name is None:
                continue
            lower_name = name.lower()
            if lower_name.startswith("opencode"):
                targets.append(proc)
                continue
            if lower_name == "node.exe":
                cmdline = " ".join(proc.cmdline() or [])
                if "opencode" in cmdline.lower():
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

    # Give the port and profile locks a moment to free up.
    time.sleep(1.0)


def send_message(
    provider_model: str,
    message: str,
    timeout: float = 180.0,
) -> tuple[int, str, str]:
    """Send a one-shot message through OpenCode and return (rc, stdout, stderr)."""
    opencode_cmd = _find_opencode_cmd()
    result = subprocess.run(
        [opencode_cmd, "run", message, "-m", provider_model],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="ignore",
        timeout=timeout,
        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
        check=False,
    )
    return result.returncode, result.stdout, result.stderr
