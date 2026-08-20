"""Linux self-update E2E (Velopack AppImage against a local feed).

Mirrors tests/migration/test_selfrelease_upgrade.py on Windows: the packed
AppImage is launched with AGENTREDACTOR_UPDATE_FEED pointed at a loopback
http.server feed and AGENTREDACTOR_UPDATE_AUTOAPPLY=1, and the test asserts
the updater downloads the vNext package and swaps the AppImage in place.

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
APPIMAGE = VELOPACK_OUT / "AgentRedactor.AppImage"

# Velopack's machine-wide package cache/staging for this packId.
VELOPACK_STATE = Path("/var/tmp/velopack/AgentRedactor")

POLL_TIMEOUT_S = 180.0
POLL_INTERVAL_S = 2.0


def _vpk() -> str | None:
    return shutil.which("vpk") or (
        str(Path.home() / ".dotnet" / "tools" / "vpk")
        if (Path.home() / ".dotnet" / "tools" / "vpk").is_file()
        else None
    )


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _current_feed_version() -> str:
    feed = json.loads((VELOPACK_OUT / "releases.linux.json").read_text(encoding="utf-8"))
    return feed["Assets"][0]["Version"]


def _bump_patch(version: str) -> str:
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", version)
    assert m, f"unexpected version format: {version}"
    return f"{m.group(1)}.{m.group(2)}.{int(m.group(3)) + 1}"


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


@pytest.fixture()
def update_env(tmp_path: Path):
    if not APPIMAGE.is_file():
        pytest.skip(f"AppImage not packed: {APPIMAGE} (run linux/build-release.sh)")
    if not APPDIR.is_dir():
        pytest.skip(f"staged AppDir missing: {APPDIR} (run linux/build-release.sh)")
    vpk = _vpk()
    if not vpk:
        pytest.skip("vpk (Velopack CLI) not installed")
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
            "--runtime", "linux-x64",
            "--channel", "linux",
            "--outputDir", str(feed_dir),
        ],
        check=True, capture_output=True, text=True, timeout=300,
    )
    assert (feed_dir / "releases.linux.json").is_file()

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
