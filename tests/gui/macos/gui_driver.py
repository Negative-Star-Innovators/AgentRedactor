from __future__ import annotations

import subprocess
import time
from pathlib import Path
from typing import Any, Callable

HELPER = Path(__file__).parent / "AccessibilityHelper" / "bin" / "agentredactor-gui-helper"


def _run(*args: str, input_text: str | None = None, timeout: float = 45) -> str:
    if not HELPER.is_file():
        raise FileNotFoundError(f"GUI helper not built: {HELPER}")
    result = subprocess.run(
        [str(HELPER), *args], input=input_text, capture_output=True,
        text=True, timeout=timeout, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"GUI helper failed ({' '.join(args)}): {result.stdout}{result.stderr}")
    return result.stdout.strip()


def wait_until(description: str, getter: Callable[[], Any],
               predicate: Callable[[Any], bool], timeout: float = 30,
               interval: float = 0.25) -> Any:
    deadline = time.monotonic() + timeout
    last: Any = None
    while time.monotonic() < deadline:
        try:
            last = getter()
            if predicate(last):
                return last
        except Exception:
            pass
        time.sleep(interval)
    raise AssertionError(f"timed out waiting for {description}; last value: {last!r}")


def wait_window(pid: int) -> None:
    _run("wait-window", str(pid))


def window_count(pid: int) -> int:
    return int(_run("window-count", str(pid)))


def dump(pid: int) -> str:
    return _run("dump", str(pid))


def activate(pid: int) -> None:
    _run("activate", str(pid))


def click(pid: int, identifier: str) -> None:
    _run("click", str(pid), identifier)


def click_title(pid: int, title: str) -> None:
    _run("click-title", str(pid), title)


def set_text(pid: int, identifier: str, text: str) -> None:
    _run("set-text", str(pid), identifier, text)


def select_popup(pid: int, identifier: str, title: str) -> None:
    _run("select-popup", str(pid), identifier, title)


def exists(pid: int, identifier: str) -> bool:
    return _run("exists", str(pid), identifier) == "true"


def close_window(pid: int) -> None:
    _run("close-window", str(pid))


def auth_count() -> int:
    return int(_run("auth-count"))


def cancel_auth() -> None:
    _run("cancel-auth")


def submit_auth(password: str) -> None:
    _run("submit-auth", input_text=password + "\n")
