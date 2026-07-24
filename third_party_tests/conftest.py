"""pytest fixtures for third-party integration tests."""

from __future__ import annotations

import asyncio
import os
import shutil
import socket
import sys
import time
from collections.abc import AsyncIterator, Iterator
from pathlib import Path

import pytest
import pytest_asyncio
from dotenv import load_dotenv

# Load environment variables from third_party_tests/.env
load_dotenv(Path(__file__).resolve().parent / ".env")

# Allow importing shared test utilities from tests/
_tests_root = Path(__file__).resolve().parent.parent / "tests"
if str(_tests_root) not in sys.path:
    sys.path.insert(0, str(_tests_root))

from config_factory import create_settings
from gui.gui_process import GuiAppProcess, _kill_existing_agent_redactor
from gui.windows.gui_driver import quit_app
from mock_llm_server import MockLLMServer


PROXY_PORT = 8081


def _is_port_in_use(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        return s.connect_ex(("127.0.0.1", port)) == 0


def _rmtree_with_retry(path: Path, retries: int = 5, delay: float = 1.0) -> None:
    """Remove a directory tree, retrying if a process holds files open."""
    if not path.exists():
        return
    for attempt in range(retries):
        try:
            shutil.rmtree(path, ignore_errors=True)
            if not path.exists():
                return
        except PermissionError:
            pass
        _kill_existing_agent_redactor()
        time.sleep(delay)
    # Last-ditch attempt; let any exception propagate.
    shutil.rmtree(path, ignore_errors=False)


def _move_with_retry(src: Path, dst: Path, retries: int = 5, delay: float = 1.0) -> None:
    """Move a directory, retrying on file locks. No-op if the source does not exist."""
    if not src.exists():
        return
    for attempt in range(retries):
        try:
            shutil.move(str(src), str(dst))
            return
        except PermissionError:
            _kill_existing_agent_redactor()
            time.sleep(delay)
    shutil.move(str(src), str(dst))


@pytest_asyncio.fixture
async def mock_llm() -> AsyncIterator[MockLLMServer]:
    """Provide a mock LLM that echoes request content back as the response."""
    async with MockLLMServer() as llm:
        yield llm


@pytest_asyncio.fixture
async def agentredactor_app_redacting(
    user_data_backup: Path,
    mock_llm: MockLLMServer,
) -> AsyncIterator[tuple[GuiAppProcess, MockLLMServer]]:
    """Start AgentRedactor on port 8081 pointing at the mock LLM with keyword redaction."""
    if _is_port_in_use(PROXY_PORT):
        raise RuntimeError(
            f"Port {PROXY_PORT} is already in use. "
            "The third-party setup scripts expect the proxy on this port."
        )

    create_settings(
        data_dir=user_data_backup,
        upstream_url=mock_llm.base_url,
        api_key="test-api-key",
        proxy_port=PROXY_PORT,
        logging_enabled=True,
        keywords=[
            {"text": "hugh", "case_sensitive": False, "enabled": True},
        ],
        regex_patterns=[],
        enabled_pii_types=[],
    )

    app = GuiAppProcess(proxy_port=PROXY_PORT)
    _start = time.monotonic()
    app.start()
    print(f"\n[3RD PARTY REDACTION FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app, mock_llm
    finally:
        returncode = app.process.poll() if app.process else None
        app.stop()
        if returncode is not None and returncode != 0:
            localappdata = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
            agent_dir = localappdata / "AgentRedactor"
            for rel in ("sessions/gui_test_stdout.txt", "sessions/gui_test_stderr.txt", "agent_redactor.log"):
                path = agent_dir / rel
                if path.exists():
                    print(f"\n--- {rel} ---")
                    try:
                        print(path.read_text(encoding="utf-8", errors="ignore")[-4000:])
                    except Exception as e:
                        print(f"Failed to read log: {e}")
        # Ensure no lingering process after the test. This also releases file
        # locks on the session log files before user_data_backup tears down.
        try:
            quit_app()
        except Exception:
            pass
        _kill_existing_agent_redactor()
        time.sleep(0.5)


@pytest.fixture(scope="session")
def event_loop():
    """Provide a session-scoped event loop for pytest-asyncio."""
    loop = asyncio.get_event_loop_policy().new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session", autouse=True)
def _cleanup_leftover_agentredactor() -> None:
    """Kill any AgentRedactor.exe left over from a previous aborted run.

    This runs once at session start so the per-test backup/restore fixture does
    not fail with PermissionError on locked session log files.
    """
    _kill_existing_agent_redactor()
    time.sleep(0.5)


@pytest_asyncio.fixture
async def client() -> AsyncIterator[aiohttp.ClientSession]:
    """Provide an aiohttp client session."""
    import aiohttp

    async with aiohttp.ClientSession() as session:
        yield session


@pytest.fixture
def openrouter_api_key() -> str:
    """Return the OpenRouter API key from the environment.

    Skips the test if the key is not configured.
    """
    key = os.environ.get("OPENROUTER_API_KEY")
    if not key:
        pytest.skip(
            "OPENROUTER_API_KEY is not set. "
            "Copy third_party_tests/.env.example to .env and add your key."
        )
    return key


@pytest.fixture(scope="function")
def user_data_backup() -> Iterator[Path]:
    """Backup real AgentRedactor user data, yield %APPDATA% path, then restore."""
    appdata = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    localappdata = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))

    app_dir = appdata / "AgentRedactor"
    local_dir = localappdata / "AgentRedactor"
    app_backup = appdata / "AgentRedactor.third_party_test_backup"
    local_backup = localappdata / "AgentRedactor.third_party_test_backup"

    # Remove any stale backups from a previous aborted run.
    if app_backup.exists():
        shutil.rmtree(app_backup, ignore_errors=True)
    if local_backup.exists():
        shutil.rmtree(local_backup, ignore_errors=True)

    # Move current user data to backup if it exists. A lingering process from
    # an aborted run may hold files open, so kill it and retry on lock errors.
    _move_with_retry(app_dir, app_backup)
    _move_with_retry(local_dir, local_backup)

    # Create fresh directories for the test.
    app_dir.mkdir(parents=True, exist_ok=True)
    local_dir.mkdir(parents=True, exist_ok=True)

    try:
        yield app_dir
    finally:
        # Clean up test data. AgentRedactor may still hold file locks if a
        # previous fixture did not shut down cleanly, so kill it and retry.
        _rmtree_with_retry(app_dir)
        _rmtree_with_retry(local_dir)

        # Restore original user data.
        if app_backup.exists():
            _move_with_retry(app_backup, app_dir)
        if local_backup.exists():
            _move_with_retry(local_backup, local_dir)


