"""GUI tests for the API key password box visibility toggle."""

from __future__ import annotations

import pytest

from mock_llm import MockLLM

from gui_process import GuiAppProcess
from windows.gui_driver import (
    get_api_key_visibility,
    save_profile,
    set_api_key,
    toggle_show_api_key,
)


@pytest.mark.asyncio
async def test_gui_api_key_hidden_by_default(
    gui_app: GuiAppProcess,
    mock_llm: MockLLM,
) -> None:
    secret_key = "sk-hidden-default-test-98765"
    set_api_key(secret_key)
    save_profile()

    show_checked, visible_text = get_api_key_visibility()
    assert not show_checked
    assert secret_key not in visible_text


@pytest.mark.asyncio
async def test_gui_api_key_show_checkbox_reveals(
    gui_app: GuiAppProcess,
    mock_llm: MockLLM,
) -> None:
    secret_key = "sk-show-checkbox-test-12345"
    set_api_key(secret_key)
    save_profile()

    toggle_show_api_key()
    show_checked, visible_text = get_api_key_visibility()
    assert show_checked
    assert secret_key in visible_text


@pytest.mark.asyncio
async def test_gui_api_key_hide_checkbox_conceals(
    gui_app: GuiAppProcess,
    mock_llm: MockLLM,
) -> None:
    secret_key = "sk-hide-checkbox-test-54321"
    set_api_key(secret_key)
    save_profile()

    toggle_show_api_key()
    toggle_show_api_key()
    show_checked, visible_text = get_api_key_visibility()
    assert not show_checked
    assert secret_key not in visible_text
