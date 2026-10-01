# Agent Redactor

A local privacy proxy for AI coding agents, with a desktop app on Windows and
Linux. It sits between your agents and their LLM endpoints, redacting PII
(names, emails, phone numbers, secrets, and more) from outbound requests
before they leave your machine — and un-redacting the responses coming back.

- Local HTTP proxy with an on-device ONNX NER model — no cloud calls for detection
- Custom keyword and regex redaction rules on top of model-based PII detection
- Works with any agent that can point at a local proxy — Claude Code, Codex, OpenClaw, OpenCode, Hermes (guides below)
- Localized UI in 53 languages
- Per-user install — no admin or root required; on headless Linux the installer sets up a systemd user service
- Auto-updates via the Microsoft Store or the built-in Velopack updater

**Website:** <https://agentredactor.negativestarinnovators.com/>

## Install

### Windows

**Microsoft Store** (x64 and ARM64; updates via the Store):
<https://apps.microsoft.com/detail/9pn44k2tm2g3>

**Self-release** (x64 and ARM64; updates itself via Velopack). Run in PowerShell:

```powershell
iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"
```

The installer picks the native build for your architecture (falling back to
the x64 build on ARM64 if no native package is published yet) and installs
per-user under `%LOCALAPPDATA%\AgentRedactor`. Self-release builds are
unsigned for now — Windows SmartScreen may warn on first run.

### Linux

(x64 and ARM64; per-user AppImage install). Run in a terminal:

```bash
curl -fsSL https://api.agentredactor.negativestarinnovators.com/install.sh | bash
```

The installer detects your architecture and:

- places the AppImage in `~/Applications` and symlinks the `agentredactor` CLI into `~/.local/bin`
- with a display, starts the GUI; on a headless machine it instead installs a
  systemd user service (starts at boot, survives logout), downloads the AI
  model, and confirms with `agentredactor status`
- works under WSL as well (GUI via WSLg, headless otherwise)

Remove with `agentredactor uninstall`.

**Updates.** Linux GUI installs self-update in-app, like Windows: the app
checks at startup and offers "Restart now / later" once an update is
downloaded (Settings also has a "Check for updates" button). Headless installs
have no GUI prompt — `agentredactor status` reports when a newer release
exists, then run `agentredactor update` and
`systemctl --user restart agentredactor` to apply.

## Documentation

Step-by-step integration guides for the supported AI coding agents are on the
website:

