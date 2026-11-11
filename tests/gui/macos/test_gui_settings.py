from __future__ import annotations

import os
import sys
import time

import pytest

from app_process import MacAppProcess
import gui_driver

pytestmark = [
    pytest.mark.macos_gui,
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS Accessibility GUI test"),
    pytest.mark.skipif(os.environ.get("AGENTREDACTOR_GUI_TEST") != "1",
                       reason="opt-in: set AGENTREDACTOR_GUI_TEST=1"),
]


def selected_profile(app: MacAppProcess) -> dict:
    return app.api("GET", "/profiles")[0]


def test_keyword_and_regex_rules(app: MacAppProcess) -> None:
    gui_driver.click(app.pid, "keywords.new.case-sensitive")
    gui_driver.set_text(app.pid, "keywords.new.text", "Project Chimera")
    gui_driver.click(app.pid, "keywords.add")
    gui_driver.wait_until(
        "keyword saved", lambda: selected_profile(app),
        lambda profile: any(
            item.get("text") == "Project Chimera" and item.get("case_sensitive") is False
            for item in profile.get("keywords", [])))

    pattern = r"\bPN-\d{5}\b"
    gui_driver.set_text(app.pid, "regex.new.pattern", pattern)
    gui_driver.click(app.pid, "regex.add")
    gui_driver.wait_until(
        "regex saved", lambda: selected_profile(app),
        lambda profile: any(
            item.get("pattern") == pattern and item.get("enabled") is True
            for item in profile.get("regex_patterns", [])))


def test_detection_logging_and_language(app: MacAppProcess) -> None:
    gui_driver.click(app.pid, "detection.ai-enabled")
    gui_driver.wait_until(
        "AI detection disabled", lambda: selected_profile(app),
        lambda profile: profile.get("use_openai_model") is False)

    gui_driver.click(app.pid, "logging.enabled")
    gui_driver.wait_until(
        "logging enabled", lambda: app.api("GET", "/settings"),
        lambda settings: settings.get("loggingEnabled") is True)

    gui_driver.select_popup(app.pid, "settings.language", "Deutsch")
    gui_driver.wait_until(
        "German language saved", lambda: app.api("GET", "/settings"),
        lambda settings: settings.get("appLanguage") == "de")


def test_unlock_restores_sensitive_fields_before_gui_save(app: MacAppProcess) -> None:
    profile = selected_profile(app)
    profile["api_key"] = "sk-unlock-gui-test"
    profile["keywords"] = [
        {"text": "Unlock keyword", "case_sensitive": False, "enabled": True},
    ]
    profile["regex_patterns"] = [
        {"pattern": r"\bUNLOCK-\d{5}\b", "enabled": True},
    ]
    app.api("PUT", f"/profiles/{profile['id']}", profile)
    app.api("PUT", "/settings/enableMasterPassword", {"password": "unlock-gui-test"})

    app.stop()
    time.sleep(1)
    app.start()
    app.wait_for_engine()
    gui_driver.wait_window(app.pid)
    gui_driver.wait_until(
        "locked startup", lambda: app.api("GET", "/status"),
        lambda status: status.get("unlocked") is False)

    result = app.api("POST", "/unlock", {"password": "unlock-gui-test"})
    assert result.get("ok") is True

    def restored_values() -> tuple[str, str, str]:
        try:
            return (
                gui_driver.get_text(app.pid, "keywords.row.0.text"),
                gui_driver.get_text(app.pid, "regex.row.0.text"),
                gui_driver.get_text(app.pid, "profile.api-key"),
            )
        except Exception as error:
            return (type(error).__name__, str(error), "")

    gui_driver.wait_until(
        "sensitive fields restored in the GUI", restored_values,
        lambda values: values == (
            "Unlock keyword", r"\bUNLOCK-\d{5}\b",
            "\uf79a" * len(selected_profile(app)["api_key"])))

    gui_driver.click(app.pid, "detection.ai-enabled")
    gui_driver.wait_until(
        "post-unlock GUI save", lambda: selected_profile(app),
        lambda value: value.get("use_openai_model") is False)
    saved = selected_profile(app)
    assert saved["keywords"] == profile["keywords"]
    assert saved["regex_patterns"] == profile["regex_patterns"]
    assert app.api("GET", f"/profiles/{profile['id']}/apikey")["apiKey"] == "sk-unlock-gui-test"
