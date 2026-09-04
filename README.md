# Agent Redactor

A desktop app for Windows and Linux that sits between AI coding agents and
their LLM endpoints as a local proxy, redacting PII (names, emails, phone
numbers, secrets, and more) from outbound requests before they leave your
machine — and un-redacting the responses coming back.

- Windows: WinUI 3 (C++/WinRT) desktop app, self-contained Windows App SDK
- Local HTTP proxy with on-device ONNX NER model — no cloud calls for detection
- Custom keyword and regex redaction rules on top of model-based PII detection
- Localized UI in 53 languages
- Distributed via the Microsoft Store (Windows MSIX) and a self-release channel
  (Velopack, with built-in auto-updates) for Windows and Linux

**Website:** <https://agentredactor.negativestarinnovators.com/>

## Documentation

Step-by-step integration guides for the supported AI coding agents are on the
website:

- [Claude Code](https://agentredactor.negativestarinnovators.com/claude-code.html)
- [Codex](https://agentredactor.negativestarinnovators.com/codex.html)
- [OpenClaw](https://agentredactor.negativestarinnovators.com/openclaw.html)
- [OpenCode](https://agentredactor.negativestarinnovators.com/opencode.html)
- [Hermes](https://agentredactor.negativestarinnovators.com/hermes.html)

## Install

**Microsoft Store** (x64 and ARM64; updates via the Store):
<https://apps.microsoft.com/detail/9pn44k2tm2g3>

**Self-release** (x64 and ARM64; updates itself via Velopack). Run in PowerShell:

```powershell
iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"
```

The installer picks the native build for your architecture (falling back to the
x64 build on ARM64 if no native package is published yet) and installs per-user
under `%LOCALAPPDATA%\AgentRedactor`. Self-release builds are unsigned for now —
Windows SmartScreen may warn on first run.

## Repository layout

| Path | Contents |
|---|---|
| `windows/` | The WinUI 3 app (C++), build scripts, models, resources |
| `core/` | OS-agnostic C++ core (HTTP proxy, ONNX NER, regex/redaction engines) shared by all platform frontends; CMake scaffolding, currently built via the Windows project |
| `cloudflare/` | The Cloudflare worker + R2 behind the self-release channel (`/install.ps1`, `/updates`, `/models`) |
| `tests/` | GUI end-to-end tests (FlaUI + pytest + mock LLM) and self-release install/upgrade E2E |
| `third_party_tests/` | Integration tests driving real third-party agent CLIs through the proxy |
| `scripts/` | PowerShell helpers that configure third-party clients for the integration tests |
| `.github/workflows/` | CI: MSIX build + package tests, self-release (Velopack) build/test/publish, worker deploy |

## Prerequisites

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

## Building

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
tests and uploads both channels to R2 (see `cloudflare/README.md`).

## Tests

See `tests/README.md` and `third_party_tests/README.md`. In short:

```powershell
cd tests
pip install -r requirements.txt
pytest -v gui/
```

The tests drive the real application UI (FlaUI) and therefore require an
interactive Windows desktop session — in CI they run on GitHub-hosted runners
(`windows-latest` and `windows-11-arm`, which provide one) via the
workflow-dispatch **Tests** workflow, or locally on any Windows desktop.
Third-party integration tests additionally need an
OpenRouter API key (copy `third_party_tests\.env.example` to `.env`).

## License

MIT — see [LICENSE](LICENSE).
