"""Python wrapper for the FlaUI C# GUI automation helper."""

from __future__ import annotations

import os
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent.parent
HELPER_EXE = (
    PROJECT_ROOT
    / "tests"
    / "gui"
    / "windows"
    / "FlaUIHelper"
    / "bin"
    / "x64"
    / "Release"
    / "FlaUIHelper.exe"
)


def _run_helper(*args: str, timeout: float = 120.0) -> str:
    if not HELPER_EXE.exists():
        raise FileNotFoundError(
            f"FlaUIHelper.exe not found at {HELPER_EXE}. "
            "Run tests/gui/windows/build.ps1 first."
        )
    cmd = [str(HELPER_EXE), *args]
    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    max_attempts = 3
    for attempt in range(1, max_attempts + 1):
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            creationflags=creationflags,
            check=False,
        )
        output = (result.stdout or "") + (result.stderr or "")
        print(f"[FlaUIHelper] {' '.join(args)} -> rc={result.returncode}")
        if output.strip():
            print(f"[FlaUIHelper output]\n{output}")
        if result.returncode == 0:
            return output
        # A transient WaitUntilClickable timeout can happen when the UI is still
        # busy (e.g. after removing a profile). Retry a few times before failing.
        if (
            attempt < max_attempts
            and "TimeoutException" in output
            and "WaitUntilClickable" in output
        ):
            time.sleep(1.0)
            continue
        raise RuntimeError(
            f"FlaUIHelper failed ({result.returncode}): {output}"
        )
    return ""


def add_keyword(text: str, case_sensitive: bool = False) -> None:
    """Add a keyword via the AgentRedactor UI."""
    _run_helper(
        "add-keyword",
        "--text",
        text,
        "--case-sensitive",
        "true" if case_sensitive else "false",
    )


def get_keywords() -> list[dict[str, str | bool]]:
    """Return the keyword rows currently shown in the UI.

    Each row is a dict with keys: text, case_sensitive, enabled.
    """
    output = _run_helper("get-keywords").strip()
    lines = output.splitlines()
    keywords = []
    for line in lines[1:]:  # skip COUNT:N
        if not line:
            continue
        parts = line.split("|")
        data = {}
        for part in parts:
            key, value = part.split(":", 1)
            data[key.lower()] = value
        keywords.append(
            {
                "text": data.get("text", ""),
                "case_sensitive": data.get("case", "No") == "Yes",
                "enabled": data.get("enabled", "False") == "True",
            }
        )
    return keywords


def delete_keyword(text: str) -> None:
    """Delete a keyword from the UI keyword list."""
    _run_helper("delete-keyword", "--text", text)


def toggle_keyword(text: str) -> None:
    """Toggle the enabled checkbox of a keyword in the UI."""
    _run_helper("toggle-keyword", "--text", text)


def set_keyword_case(text: str, case_sensitive: bool) -> None:
    """Set the case-sensitivity flag of an existing keyword."""
    _run_helper(
        "set-keyword-case",
        "--text",
        text,
        "--case-sensitive",
        "true" if case_sensitive else "false",
    )


def set_keyword_text(old_text: str, new_text: str) -> None:
    """Change the text of an existing keyword in the UI."""
    _run_helper("set-keyword-text", "--old", old_text, "--new", new_text)


def get_regexes() -> list[dict[str, str | bool]]:
    """Return the regex rows currently shown in the UI.

    Each row is a dict with keys: pattern, enabled.
    """
    output = _run_helper("get-regexes").strip()
    lines = output.splitlines()
    regexes: list[dict[str, str | bool]] = []
    i = 1 if lines and lines[0].startswith("COUNT:") else 0
    while i + 1 < len(lines):
        pattern_line = lines[i]
        enabled_line = lines[i + 1]
        i += 2
        if not pattern_line.startswith("PATTERN:") or not enabled_line.startswith("ENABLED:"):
            continue
        regexes.append(
            {
                "pattern": pattern_line[len("PATTERN:"):],
                "enabled": enabled_line[len("ENABLED:"):].strip() == "True",
            }
        )
    return regexes


def delete_regex(pattern: str) -> None:
    """Delete a regex from the UI regex list."""
    _run_helper("delete-regex", "--text", pattern)


