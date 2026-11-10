"""macOS self-update E2E: previous live R2 release -> current candidate.

Skipped unless AGENTREDACTOR_UPGRADE_TEST=1. Required environment:
  AGENTREDACTOR_PREV_DMG       previous live AgentRedactor-osx-arm64.dmg
  AGENTREDACTOR_FEED_DIR       current vpk output containing the osx-arm64 feed
Optional:
  AGENTREDACTOR_EXPECT_VERSION expected candidate version
"""

from __future__ import annotations

import json
import os
import plistlib
import re
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
from feed_server import FeedServer  # noqa: E402
from gui_process import _find_free_port, _kill_existing_agent_redactor  # noqa: E402

pytestmark = [
    pytest.mark.upgrade,
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS upgrade E2E"),
    pytest.mark.skipif(
        os.environ.get("AGENTREDACTOR_UPGRADE_TEST") != "1",
        reason="opt-in E2E: set AGENTREDACTOR_UPGRADE_TEST=1 "
        "(plus AGENTREDACTOR_PREV_DMG / AGENTREDACTOR_FEED_DIR)",
    ),
]

CHANNEL = "osx-arm64"
POLL_TIMEOUT_S = 300.0
POLL_INTERVAL_S = 2.0


def _version_key(text: str) -> tuple[int, int, int]:
    core = text.strip().lstrip("vV").split("-")[0].split("+")[0]
    parts = [int(p) for p in core.split(".") if p]
    return tuple((parts + [0, 0, 0])[:3])


def _latest_feed_version(feed_dir: Path) -> str:
    feed = json.loads((feed_dir / f"releases.{CHANNEL}.json").read_text(encoding="utf-8"))
    versions = [
        asset["Version"] for asset in feed.get("Assets", [])
        if str(asset.get("Type")) in ("1", "Full") and asset.get("Version")
    ]
    if not versions:
        pytest.fail("candidate feed contains no full package")
    return max(versions, key=_version_key)


def _bundle_version(app: Path) -> str:
    with (app / "Contents" / "Info.plist").open("rb") as handle:
        return plistlib.load(handle)["CFBundleShortVersionString"]


def _velopack_version(app: Path) -> str:
    metadata = (app / "Contents" / "Resources" / "sq.version").read_text(encoding="utf-8")
    match = re.search(r"<version>([^<]+)</version>", metadata)
    assert match, "Velopack sq.version has no package version"
    return match.group(1)


def _gui_processes() -> list[psutil.Process]:
    processes = []
    for process in psutil.process_iter(["name"]):
        try:
            if process.info["name"] == "AgentRedactor":
                processes.append(process)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return processes


def _stop_apps(process: subprocess.Popen | None = None) -> None:
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10.0)
    targets = _gui_processes()
    for target in targets:
        try:
            target.terminate()
        except (psutil.NoSuchProcess, ProcessLookupError):
            pass
    _, alive = psutil.wait_procs(targets, timeout=3.0)
    for target in alive:
        try:
            target.kill()
        except (psutil.NoSuchProcess, ProcessLookupError):
            pass
    if alive:
        psutil.wait_procs(alive, timeout=10.0)
    _kill_existing_agent_redactor()


