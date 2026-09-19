"""Linux-only: `agentredactor update` command wiring against the live feed.

No engine and no config dir needed — the command only reads the public
Velopack channel feed and compares it against the binary's stamped version.
The outcome depends on whether the feed has moved past this build, so both
shapes (and an offline failure) are accepted; what must never happen is a
crash, or the AppImage replacement path running without $APPIMAGE set.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

_tests_root = Path(__file__).resolve().parent.parent
if str(_tests_root) not in sys.path:
    sys.path.insert(0, str(_tests_root))

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux-only command")

PROJECT_ROOT = _tests_root.parent
ENGINE_BIN = Path(
    os.environ.get("AGENTREDACTOR_ENGINE_BIN")
    or PROJECT_ROOT / "linux" / "build" / "engine" / "agentredactor"
)


def test_update_command_feed_check() -> None:
    if not ENGINE_BIN.exists():
        pytest.skip(f"engine not built: {ENGINE_BIN}")
    env = dict(os.environ)
    env.pop("APPIMAGE", None)
    env["AGENTREDACTOR_DISABLE_KEYRING"] = "1"
    r = subprocess.run(
        [str(ENGINE_BIN), "update"],
        env=env, capture_output=True, text=True, timeout=120,
    )
    out = r.stdout + r.stderr
    assert (
        r.returncode == 0 and "up to date" in out
    ) or (
        r.returncode == 1
        and ("install script" in out or "could not read the update feed" in out)
    ), out
