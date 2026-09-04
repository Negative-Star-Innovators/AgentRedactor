"""Linux self-update E2E: previous live release -> current build.

Downloads the latest published AppImage from R2 (the live channel), points it
at a local feed built from the just-packed current build, and asserts the live
AppImage updates itself and the updated AppImage mounts/runs.

Skipped unless AGENTREDACTOR_UPGRADE_TEST=1. Required env vars:
  AGENTREDACTOR_PREV_APPIMAGE  path to the previous release's AppImage file
                               (download it from R2 before invoking this test)
  AGENTREDACTOR_FEED_DIR       folder with the current vpk output
                               (releases.<channel>.json + *-full.nupkg)
Optional:
  AGENTREDACTOR_CHANNEL        update channel: 'linux' (x64, default) or
                               'linux-arm64'.
  AGENTREDACTOR_EXPECT_VERSION expected new version (default: newest version
                               in the feed's releases.<channel>.json)

See tests/linux/test_update_feed.py for the same-build update test and
.tests/migration/test_selfrelease_upgrade.py for the Windows equivalent.
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

pytestmark = [
    pytest.mark.skipif(sys.platform == "win32", reason="Linux upgrade E2E"),
    pytest.mark.skipif(
        os.environ.get("AGENTREDACTOR_UPGRADE_TEST") != "1",
        reason="opt-in E2E: set AGENTREDACTOR_UPGRADE_TEST=1 "
        "(plus AGENTREDACTOR_PREV_APPIMAGE / AGENTREDACTOR_FEED_DIR)",
    ),
]

# Velopack's machine-wide package cache/staging for this packId.
VELOPACK_STATE = Path("/var/tmp/velopack/AgentRedactor")

POLL_TIMEOUT_S = 300.0
POLL_INTERVAL_S = 2.0
RELAUNCH_ALIVE_S = 10.0


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _fuse2_available() -> bool:
    for search in ("/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu"):
        if Path(search, "libfuse.so.2").is_file():
            return True
    return False


def _version_key(text: str) -> tuple[int, int, int]:
    core = text.strip().lstrip("vV").split("-")[0].split("+")[0]
    parts = [int(p) for p in core.split(".") if p != ""]
    return tuple((parts + [0, 0, 0])[:3])


def _latest_feed_version(feed_dir: Path, channel: str) -> str:
    feed = json.loads((feed_dir / f"releases.{channel}.json").read_text(encoding="utf-8"))
    versions = [
        asset["Version"]
        for asset in feed.get("Assets", [])
        if asset.get("Type") in (1, "Full") and asset.get("Version")
    ]
    if not versions:
        pytest.fail(f"no full-package assets in {feed_dir / f'releases.{channel}.json'}")
    return max(versions, key=_version_key)


def _gui_processes_for(path: Path) -> list[psutil.Process]:
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
    out = []
    for p in psutil.process_iter(["name"]):
        try:
            if p.info["name"] == "agentredactor-gui":
                out.append(p)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


def _read_desktop_exec(path: Path) -> str:
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("Exec="):
            rest = raw[5:]
            if rest.startswith('"'):
                end = rest.find('"', 1)
                if end > 0:
                    return rest[1:end]
            return rest.split()[0]
    return ""


@pytest.fixture()
def upgrade_env(tmp_path: Path):
    prev = os.environ.get("AGENTREDACTOR_PREV_APPIMAGE")
    if not prev or not Path(prev).is_file():
        pytest.skip("AGENTREDACTOR_PREV_APPIMAGE is not set or does not point at an AppImage")
    channel = os.environ.get("AGENTREDACTOR_CHANNEL", "linux")
    feed_dir = os.environ.get("AGENTREDACTOR_FEED_DIR")
    if not feed_dir or not (Path(feed_dir) / f"releases.{channel}.json").is_file():
        pytest.skip(
            f"AGENTREDACTOR_FEED_DIR is not set or has no releases.{channel}.json "
            "(point it at the vpk output folder, e.g. linux/build-release/velopack)"
        )
    if not _fuse2_available():
        pytest.skip("libfuse2 is not installed (AppImage FUSE mount required for upgrade test)")
    _kill_existing_agent_redactor()
    shutil.rmtree(VELOPACK_STATE, ignore_errors=True)
    yield tmp_path, Path(prev), channel, Path(feed_dir)
    _kill_existing_agent_redactor()
    for p in _gui_processes_for(tmp_path):
        try:
            p.kill()
        except psutil.NoSuchProcess:
            pass
    shutil.rmtree(VELOPACK_STATE, ignore_errors=True)


def _expected_appimage_filename(channel: str) -> str:
    if channel == "linux-arm64":
        return "AgentRedactor-linux-arm64.AppImage"
    return "AgentRedactor.AppImage"


def test_upgrade_from_live_appimage(upgrade_env) -> None:
    tmp_path, prev_appimage, channel, feed_dir = upgrade_env
    expected = os.environ.get("AGENTREDACTOR_EXPECT_VERSION") or _latest_feed_version(
        feed_dir, channel
    )
    expected_core = re.match(r"^(\d+)\.(\d+)\.(\d+)", expected)
    assert expected_core, f"unexpected expected version: {expected}"

    work_dir = tmp_path / "work"
    work_dir.mkdir()
    app = work_dir / "AgentRedactor.AppImage"
    shutil.copy2(prev_appimage, app)
    app.chmod(0o755)
    before = _sha256(app)

    config_dir = tmp_path / "config"
    home_dir = tmp_path / "home"
    home_dir.mkdir()
    create_settings(
        data_dir=config_dir,
        upstream_url="http://127.0.0.1:9",
        api_key="sk-upgrade-e2e",
        proxy_port=_find_free_port(),
        logging_enabled=False,
        keywords=[],
        regex_patterns=[],
    )

    port = _find_free_port()
    server = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(port),
         "--bind", "127.0.0.1", "--directory", str(feed_dir)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    original_pid: int | None = None
    try:
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
        out_path = tmp_path / "app.stdout.log"
        err_path = tmp_path / "app.stderr.log"
        with out_path.open("w") as out_f, err_path.open("w") as err_f:
            proc = subprocess.Popen([str(app)], env=env,
                                    stdout=out_f, stderr=err_f)
            original_pid = proc.pid

            deadline = time.monotonic() + POLL_TIMEOUT_S
            upgraded = False
            while time.monotonic() < deadline:
                time.sleep(POLL_INTERVAL_S)
                try:
                    if app.is_file() and _sha256(app) != before:
                        upgraded = True
                        break
                except OSError:
                    continue
            if not upgraded:
                proc.kill()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                stdout_text = out_path.read_text(encoding="utf-8", errors="replace")
                stderr_text = err_path.read_text(encoding="utf-8", errors="replace")
                # Dump Velopack state for post-mortem.
                state_lines: list[str] = []
                if VELOPACK_STATE.is_dir():
                    for p in sorted(VELOPACK_STATE.rglob("*")):
                        try:
                            state_lines.append(f"{p.relative_to(VELOPACK_STATE)} size={p.stat().st_size}")
                        except OSError:
                            state_lines.append(str(p))
                pytest.fail(
                    f"AppImage was not upgraded to v{expected} within {POLL_TIMEOUT_S}s.\n"
                    f"process alive={proc.poll() is None} returncode={proc.poll()}\n"
                    f"--- stdout ---\n{stdout_text}\n"
                    f"--- stderr ---\n{stderr_text}\n"
                    f"--- velopack state ({VELOPACK_STATE}) ---\n" + "\n".join(state_lines)
                )

        # Velopack replaces the AppImage and restarts the GUI; wait for the new
        # instance to be alive so the engine is reachable for --cli status.
        deadline = time.monotonic() + RELAUNCH_ALIVE_S
        relaunched: list[psutil.Process] = []
        while time.monotonic() < deadline:
            relaunched = [
                p for p in _all_gui_processes()
                if original_pid is None or p.pid != original_pid
            ]
            if relaunched:
                break
            time.sleep(0.5)
        assert relaunched, "updater did not spawn a new GUI process after the upgrade"
        time.sleep(3)
        still_alive = [p for p in relaunched if p.is_running()]
        assert still_alive, "relaunched GUI process died shortly after the upgrade"

        assert os.access(app, os.X_OK), f"upgraded AppImage is not executable: {app}"

        cli_env = dict(os.environ)
        cli_env.update({
            "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
            "HOME": str(home_dir),
            "XDG_CONFIG_HOME": str(home_dir / ".config"),
            "XDG_DATA_HOME": str(home_dir / ".local" / "share"),
        })
        version_check = subprocess.run(
            [str(app), "--cli", "status"], env=cli_env,
            capture_output=True, text=True, timeout=60)
        assert version_check.returncode == 0, (
            f"upgraded AppImage failed to run --cli status: {version_check.stdout} {version_check.stderr}")
        assert expected_core.group(0) in version_check.stdout, (
            f"upgraded AppImage does not report expected version {expected_core.group(0)}: "
            f"{version_check.stdout}")
    finally:
        server.terminate()
        for p in _gui_processes_for(tmp_path):
            try:
                p.terminate()
            except psutil.NoSuchProcess:
                pass

    autostart = home_dir / ".config" / "autostart" / "agentredactor.desktop"
    applications = home_dir / ".local" / "share" / "applications" / "agentredactor.desktop"
    for entry in (autostart, applications):
        if entry.is_file():
            exec_path = _read_desktop_exec(entry)
            assert exec_path, f"{entry} has no usable Exec= path"
            assert not exec_path.startswith("/tmp/.mount_"), (
                f"{entry} points at ephemeral AppImage mount: {exec_path}")