@pytest_asyncio.fixture
async def agentredactor_app(
    user_data_backup: Path,
    openrouter_api_key: str,
) -> AsyncIterator[GuiAppProcess]:
    """Start AgentRedactor on port 8081 pointing at the real OpenRouter upstream."""
    if _is_port_in_use(PROXY_PORT):
        raise RuntimeError(
            f"Port {PROXY_PORT} is already in use. "
            "The OpenClaw setup scripts expect the proxy on this port."
        )

    create_settings(
        data_dir=user_data_backup,
        upstream_url="https://openrouter.ai/api/v1",
        api_key=openrouter_api_key,
        proxy_port=PROXY_PORT,
        logging_enabled=True,
        keywords=[],
        regex_patterns=[],
    )

    app = GuiAppProcess(proxy_port=PROXY_PORT)
    _start = time.monotonic()
    app.start()
    print(f"\n[3RD PARTY FIXTURE] app startup took {time.monotonic() - _start:.2f}s")
    try:
        yield app
    finally:
        returncode = app.process.poll() if app.process else None
        app.stop()
        if returncode is not None and returncode != 0:
            localappdata = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
            agent_dir = localappdata / "AgentRedactor"
            for rel in ("sessions/gui_test_stdout.txt", "sessions/gui_test_stderr.txt", "agent_redactor.log"):
                path = agent_dir / rel
                if path.exists():
                    print(f"\n--- {rel} ---")
                    try:
                        print(path.read_text(encoding="utf-8", errors="ignore")[-4000:])
                    except Exception as e:
                        print(f"Failed to read log: {e}")
        # Ensure no lingering process after the test. This also releases file
        # locks on the session log files before user_data_backup tears down.
        try:
            quit_app()
        except Exception:
            pass
        _kill_existing_agent_redactor()
        time.sleep(0.5)
