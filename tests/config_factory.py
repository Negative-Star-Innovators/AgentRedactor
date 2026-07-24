"""Generate a plaintext AgentRedactor settings.json for integration tests."""

import json
import uuid
from pathlib import Path
from typing import Any


# PII types supported by AgentRedactor (see include/constants.h)
ENABLED_PII_TYPES = [
    "account_number",
    "private_address",
    "private_date",
    "private_email",
    "private_person",
    "private_phone",
    "private_url",
    "secret",
]

DEFAULT_REGEX_PATTERNS = [
    {
        "pattern": r"\bPN-\d{5}\b",
        "enabled": True,
    }
]

DEFAULT_KEYWORDS = [
    {
        "text": "Project Chimera",
        "case_sensitive": False,
        "enabled": True,
    }
]


def _empty_stats() -> dict[str, Any]:
    return {
        "total_requests": 0,
        "total_pii_detected": 0,
        "total_regex_matches": 0,
        "total_keyword_matches": 0,
        "pii_type_breakdown": {},
    }


def create_profile(
    upstream_url: str,
    api_key: str,
    port: int,
    alias: str = "test-profile",
    regex_patterns: list[dict[str, Any]] | None = None,
    keywords: list[dict[str, Any]] | None = None,
    enabled_pii_types: list[str] | None = None,
    enabled: bool = True,
) -> dict[str, Any]:
    return {
        "id": str(uuid.uuid4()),
        "alias": alias,
        "upstream_url": upstream_url,
        "api_key": api_key,
        "port": port,
        "use_openai_model": True,
        "enabled_pii_types": enabled_pii_types if enabled_pii_types is not None else ENABLED_PII_TYPES,
        "pii_confidence_threshold": 0.9,
        "regex_patterns": regex_patterns if regex_patterns is not None else DEFAULT_REGEX_PATTERNS,
        "keywords": keywords if keywords is not None else DEFAULT_KEYWORDS,
        "stats": _empty_stats(),
        "enabled": enabled,
    }


def create_settings(
    data_dir: Path,
    upstream_url: str,
    api_key: str,
    proxy_port: int,
    logging_enabled: bool = True,
    regex_patterns: list[dict[str, Any]] | None = None,
    keywords: list[dict[str, Any]] | None = None,
    enabled_pii_types: list[str] | None = None,
) -> Path:
    """Write a test settings.json with a single profile and return its path."""
    return create_settings_with_profiles(
        data_dir=data_dir,
        profiles=[
            create_profile(
                upstream_url=upstream_url,
                api_key=api_key,
                port=proxy_port,
                regex_patterns=regex_patterns,
                keywords=keywords,
                enabled_pii_types=enabled_pii_types,
            )
        ],
        logging_enabled=logging_enabled,
    )


def create_settings_with_profiles(
    data_dir: Path,
    profiles: list[dict[str, Any]],
    logging_enabled: bool = True,
) -> Path:
    """Write a test settings.json with arbitrary profiles and return its path."""
    settings = {
        "start_on_boot": False,
        "logging_enabled": logging_enabled,
        "app_language": "en",
        "onnx_provider": "cpu",
        "profiles": profiles,
    }
    data_dir.mkdir(parents=True, exist_ok=True)
    settings_path = data_dir / "settings.json"
    settings_path.write_text(json.dumps(settings, indent=2), encoding="utf-8")
    return settings_path