- [Claude Code](https://agentredactor.negativestarinnovators.com/claude-code.html)
- [Codex](https://agentredactor.negativestarinnovators.com/codex.html)
- [OpenClaw](https://agentredactor.negativestarinnovators.com/openclaw.html)
- [OpenCode](https://agentredactor.negativestarinnovators.com/opencode.html)
- [Hermes](https://agentredactor.negativestarinnovators.com/hermes.html)

The engine binary also doubles as a scriptable CLI (`agentredactor status`,
`keywords add`, `profiles add`, …) for terminals, scripts, and headless
machines — see [docs/cli.md](docs/cli.md).

## Repository layout

| Path | Contents |
|---|---|
| `windows/` | The Windows app (WinUI 3, C++/WinRT), build scripts, models, resources, MSIX packaging |
| `linux/` | The Linux app (Qt 6 GUI plus engine/CLI build), AppImage packaging, systemd user unit |
| `core/` | OS-agnostic C++ core (HTTP proxy, ONNX NER, regex/redaction engines, CLI) shared by the platform frontends; built by both the Windows and Linux projects |
| `cloudflare/` | The Cloudflare worker + R2 behind the self-release channel (`/install.ps1`, `/install.sh`, `/updates`, `/models`) |
| `website/` | The website with the agent integration guides, translated into every supported language |
| `docs/` | Design and spec documents (Linux implementation spec, port plan) |
| `tests/` | pytest suites: cross-platform `cli/` and `migration/`, Linux GUI tests in `linux/` (AT-SPI), Windows GUI end-to-end tests in `gui/` (FlaUI + mock LLM), self-release install/upgrade E2E |
| `third_party_tests/` | Integration tests driving real third-party agent CLIs through the proxy |
| `scripts/` | Build/release helpers and scripts that configure third-party clients for the integration tests |
| `.github/workflows/` | CI: MSIX build + package tests, self-release (Velopack) build/test/publish for Windows and Linux, worker deploy |

## Building

### Windows

Prerequisites:

- Windows 10/11, x64 or ARM64
- Visual Studio 2022 (or Build Tools) with the *Desktop development with C++* workload
- Windows 10/11 SDK (10.0.19041.0 or newer)
- [vcpkg](https://github.com/microsoft/vcpkg) cloned **as a sibling folder** of this
  repository (the project references `..\..\vcpkg`), with the dependencies installed:
  ```powershell
  git clone https://github.com/microsoft/vcpkg ..\vcpkg
  ..\vcpkg\bootstrap-vcpkg.bat
  ..\vcpkg\vcpkg install onnxruntime:x64-windows nlohmann-json:x64-windows wil:x64-windows
  ```
- The ONNX model weights. `model_quantized.onnx_data` (~1.6 GB) is **not in the
  repo**; download it from the
  [Releases](https://github.com/Negative-Star-Innovators/AgentRedactor/releases) page and
  place it in `windows\models\onnx\`.

Quick build for local development (EXE only, no packaging):

```powershell
cd windows
.\buildquick.ps1
```

Release build producing the per-architecture MSIX package (`windows\build\AgentRedactor-x64.msix`; pass `-Platform ARM64` for the ARM64 build):

```powershell
cd windows
.\build.ps1
```

For the Store, upload **both** per-architecture MSIX files (`AgentRedactor-x64.msix`
and `AgentRedactor-arm64.msix`) to a single Partner Center submission — the Store
serves the right architecture to each device. (A combined `.msixbundle` also works
— `.\buildbundle.ps1` builds one — but the two-file submission is what we publish,
since the bundle exceeds GitHub's 2 GB release-asset limit.)

The MSIX packages are unsigned; the Microsoft Store signs them on submission. To install
locally you must sign with your own certificate first.

Self-release (Velopack) build producing the installer, feed and full package under
`windows\build\velopack\` (pass `-Platform ARM64` for the ARM64 channel):

```powershell
cd windows
.\build-selfrelease.ps1 -Version 1.1.1
```

Releases are published by pushing a `v*` tag — the Self-Release workflow builds,
tests and uploads both Windows channels to R2 (see `cloudflare/README.md`).

### Linux

Prerequisites (Ubuntu 24.04):

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libsecret-1-dev libcurl4-openssl-dev libssl-dev nlohmann-json3-dev \
  qt6-base-dev qt6-l10n-tools libgl1-mesa-dev patchelf

# onnxruntime is not packaged in apt; use the official x64 tarball
# (developed/tested against 1.29.0; on ARM64 use the aarch64 tarball):
mkdir -p ~/onnxruntime
curl -sL https://github.com/microsoft/onnxruntime/releases/download/v1.29.0/onnxruntime-linux-x64-1.29.0.tgz \
  | tar xz -C ~/onnxruntime --strip-components=1
```

The engine also needs the NER model files: `config.json`, `tokenizer.json`,
`viterbi_calibration.json` and `onnx/model_quantized.onnx` live in
`windows/models/`; the ~1.6 GB `onnx/model_quantized.onnx_data` weights are
downloaded automatically on first run.

Build the GUI and engine/CLI:

```bash
cd linux
cmake -B build -G Ninja \
  -DONNXRUNTIME_INCLUDE_DIR=~/onnxruntime/include \
  -DONNXRUNTIME_LIB=~/onnxruntime/lib/libonnxruntime.so
cmake --build build
```

Release packaging (Velopack AppImage, produced under
`linux/build-release/velopack/`) is `linux/build-release.sh` — it needs the
.NET SDK and the pinned `vpk` tool. See `linux/README.md` for packaging,
publishing and running the AppImage. Linux releases publish the same way as
Windows: push a `v*` tag and the Build Linux workflow builds, tests and
uploads both channels to R2.

## Tests

The suites live in `tests/` (see `tests/README.md`) and `third_party_tests/`
(see `third_party_tests/README.md`). The cross-platform suites (`cli/`,
`migration/`) and the Linux suite (`linux/`) run in CI on every PR; the
`linux/` AT-SPI GUI tests need a display (CI runs them under Xvfb + D-Bus —
see `linux/README.md`):

```bash
cd tests
python -m pytest cli -q
python -m pytest migration -q
python -m pytest linux -q
```

The Windows GUI end-to-end tests in `tests/gui/` drive the real application UI
(FlaUI) and therefore require an interactive Windows desktop session — in CI
they run on GitHub-hosted runners (`windows-latest` and `windows-11-arm`,
which provide one) via the workflow-dispatch **Tests** workflow, or locally on
any Windows desktop. The dispatched Tests workflow also runs the CLI suite on
Windows. Third-party integration tests additionally need an
OpenRouter API key (copy `third_party_tests\.env.example` to `.env`).

## License

MIT — see [LICENSE](LICENSE).
