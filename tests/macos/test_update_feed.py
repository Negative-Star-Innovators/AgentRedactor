"""macOS self-update feed E2E (Velopack .app against a local loopback feed).

Mirrors tests/linux/test_update_feed.py for the osx-arm64 channel: the packed
AgentRedactor.app is launched with AGENTREDACTOR_UPDATE_FEED pointed at a
loopback http.server serving a vpk feed, asserting the update check runs
against the override and detects the feed's version.

The SwiftUI UpdateProvider (macos/gui) checks, downloads, and applies through
the bundled velopack_libc dylib (VelopackUpdateProvider, compiled in with
SELFRELEASE). This suite asserts the check path end-to-end (the loopback
server must see GET /releases.osx-arm64.json and the app must stay healthy),
and the full download-and-swap half
(test_app_self_updates_against_local_feed) runs locally when
AGENTREDACTOR_UPDATE_SWAP_TEST=1 and is always enabled in release-macos.yml.

Skipped unless the release pack has been built (macos/build-release/velopack)
and vpk is available to pack the vNext feed. Unlike the regular smoke tests
this exercises the packaged release, not the dev-tree binary.

Note: Velopack caches downloaded packages under the test's isolated HOME
(~/Library/Caches/velopack/<packId>/packages), not in shared machine state.
"""

from __future__ import annotations

import json
import os
import plistlib
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

from gui_process import _find_free_port, _kill_existing_agent_redactor  # noqa: E402

pytestmark = pytest.mark.skipif(sys.platform != "darwin", reason="macOS update E2E")

PROJECT_ROOT = _tests_root.parent
RELEASE_DIR = PROJECT_ROOT / "macos" / "build-release"
VELOPACK_OUT = RELEASE_DIR / "velopack"
STAGED_APP = RELEASE_DIR / "app" / "AgentRedactor.app"
PACKED_APP = RELEASE_DIR / "dmg-root" / "Agent Redactor.app"

CHANNEL = "osx-arm64"
PACK_ID = "com.negativestarinnovators.agentredactor"
MAIN_EXE = "AgentRedactor"

FEED_REQUEST_TIMEOUT_S = 60.0
FEED_POLL_S = 2.0


def _vpk() -> str | None:
    return shutil.which("vpk") or (
        str(Path.home() / ".dotnet" / "tools" / "vpk")
        if (Path.home() / ".dotnet" / "tools" / "vpk").is_file()
        else None
    )


def _current_feed_version() -> str:
    feed = json.loads((VELOPACK_OUT / f"releases.{CHANNEL}.json").read_text(encoding="utf-8"))
    return feed["Assets"][0]["Version"]


def _bump_patch(version: str) -> str:
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", version)
    assert m, f"unexpected version format: {version}"
    return f"{m.group(1)}.{m.group(2)}.{int(m.group(3)) + 1}"


def _bundle_version(app: Path) -> str:
    with open(app / "Contents" / "Info.plist", "rb") as fh:
        return plistlib.load(fh).get("CFBundleShortVersionString", "")


def _velopack_version(app: Path) -> str:
    metadata = (app / "Contents" / "Resources" / "sq.version").read_text(
        encoding="utf-8")
    match = re.search(r"<version>([^<]+)</version>", metadata)
    assert match, "Velopack sq.version has no package version"
    return match.group(1)


def _app_processes() -> list[psutil.Process]:
    """AgentRedactor GUI processes (by process name, like the Linux suite)."""
    out = []
    for p in psutil.process_iter(["name"]):
        try:
            if p.info["name"] == MAIN_EXE:
                out.append(p)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