def toggle_regex(pattern: str) -> None:
    """Toggle the enabled checkbox of a regex in the UI."""
    _run_helper("toggle-regex", "--text", pattern)


def set_regex_text(old_pattern: str, new_pattern: str) -> None:
    """Change the pattern of an existing regex in the UI."""
    _run_helper("set-regex-text", "--old", old_pattern, "--new", new_pattern)


def add_regex(pattern: str) -> None:
    """Add a regex pattern via the AgentRedactor UI."""
    _run_helper("add-regex", "--pattern", pattern)


def get_log_text(max_chars: int = 50_000) -> str:
    """Read the tail of the AgentRedactor log file.

    The app writes its logs to Roaming APPDATA (with settings), NOT to
    LocalAppData (see InitializeLogging in src/utils.cpp). The GUI test
    fixtures isolate that directory for each test run.
    """
    log_path = (
        Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming"))
        / "AgentRedactor"
        / "agent_redactor.log"
    )
    if not log_path.exists():
        return ""
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    if len(text) > max_chars:
        return text[-max_chars:]
    return text


def get_debug_log_text(max_chars: int = 50_000) -> str:
    """Read the tail of the AgentRedactor debug traffic log file.

    The app writes its logs to Roaming APPDATA (with settings), NOT to
    LocalAppData (see InitializeDebugTrafficLogging in src/utils.cpp).
    Returns "" if the file does not exist.
    """
    log_path = (
        Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming"))
        / "AgentRedactor"
        / "agent_redactor_debug.log"
    )
    if not log_path.exists():
        return ""
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    if len(text) > max_chars:
        return text[-max_chars:]
    return text


def set_forward_url(url: str) -> None:
    """Set the current profile's forward/upstream URL."""
    _run_helper("set-forward-url", "--url", url)


def set_api_key(key: str) -> None:
    """Set the current profile's API key."""
    _run_helper("set-api-key", "--key", key)


def set_port(port: int) -> None:
    """Set the current profile's local proxy port."""
    _run_helper("set-port", "--port", str(port))


def save_profile() -> None:
    """Click Save to persist the current profile form."""
    _run_helper("save-profile")


def set_enable_logging(enabled: bool) -> None:
    """Toggle the global Enable logging switch."""
    _run_helper(
        "set-enable-logging",
        "--enabled",
        "true" if enabled else "false",
    )


def set_show_sensitive(enabled: bool) -> None:
    """Toggle the Show sensitive information switch (confirms the dialog)."""
    _run_helper(
        "set-show-sensitive",
        "--enabled",
        "true" if enabled else "false",
    )


def get_is_enabled(automation_id: str) -> bool:
    """Return whether the control with the given AutomationId is enabled."""
    output = _run_helper("get-is-enabled", "--id", automation_id).strip()
    for line in output.splitlines():
        if line.startswith("ENABLED:"):
            return line[len("ENABLED:"):].strip() == "True"
    raise RuntimeError(f"get-is-enabled returned no ENABLED line: {output}")


def get_is_checked(automation_id: str) -> bool:
    """Return whether the checkbox with the given AutomationId is checked."""
    output = _run_helper("get-is-enabled", "--id", automation_id).strip()
    for line in output.splitlines():
        if line.startswith("CHECKED:"):
            return line[len("CHECKED:"):].strip() == "True"
    raise RuntimeError(f"get-is-enabled returned no CHECKED line: {output}")


def add_profile(alias: str) -> None:
    """Add a new profile with the given alias."""
    _run_helper("add-profile", "--alias", alias)


def select_profile(alias: str) -> None:
    """Select the profile with the given alias in the profile list."""
    _run_helper("select-profile", "--alias", alias)


def set_pii_type(pii_type: str, enabled: bool) -> None:
    """Toggle a PII type checkbox for the current profile.

    pii_type is the internal name, e.g. "private_person" or "private_email".
    """
    _run_helper(
        "set-pii-type",
        "--type",
        pii_type,
        "--enabled",
        "true" if enabled else "false",
    )


def get_pii_master_state() -> bool:
    """Return whether the master AI/PII detection switch is on."""
    output = _run_helper("get-pii-master-state").strip()
    return output == "True"


def set_pii_master(enabled: bool) -> None:
    """Set the master AI/PII detection switch on or off."""
    _run_helper(
        "set-pii-master",
        "--enabled",
        "true" if enabled else "false",
    )


