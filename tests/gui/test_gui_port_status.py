"""GUI tests for port availability status and port-conflict validation."""

from __future__ import annotations

import json
import socket
import time
from pathlib import Path

import aiohttp
import pytest

from mock_llm import MockLLM

from config_factory import create_settings
from gui_process import GuiAppProcess, _find_free_port
from gui_process import _wait_for_port as wait_for_port
from windows.gui_driver import (
    add_profile,
    dismiss_content_dialog,
    get_content_dialog_text,
    get_profiles,
    get_proxy_status,
    save_profile,
    select_profile,
    set_forward_url,
    set_api_key,
    set_port,
    set_profile_alias,
)


OPENAI_PATH = "/v1/chat/completions"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _settings_path(user_data_backup: Path) -> Path:
    return user_data_backup / "settings.json"


def _extract_profile_ports(user_data_backup: Path) -> dict[str, int]:
    settings = json.loads(_settings_path(user_data_backup).read_text(encoding="utf-8"))
    return {p["alias"]: p["port"] for p in settings["profiles"]}


@pytest.mark.asyncio
async def test_gui_save_profile_rejects_port_used_by_another_profile(
    gui_app: GuiAppProcess,
    user_data_backup: Path,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    default_port = gui_app.proxy_port
    second_port = _find_free_port()

    add_profile("second-profile")
    set_forward_url(mock_llm.base_url)
    set_api_key("test-api-key-second")
    set_port(second_port)
    # Type the alias last (immediately before the save) so the app's 1-second
    # settings poll cannot revert an unsaved alias edit in between.
    set_profile_alias("second-profile")
    save_profile()

    wait_for_port(second_port)

    # Sanity-check the second profile works before attempting the conflict.
    async with client.post(
        f"http://127.0.0.1:{second_port}{OPENAI_PATH}",
        json=_chat_request("pre-conflict"),
    ) as resp:
        assert resp.status == 200

    # Attempt to change the second profile's port to the default profile's port.
    select_profile("second-profile")
    set_port(default_port)
    save_profile()

    # A validation dialog should appear; dismiss it so the test can continue.
    dialog_lines = get_content_dialog_text()
    assert any("already used by profile" in line.lower() for line in dialog_lines), dialog_lines
    assert any(str(default_port) in line for line in dialog_lines), dialog_lines

    dismiss_content_dialog()

    # The settings file must still show the second profile on its original port.
    ports = _extract_profile_ports(user_data_backup)
    assert ports.get("second-profile") == second_port
    # The default profile alias comes from create_settings, not the UI localization.
    default_alias = "test-profile"
    assert ports.get(default_alias) == default_port

    # Both original ports should still be functional.
    async with client.post(
        f"http://127.0.0.1:{second_port}{OPENAI_PATH}",
        json=_chat_request("second still works"),
    ) as resp:
        assert resp.status == 200

    async with client.post(
        f"{gui_app.proxy_url}{OPENAI_PATH}",
        json=_chat_request("default still works"),
    ) as resp:
        assert resp.status == 200


@pytest.mark.asyncio
async def test_gui_port_status_available_shows_green_text(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    new_port = _find_free_port()
    set_port(new_port)
    time.sleep(0.5)

    status = get_proxy_status()
    assert f"Port {new_port} is available" in status


@pytest.mark.asyncio
async def test_gui_port_status_in_use_external_shows_red_text(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    # Occupy a port from outside the app on the same dual-stack address the
    # app uses for its availability check.
    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        s.bind(("::", 0))
        occupied_port = s.getsockname()[1]
        s.listen(1)

        set_port(occupied_port)
        time.sleep(0.5)

        status = get_proxy_status()
        assert f"Port {occupied_port} is already in use" in status


@pytest.mark.asyncio
async def test_gui_port_status_used_by_another_profile_shows_red_text(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    second_port = _find_free_port()

    add_profile("second-profile")
    set_forward_url(mock_llm.base_url)
    set_api_key("test-api-key-second")
    set_port(second_port)
    # Type the alias last (immediately before the save) so the app's 1-second
    # settings poll cannot revert an unsaved alias edit in between.
    set_profile_alias("second-profile")
    save_profile()
    wait_for_port(second_port)

    profiles = get_profiles()
    assert len(profiles) >= 2
    first_alias = profiles[0]
    select_profile(first_alias)
    set_port(second_port)
    time.sleep(0.5)

    status = get_proxy_status()
    assert "is already used by profile 'second-profile'" in status


@pytest.mark.asyncio
async def test_gui_port_status_invalid_range_clears_status(
    gui_app: GuiAppProcess,
    client: aiohttp.ClientSession,
    mock_llm: MockLLM,
) -> None:
    set_port(0)
    time.sleep(0.3)
    status = get_proxy_status()
    assert "available" not in status.lower()
    assert "in use" not in status.lower()

    set_port(70000)
    time.sleep(0.3)
    status = get_proxy_status()
    assert "available" not in status.lower()
    assert "in use" not in status.lower()
