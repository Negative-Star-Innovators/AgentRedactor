"""Opt-in end-to-end test for the Velopack self-release *fresh install* path
on macOS (osx-arm64 channel).

Mounts a just-built ``AgentRedactor-osx-arm64.dmg``, verifies its
drag-to-Applications layout, and copies the app to the selected Applications
folder (per-user by default). The test then verifies the installed layout and
version, checks the bundled engine is a native arm64 Mach-O, and runs the
headless ``--selftest-migrate-settings`` hook on the *installed* engine (so the
test exercises exactly the bits a user gets, not the build folder). Removes the
app again afterwards unless told to keep it.

Skipped unless AGENTREDACTOR_INSTALL_TEST=1. Required env vars:
  AGENTREDACTOR_DMG          path to the AgentRedactor-*.dmg under test
Optional:
  AGENTREDACTOR_INSTALL_DIR     install parent (default ``~/Applications``;
                                CI uses ``/Applications``)
  AGENTREDACTOR_EXPECT_VERSION  expected CFBundleShortVersionString of the
                                installed app (e.g. "1.1.0"); skipped when unset
  AGENTREDACTOR_FEED_DIR        vpk output folder containing
                                releases.<channel>.json; when set, the freshly
                                installed app's OWN update check is pointed at
                                that feed over localhost HTTP and must fetch
                                releases.<channel>.json (same-version feed, so
                                nothing downloads). The macOS GUI has no
                                "[UpdateManager] Up to date" lifecycle-log
                                contract yet (the check lives in SwiftUI), so
                                the assertion is the feed fetch itself.
  AGENTREDACTOR_CHANNEL         feed channel name (default "osx-arm64")
  AGENTREDACTOR_MODEL_SMOKE=1   after install verification, launch the app and
                                assert the first-run model download actually
                                starts fetching (growing .partial/.partN files
                                under the isolated HOME's fallback model dir),
                                then kill it. Does NOT wait for the full
                                1.6 GB download.
  AGENTREDACTOR_KEEP_INSTALL=1  do not remove the installed app afterwards

See tests/macos/README.md for the full contract and CI wiring.
"""

from __future__ import annotations

import json
import os
import plistlib
import shutil
import struct
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

import psutil
import pytest

from feed_server import FeedServer

pytestmark = [
    pytest.mark.install,
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS DMG install E2E"),
    pytest.mark.skipif(
        os.environ.get("AGENTREDACTOR_INSTALL_TEST") != "1",
        reason="opt-in E2E: set AGENTREDACTOR_INSTALL_TEST=1 "
        "(plus AGENTREDACTOR_DMG)",
    ),
]

# Local runs default to the no-elevation per-user destination. CI explicitly
# selects /Applications on its clean admin runner.
INSTALL_DIR = Path(os.environ.get(
    "AGENTREDACTOR_INSTALL_DIR", str(Path.home() / "Applications")))
APP_PATH = INSTALL_DIR / "Agent Redactor.app"
ENGINE_REL = Path("Contents/Resources/agentredactor")

SETUP_TIMEOUT_S = 300
FEED_REQUEST_TIMEOUT_S = 60.0
FEED_POLL_S = 2.0
MODEL_SMOKE_TIMEOUT_S = 180  # first-run segmented download start
MODEL_SMOKE_POLL_S = 5

# Mach-O constants (mach/machine.h).
MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C


def _version_key(text: str) -> tuple[int, int, int]:
    """Normalize 'v1.2.3[.0][-suffix]' to a comparable (1, 2, 3) tuple."""
    core = text.strip().lstrip("vV").split("-")[0].split("+")[0]
    parts = [int(p) for p in core.split(".") if p != ""]
    return tuple((parts + [0, 0, 0])[:3])


def _bundle_version(app: Path) -> str:
    with open(app / "Contents" / "Info.plist", "rb") as fh:
        return plistlib.load(fh).get("CFBundleShortVersionString", "")


def _mach_o_cputype(binary: Path) -> int:
    """Return the CPU type of a thin 64-bit Mach-O (assert thinness first)."""
    with open(binary, "rb") as fh:
        header = fh.read(16)
        magic = struct.unpack_from("<I", header, 0)[0]
        if magic != MH_MAGIC_64:
            pytest.fail(
                f"{binary} is not a thin 64-bit Mach-O (magic 0x{magic:08X}); "
                "fat/universal handling is not implemented for this check")
        return struct.unpack_from("<I", header, 4)[0]


