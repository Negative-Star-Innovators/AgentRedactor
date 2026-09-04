"""Linux self-update E2E (Velopack AppImage against a local feed).

Mirrors tests/migration/test_selfrelease_upgrade.py on Windows: the packed
AppImage is launched with AGENTREDACTOR_UPDATE_FEED pointed at a loopback
http.server feed and AGENTREDACTOR_UPDATE_AUTOAPPLY=1, and the test asserts
the updater downloads the vNext package, swaps the AppImage in place, and the
new AppImage successfully mounts and runs.

Skipped unless the release pack has been built (linux/build-release/velopack)
and vpk is available to pack the vNext feed. Unlike the regular smoke tests
this exercises the AR_SELFRELEASE build, not the dev-tree binary.

Note: Velopack keeps downloaded packages in a fixed machine-wide state dir
(/var/tmp/velopack/<packId>) that does NOT follow the test's isolated
HOME/XDG. The fixture purges it before and after — a leftover vNext package
there would otherwise be applied to ANY AgentRedactor AppImage started later
(including a freshly packed one), silently reverting it to the test build.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import psutil
import pytest

_tests_root = Path(__file__).resolve().parent.parent
for _p in (str(_tests_root), str(_tests_root / "gui")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from config_factory import create_settings  # noqa: E402
from gui_process import _find_free_port, _kill_existing_agent_redactor  # noqa: E402

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux update E2E")

PROJECT_ROOT = _tests_root.parent
RELEASE_DIR = PROJECT_ROOT / "linux" / "build-release"
VELOPACK_OUT = RELEASE_DIR / "velopack"
APPDIR = RELEASE_DIR / "appdir"


def _appimage_path() -> Path:
    """Velopack output AppImage name matches the documented download URL."""
    if platform.machine() in ("aarch64", "arm64"):
        return VELOPACK_OUT / "AgentRedactor-linux-arm64.AppImage"
    return VELOPACK_OUT / "AgentRedactor.AppImage"


APPIMAGE = _appimage_path()

# Velopack's machine-wide package cache/staging for this packId.
VELOPACK_STATE = Path("/var/tmp/velopack/AgentRedactor")

POLL_TIMEOUT_S = 180.0
POLL_INTERVAL_S = 2.0
RELAUNCH_ALIVE_S = 10.0


def _vpk() -> str | None:
    return shutil.which("vpk") or (
        str(Path.home() / ".dotnet" / "tools" / "vpk")
        if (Path.home() / ".dotnet" / "tools" / "vpk").is_file()
        else None
    )


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _fuse2_available() -> bool:
    """True when libfuse.so.2 is present (AppImage FUSE mount works)."""
    for search in ("/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu"):
        if Path(search, "libfuse.so.2").is_file():
            return True
    return False


def _channel_and_runtime() -> tuple[str, str]:
    """Return (vpk_channel, vpk_runtime) matching linux/build-release.sh."""
    machine = platform.machine()
    if machine in ("aarch64", "arm64"):
        return "linux-arm64", "linux-arm64"
    return "linux", "linux-x64"


def _current_feed_version() -> str:
    channel, _ = _channel_and_runtime()
    feed = json.loads((VELOPACK_OUT / f"releases.{channel}.json").read_text(encoding="utf-8"))
    return feed["Assets"][0]["Version"]


def _bump_patch(version: str) -> str:
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", version)
    assert m, f"unexpected version format: {version}"
    return f"{m.group(1)}.{m.group(2)}.{int(m.group(3)) + 1}"


def _gui_processes_for(path: Path) -> list[psutil.Process]:
    """Return agentredactor-gui processes whose command line references path.

    When an AppImage mounts, /proc/<pid>/exe points inside /tmp/.mount_* and
    the cmdline may not contain the AppImage file path. Callers that need to
    detect a relaunch should use _all_gui_processes and compare PIDs.
    """
    out = []
    for p in psutil.process_iter(["name", "cmdline"]):
        try:
            if p.info["name"] == "agentredactor-gui" and str(path) in " ".join(
                p.info["cmdline"] or []
            ):
                out.append(p)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


def _all_gui_processes() -> list[psutil.Process]:
    """Return all agentredactor-gui processes (by process name)."""
    out = []
    for p in psutil.process_iter(["name"]):
        try:
            if p.info["name"] == "agentredactor-gui":
                out.append(p)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


def _read_desktop_exec(path: Path) -> str:
    """Return the first token of the Exec= line from a .desktop file."""
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("Exec="):
            rest = raw[5:]
            # Simple unquote: remove surrounding quotes and strip args.
            if rest.startswith('"'):
                end = rest.find('"', 1)
                if end > 0:
                    return rest[1:end]
            return rest.split()[0]
    return ""


@pytest.fixture()
def update_env(tmp_path: Path):
    if not APPIMAGE.is_file():
        pytest.skip(f"AppImage not packed: {APPIMAGE} (run linux/build-release.sh)")
    if not APPDIR.is_dir():
        pytest.skip(f"staged AppDir missing: {APPDIR} (run linux/build-release.sh)")
    vpk = _vpk()
    if not vpk:
        pytest.skip("vpk (Velopack CLI) not installed")
    if not _fuse2_available():
        pytest.skip("libfuse2 is not installed (AppImage FUSE mount required for update test)")
    _kill_existing_agent_redactor()
    # No leftover staged packages from a previous run (theirs or ours).
    shutil.rmtree(VELOPACK_STATE, ignore_errors=True)
    yield tmp_path, vpk
    _kill_existing_agent_redactor()
    for p in _gui_processes_for(tmp_path):
        try:
            p.kill()
        except psutil.NoSuchProcess:
            pass
    # Never leave the test's vNext package in the machine-wide Velopack
    # state dir: the next real AppImage launch would apply it in place.
    shutil.rmtree(VELOPACK_STATE, ignore_errors=True)


def test_appimage_self_updates_against_local_feed(update_env) -> None:
    tmp_path, vpk = update_env
    channel, runtime = _channel_and_runtime()

    # 1. Pack a vNext feed from the same staged AppDir (the binary content is
    #    identical; Velopack compares the package versions in the manifest).
    feed_dir = tmp_path / "feed"
    feed_dir.mkdir()
    next_version = _bump_patch(_current_feed_version())
    subprocess.run(
        [
            vpk, "pack",
            "--packId", "AgentRedactor",
            "--packVersion", next_version,
            "--packDir", str(APPDIR),
            "--mainExe", "agentredactor-gui",
            "--runtime", runtime,
            "--channel", channel,
            "--outputDir", str(feed_dir),
        ],
        check=True, capture_output=True, text=True, timeout=300,
    )
    assert (feed_dir / f"releases.{channel}.json").is_file()

    # 2. Seed an isolated config so the engine starts without needing the
    #    model download, plus an isolated HOME (first-run CLI symlink target).
    config_dir = tmp_path / "config"
    home_dir = tmp_path / "home"
    home_dir.mkdir()
    create_settings(
        data_dir=config_dir,
        upstream_url="http://127.0.0.1:9",  # unreachable on purpose
        api_key="sk-update-e2e",
        proxy_port=_find_free_port(),
        logging_enabled=False,
        keywords=[],
        regex_patterns=[],
    )

    # 3. Copy the shipped AppImage aside; the updater swaps this file.
    work_dir = tmp_path / "work"
    work_dir.mkdir()
    app = work_dir / "AgentRedactor.AppImage"
    shutil.copy2(APPIMAGE, app)
    app.chmod(0o755)
    before = _sha256(app)

    # 4. Serve the vNext feed on loopback (the override is loopback-only).
    port = _find_free_port()
    server = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(port),
         "--bind", "127.0.0.1", "--directory", str(feed_dir)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    original_pid: int | None = None
    try:
        # 5. Launch the AppImage with the feed override + auto-apply.
        env = dict(os.environ)
        env.update({
            "QT_QPA_PLATFORM": "offscreen",
            "AGENTREDACTOR_UPDATE_FEED": f"http://127.0.0.1:{port}",
            "AGENTREDACTOR_UPDATE_AUTOAPPLY": "1",
            "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
            "HOME": str(home_dir),
            "XDG_CONFIG_HOME": str(home_dir / ".config"),
            "XDG_DATA_HOME": str(home_dir / ".local" / "share"),
        })
        proc = subprocess.Popen(
            [str(app)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        original_pid = proc.pid

        # 6. Poll until the AppImage file changes (updater applied vNext and
        #    restarted the app; the relaunched instance sees no further update
        #    and just keeps running).
        deadline = time.monotonic() + POLL_TIMEOUT_S
        while time.monotonic() < deadline:
            time.sleep(POLL_INTERVAL_S)
            try:
                if app.is_file() and _sha256(app) != before:
                    break
            except OSError:
                continue  # mid-swap
        else:
            proc.kill()
            raise AssertionError(
                f"AppImage was not updated to v{next_version} within {POLL_TIMEOUT_S}s")
    finally:
        server.terminate()
        for p in _gui_processes_for(tmp_path):
            try:
                p.terminate()
            except psutil.NoSuchProcess:
                pass

    assert _sha256(app) != before
    assert os.access(app, os.X_OK), f"updated AppImage is not executable: {app}"

    # 7. The updated AppImage actually mounts and executes. Run a quick CLI
    #    command through the AppImage itself (no shim yet) to prove FUSE/
    #    runtime is intact.
    cli_env = dict(os.environ)
    cli_env.update({
        "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
        "HOME": str(home_dir),
        "XDG_CONFIG_HOME": str(home_dir / ".config"),
        "XDG_DATA_HOME": str(home_dir / ".local" / "share"),
    })
    cli = subprocess.run(
        [str(app), "--cli", "help"], env=cli_env,
        capture_output=True, text=True, timeout=60)
    assert cli.returncode == 0, (
        f"updated AppImage failed to run --cli help: {cli.stdout} {cli.stderr}")
    assert "profiles" in cli.stdout and "keywords" in cli.stdout

    # 8. A new GUI process should have been spawned by the updater. Verify it
    #    stayed alive for a short grace period after the swap. AppImage mounts
    #    make /proc/<pid>/exe point inside /tmp/.mount_*, so detect by process
    #    name and exclude the original launcher PID.
    deadline = time.monotonic() + RELAUNCH_ALIVE_S
    relaunched = []
    while time.monotonic() < deadline:
        relaunched = [
            p for p in _all_gui_processes()
            if original_pid is None or p.pid != original_pid
        ]
        if relaunched:
            break
        time.sleep(0.5)
    assert relaunched, "updater did not spawn a new GUI process after applying the update"
    # Give the relaunched process a moment to settle, then confirm it is still alive.
    time.sleep(3)
    still_alive = [p for p in relaunched if p.is_running()]
    assert still_alive, "relaunched GUI process died shortly after the update"
    for p in still_alive:
        try:
            p.terminate()
        except psutil.NoSuchProcess:
            pass

    # 9. First-run CLI shim: launching the AppImage drops a wrapper at
    #    ~/.local/bin/agentredactor that re-invokes the AppImage with --cli.
    #    Drive the full chain end-to-end: wrapper -> AppImage runtime -> GUI
    #    binary -> exec of the bundled dual-mode CLI binary.
    shim = home_dir / ".local" / "bin" / "agentredactor"
    assert shim.is_file(), f"CLI shim missing at {shim}"
    assert os.access(shim, os.X_OK)
    assert str(app) in shim.read_text(encoding="utf-8")
    cli2 = subprocess.run(
        [str(shim), "help"], env=cli_env,
        capture_output=True, text=True, timeout=60)
    assert cli2.returncode == 0, f"CLI via shim failed: {cli2.stdout} {cli2.stderr}"
    assert "profiles" in cli2.stdout and "keywords" in cli2.stdout

    # 10. Desktop entries written during the test must point at the updated
    #     AppImage file, never at an ephemeral /tmp/.mount_* path.
    autostart = home_dir / ".config" / "autostart" / "agentredactor.desktop"
    applications = home_dir / ".local" / "share" / "applications" / "agentredactor.desktop"
    for entry in (autostart, applications):
        if entry.is_file():
            exec_path = _read_desktop_exec(entry)
            assert exec_path, f"{entry} has no usable Exec= path"
            assert not exec_path.startswith("/tmp/.mount_"), (
                f"{entry} points at ephemeral AppImage mount: {exec_path}")
            assert Path(exec_path).resolve() == app.resolve(), (
                f"{entry} Exec path does not point at test AppImage: {exec_path}")
