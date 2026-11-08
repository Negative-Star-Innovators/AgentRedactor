from __future__ import annotations

import os
import plistlib
import subprocess
import sys
from pathlib import Path

import pytest

pytestmark = [
    pytest.mark.skipif(sys.platform != "darwin", reason="macOS App Store package test"),
    pytest.mark.skipif(os.environ.get("AGENTREDACTOR_STORE_TEST") != "1",
                       reason="opt-in App Store package test"),
]


def test_store_package_layout_and_entitlements() -> None:
    app = Path(os.environ.get(
        "AGENTREDACTOR_STORE_APP", "macos/build-store/app/AgentRedactor.app"))
    if not app.is_dir():
        pytest.fail(f"Store app not built: {app}")

    subprocess.run(["codesign", "--verify", "--strict", str(app)],
                   check=True, capture_output=True, timeout=60)
    entitlements = subprocess.run(
        ["codesign", "-d", "--entitlements", ":-", str(app)],
        check=True, capture_output=True, timeout=60).stdout
    values = plistlib.loads(entitlements)
    assert values["com.apple.security.app-sandbox"] is True
    assert values["com.apple.security.network.client"] is True
    assert values["com.apple.security.network.server"] is True
    assert "com.apple.security.cs.disable-library-validation" not in values

    resources = app / "Contents" / "Resources"
    assert (resources / "agentredactor").is_file()
    assert (resources / "models" / "onnx" / "model_quantized.onnx_data").stat().st_size == 1618042064
    assert not (app / "Contents" / "MacOS" / "UpdateMac").exists()
    assert not (app / "Contents" / "Frameworks" / "velopack_libc_osx.dylib").exists()

    with (app / "Contents" / "Info.plist").open("rb") as handle:
        info = plistlib.load(handle)
    expected = os.environ.get("AGENTREDACTOR_EXPECT_VERSION")
    if expected:
        assert info["CFBundleShortVersionString"] == expected