def test_upgrade_from_live_dmg(tmp_path: Path) -> None:
    previous_dmg = Path(os.environ.get("AGENTREDACTOR_PREV_DMG", ""))
    feed_dir = Path(os.environ.get("AGENTREDACTOR_FEED_DIR", ""))
    if not previous_dmg.is_file():
        pytest.skip("AGENTREDACTOR_PREV_DMG does not point at the previous live DMG")
    if not (feed_dir / f"releases.{CHANNEL}.json").is_file():
        pytest.skip("AGENTREDACTOR_FEED_DIR does not contain the current osx-arm64 feed")

    expected = os.environ.get("AGENTREDACTOR_EXPECT_VERSION") or _latest_feed_version(feed_dir)
    expected_match = re.match(r"^(\d+)\.(\d+)\.(\d+)", expected)
    assert expected_match, f"unexpected candidate version: {expected}"
    expected_core = expected_match.group(0)

    cached_models = Path.home() / "Library" / "Application Support" \
        / "AgentRedactor" / "models"
    if not (cached_models / "onnx" / "model_quantized.onnx_data").is_file():
        pytest.skip("the CI model cache is required to avoid a second 1.6 GB download")

    _stop_apps()
    app = tmp_path / "installed" / "Agent Redactor.app"
    mount_point = tmp_path / "previous-dmg"
    mount_point.mkdir()
    mounted = False
    server = FeedServer(feed_dir)
    process: subprocess.Popen | None = None
    relaunched: subprocess.Popen | None = None
    config_dir = tmp_path / "config"
    home_dir = tmp_path / "home"
    log_handle = None

    try:
        subprocess.run(
            ["hdiutil", "attach", str(previous_dmg), "-nobrowse", "-readonly",
             "-mountpoint", str(mount_point)],
            check=True, capture_output=True, timeout=120,
        )
        mounted = True
        app.parent.mkdir()
        subprocess.run(
            ["ditto", str(mount_point / "Agent Redactor.app"), str(app)],
            check=True, capture_output=True, timeout=180,
        )
        subprocess.run(
            ["hdiutil", "detach", str(mount_point)],
            check=True, capture_output=True, timeout=60,
        )
        mounted = False

        previous = _bundle_version(app)
        assert _version_key(previous) < _version_key(expected), (
            f"previous live version {previous} is not older than candidate {expected}"
        )
        assert _version_key(_velopack_version(app)) == _version_key(previous)

        proxy_port = _find_free_port()
        create_settings(
            data_dir=config_dir,
            upstream_url="http://127.0.0.1:9",
            api_key="sk-upgrade-e2e",
            proxy_port=proxy_port,
            logging_enabled=False,
        )
        settings_path = config_dir / "settings.json"
        settings = json.loads(settings_path.read_text(encoding="utf-8"))
        settings["profiles"][0]["alias"] = "upgrade-from-live"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")

        models_parent = home_dir / "Library" / "Application Support" / "AgentRedactor"
        models_parent.mkdir(parents=True)
        (models_parent / "models").symlink_to(cached_models, target_is_directory=True)

        env = dict(os.environ)
        env.update({
            "AGENTREDACTOR_UPDATE_FEED": f"http://127.0.0.1:{server.port}",
            "AGENTREDACTOR_UPDATE_AUTOAPPLY": "1",
            "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
            "AGENTREDACTOR_DISABLE_KEYRING": "1",
            "HOME": str(home_dir),
        })
        config_dir.mkdir(exist_ok=True)
        log_handle = open(config_dir / "upgrade-gui.log", "a")
        process = subprocess.Popen(
            [str(app / "Contents" / "MacOS" / "AgentRedactor")],
            env=env, stdout=log_handle, stderr=log_handle,
        )

        deadline = time.monotonic() + POLL_TIMEOUT_S
        seen = _velopack_version(app)
        while time.monotonic() < deadline:
            time.sleep(POLL_INTERVAL_S)
            try:
                seen = _velopack_version(app)
            except (FileNotFoundError, OSError):
                continue
            if _version_key(seen) >= _version_key(expected):
                break
        else:
            log_tail = ""
            if log_handle:
                log_handle.flush()
                log_tail = (config_dir / "upgrade-gui.log").read_text(
                    encoding="utf-8", errors="ignore")[-12000:]
            pytest.fail(
                f"macOS app did not upgrade from {previous} to {expected}; "
                f"last Velopack version was {seen}\n{log_tail}"
            )

        assert _version_key(_velopack_version(app)) == _version_key(expected)
        assert _bundle_version(app) == expected_core

        relaunched = subprocess.Popen(
            [str(app / "Contents" / "MacOS" / "AgentRedactor")],
            env=env, stdout=log_handle, stderr=log_handle,
        )
        time.sleep(5)
        assert relaunched.poll() is None or _gui_processes(), (
            "updated app failed to relaunch"
        )

        _stop_apps(relaunched)
        relaunched = None
        result = subprocess.run(
            [str(app / "Contents" / "Resources" / "agentredactor"),
             "--selftest-migrate-settings"],
            env={**os.environ,
                 "AGENTREDACTOR_CONFIG_DIR": str(config_dir),
                 "AGENTREDACTOR_DISABLE_KEYRING": "1",
                 "HOME": str(home_dir)},
            capture_output=True, text=True, timeout=60,
        )
        assert result.returncode == 0, (
            f"upgraded engine settings migration failed: {result.stdout} {result.stderr}"
        )
        assert "SETTINGS_MIGRATION_OK" in result.stdout

        upgraded_settings = json.loads(settings_path.read_text(encoding="utf-8"))
        assert upgraded_settings["settings_version"] == 2
        assert upgraded_settings["app_language"] == "en"
        assert upgraded_settings["profiles"][0]["alias"] == "upgrade-from-live"
        assert upgraded_settings["profiles"][0]["port"] == proxy_port
        assert upgraded_settings["profiles"][0]["api_key"]
    finally:
        _stop_apps(relaunched or process)
        server.stop()
        if log_handle:
            log_handle.close()
        if mounted:
            subprocess.run(
                ["hdiutil", "detach", str(mount_point), "-force"],
                check=False, capture_output=True, timeout=60,
            )
