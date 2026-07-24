"""GUI tests for the master password feature."""

from __future__ import annotations

import time
from pathlib import Path

import aiohttp
import pytest

from mock_llm import MockLLM

from config_factory import create_settings
from gui_process import GuiAppProcess, _find_free_port
from windows.gui_driver import (
    change_master_password,
    dismiss_content_dialog,
    get_change_password_button_state,
    get_content_dialog_text,
    set_master_password,
    unlock_master_password,
)


OPENAI_PATH = "/v1/chat/completions"


def _chat_request(content: str) -> dict:
    return {
        "model": "mock-model",
        "messages": [{"role": "user", "content": content}],
    }


def _start_app(
    data_dir: Path, proxy_port: int, mock_llm: MockLLM, *, create: bool = True
) -> GuiAppProcess:
    if create:
        create_settings(
            data_dir=data_dir,
            upstream_url=mock_llm.base_url,
            api_key="test-api-key",
            proxy_port=proxy_port,
            logging_enabled=True,
            keywords=[],
            regex_patterns=[],
        )
    app = GuiAppProcess(proxy_port=proxy_port)
    app.start()
    return app


@pytest.mark.asyncio
async def test_gui_enable_master_password_enables_change_button(
    gui_app: GuiAppProcess,
    mock_llm: MockLLM,
) -> None:
    assert not get_change_password_button_state()

    set_master_password(enabled=True, password="EnablePass123")

    assert get_change_password_button_state()


@pytest.mark.asyncio
async def test_gui_disable_master_password_disables_change_button(
    gui_app: GuiAppProcess,
    mock_llm: MockLLM,
) -> None:
    set_master_password(enabled=True, password="DisablePass123")
    assert get_change_password_button_state()

    set_master_password(enabled=False, password="DisablePass123")
    assert not get_change_password_button_state()


@pytest.mark.asyncio
async def test_gui_master_password_unlocks_after_restart(
    user_data_backup: Path,
    mock_llm: MockLLM,
) -> None:
    port = _find_free_port()
    app = _start_app(user_data_backup, port, mock_llm)
    try:
        set_master_password(enabled=True, password="RestartPass123")
        assert get_change_password_button_state()
    finally:
        app.stop()

    # Restart the app; it should prompt for the master password.
    app = _start_app(user_data_backup, port, mock_llm, create=False)
    try:
        unlock_master_password("RestartPass123")
        # The UI should now be accessible and the change-password button enabled.
        assert get_change_password_button_state()
    finally:
        app.stop()


@pytest.mark.asyncio
async def test_gui_master_password_wrong_password_blocks(
    user_data_backup: Path,
    mock_llm: MockLLM,
) -> None:
    port = _find_free_port()
    app = _start_app(user_data_backup, port, mock_llm)
    try:
        set_master_password(enabled=True, password="RightPass456")
    finally:
        app.stop()

    app = _start_app(user_data_backup, port, mock_llm, create=False)
    try:
        unlock_master_password("WrongPass000")
        # An incorrect-password dialog should be showing after a wrong unlock.
        dialog_text = get_content_dialog_text()
        assert any("incorrect" in line.lower() for line in dialog_text), dialog_text
        dismiss_content_dialog()
    finally:
        app.stop()


@pytest.mark.asyncio
async def test_gui_change_master_password_works(
    user_data_backup: Path,
    mock_llm: MockLLM,
) -> None:
    port = _find_free_port()
    app = _start_app(user_data_backup, port, mock_llm)
    try:
        set_master_password(enabled=True, password="OriginalPass789")
        change_master_password(old="OriginalPass789", new="NewPass000")
    finally:
        app.stop()

    # Restart and unlock with the new password.
    app = _start_app(user_data_backup, port, mock_llm, create=False)
    try:
        unlock_master_password("NewPass000")
        assert get_change_password_button_state()
    finally:
        app.stop()