def get_statistics() -> str:
    """Return the statistics text shown in the Statistics card."""
    return _run_helper("get-statistics").strip()


def get_session_redactions() -> list[str]:
    """Return the current session redaction entries from the UI list."""
    output = _run_helper("get-session-redactions").strip()
    lines = output.splitlines()
    # First line is COUNT:N, remaining lines are the entries.
    return [line for line in lines[1:] if line]


def get_profiles() -> list[str]:
    """Return the aliases of all profiles in the profile list."""
    output = _run_helper("get-profiles").strip()
    lines = output.splitlines()
    # First line is COUNT:N, remaining lines are the aliases.
    return [line for line in lines[1:] if line]


def remove_profile(alias: str) -> None:
    """Remove the profile with the given alias."""
    _run_helper("remove-profile", "--alias", alias)


def clear_statistics() -> None:
    """Click the Clear button on the Statistics card."""
    _run_helper("clear-statistics")


def clear_session_redactions() -> None:
    """Click the Clear button on the Session Redactions card."""
    _run_helper("clear-session-redactions")


def clear_logs() -> None:
    """Click the Clear Logs button and confirm the deletion dialog."""
    _run_helper("clear-logs")


def get_proxy_status() -> str:
    """Return the port-availability status text shown next to the Port box."""
    output = _run_helper("get-proxy-status").strip()
    if output.startswith("TEXT:"):
        return output[len("TEXT:"):]
    return output


def get_profile_details() -> list[dict[str, str]]:
    """Return alias/port tuples for every profile in the profile list.

    Note: this helper selects each profile briefly to read its port and then
    restores the original selection.
    """
    output = _run_helper("get-profile-details").strip()
    lines = output.splitlines()
    details = []
    for line in lines[1:]:  # skip COUNT:N
        if not line or not line.startswith("ALIAS:"):
            continue
        line = line[len("ALIAS:"):]
        alias, _, port = line.partition("|PORT:")
        details.append({"alias": alias, "port": port})
    return details


def get_api_key_visibility() -> tuple[bool, str]:
    """Return (show_key_checkbox_checked, api_key_text)."""
    output = _run_helper("get-api-key-visibility").strip()
    checked = False
    text = ""
    for line in output.splitlines():
        if line.startswith("CHECKED:"):
            checked = line[len("CHECKED:"):].strip().lower() == "true"
        elif line.startswith("TEXT:"):
            text = line[len("TEXT:"):]
    return checked, text


def toggle_show_api_key() -> None:
    """Toggle the Show API key checkbox."""
    _run_helper("toggle-show-api-key")


def set_master_password(enabled: bool, password: str, confirm: str | None = None) -> None:
    """Enable or disable the master password through the Home page Password card."""
    confirm = confirm if confirm is not None else password
    _run_helper(
        "set-master-password",
        "--enabled",
        "true" if enabled else "false",
        "--password",
        password,
        "--confirm",
        confirm,
    )


def change_master_password(old: str, new: str, confirm: str | None = None) -> None:
    """Change the master password using the Change password button."""
    confirm = confirm if confirm is not None else new
    _run_helper(
        "change-master-password",
        "--old",
        old,
        "--new",
        new,
        "--confirm",
        confirm,
    )


def unlock_master_password(password: str) -> None:
    """Unlock the app on startup when a master password is required."""
    _run_helper("unlock-master-password", "--password", password)


def get_change_password_button_state() -> bool:
    """Return whether the Change password button is enabled."""
    output = _run_helper("get-change-password-button-state").strip()
    return output == "ENABLED:True"


def get_content_dialog_text() -> list[str]:
    """Return the text of the currently open ContentDialog, or []."""
    output = _run_helper("get-content-dialog-text").strip()
    if output == "NO_DIALOG":
        return []
    lines = []
    for line in output.splitlines():
        if line.startswith("NAME:"):
            lines.append(line[len("NAME:"):])
        elif line.startswith("TEXT:"):
            lines.append(line[len("TEXT:"):])
    return lines


def dismiss_content_dialog() -> None:
    """Click the close/OK button on the currently open ContentDialog."""
    _run_helper("dismiss-content-dialog")


def quit_app() -> None:
    """Terminate the AgentRedactor process."""
    _run_helper("quit")
