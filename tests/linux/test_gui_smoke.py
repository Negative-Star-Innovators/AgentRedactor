"""Linux GUI smoke tests (Qt6, QT_QPA_PLATFORM=offscreen).

These drive the real agentredactor-gui binary: engine spawn/stop ownership,
lock-on-quit when the typed master password is enabled, and XDG autostart
reconciliation against the persisted startOnBoot setting. UI interactions
themselves are not automatable offscreen; the Windows FlaUI suite covers the
equivalent UI-driven flows on Windows.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

import psutil
import pytest

_tests_root = Path(__file__).resolve().parent.parent
for _p in (str(_tests_root), str(_tests_root / "gui")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from config_factory import create_settings  # noqa: E402
from gui_process import _find_free_port, _kill_existing_agent_redactor, _wait_for_port  # noqa: E402

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux GUI smoke tests")

PROJECT_ROOT = _tests_root.parent
GUI_BIN = Path(
    os.environ.get("AGENTREDACTOR_GUI_BIN")
    or PROJECT_ROOT / "linux" / "build" / "gui" / "agentredactor-gui"
)
ENGINE_BIN = PROJECT_ROOT / "linux" / "build" / "engine" / "agentredactor"
I18N_DIR = PROJECT_ROOT / "linux" / "gui" / "i18n"


def _cli(config_dir: Path, *args: str) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    return subprocess.run(
        [str(ENGINE_BIN), *args], env=env,
        stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
    )


def _engine_processes() -> list[psutil.Process]:
    return [p for p in psutil.process_iter(["name"]) if p.info["name"] == "agentredactor"]


def _wait_engine_gone(timeout: float = 15.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not _engine_processes():
            return True
        time.sleep(0.2)
    return False


def _control_api(config_dir: Path, path: str) -> dict:
    token = json.loads((config_dir / "control.json").read_text(encoding="utf-8"))["token"]
    port = json.loads((config_dir / "control.json").read_text(encoding="utf-8"))["port"]
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        headers={"Authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.loads(resp.read())


class GuiProcess:
    """Launches the GUI offscreen with an isolated config/XDG home."""

    def __init__(self, config_dir: Path, xdg_home: Path) -> None:
        self.config_dir = config_dir
        self.process: subprocess.Popen | None = None
        self.env = dict(os.environ)
        self.env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
        self.env["XDG_CONFIG_HOME"] = str(xdg_home)
        self.env["QT_QPA_PLATFORM"] = "offscreen"

    def start(self, *args: str) -> None:
        self.process = subprocess.Popen(
            [str(GUI_BIN), *args],
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()  # SIGTERM -> graceful quit path
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None


@pytest.fixture()
def gui_env(tmp_path: Path):
    """Isolated config dir with one seeded profile + isolated XDG home."""
    if not GUI_BIN.is_file():
        pytest.skip(f"GUI binary not built: {GUI_BIN}")
    if not ENGINE_BIN.is_file():
        pytest.skip(f"engine binary not built: {ENGINE_BIN}")
    _kill_existing_agent_redactor()
    config_dir = tmp_path / "config"
    xdg_home = tmp_path / "xdg"
    proxy_port = _find_free_port()
    create_settings(
        data_dir=config_dir,
        upstream_url="http://127.0.0.1:9",  # unreachable on purpose
        api_key="sk-gui-smoke",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )
    yield config_dir, xdg_home, proxy_port
    _kill_existing_agent_redactor()


def _start_engine(config_dir: Path, proxy_port: int) -> None:
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    proc = subprocess.Popen(
        [str(ENGINE_BIN)], env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    deadline = time.monotonic() + 90
    while not (config_dir / "control.json").exists():
        if proc.poll() is not None:
            raise RuntimeError("engine exited during startup")
        if time.monotonic() > deadline:
            raise RuntimeError("control.json did not appear")
        time.sleep(0.1)
    if not _wait_for_port(proxy_port, timeout=90.0):
        raise RuntimeError("engine proxy port did not open")


def test_gui_connects_to_running_engine(gui_env) -> None:
    config_dir, xdg_home, proxy_port = gui_env
    _start_engine(config_dir, proxy_port)

    gui = GuiProcess(config_dir, xdg_home)
    gui.start()
    try:
        time.sleep(3)
        assert gui.process.poll() is None, "GUI exited while the engine was up"
    finally:
        gui.stop()
    assert _engine_processes(), "GUI stopped an engine it did not spawn"


def test_gui_spawns_and_stops_engine(gui_env) -> None:
    config_dir, xdg_home, _ = gui_env
    assert not (config_dir / "control.json").exists()

    gui = GuiProcess(config_dir, xdg_home)
    gui.start()
    try:
        # The GUI spawns the engine itself (model load takes a few seconds).
        deadline = time.monotonic() + 60
        while not (config_dir / "control.json").exists():
            assert gui.process.poll() is None, "GUI exited before spawning the engine"
            if time.monotonic() > deadline:
                raise RuntimeError("GUI did not spawn the engine")
            time.sleep(0.2)
        assert _engine_processes()
    finally:
        gui.stop()
    # The GUI spawned the engine, so quitting the GUI stops it.
    assert _wait_engine_gone(), "GUI-spawned engine survived the GUI"


def test_gui_locks_engine_on_quit_when_protected(gui_env) -> None:
    config_dir, xdg_home, proxy_port = gui_env
    _start_engine(config_dir, proxy_port)

    # Enable the typed master password via the real CLI (piped stdin).
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    r = subprocess.run(
        [str(ENGINE_BIN), "password", "enable"],
        env=env, input="gui-smoke-pw\ngui-smoke-pw\n",
        capture_output=True, text=True, timeout=30,
    )
    assert r.returncode == 0, r.stdout + r.stderr

    gui = GuiProcess(config_dir, xdg_home)
    gui.start()
    try:
        time.sleep(3)
        assert gui.process.poll() is None
    finally:
        gui.stop()

    # The engine survives (the GUI did not spawn it) but is locked again.
    assert _engine_processes(), "engine should survive a foreign GUI quit"
    status = _control_api(config_dir, "/status")
    assert status.get("masterPasswordEnabled") is True
    assert status.get("unlocked") is False


def test_autostart_file_reconciled_with_setting(gui_env) -> None:
    config_dir, xdg_home, proxy_port = gui_env
    desktop_file = xdg_home / "autostart" / "agentredactor.desktop"
    _start_engine(config_dir, proxy_port)

    # Off by default: a GUI run must not create the file.
    gui = GuiProcess(config_dir, xdg_home)
    gui.start()
    time.sleep(3)
    gui.stop()
    assert not desktop_file.exists()

    # Turn the setting on via the CLI (as a script/AI agent would); the next
    # GUI run reconciles the autostart file with it.
    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    r = subprocess.run(
        [str(ENGINE_BIN), "set", "start-on-boot", "true"],
        env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
    )
    assert r.returncode == 0, r.stdout + r.stderr

    gui.start()
    deadline = time.monotonic() + 10
    while not desktop_file.exists() and time.monotonic() < deadline:
        time.sleep(0.2)
    gui.stop()
    assert desktop_file.exists()
    content = desktop_file.read_text(encoding="utf-8")
    assert "--tray-only" in content
    assert "Exec=" in content

    # And back off via the CLI.
    r = subprocess.run(
        [str(ENGINE_BIN), "set", "start-on-boot", "false"],
        env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
    )
    assert r.returncode == 0, r.stdout + r.stderr

    gui.start()
    deadline = time.monotonic() + 10
    while desktop_file.exists() and time.monotonic() < deadline:
        time.sleep(0.2)
    gui.stop()
    assert not desktop_file.exists()


def test_autostart_exec_uses_stable_appimage_path(gui_env) -> None:
    """Regression: an autostart entry pointing into the transient
    /tmp/.mount_* AppImage FUSE mount never survives a reboot. Under an
    AppImage the entry must exec $APPIMAGE (the AppImage file), and a stale
    mount-path entry must be rewritten when the GUI next runs."""
    config_dir, xdg_home, proxy_port = gui_env
    desktop_file = xdg_home / "autostart" / "agentredactor.desktop"
    _start_engine(config_dir, proxy_port)

    env = dict(os.environ)
    env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
    r = subprocess.run(
        [str(ENGINE_BIN), "set", "start-on-boot", "true"],
        env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30,
    )
    assert r.returncode == 0, r.stdout + r.stderr

    # Stale entry as written by the old code (ephemeral mount path).
    desktop_file.parent.mkdir(parents=True, exist_ok=True)
    desktop_file.write_text(
        "[Desktop Entry]\nType=Application\nName=Agent Redactor\n"
        "Exec=/tmp/.mount_AgentRLghAAm/usr/bin/agentredactor-gui --tray-only\n"
        "X-GNOME-Autostart-enabled=true\n",
        encoding="utf-8",
    )

    gui = GuiProcess(config_dir, xdg_home)
    gui.env["APPIMAGE"] = "/fake/stable/AgentRedactor.AppImage"
    gui.start()
    try:
        deadline = time.monotonic() + 15
        content = desktop_file.read_text(encoding="utf-8")
        while "/tmp/.mount_" in content and time.monotonic() < deadline:
            time.sleep(0.2)
            content = desktop_file.read_text(encoding="utf-8")
    finally:
        gui.stop()

    assert "/tmp/.mount_" not in content, f"stale mount path kept: {content}"
    assert 'Exec="/fake/stable/AgentRedactor.AppImage" --tray-only' in content


def test_all_supported_languages_have_catalogs(gui_env) -> None:
    """Every language the CLI/engine supports must have a Qt catalog, so the
    GUI can never offer a language it cannot display."""
    config_dir, _, proxy_port = gui_env
    _start_engine(config_dir, proxy_port)

    r = _cli(config_dir, "languages")
    assert r.returncode == 0, r.stdout + r.stderr
    tags = [line.strip() for line in r.stdout.splitlines() if line.strip()]
    assert len(tags) > 50

    missing = [
        tag for tag in tags
        if tag != "en"  # English is the source language; no catalog needed
        and not (I18N_DIR / f"agentredactor_{tag.replace('-', '_')}.ts").is_file()
    ]
    assert not missing, f"languages without a translation catalog: {missing}"


def test_gui_applies_language_setting_live(gui_env) -> None:
    """Switching app-language via the CLI while the GUI runs is picked up by
    the settings poll and retranslates without a restart (and without a
    crash — offscreen we cannot assert pixels, but the whole load/retranslate
    path executes, including the RTL layout flip)."""
    config_dir, xdg_home, proxy_port = gui_env
    _start_engine(config_dir, proxy_port)

    gui = GuiProcess(config_dir, xdg_home)
    gui.start()
    try:
        time.sleep(3)
        assert gui.process.poll() is None

        for tag in ("de", "ar", "zh-CN", "en"):
            r = _cli(config_dir, "set", "app-language", tag)
            assert r.returncode == 0, r.stdout + r.stderr
            time.sleep(2.5)  # at least one settings poll + LanguageChange
            assert gui.process.poll() is None, f"GUI died switching to {tag}"
            settings = _control_api(config_dir, "/settings")
            assert settings.get("appLanguage") == tag
    finally:
        gui.stop()
