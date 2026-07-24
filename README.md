# Agent Redactor

A Windows desktop app that sits between AI coding agents and their LLM
endpoints as a local proxy, redacting PII (names, emails, phone numbers,
secrets, and more) from outbound requests before they leave your machine —
and un-redacting the responses coming back.

- WinUI 3 (C++/WinRT) desktop app, self-contained Windows App SDK
- Local HTTP proxy with on-device ONNX NER model — no cloud calls for detection
- Custom keyword and regex redaction rules on top of model-based PII detection
- Localized UI in 53 languages
- Distributed via the Microsoft Store (MSIX)

## Repository layout

| Path | Contents |
|---|---|
| `AgentRedactor/` | The WinUI 3 app (C++), build scripts, models, resources |
| `tests/` | GUI end-to-end tests (FlaUI + pytest + mock LLM) |
| `third_party_tests/` | Integration tests driving real third-party agent CLIs through the proxy |
| `scripts/` | PowerShell helpers that configure third-party clients for the integration tests |
| `.github/workflows/` | CI: MSIX build and tests |

## Prerequisites

- Windows 10/11 x64
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
  place it in `AgentRedactor\models\onnx\`.

## Building

Quick build for local development (EXE only, no packaging):

```powershell
cd AgentRedactor
.\buildquick.ps1
```

Release build producing the MSIX package (`AgentRedactor\build\AgentRedactor.msix`):

```powershell
cd AgentRedactor
.\build.ps1
```

The MSIX is unsigned; the Microsoft Store signs it on submission. To install it
locally you must sign it with your own certificate first.

## Tests

See `tests/README.md` and `third_party_tests/README.md`. In short:

```powershell
cd tests
pip install -r requirements.txt
pytest -v gui/
```

The tests drive the real application UI (FlaUI) and therefore require an
interactive Windows desktop session — they run on a self-hosted runner, not on
GitHub-hosted runners. Third-party integration tests additionally need an
OpenRouter API key (copy `third_party_tests\.env.example` to `.env`).

## License

MIT — see [LICENSE](LICENSE).
