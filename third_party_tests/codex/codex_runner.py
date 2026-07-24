"""Helpers for configuring and driving OpenAI Codex from the third-party tests."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = PROJECT_ROOT / "scripts"

# The API key environment variable referenced by the Codex config.toml.
CODEX_API_KEY_ENV = "DUMMY_API_KEY"
CODEX_API_KEY_VALUE = "dummy"


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


def run_setup_script(script_name: str, base_url: str = "http://localhost:8081/v1") -> str:
    """Run a Codex setup script and return its stdout.

    The script resets Codex state, writes a fresh config.toml, and validates the
    installation by running ``codex --version``.
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

    This lets a just-installed codex be discoverable in the current process
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


def _find_codex_cmd() -> str:
    """Find the codex command executable."""
    _refresh_path_from_registry()
    for name in ("codex", "codex.cmd"):
        path = shutil.which(name)
        if path:
            return path
    raise FileNotFoundError(
        "codex not found in PATH. "
        "Make sure OpenAI Codex is installed and its bin folder is on PATH."
    )


def stop_codex() -> None:
    """Stop any running Codex processes."""
    own_pid = os.getpid()

    try:
        import psutil
    except ImportError:
        # Fallback to taskkill on Windows if psutil is missing.
        subprocess.run(
            ["taskkill", "/F", "/IM", "codex.exe"],
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
                "COMMANDLINE eq *codex*",
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
            if lower_name.startswith("codex"):
                targets.append(proc)
                continue
            if lower_name == "node.exe":
                cmdline = " ".join(proc.cmdline() or [])
                if "codex" in cmdline.lower():
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
    message: str,
    project_dir: Path,
    timeout: float = 300.0,
) -> tuple[int, str, str]:
    """Send a one-shot message through Codex and return (rc, stdout, stderr).

    Runs in ``project_dir`` with the configured API key environment variable set.
    Stdout and stderr are redirected through temporary files instead of pipes to
    avoid hangs when Codex spawns long-lived child processes that inherit pipes.
    """
    codex_cmd = _find_codex_cmd()

    env = os.environ.copy()
    env[CODEX_API_KEY_ENV] = CODEX_API_KEY_VALUE

    # Use `codex exec` (non-interactive) so we do not need a terminal.
    # Read-only sandbox + ephemeral session + skip git-repo check keeps the
    # test isolated and avoids writing to disk.
    # Write stdout/stderr to files outside the project directory so that
    # lingering child-process file handles do not block temp-dir cleanup.
    stdout_fd, stdout_path = tempfile.mkstemp(prefix="codex_stdout_", suffix=".txt")
    stderr_fd, stderr_path = tempfile.mkstemp(prefix="codex_stderr_", suffix=".txt")
    os.close(stdout_fd)
    os.close(stderr_fd)
    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    try:
        with open(stdout_path, "w", encoding="utf-8", errors="ignore") as stdout_file, \
             open(stderr_path, "w", encoding="utf-8", errors="ignore") as stderr_file:
            result = subprocess.run(
                [
                    codex_cmd,
                    "exec",
                    "--disable",
                    "apps",
                    "--disable",
                    "multi_agent",
                    "--disable",
                    "multi_agent_v2",
                    "--disable",
                    "collaboration_modes",
                    "-c",
                    'web_search="disabled"',
                    "-c",
                    "tools.view_image=false",
                    "--sandbox",
                    "read-only",
                    "--skip-git-repo-check",
                    "--ephemeral",
                    message,
                ],
                stdout=stdout_file,
                stderr=stderr_file,
                timeout=timeout,
                creationflags=creationflags,
                check=False,
                cwd=str(project_dir),
                env=env,
            )
        stdout_text = Path(stdout_path).read_text(encoding="utf-8", errors="ignore")
        stderr_text = Path(stderr_path).read_text(encoding="utf-8", errors="ignore")
        return result.returncode, stdout_text, stderr_text
    finally:
        for path in (stdout_path, stderr_path):
            try:
                os.unlink(path)
            except Exception:
                pass