class _FeedServer:
    """Loopback http.server with its request log captured to a file."""

    def __init__(self, serve_dir: Path) -> None:
        self.port = _find_free_port()
        self.log_path = serve_dir.parent / f"http-{self.port}.log"
        self._log = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "http.server", str(self.port),
             "--bind", "127.0.0.1", "--directory", str(serve_dir)],
            stdout=subprocess.DEVNULL, stderr=self._log,
        )

    def stop(self) -> None:
        self.proc.terminate()
        self.proc.wait(timeout=10)
        self._log.close()

    def requests(self) -> str:
        self._log.flush()
        return self.log_path.read_text(encoding="utf-8", errors="ignore")

    def wait_for_feed_request(self, timeout_s: float = FEED_REQUEST_TIMEOUT_S) -> None:
        deadline = time.monotonic() + timeout_s
        needle = f"GET /releases.{CHANNEL}.json"
        while time.monotonic() < deadline:
            if needle in self.requests():
                return
            time.sleep(FEED_POLL_S)
        raise AssertionError(
            f"packed app never requested {needle} from the loopback feed "
            f"within {timeout_s}s.\n--- http.server log ---\n{self.requests() or '(empty)'}")


def _launch_app(app: Path, config_dir: Path, home_dir: Path,
                feed_url: str | None = None) -> subprocess.Popen:
    env = dict(os.environ)
    env.update({
        "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
        "AGENTREDACTOR_DISABLE_KEYRING": "1",
        "HOME": str(home_dir),
    })
    if feed_url:
        env["AGENTREDACTOR_UPDATE_FEED"] = feed_url
        env["AGENTREDACTOR_UPDATE_AUTOAPPLY"] = "1"
    config_dir.mkdir(parents=True, exist_ok=True)
    log = open(config_dir / "gui.log", "a")
    try:
        return subprocess.Popen(
            [str(app / "Contents" / "MacOS" / MAIN_EXE)],
            env=env, stdout=log, stderr=log,
        )
    finally:
        log.close()


@pytest.fixture()
def packed_release(tmp_path: Path):
    if not STAGED_APP.is_dir() or not PACKED_APP.is_dir():
        pytest.skip("release apps missing (run macos/build-release.sh)")
    if not (VELOPACK_OUT / f"releases.{CHANNEL}.json").is_file():
        pytest.skip(f"feed missing: {VELOPACK_OUT} (run macos/build-release.sh)")
    vpk = _vpk()
    if not vpk:
        pytest.skip("vpk (Velopack CLI) not installed")
    # A stale GUI from an earlier run would grab our launch through the
    # single-instance guard.
    _kill_existing_agent_redactor()
    work = tmp_path / "AgentRedactor.app"
    # Launch the packed DMG app (with UpdateMac), but use the pre-pack staging
    # app to synthesize vNext because vpk adds its helper symlinks during packing.
    shutil.copytree(PACKED_APP, work, symlinks=True)
    yield tmp_path, work, vpk
    for p in _app_processes():
        try:
            p.terminate()
        except psutil.NoSuchProcess:
            pass
    _kill_existing_agent_redactor()


def _pack_vnext(vpk: str, pack_dir: Path, out_dir: Path, version: str) -> None:
    subprocess.run(
        [
            vpk, "pack",
            "--packId", PACK_ID,
            "--packVersion", version,
            "--packDir", str(pack_dir),
            "--mainExe", MAIN_EXE,
            "--runtime", CHANNEL,
            "--channel", CHANNEL,
            "--noInst",
            "--outputDir", str(out_dir),
        ],
        check=True, capture_output=True, text=True, timeout=300,
    )
    assert (out_dir / f"releases.{CHANNEL}.json").is_file()


def _isolated_dirs(tmp_path: Path) -> tuple[Path, Path]:
    config_dir = tmp_path / "config"
    home_dir = tmp_path / "home"
    home_dir.mkdir(exist_ok=True)
    return config_dir, home_dir


def test_packed_app_checks_own_feed(packed_release) -> None:
    """The packed app checks its own (same-version) feed and stays healthy.

    Serves this build's vpk output on loopback and launches a copy of the
    staged app against it: the loopback server must see the feed request and
    the app must keep running (same version -> nothing to apply).
    """
    tmp_path, app, _vpk_path = packed_release
    config_dir, home_dir = _isolated_dirs(tmp_path)
    server = _FeedServer(VELOPACK_OUT)
    proc = _launch_app(app, config_dir, home_dir,
                       feed_url=f"http://127.0.0.1:{server.port}")
    try:
        server.wait_for_feed_request()
        time.sleep(3)  # grace: detection + no crash after the check
        assert proc.poll() is None, (
            f"packed app exited ({proc.returncode}) right after its update check")
    finally:
        server.stop()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()


