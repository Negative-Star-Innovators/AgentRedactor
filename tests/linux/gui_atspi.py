"""AT-SPI driver for the real Qt6 Linux GUI (agentredactor-gui).

The Linux mirror of tests/gui/windows/gui_driver.py: same operations (profiles,
keywords, regexes, PII toggles, logging, lock/unlock), but driven through the
AT-SPI accessibility tree instead of a FlaUI helper process. The GUI only
registers on the accessibility bus under QT_QPA_PLATFORM=xcb (never
offscreen), so tests using this driver need a display (real session or Xvfb)
and the gi/Atspi bindings (gir1.2-atspi-2.0 + python3-gi).

Verified tree shape (English UI): [application 'agentredactor'] >
[frame 'Agent Redactor'] > pages; cards are [panel] nodes named by their
group-box title ('Profile', 'Detection', 'Keywords', ...). The lock overlay
and the content page both sit in the tree even when hidden — every lookup
here filters on StateType.SHOWING. Accessible nodes go stale whenever the UI
rebuilds (profile reloads recreate rows), so all queries re-walk the tree and
actions retry on GLib.GError.

Text-entry recipe: EditableText.set_text_contents() sets the value but never
fires QLineEdit::editingFinished, so row edits must be persisted by pressing
the profile card's Save button (MainWindow::gatherProfileFromForm rebuilds the
keyword/regex arrays from the row widgets).
"""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path
from typing import Any, Callable

import pytest

try:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi, GLib

    _ATSPI_AVAILABLE = True
    _ATSPI_ERROR: Exception | None = None
except Exception as exc:  # ImportError, ValueError (typelib missing)
    gi = None  # type: ignore[assignment]
    Atspi = None  # type: ignore[assignment]
    GLib = None  # type: ignore[assignment]
    _ATSPI_AVAILABLE = False
    _ATSPI_ERROR = exc

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
GUI_BIN = Path(
    os.environ.get("AGENTREDACTOR_GUI_BIN")
    or PROJECT_ROOT / "linux" / "build" / "gui" / "agentredactor-gui"
)
ENGINE_BIN = PROJECT_ROOT / "linux" / "build" / "engine" / "agentredactor"


_ATSPI_INITIALIZED = False


def require_atspi() -> None:
    """pytest.skip unless AT-SPI automation can work in this environment."""
    global _ATSPI_INITIALIZED
    if not _ATSPI_AVAILABLE:
        pytest.skip(f"gi/Atspi bindings unavailable: {_ATSPI_ERROR}")
    if not os.environ.get("DISPLAY"):
        pytest.skip("no DISPLAY (the GUI only registers on the a11y bus under xcb)")
    if not GUI_BIN.is_file():
        pytest.skip(f"GUI binary not built: {GUI_BIN}")
    if not ENGINE_BIN.is_file():
        pytest.skip(f"engine binary not built: {ENGINE_BIN}")
    if not _ATSPI_INITIALIZED:
        Atspi.init()
        _ATSPI_INITIALIZED = True


def wait_until(
    description: str,
    getter: Callable[[], Any],
    predicate: Callable[[Any], bool],
    timeout: float = 20.0,
    interval: float = 0.5,
) -> Any:
    """Poll getter() until predicate(value); AssertionError with context on timeout.

    The GUI learns about engine-side changes through a 1-second /status and
    /settings poll, so reads taken right after proxy traffic need
    eventual-consistency waits (same rationale as the Windows driver).
    """
    deadline = time.monotonic() + timeout
    last: Any = None
    while True:
        try:
            last = getter()
            if predicate(last):
                return last
        except Exception as exc:  # transient: stale node, engine not up yet
            last = exc
        if time.monotonic() >= deadline:
            raise AssertionError(
                f"timed out ({timeout}s) waiting for {description}; last value: {last!r}"
            )
        time.sleep(interval)


def _is_showing(node: Any) -> bool:
    try:
        return bool(node.get_state_set().contains(Atspi.StateType.SHOWING))
    except Exception:
        return False


def _walk(node: Any):
    yield node
    try:
        count = node.get_child_count()
    except Exception:
        return
    for i in range(count):
        try:
            child = node.get_child_at_index(i)
        except Exception:
            continue
        if child is not None:
            yield from _walk(child)


def _text_of(node: Any) -> str | None:
    try:
        iface = node.get_text_iface()
        if iface is None:
            return None
        return Atspi.Text.get_text(iface, 0, -1)
    except Exception:
        return None


