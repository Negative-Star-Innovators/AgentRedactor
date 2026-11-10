from __future__ import annotations

import json
import sys
import time
import urllib.request
from pathlib import Path

from config_factory import create_settings

PASSWORD = "upgrade-e2e-password"
API_KEY = "sk-upgrade-e2e"
KEYWORDS = [
    {"text": "Upgrade keyword", "case_sensitive": False, "enabled": True},
    {"text": "Second keyword", "case_sensitive": True, "enabled": False},
]
REGEXES = [
    {"pattern": r"\bUPG-\d{5}\b", "enabled": False},
    {"pattern": r"\bKEEP-[A-Z]+\b", "enabled": True},
]


def seed_settings(config_dir: Path, proxy_port: int) -> None:
    path = create_settings(
        data_dir=config_dir,
        upstream_url="http://127.0.0.1:9",
        api_key=API_KEY,
        proxy_port=proxy_port,
        logging_enabled=False,
        keywords=KEYWORDS,
        regex_patterns=REGEXES,
    )
    settings = json.loads(path.read_text(encoding="utf-8"))
    profile = settings["profiles"][0]
    profile["alias"] = "upgrade-from-live"
    profile["stats"] = {
        "total_requests": 7,
        "total_pii_detected": 2,
        "total_regex_matches": 3,
        "total_keyword_matches": 4,
        "pii_type_breakdown": {"private_email": 2},
    }
    path.write_text(json.dumps(settings), encoding="utf-8")


def api(config_dir: Path, method: str, path: str, body: dict | None = None) -> dict | list:
    control = json.loads((config_dir / "control.json").read_text(encoding="utf-8"))
    request = urllib.request.Request(
        f"http://127.0.0.1:{control['port']}{path}",
        data=None if body is None else json.dumps(body).encode(),
        headers={"Authorization": f"Bearer {control['token']}",
                 "Content-Type": "application/json"},
        method=method,
    )
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.load(response)


def wait_for_engine(config_dir: Path, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (config_dir / "control.json").is_file():
            try:
                api(config_dir, "GET", "/status")
                return
            except Exception:
                pass
        time.sleep(0.2)
    raise AssertionError("engine did not become ready")


def wait_for_engine_version(config_dir: Path, expected: str, timeout: float = 90.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            status = api(config_dir, "GET", "/status")
            if status.get("engineVersion") == expected:
                return
        except Exception:
            pass
        time.sleep(0.5)
    raise AssertionError(f"engine did not reach candidate version {expected}")


def enable_password(config_dir: Path) -> None:
    body = {"hello": True} if sys.platform == "win32" else {"password": PASSWORD}
    result = api(config_dir, "PUT", "/settings/enableMasterPassword", body)
    assert result.get("ok") is True
    settings = api(config_dir, "GET", "/settings")
    assert settings["masterPasswordEnabled"] is True


def exercise_locked_save(config_dir: Path) -> None:
    unlock(config_dir)
    assert api(config_dir, "PUT", "/settings/lock", {"value": True}).get("ok") is True
    assert api(config_dir, "GET", "/status")["unlocked"] is False
    assert api(config_dir, "PUT", "/settings/appLanguage", {"value": "en"}).get("ok") is True
    unlock(config_dir)
    assert api(config_dir, "GET", "/status")["unlocked"] is True


def unlock(config_dir: Path) -> None:
    body = {} if sys.platform == "win32" else {"password": PASSWORD}
    result = api(config_dir, "POST", "/unlock", body)
    assert result.get("ok") is True


def assert_profile(config_dir: Path, proxy_port: int) -> None:
    profiles = api(config_dir, "GET", "/profiles")
    assert len(profiles) == 1
    profile = profiles[0]
    assert profile["alias"] == "upgrade-from-live"
    assert profile["port"] == proxy_port
    assert profile["keywords"] == KEYWORDS
    assert profile["regex_patterns"] == REGEXES
    assert profile["stats"]["total_requests"] == 7
    assert profile["stats"]["total_regex_matches"] == 3
    assert profile["stats"]["total_keyword_matches"] == 4
    assert api(config_dir, "GET", f"/profiles/{profile['id']}/apikey")["apiKey"] == API_KEY
