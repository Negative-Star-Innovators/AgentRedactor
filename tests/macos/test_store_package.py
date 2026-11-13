from __future__ import annotations

import fnmatch
import os
import plistlib
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest

pytestmark = [
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS App Store package test"),
    pytest.mark.skipif(os.environ.get("AGENTREDACTOR_STORE_TEST") != "1",
                       reason="opt-in App Store package test"),
]


def signed_entitlements(executable: Path) -> dict:
    entitlements = subprocess.run(
        ["codesign", "-d", "--entitlements", ":-", str(executable)],
        check=True, capture_output=True, timeout=60).stdout
    return plistlib.loads(entitlements)


def test_store_package_layout_and_entitlements() -> None:
    app = Path(os.environ.get(
        "AGENTREDACTOR_STORE_APP", "macos/build-store/app/AgentRedactor.app"))
    if not app.is_dir():
        pytest.fail(f"Store app not built: {app}")

    subprocess.run(["codesign", "--verify", "--strict", str(app)],
                   check=True, capture_output=True, timeout=60)
    values = signed_entitlements(app)
    assert values["com.apple.security.app-sandbox"] is True
    assert values["com.apple.security.network.client"] is True
    assert values["com.apple.security.network.server"] is True
    assert "com.apple.security.cs.disable-library-validation" not in values
    assert "com.apple.security.keychain-access-groups" not in values

    resources = app / "Contents" / "Resources"
    engine = resources / "agentredactor"
    assert engine.is_file()
    subprocess.run(["codesign", "--verify", "--strict", str(engine)],
                   check=True, capture_output=True, timeout=60)
    engine_values = signed_entitlements(engine)
    assert engine_values == {
        "com.apple.security.app-sandbox": True,
        "com.apple.security.inherit": True,
    }
    assert (resources / "models" / "onnx" / "model_quantized.onnx_data").stat().st_size == 1618042064
    assert not (app / "Contents" / "MacOS" / "UpdateMac").exists()
    assert not (app / "Contents" / "Frameworks" / "velopack_libc_osx.dylib").exists()

    with (app / "Contents" / "Info.plist").open("rb") as handle:
        info = plistlib.load(handle)
    assert info["LSApplicationCategoryType"] == "public.app-category.utilities"
    expected = os.environ.get("AGENTREDACTOR_EXPECT_VERSION")
    if expected:
        assert info["CFBundleShortVersionString"] == expected

    bundle_id = info["CFBundleIdentifier"]
    expected_identity = os.environ.get("AGENTREDACTOR_EXPECT_INSTALLER_IDENTITY")
    profile = app / "Contents" / "embedded.provisionprofile"
    if expected_identity:
        team_id = os.environ.get("AGENTREDACTOR_EXPECT_TEAM_ID")
        assert team_id, "AGENTREDACTOR_EXPECT_TEAM_ID is required for signed Store tests"
        application_identifier = f"{team_id}.{bundle_id}"
        assert values["com.apple.application-identifier"] == application_identifier
        assert values["com.apple.developer.team-identifier"] == team_id
        assert values["keychain-access-groups"] == [application_identifier]

        assert profile.is_file(), "Mac App Store provisioning profile is not embedded"
        decoded = subprocess.run(
            ["security", "cms", "-D", "-i", str(profile)],
            check=True, capture_output=True, timeout=60).stdout
        profile_values = plistlib.loads(decoded)
        profile_entitlements = profile_values["Entitlements"]
        assert profile_entitlements["com.apple.application-identifier"] == application_identifier
        assert profile_entitlements["com.apple.developer.team-identifier"] == team_id
        assert any(fnmatch.fnmatchcase(application_identifier, pattern)
                   for pattern in profile_entitlements["keychain-access-groups"])
        expiration = profile_values.get("ExpirationDate")
        if expiration is not None:
            if expiration.tzinfo is None:
                expiration = expiration.replace(tzinfo=timezone.utc)
            assert expiration > datetime.now(timezone.utc), "Store provisioning profile has expired"
    else:
        assert not profile.exists()
        assert "com.apple.application-identifier" not in values
        assert "com.apple.developer.team-identifier" not in values
        assert "keychain-access-groups" not in values

    version = Path("windows/version.txt").read_text().strip()
    package = Path(os.environ.get(
        "AGENTREDACTOR_STORE_PKG",
        f"macos/build-store/AgentRedactor-store-{version}.pkg"))
    assert package.is_file(), f"Store installer package not built: {package}"

    if expected_identity:
        signature = subprocess.run(
            ["pkgutil", "--check-signature", str(package)],
            check=True, capture_output=True, text=True, timeout=120).stdout
        assert "Status: signed" in signature
        assert expected_identity in signature