def _checked(node: Any) -> bool:
    try:
        return bool(node.get_state_set().contains(Atspi.StateType.CHECKED))
    except Exception:
        return False


def _sensitive(node: Any) -> bool:
    try:
        return bool(node.get_state_set().contains(Atspi.StateType.SENSITIVE))
    except Exception:
        return False


class AtspiGui:
    """Owns one agentredactor-gui process and exposes AT-SPI operations on it."""

    def __init__(self, config_dir: Path, xdg_home: Path) -> None:
        self.config_dir = config_dir
        self.process: subprocess.Popen | None = None
        self.env = dict(os.environ)
        self.env["AGENTREDACTOR_CONFIG_DIR"] = str(config_dir)
        self.env["XDG_CONFIG_HOME"] = str(xdg_home)
        # offscreen never registers on the a11y bus; xcb does.
        self.env["QT_QPA_PLATFORM"] = "xcb"
        self.env["QT_LINUX_ACCESSIBILITY_ALWAYS_ON"] = "1"
        # Under the test session bus (dbus-run-session) a keyring daemon can
        # own org.freedesktop.secrets without serving promptly; skip libsecret
        # so engine startup stays deterministic (~4 s, machine-id fallback).
        self.env["AGENTREDACTOR_DISABLE_KEYRING"] = "1"
        self._log_file: Any | None = None

    # -- process lifecycle --------------------------------------------------

    def start(self) -> None:
        self._log_file = open(self.config_dir.parent / "gui_atspi.log", "ab")
        self.process = subprocess.Popen(
            [str(GUI_BIN)], env=self.env,
            stdout=self._log_file, stderr=subprocess.STDOUT,
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
        if self._log_file:
            try:
                self._log_file.close()
            except Exception:
                pass
            self._log_file = None

    def running(self) -> bool:
        return self.process is not None and self.process.poll() is None

    # -- tree access --------------------------------------------------------

    def _app_once(self) -> Any | None:
        """The 'agentredactor' application node, or None.

        The GUI also registers as 'agentredactor-gui' (0 children — ignore
        it); the real widget tree hangs under 'agentredactor'. Match the
        process id so a restarted GUI (or a defunct node left on the bus by
        the previous instance) can never be mistaken for the live one.
        """
        pid = self.process.pid if self.process else None
        desktop = Atspi.get_desktop(0)
        for i in range(desktop.get_child_count()):
            app = desktop.get_child_at_index(i)
            try:
                if app.get_child_count() == 0:
                    continue  # e.g. the sibling 'agentredactor-gui' registration
                if pid is not None:
                    if app.get_process_id() == pid:
                        return app
                elif app.get_name() == "agentredactor":
                    return app
            except Exception:
                continue
        return None

    def app(self, timeout: float = 120.0) -> Any:
        """Wait for the application node with a realized widget tree."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise RuntimeError(
                    f"GUI exited early (rc={self.process.returncode}); "
                    f"log: {self.config_dir.parent / 'gui_atspi.log'}"
                )
            try:
                app = self._app_once()
                if app is not None:
                    return app
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError("agentredactor application did not appear on the a11y bus")

    def wait_ready(self, timeout: float = 60.0) -> None:
        """Wait until the main window content is up (Profile card showing)."""
        self.panel("Profile", timeout=timeout)

    def find_all(
        self,
        role: str | None = None,
        name: str | None = None,
        *,
        text: str | None = None,
        within: Any | None = None,
        showing: bool = True,
    ) -> list[Any]:
        """All matching accessible nodes (default: currently showing only)."""
        root = within if within is not None else self._app_once()
        if root is None:
            return []
        out: list[Any] = []
        for node in _walk(root):
            try:
                if showing and not _is_showing(node):
                    continue
                if role is not None and node.get_role_name() != role:
                    continue
                if name is not None and node.get_name() != name:
                    continue
                if text is not None and _text_of(node) != text:
                    continue
            except Exception:
                continue
            out.append(node)
        return out

    def find(
        self,
        role: str | None = None,
        name: str | None = None,
        *,
        text: str | None = None,
        within: Any | None = None,
        timeout: float = 20.0,
        showing: bool = True,
    ) -> Any:
        """Wait for a single matching node; AssertionError listing candidates on timeout."""
        def _get() -> list[Any]:
            return self.find_all(role, name, text=text, within=within, showing=showing)

        try:
            return wait_until(
                f"node role={role!r} name={name!r} text={text!r}",
                _get,
                lambda nodes: len(nodes) > 0,
                timeout=timeout,
            )[0]
        except AssertionError as exc:
            # Attach the live tree so CI failures (no display to inspect) show
            # what the GUI actually exposed at the timeout.
            dump = self.dump_tree()
            raise AssertionError(f"{exc}\n--- accessible tree at timeout ---\n{dump}") from exc

    def dump_tree(self, max_lines: int = 150) -> str:
        """Compact dump of the app tree (role, name, showing) for diagnostics."""
        root = self._app_once()
        if root is None:
            return "<no application node on the a11y bus>"
        lines: list[str] = []

        def rec(node: Any, depth: int) -> None:
            if len(lines) >= max_lines:
                return
            try:
                lines.append(
                    "  " * depth
                    + f"[{node.get_role_name()}] {node.get_name()!r} "
                    + ("showing" if _is_showing(node) else "hidden")
                )
            except Exception as exc:
                lines.append("  " * depth + f"<error: {exc}>")
                return
            for i in range(node.get_child_count()):
                try:
                    child = node.get_child_at_index(i)
                except Exception:
                    continue
                if child is not None:
                    rec(child, depth + 1)

        rec(root, 0)
        if len(lines) >= max_lines:
            lines.append("... (truncated)")
        out = "\n".join(lines)
        try:  # best-effort artifact next to gui_atspi.log for CI upload
            (self.config_dir.parent / "atspi_tree_dump.txt").write_text(out, encoding="utf-8")
        except Exception:
            pass
        return out

    # -- primitive actions (with stale-node retry) ---------------------------

    @staticmethod
    def _retry(action: Callable[[], Any], attempts: int = 4, delay: float = 0.5) -> Any:
        last: Exception | None = None
        for _ in range(attempts):
            try:
                return action()
            except GLib.GError as exc:  # node went stale / bus hiccup
                last = exc
                time.sleep(delay)
        raise last  # type: ignore[misc]

    def press(self, node: Any) -> None:
        self._retry(lambda: node.get_action_iface().do_action(0))

    def set_text(self, node: Any, value: str) -> None:
        self._retry(lambda: node.get_editable_text_iface().set_text_contents(value))

    def press_refound(self, finder: Callable[[], Any]) -> None:
        """Press via a finder callable: every attempt re-walks the tree.

        Row mutations bump profilesRevision, and the settings-poll reload then
        rebuilds the very rows we are clicking — a found node can go stale
        between find and press, so the retry must re-find, not reuse.
        """
        last: Exception | None = None
        for _ in range(6):
            try:
                finder().get_action_iface().do_action(0)
                return
            except (GLib.GError, AssertionError) as exc:
                last = exc
                time.sleep(0.5)
        raise last  # type: ignore[misc]

    def press_until(self, description: str, finder: Callable[[], Any],
                    predicate: Callable[[], bool], timeout: float = 30.0) -> None:
        """Press, then verify the effect; re-press if the click was lost.

        A press that lands while the settings-poll reload is rebuilding the
        rows can be swallowed (the toggled handler sees currentRow == -1 and
        returns), so verify the observable state and retry instead of
        trusting that one click took effect.
        """
        deadline = time.monotonic() + timeout
        last: Exception | None = None
        while time.monotonic() < deadline:
            try:
                if predicate():
                    return
            except Exception:
                pass
            try:
                finder().get_action_iface().do_action(0)
            except (GLib.GError, AssertionError) as exc:
                last = exc
            time.sleep(1.0)
        raise AssertionError(f"press_until({description}) never took effect; last: {last!r}")

    def set_text_refound(self, finder: Callable[[], Any], value: str) -> None:
        last: Exception | None = None
        for _ in range(6):
            try:
                node = finder()
                if _text_of(node) == value:
                    return
                node.get_editable_text_iface().set_text_contents(value)
                return
            except (GLib.GError, AssertionError) as exc:
                last = exc
                time.sleep(0.5)
        raise last  # type: ignore[misc]

    @staticmethod
    def checked(node: Any) -> bool:
        return _checked(node)

    @staticmethod
    def sensitive(node: Any) -> bool:
        return _sensitive(node)

    @staticmethod
    def text_of(node: Any) -> str | None:
        return _text_of(node)

    def press_named(
        self, name: str, *, within: Any | None = None, role: str = "button", timeout: float = 20.0
    ) -> None:
        """Re-find and press a button/check box by name (survives UI rebuilds)."""
        self.press_refound(lambda: self.find(role, name, within=within, timeout=timeout))

    # -- structure helpers ----------------------------------------------------

    def panel(self, title: str, timeout: float = 20.0) -> Any:
        """A card (group box) by title, e.g. panel('Keywords')."""
        return self.find("panel", title, timeout=timeout)

    def set_field(self, accessible_name: str, value: str, *, within: Any | None = None) -> None:
        """Set an editable field located by its accessible name."""
        self.set_text_refound(lambda: self.find(name=accessible_name, within=within), value)

    def field_text(self, accessible_name: str, *, within: Any | None = None) -> str | None:
        return self.text_of(self.find(name=accessible_name, within=within))

    def checkbox(self, name: str, *, within: Any | None = None, timeout: float = 20.0) -> Any:
        return self.find("check box", name, within=within, timeout=timeout)

    def set_checkbox(self, name: str, on: bool, *, within: Any | None = None) -> None:
        """Press a check box only if its state differs from the target."""
        node = self.checkbox(name, within=within)
        if _checked(node) != on:
            self.press_named(name, within=within, role="check box")

    # -- dialogs (QMessageBox appears as an [alert] top-level) -----------------

    def dialog_text(self, title: str | None = None, timeout: float = 10.0) -> list[str]:
        """Names/labels of the currently showing alert dialog (empty if none)."""

        def _get() -> list[str]:
            for role in ("alert", "dialog"):
                for dlg in self.find_all(role, title):
                    lines = []
                    for node in _walk(dlg):
                        try:
                            name = node.get_name()
                        except Exception:
                            continue
                        if name:
                            lines.append(name)
                    return lines
            return []

        return wait_until(
            f"dialog {title!r}", _get, lambda lines: len(lines) > 0, timeout=timeout
        )

    def dismiss_dialog(self, button: str = "OK", title: str | None = None) -> None:
        """Press a button on the showing alert/dialog, then wait until it closes."""
        self.press_named(button, role="button")
        wait_until(
            f"dialog closed after {button!r}",
            lambda: self.find_all("alert", title) + self.find_all("dialog", title),
            lambda dlgs: len(dlgs) == 0,
        )

    # -- profiles --------------------------------------------------------------

    def profile_list(self) -> Any:
        return self.find("list", "Profiles")

    def profiles(self) -> list[str]:
        lst = self.profile_list()
        names = []
        for i in range(lst.get_child_count()):
            try:
                names.append(lst.get_child_at_index(i).get_name())
            except Exception:
                continue
        return names

    def _sidebar(self) -> Any:
        return self.profile_list().get_parent()

    def add_profile(self) -> None:
        """Click the sidebar Add button (the new profile gets a default alias)."""
        self.press_named("Add", within=self._sidebar())

    def select_profile(self, alias: str) -> None:
        lst = self.profile_list()
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                for i in range(lst.get_child_count()):
                    item = lst.get_child_at_index(i)
                    if item.get_name() == alias:
                        self._retry(lambda: item.get_action_iface().do_action(0))
                        return
            except Exception:
                pass
            time.sleep(0.5)
            lst = self.profile_list()
        raise AssertionError(f"profile {alias!r} not found; have: {self.profiles()}")

    def remove_profile(self, alias: str) -> None:
        """Select a profile, click Remove, confirm the dialog with Yes."""
        self.select_profile(alias)
        wait_until(
            "Remove button enabled",
            lambda: self.find("button", "Remove", within=self._sidebar()),
            _sensitive,
        )
        self.press_named("Remove", within=self._sidebar())
        self.dialog_text("Remove Profile")
        self.dismiss_dialog("Yes", title="Remove Profile")
        wait_until(
            f"profile {alias!r} removed",
            self.profiles,
            lambda names: alias not in names,
        )

    def save_profile(self) -> None:
        """Press Save on the Profile card and wait for the reload to settle."""
        card = self.panel("Profile")
        self.press_named("Save", within=card)
        # The save round-trips through the engine and reloads the rows.
        time.sleep(1.0)

    # -- keywords / regexes ------------------------------------------------------

    def add_keyword(self, text: str, case_sensitive: bool = False) -> None:
        card = self.panel("Keywords")
        self.set_field("New keyword", text, within=card)
        self.set_checkbox("Case sensitive", case_sensitive, within=card)
        self.press_named("Add", within=card)
        wait_until("keyword row added", lambda: self.keywords(), lambda ks: text in ks)

    def keywords(self) -> dict[str, dict[str, Any]]:
        """Keyword rows: text -> {enabled, case_sensitive}."""
        card = self.panel("Keywords")
        rows: dict[str, dict[str, Any]] = {}
        for box in self.find_all("text", "Keyword text", within=card):
            row_widget = box.get_parent()
            value = _text_of(box) or ""
            enabled_box = None
            case_btn = None
            for node in _walk(row_widget):
                try:
                    role = node.get_role_name()
                except Exception:
                    continue
                if role == "check box" and node.get_name() == "Enable keyword":
                    enabled_box = node
                elif role == "button" and (node.get_name() or "").startswith("Case:"):
                    case_btn = node
            rows[value] = {
                "enabled": _checked(enabled_box) if enabled_box is not None else None,
                "case_sensitive": (case_btn.get_name() == "Case: Yes")
                if case_btn is not None
                else None,
            }
        return rows

    def _keyword_row_child(self, text: str, role: str, name: str | None = None) -> Any:
        card = self.panel("Keywords")
        box = self.find("text", "Keyword text", text=text, within=card)
        row_widget = box.get_parent()
        for node in _walk(row_widget):
            try:
                if node.get_role_name() != role:
                    continue
                if name is not None and node.get_name() != name:
                    continue
                return node
            except Exception:
                continue
        raise AssertionError(f"no {role} {name!r} in keyword row {text!r}")

    def toggle_keyword(self, text: str) -> None:
        target = not self.keywords()[text]["enabled"]
        self.press_until(
            f"keyword {text!r} enabled -> {target}",
            lambda: self._keyword_row_child(text, "check box", "Enable keyword"),
            lambda: self.keywords().get(text, {}).get("enabled") is target,
        )

    def toggle_keyword_case(self, text: str) -> None:
        target = not self.keywords()[text]["case_sensitive"]
        self.press_until(
            f"keyword {text!r} case_sensitive -> {target}",
            lambda: self._keyword_case_button(text),
            lambda: self.keywords().get(text, {}).get("case_sensitive") is target,
        )

    def _keyword_case_button(self, text: str) -> Any:
        card = self.panel("Keywords")
        box = self.find("text", "Keyword text", text=text, within=card)
        for node in _walk(box.get_parent()):
            try:
                if node.get_role_name() == "button" \
                        and (node.get_name() or "").startswith("Case:"):
                    return node
            except Exception:
                continue
        raise AssertionError(f"no case button in keyword row {text!r}")

    def set_keyword_text(self, old: str, new: str) -> None:
        """Edit the row text, then Save (set_text never fires editingFinished)."""
        self.set_text_refound(
            lambda: self._keyword_row_child(old, "text", "Keyword text"), new)
        self.save_profile()
        wait_until("keyword renamed", self.keywords, lambda ks: new in ks and old not in ks)

    def delete_keyword(self, text: str) -> None:
        self.press_refound(lambda: self._keyword_row_child(text, "button", "Delete"))
        wait_until("keyword deleted", self.keywords, lambda ks: text not in ks)

    def add_regex(self, pattern: str) -> None:
        card = self.panel("Regex Patterns")
        self.set_field("New regex pattern", pattern, within=card)
        self.press_named("Add", within=card)

    def regexes(self) -> dict[str, bool]:
        """Regex rows: pattern -> enabled."""
        card = self.panel("Regex Patterns")
        rows: dict[str, bool] = {}
        for box in self.find_all("text", "Regex pattern", within=card):
            row_widget = box.get_parent()
            enabled_box = None
            for node in _walk(row_widget):
                try:
                    if node.get_role_name() == "check box" and node.get_name() == "Enable pattern":
                        enabled_box = node
                except Exception:
                    continue
            rows[_text_of(box) or ""] = (
                _checked(enabled_box) if enabled_box is not None else False
            )
        return rows

    def _regex_row_child(self, pattern: str, role: str, name: str | None = None) -> Any:
        card = self.panel("Regex Patterns")
        box = self.find("text", "Regex pattern", text=pattern, within=card)
        row_widget = box.get_parent()
        for node in _walk(row_widget):
            try:
                if node.get_role_name() != role:
                    continue
                if name is not None and node.get_name() != name:
                    continue
                return node
            except Exception:
                continue
        raise AssertionError(f"no {role} {name!r} in regex row {pattern!r}")

    def toggle_regex(self, pattern: str) -> None:
        target = not self.regexes()[pattern]
        self.press_until(
            f"regex {pattern!r} enabled -> {target}",
            lambda: self._regex_row_child(pattern, "check box", "Enable pattern"),
            lambda: self.regexes().get(pattern) is target,
        )

    def delete_regex(self, pattern: str) -> None:
        self.press_refound(lambda: self._regex_row_child(pattern, "button", "Delete"))
        wait_until("regex deleted", self.regexes, lambda rs: pattern not in rs)

    # -- PII / detection ---------------------------------------------------------

    _PII_LABELS = {
        "account_number": "Account number",
        "private_address": "Address",
        "private_date": "Date",
        "private_email": "Email",
        "private_person": "Person",
        "private_phone": "Phone",
        "private_url": "URL",
        "secret": "Secret",
    }

    def set_pii_type(self, pii_type: str, enabled: bool) -> None:
        label = self._PII_LABELS[pii_type]
        card = self.panel("Detection")
        self.set_checkbox(label, enabled, within=card)

    def set_pii_master(self, enabled: bool) -> None:
        card = self.panel("Detection")
        self.set_checkbox("Use AI model for PII detection", enabled, within=card)

    def pii_master_state(self) -> bool:
        card = self.panel("Detection")
        return _checked(self.checkbox("Use AI model for PII detection", within=card))

    # -- statistics / session redactions ------------------------------------------

    def statistics(self) -> str:
        card = self.panel("Statistics")
        for node in self.find_all("label", within=card):
            name = node.get_name() or ""
            if name.startswith("Requests:"):
                return name
        return ""

    def session_redactions(self) -> list[str]:
        card = self.panel("Session Redactions")
        lst = self.find("list", "Session redactions", within=card)
        out = []
        for i in range(lst.get_child_count()):
            try:
                out.append(lst.get_child_at_index(i).get_name())
            except Exception:
                continue
        return out

    def clear_statistics(self) -> None:
        card = self.panel("Statistics")
        self.press_named("Clear statistics", within=card)

    def clear_session_redactions(self) -> None:
        card = self.panel("Session Redactions")
        self.press_named("Clear", within=card)

    def clear_logs(self) -> None:
        card = self.panel("Logs")
        self.press_named("Delete all logs", within=card)
        self.dialog_text("Delete all logs?")
        self.dismiss_dialog("Yes", title="Delete all logs?")

    # -- logging / API key ---------------------------------------------------------

    def set_logging(self, enabled: bool) -> None:
        card = self.panel("Logs")
        self.set_checkbox("Enable logging", enabled, within=card)

    def logging_checked(self) -> bool:
        card = self.panel("Logs")
        return _checked(self.checkbox("Enable logging", within=card))

    def api_key_role(self) -> str:
        """'password text' while masked, 'text' when Show API key is checked."""
        card = self.panel("Profile")
        return self.find(name="API key", within=card).get_role_name()

    def api_key_text(self) -> str | None:
        card = self.panel("Profile")
        return self.text_of(self.find(name="API key", within=card))

    def toggle_show_api_key(self) -> None:
        card = self.panel("Profile")
        self.press_named("Show API key", within=card, role="check box")

    # -- lock overlay ----------------------------------------------------------------

    def locked(self) -> bool:
        return bool(self.find_all("label", "Agent Redactor is locked"))

    def unlock(self, password: str) -> None:
        box = self.find("password text", "Master password")
        self.set_text(box, password)
        self.press_named("Unlock")

    def unlock_error(self) -> str | None:
        for label in self.find_all("label", "Wrong password."):
            return label.get_name()
        return None

    # -- form fields --------------------------------------------------------------------

    def set_form(self, *, alias: str | None = None, port: int | None = None,
                 url: str | None = None, api_key: str | None = None) -> None:
        card = self.panel("Profile")
        if url is not None:
            self.set_field("Forward To URL", url, within=card)
        if api_key is not None:
            self.set_field("API key", api_key, within=card)
        if port is not None:
            self.set_field("Proxy port", str(port), within=card)
        if alias is not None:
            self.set_field("Profile name", alias, within=card)
