"""Helpers for configuring and driving Claude Code from the third-party tests."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = PROJECT_ROOT / "scripts"

# The Anthropic-compatible environment values written to ~/.claude/settings.json.
CLAUDE_BASE_URL = "http://localhost:8081/"
CLAUDE_AUTH_TOKEN = "dummy"
CLAUDE_MODEL = "anthropic/claude-haiku-4.5"


def _powershell(cmd: list[str], timeout: float = 600.0) -> subprocess.CompletedProcess[str]:
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


def run_setup_script(
    script_name: str = "setup-claudecode-test-anthropic-claude-haiku.ps1",
    base_url: str = CLAUDE_BASE_URL,
) -> str:
    """Run the Claude Code setup script and return its stdout.

    The script installs the Claude Code CLI if necessary, backs up the existing
    ~/.claude directory, and writes the Anthropic-compatible env block to
    ~/.claude/settings.json.
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

    This lets a just-installed Claude Code CLI be discoverable in the current
    process without requiring a shell restart.
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


def _find_claudecode_cmd() -> str:
    """Find the Claude Code command executable."""
    _refresh_path_from_registry()
    for name in ("claude", "claude.cmd", "claudecode", "claudecode.cmd"):
        path = shutil.which(name)
        if path:
            return path
    raise FileNotFoundError(
        "claude not found in PATH. "
        "Make sure Claude Code is installed and its bin folder is on PATH."
    )


def _claudecode_command() -> list[str]:
    """Return the Claude Code CLI invocation as a list.

    We bypass the npm ``.cmd`` shim and invoke Node directly with the Claude
    Code CLI entry point. This avoids intermittent "'node' is not recognized"
    errors from the shim when the subprocess environment differs from the
    parent shell.
    """
    node_path = shutil.which("node")
    if not node_path:
        raise FileNotFoundError(
            "node not found in PATH. Make sure Node.js is installed and on PATH."
        )

    shim_path = Path(_find_claudecode_cmd())
    cli_path = shim_path.parent / "node_modules" / "@anthropic-ai" / "claude-code" / "cli.js"
    if not cli_path.exists():
        # Fallback to known global locations if the shim is not in npm.
        for candidate in (
            Path(node_path).parent.parent
            / "node_modules"
            / "@anthropic-ai"
            / "claude-code"
            / "cli.js",
            Path.home()
            / "AppData"
            / "Roaming"
            / "npm"
            / "node_modules"
            / "@anthropic-ai"
            / "claude-code"
            / "cli.js",
        ):
            if candidate.exists():
                cli_path = candidate
                break
        else:
            raise FileNotFoundError(
                f"claude-code cli.js not found next to {shim_path} or in known global locations."
            )

    return [node_path, str(cli_path)]


def stop_claudecode() -> None:
    """Stop any running Claude Code processes."""
    own_pid = os.getpid()

    try:
        import psutil
    except ImportError:
        # Fallback to taskkill on Windows if psutil is missing.
        subprocess.run(
            ["taskkill", "/F", "/IM", "claude.exe"],
            capture_output=True,
            check=False,
        )
        subprocess.run(
            ["taskkill", "/F", "/IM", "node.exe", "/FI", "COMMANDLINE eq *claude*"],
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
            if lower_name.startswith("claude") or lower_name.startswith("claudecode"):
                targets.append(proc)
                continue
            if lower_name == "node.exe":
                cmdline = " ".join(proc.cmdline() or [])
                lowered = cmdline.lower()
                if "claude" in lowered or "claudecode" in lowered:
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


def _trust_project(project_dir: Path) -> None:
    """Pre-accept the workspace trust dialog for ``project_dir``.

    Claude Code stores per-project trust state in ~/.claude.json. Writing the
    temporary project path before invoking ``claude -p`` prevents the headless
    invocation from blocking on the trust dialog.
    """
    claude_json = Path.home() / ".claude.json"
    data: dict = {}
    if claude_json.exists():
        try:
            data = json.loads(claude_json.read_text(encoding="utf-8"))
        except Exception:
            data = {}

    if not isinstance(data.get("projects"), dict):
        data["projects"] = {}
    data.setdefault("hasCompletedOnboarding", True)
    data["projects"][project_dir.resolve().as_posix()] = {"hasTrustDialogAccepted": True}

    try:
        claude_json.write_text(json.dumps(data, indent=2), encoding="utf-8")
    except Exception:
        # Trust pre-seeding is best-effort; the caller's assertions will catch
        # a hung or failed invocation.
        pass


def send_message(
    message: str,
    project_dir: Path,
    timeout: float = 300.0,
) -> tuple[int, str, str]:
    """Send a one-shot message through Claude Code and return (rc, stdout, stderr).

    Runs in ``project_dir``. Claude Code loads the Anthropic-compatible endpoint
    and model from ~/.claude/settings.json.
    """
    cmd = _claudecode_command()

    # Pre-accept trust for the temporary project so the headless run does not
    # block on an interactive dialog.
    _trust_project(project_dir)

    env = os.environ.copy()
    # Make sure a real Anthropic API key does not take precedence over the
    # ANTHROPIC_AUTH_TOKEN configured in settings.json.
    env.pop("ANTHROPIC_API_KEY", None)
    # Pin the test to the version installed by the setup script. The latest
    # Claude Code native binary has a Windows headless stdout bug that makes
    # ``claude -p`` return empty output when captured via pipes.
    env["DISABLE_AUTOUPDATER"] = "1"
    # Cap the requested output tokens. Claude Code defaults to 32K, which is
    # more than the test OpenRouter key can afford for Claude Haiku.
    env["CLAUDE_CODE_MAX_OUTPUT_TOKENS"] = "4096"

    # Run headless with a read-only permission profile so the test does not hang
    # on an interactive approval prompt. Use streaming JSON output so we can
    # reliably extract the response text on Windows (plain-text ``-p`` output
    # and the final ``result`` field can both be empty in some builds).
    result = subprocess.run(
        cmd
        + [
            "-p",
            message,
            "--permission-mode",
            "dontAsk",
            "--allowedTools",
            "Read",
            "--output-format",
            "stream-json",
            "--verbose",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="ignore",
        timeout=timeout,
        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
        check=False,
        cwd=str(project_dir),
        env=env,
    )

    stdout_text = result.stdout
    text_parts: list[str] = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        # Claude Code's stream-json output can be either bare JSON lines or
        # SSE-style ``data: {...}`` lines.
        if line.startswith("data:"):
            line = line[len("data:"):].strip()
        if not line or not line.startswith("{"):
            continue
        try:
            event = json.loads(line)
        except Exception:
            continue
        if event.get("type") == "assistant":
            for block in event.get("message", {}).get("content", []):
                if block.get("type") == "text":
                    text_parts.append(block.get("text", ""))
        elif event.get("type") == "content_block_delta":
            delta = event.get("delta", {})
            if delta.get("type") == "text_delta":
                text_parts.append(delta.get("text", ""))
    accumulated = "".join(text_parts)
    if accumulated:
        stdout_text = accumulated

    return result.returncode, stdout_text, result.stderr
