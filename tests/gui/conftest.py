"""pytest fixtures for AgentRedactor GUI tests."""

from __future__ import annotations

import json
import os
import shutil
import sys
import time
from collections.abc import AsyncIterator, Iterator
from pathlib import Path

import pytest
import pytest_asyncio

# Allow importing shared test utilities from tests/
_tests_root = Path(__file__).resolve().parent.parent
if str(_tests_root) not in sys.path:
    sys.path.insert(0, str(_tests_root))

from config_factory import ENABLED_PII_TYPES, create_settings
from mock_llm import MockLLM

from gui_process import GuiAppProcess, _find_free_port
from windows.gui_driver import quit_app

# The GUI exists on Windows only; on Linux the headless engine/CLI suites
# (tests/cli, tests/linux) cover the port. Ignore this whole directory so a
# bare `pytest tests/` on Linux does not error trying to launch
# AgentRedactorUI.exe.
if sys.platform != "win32":
    collect_ignore_glob = ["*"]


@pytest.fixture
def proxy_port() -> int:
    """Override the shared proxy_port fixture for GUI tests."""
    return _find_free_port()


@pytest.fixture(scope="function")
def user_data_backup() -> Iterator[Path]:
    """Backup the real AgentRedactor user data, yield %APPDATA% path, then restore."""
    appdata = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    localappdata = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))

    app_dir = appdata / "AgentRedactor"
    local_dir = localappdata / "AgentRedactor"
    app_backup = appdata / "AgentRedactor.gui_test_backup"
    local_backup = localappdata / "AgentRedactor.gui_test_backup"

    # Remove any stale backups from a previous aborted run.
    if app_backup.exists():
        shutil.rmtree(app_backup, ignore_errors=True)
    if local_backup.exists():
        shutil.rmtree(local_backup, ignore_errors=True)

    # Move current user data to backup if it exists.
    if app_dir.exists():
        shutil.move(str(app_dir), str(app_backup))
    if local_dir.exists():
        shutil.move(str(local_dir), str(local_backup))

    # Create fresh directories for the test.
    app_dir.mkdir(parents=True, exist_ok=True)
    local_dir.mkdir(parents=True, exist_ok=True)

    try:
        yield app_dir
    finally:
        # Clean up test data.
        if app_dir.exists():
            shutil.rmtree(app_dir, ignore_errors=True)
        if local_dir.exists():
            shutil.rmtree(local_dir, ignore_errors=True)

        # Restore original user data.
        if app_backup.exists():
            shutil.move(str(app_backup), str(app_dir))
        if local_backup.exists():
            shutil.move(str(local_backup), str(local_dir))


@pytest_asyncio.fixture
async def gui_app(
    user_data_backup: Path,
    proxy_port: int,
    mock_llm: MockLLM,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor using the real UI and real user data directories."""
    create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],  # keyword will be added via the UI
        regex_patterns=[],  # regex patterns will be added via the UI
    )

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        returncode = app.process.poll() if app.process else None
        app.stop()
        if returncode is not None and returncode != 0:
            localappdata = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
            appdata = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
            candidates = [
                localappdata / "AgentRedactor" / "sessions" / "gui_test_stdout.txt",
                localappdata / "AgentRedactor" / "sessions" / "gui_test_stderr.txt",
                appdata / "AgentRedactor" / "agent_redactor.log",
            ]
            for path in candidates:
                if path.exists():
                    print(f"\n--- {path.name} ---")
                    try:
                        print(path.read_text(encoding="utf-8", errors="ignore")[-4000:])
                    except Exception as e:
                        print(f"Failed to read log: {e}")
        # Ensure no lingering process after the test.
        try:
            quit_app()
        except Exception:
            pass


@pytest_asyncio.fixture
async def gui_app_no_profile_config(
    user_data_backup: Path,
    proxy_port: int,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor with a default profile but no upstream config.

    Tests that configure the profile through the UI can use this fixture.
    """
    create_settings(
        data_dir=user_data_backup,
        upstream_url="http://127.0.0.1:1",
        api_key="",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        app.stop()
        try:
            quit_app()
        except Exception:
            pass


@pytest_asyncio.fixture
async def gui_app_no_pii(
    user_data_backup: Path,
    proxy_port: int,
    mock_llm: MockLLM,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor with PII detection disabled."""
    create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
        enabled_pii_types=[],
    )

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        app.stop()
        try:
            quit_app()
        except Exception:
            pass


@pytest_asyncio.fixture
async def gui_app_pii_email_only(
    user_data_backup: Path,
    proxy_port: int,
    mock_llm: MockLLM,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor with only email PII enabled."""
    create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
        enabled_pii_types=["private_email"],
    )

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        app.stop()
        try:
            quit_app()
        except Exception:
            pass


@pytest_asyncio.fixture
async def gui_app_pii_person_only(
    user_data_backup: Path,
    proxy_port: int,
    mock_llm: MockLLM,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor with only person PII enabled."""
    create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
        enabled_pii_types=["private_person"],
    )

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        app.stop()
        try:
            quit_app()
        except Exception:
            pass


@pytest_asyncio.fixture
async def gui_app_logging_disabled(
    user_data_backup: Path,
    proxy_port: int,
    mock_llm: MockLLM,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor with Enable logging turned off."""
    create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=False,
        keywords=[],
        regex_patterns=[],
    )

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        app.stop()
        try:
            quit_app()
        except Exception:
            pass


@pytest_asyncio.fixture
async def gui_app_legacy_verbose_logging(
    user_data_backup: Path,
    proxy_port: int,
    mock_llm: MockLLM,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor with a legacy settings.json that only has verbose_logging."""
    settings_path = create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=proxy_port,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )
    # Rewrite as a legacy settings file: verbose_logging only, no logging_enabled.
    settings = json.loads(settings_path.read_text(encoding="utf-8"))
    del settings["logging_enabled"]
    settings["verbose_logging"] = True
    settings_path.write_text(json.dumps(settings, indent=2), encoding="utf-8")

    app = GuiAppProcess(proxy_port=proxy_port)
    _start = time.monotonic()
    app.start()
    print(f"\n[GUI FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        app.stop()
        try:
            quit_app()
        except Exception:
            pass