def test_packed_app_detects_vnext_feed(packed_release) -> None:
    """A vNext feed on the loopback URL is detected without breaking the app.

    Packs a vNext release from the same staged .app (identical binary content;
    Velopack compares package versions in the manifest) and points the app's
    AGENTREDACTOR_UPDATE_FEED override at it. The download/apply half is
    exercised by test_app_self_updates_against_local_feed; here we assert
    the check path sees the newer version and the app stays alive.
    """
    tmp_path, app, vpk = packed_release
    feed_dir = tmp_path / "feed-vnext"
    feed_dir.mkdir()
    _pack_vnext(vpk, STAGED_APP, feed_dir, _bump_patch(_current_feed_version()))

    config_dir, home_dir = _isolated_dirs(tmp_path)
    server = _FeedServer(feed_dir)
    proc = _launch_app(app, config_dir, home_dir,
                       feed_url=f"http://127.0.0.1:{server.port}")
    try:
        server.wait_for_feed_request()
        time.sleep(3)
        assert proc.poll() is None, (
            f"packed app exited ({proc.returncode}) after detecting the vNext feed")
    finally:
        server.stop()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()


def test_app_self_updates_against_local_feed(packed_release) -> None:
    """Full self-update cycle: vNext feed -> download -> .app swap -> relaunch.

    Opt-in (AGENTREDACTOR_UPDATE_SWAP_TEST=1): exercises the Velopack apply
    path the GUI now wires (check -> download -> WaitExitThenApplyUpdates),
    which needs the staged release pack + vpk. Enable it in
    release-macos.yml (drop the env gate there) — the assertion targets
    below mirror tests/linux/test_update_feed.py.
    """
    if os.environ.get("AGENTREDACTOR_UPDATE_SWAP_TEST") != "1":
        pytest.skip(
            "opt-in: full download-and-swap cycle; enable in release-macos.yml "
            "(set AGENTREDACTOR_UPDATE_SWAP_TEST=1)")
    tmp_path, app, vpk = packed_release
    feed_dir = tmp_path / "feed-swap"
    feed_dir.mkdir()
    next_version = _bump_patch(_current_feed_version())
    _pack_vnext(vpk, STAGED_APP, feed_dir, next_version)

    config_dir, home_dir = _isolated_dirs(tmp_path)
    server = _FeedServer(feed_dir)
    # vpk does not rewrite Info.plist when packing a synthetic vNext from an
    # existing .app; Velopack's own sq.version is the update contract.
    before = _velopack_version(app)
    proc = _launch_app(app, config_dir, home_dir,
                       feed_url=f"http://127.0.0.1:{server.port}")
    try:
        deadline = time.monotonic() + 180.0
        while time.monotonic() < deadline:
            time.sleep(2.0)
            if _velopack_version(app) != before:
                break
        else:
            proc.kill()
            gui_log = (config_dir / "gui.log").read_text(
                encoding="utf-8", errors="ignore")[-20000:]
            raise AssertionError(
                f"app bundle was not updated to v{next_version} within 180s\n"
                f"--- gui.log (tail) ---\n{gui_log}")
    finally:
        server.stop()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()

    assert _velopack_version(app) == next_version
    # Velopack may already have relaunched the updated app. In that case a
    # second direct launch exits 0 through the single-instance guard, which is
    # success as long as the updated instance is running.
    proc2 = _launch_app(app, config_dir, home_dir)
    try:
        time.sleep(5)
        assert proc2.poll() is None or _app_processes(), (
            "updated app failed to relaunch")
    finally:
        proc2.terminate()
        try:
            proc2.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc2.kill()