def _kill_install_processes() -> None:
    """Kill copies that could otherwise intercept the single-instance launch."""
    for proc in psutil.process_iter(["exe"]):
        try:
            exe = proc.info.get("exe")
            if exe and ("/Agent Redactor.app/" in exe
                        or "/AgentRedactor.app/" in exe):
                proc.kill()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    time.sleep(1)


def _start_model_download(config_dir: Path) -> None:
    """Call the same endpoint as ModelDownloadSheet.onAppear, without UI timing."""
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        try:
            control = json.loads((config_dir / "control.json").read_text())
            request = urllib.request.Request(
                f"http://127.0.0.1:{control['port']}/engine/download-model",
                method="POST", data=b"")
            request.add_header("Authorization", f"Bearer {control['token']}")
            with urllib.request.urlopen(request, timeout=5) as response:
                if response.status == 200:
                    return
        except (OSError, ValueError, KeyError):
            time.sleep(0.5)
    pytest.fail("control API did not accept the model-download request")


def _assert_own_updater_healthy(app: Path, feed_dir: Path, channel: str,
                                tmp_path: Path) -> None:
    """Run the freshly installed build's OWN update check against its own feed.

    Serves feed_dir (this build's vpk output, same version as the installed
    app) over localhost HTTP, launches the installed app with the
    AGENTREDACTOR_UPDATE_FEED test hook pointing at it, and requires the feed
    to be fetched — i.e. the NEW build's fetch/parse path works. Unlike the
    Windows/Linux builds there is no "[UpdateManager] Up to date" lifecycle
    log line to assert on yet (the check lives in the SwiftUI
    UpdateProvider), so the observed contract is the HTTP request itself; a
    same-version feed means nothing downloads.
    """
    config_dir = tmp_path / "updater-config"
    config_dir.mkdir()
    home_dir = tmp_path / "updater-home"
    home_dir.mkdir()
    server = FeedServer(feed_dir)
    env = dict(os.environ)
    env.update({
        "AGENTREDACTOR_UPDATE_FEED": f"http://127.0.0.1:{server.port}",
        "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
        "AGENTREDACTOR_DISABLE_KEYRING": "1",
        "HOME": str(home_dir),
    })
    proc = subprocess.Popen(
        [str(app / "Contents" / "MacOS" / "AgentRedactor")],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        server.wait_for_feed_request(channel)
    finally:
        server.stop()
        _kill_install_processes()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()


def test_selfrelease_fresh_install_macos(tmp_path):
    dmg = os.environ.get("AGENTREDACTOR_DMG")
    if not dmg or not Path(dmg).is_file():
        pytest.skip("AGENTREDACTOR_DMG is not set or does not point at a DMG")

    engine = APP_PATH / ENGINE_REL
    mount_point = tmp_path / "dmg-mount"
    mount_point.mkdir()
    mounted = False

    try:
        _kill_install_processes()

        # 1. Mount the DMG, verify its drag-and-drop contract, and copy the
        #    app to the selected Applications destination as the current user.
        INSTALL_DIR.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["rm", "-rf", str(APP_PATH)],
            check=False, capture_output=True, timeout=60,
        )
        subprocess.run(
            ["hdiutil", "attach", dmg, "-nobrowse", "-readonly",
             "-mountpoint", str(mount_point)],
            check=True, capture_output=True, timeout=SETUP_TIMEOUT_S,
        )
        mounted = True
        assert (mount_point / "Agent Redactor.app").is_dir()
        applications = mount_point / "Applications"
        assert applications.is_symlink() and os.readlink(applications) == "/Applications"
        subprocess.run(
            ["ditto", str(mount_point / "Agent Redactor.app"), str(APP_PATH)],
            check=True, timeout=SETUP_TIMEOUT_S,
        )
        subprocess.run(
            ["hdiutil", "detach", str(mount_point)],
            check=True, capture_output=True, timeout=60,
        )
        mounted = False
        _kill_install_processes()

        # 2. Verify the installed layout, including Velopack's UpdateMac helper
        #    (added during packing and required for self-updating).
        assert (APP_PATH / "Contents" / "MacOS" / "AgentRedactor").is_file() \
            and (APP_PATH / "Contents" / "MacOS" / "UpdateMac").is_file() \
            and engine.is_file(), (
            f"Unexpected install layout; expected Agent Redactor.app with "
            f"UpdateMac and a bundled engine under {APP_PATH}")

        # 3. Version assertion (when the caller knows what it built).
        expected_version = os.environ.get("AGENTREDACTOR_EXPECT_VERSION")
        if expected_version:
            installed = _bundle_version(APP_PATH)
            assert _version_key(installed) == _version_key(expected_version), (
                f"installed version '{installed}' does not match expected "
                f"'{expected_version}'"
            )

        # 4. Architecture assertion: the bundled engine must be native arm64
        #    (guards against packing the wrong binaries into the osx-arm64
        #    channel).
        cputype = _mach_o_cputype(engine)
        assert cputype == CPU_TYPE_ARM64, (
            f"bundled engine CPU type 0x{cputype:08X}, expected ARM64 "
            f"(0x{CPU_TYPE_ARM64:08X})")

        # 5. The installed engine's headless migration hook works against an
        #    isolated config dir (proves the packaged binary actually runs).
        config_dir = tmp_path / "config"
        config_dir.mkdir()
        result = subprocess.run(
            [str(engine), "--selftest-migrate-settings"],
            env={
                **os.environ,
                "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
                "AGENTREDACTOR_DISABLE_KEYRING": "1",
            },
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert result.returncode == 0, (
            f"selftest failed on fresh install: {result.stdout} {result.stderr}"
        )
        assert "SETTINGS_MIGRATION_OK" in result.stdout

        # 6. Optional: the NEW build's own update check must complete a feed
        #    fetch against its own feed (see _assert_own_updater_healthy).
        feed_dir = os.environ.get("AGENTREDACTOR_FEED_DIR")
        channel = os.environ.get("AGENTREDACTOR_CHANNEL", "osx-arm64")
        if feed_dir and (Path(feed_dir) / f"releases.{channel}.json").is_file():
            _assert_own_updater_healthy(APP_PATH, Path(feed_dir), channel, tmp_path)

        # 7. Optional: first-run model-download smoke test. The self-release
        #    package ships without the 1.6 GB weights and downloads them from
        #    R2 on first run into the fallback model dir
        #    (<HOME>/Library/Application Support/AgentRedactor/models). HOME is
        #    isolated so the runner's real library is untouched. Assert
        #    partial files appear AND grow, then kill — the full download is
        #    NOT completed in CI.
        if os.environ.get("AGENTREDACTOR_MODEL_SMOKE") == "1":
            home_dir = tmp_path / "model-home"
            home_dir.mkdir()
            config_dir2 = tmp_path / "model-config"
            config_dir2.mkdir()
            models_dir = home_dir / "Library" / "Application Support" \
                / "AgentRedactor" / "models"
            env = dict(os.environ)
            env.update({
                "AGENTREDACTOR_CONFIG_DIR": str(config_dir2),
                "AGENTREDACTOR_DISABLE_KEYRING": "1",
                "HOME": str(home_dir),
            })
            app = subprocess.Popen(
                [str(APP_PATH / "Contents" / "MacOS" / "AgentRedactor")],
                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            try:
                _start_model_download(config_dir2)
                deadline = time.monotonic() + MODEL_SMOKE_TIMEOUT_S
                grown = False
                last_sizes: dict[str, int] = {}
                while time.monotonic() < deadline and not grown:
                    time.sleep(MODEL_SMOKE_POLL_S)
                    partials = [
                        p
                        for pattern in ("*.partial", "*.part*", "*.onnx_data")
                        for p in models_dir.rglob(pattern)
                    ] if models_dir.is_dir() else []
                    sizes = {str(p): p.stat().st_size for p in partials if p.is_file()}
                    for name, size in sizes.items():
                        if size > last_sizes.get(name, -1):
                            grown = True
                            break
                    last_sizes = sizes
                if not grown:
                    listing = (
                        "\n".join(str(p) for p in models_dir.rglob("*"))
                        if models_dir.is_dir()
                        else f"{models_dir} does not exist"
                    )
                    pytest.fail(
                        f"model download did not start within {MODEL_SMOKE_TIMEOUT_S}s "
                        f"(no growing .partial/.partN files under {models_dir}).\n{listing}"
                    )
            finally:
                _kill_install_processes()
                if app.poll() is None:
                    app.kill()
    finally:
        if mounted:
            subprocess.run(
                ["hdiutil", "detach", str(mount_point), "-force"],
                check=False, capture_output=True, timeout=60,
            )
        _kill_install_processes()
        if os.environ.get("AGENTREDACTOR_KEEP_INSTALL") != "1":
            # Best-effort removal: the runner is ephemeral anyway, so cleanup
            # must never fail or stall the test.
            subprocess.run(
                ["rm", "-rf", str(APP_PATH)],
                check=False, capture_output=True, timeout=60,
            )
            _kill_install_processes()
