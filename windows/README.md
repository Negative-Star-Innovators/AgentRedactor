# Agent Redactor

A native Windows C++ desktop application that acts as an intelligent HTTP proxy for AI coding agents and LLM APIs. It intercepts API traffic, redacts PII and sensitive information using the OpenAI Privacy Filter ONNX model, custom regex rules, and keyword lists, then reconstructs the original content on the response path — all transparently.

## Architecture

```
┌─────────────┐      localhost      ┌─────────────────┐      HTTPS       ┌──────────┐
│  IDE/Agent  │ ──────────────────> │  Agent Redactor │ ───────────────> │  OpenAI  │
│  (Cursor,   │   (per-profile port) │  (localhost)    │   (redacted)   │   API    │
│  OpenClaw)  │ <────────────────── │                 │ <────────────── │          │
└─────────────┘   (reconstructed)   └─────────────────┘   (response)    └──────────┘
```

## Features

- **Multi-Profile API Key Management**: Each alias has its own port, upstream URL, API key, rules, stats, and logs.
- **OpenAI Privacy Filter**: Local ONNX model for PII detection (emails, phones, names, addresses, secrets, etc.).
- **Custom Regex Rules**: Per-profile regex patterns for redaction.
- **Keyword Redaction**: Simple per-profile keyword lists.
- **Request/Response Reconstruction**: Labels are replaced on the way out and restored on the way back.
- **System Tray**: Runs minimized in the system tray; auto-start with Windows.
- **Native Win32 GUI**: No WebView2 — pure native C++ with RichEdit colored logs.
- **Stats & Logs**: Per-profile statistics and colored logs (blue = user→proxy, green = proxy→LLM, orange = LLM→proxy, purple = proxy→user).
- **Two distribution channels**: Microsoft Store MSIX, and a self-updating Velopack self-release channel (`version.txt` is the version source of truth).

## Project Structure

```
windows/
├── src/              # C++ source files
├── include/          # C++ headers
├── resources/        # Icons, manifest, RC file
├── models/           # OpenAI Privacy Filter ONNX model (copied from parent project)
├── CMakeLists.txt    # CMake build config
├── vcpkg.json        # vcpkg dependencies
├── Package.appxmanifest  # MSIX manifest
├── version.txt       # Self-release version source of truth
├── build.ps1         # Build script (MSIX; -SelfRelease for the Velopack channel)
└── build-selfrelease.ps1  # Self-release wrapper (reads version.txt, runs vpk pack)
```

## Distribution Channels

- **Microsoft Store (MSIX)** — built with `.\build.ps1`. Ships the full ~1.6 GB
  ONNX weights inside the package and contains no self-update code.
- **Self-release (Velopack)** — built with `.\build-selfrelease.ps1` (x64).
  The installer is produced by `vpk pack` into `build\velopack\`; the large
  model weights are downloaded on first run from the `models-v1` GitHub
  release into `%LOCALAPPDATA%\windows\models`, and app updates are
  delivered from `api.agentredactor.negativestarinnovators.com` via the
  bundled Velopack `Update.exe`. The version comes from `version.txt`.

## Build Requirements

- Windows 10/11
- Visual Studio 2022 with C++ workload
- CMake 3.20+
- vcpkg (integrated with CMake)
- ONNX Runtime model files (from `windows-app-openai/models`)

## Dependencies (vcpkg)

| Package | Purpose |
|---------|---------|
| `onnxruntime` | PII inference engine |
| `nlohmann-json` | JSON config and API parsing |
| `wil` | Windows Implementation Library |
| `cppwinrt` | C++/WinRT for modern Windows APIs |

## Build Instructions

### 1. Ensure model files are present

Copy or symlink the ONNX model from the existing project:

```powershell
Copy-Item -Recurse ..\windows-app-openai\models .\windows\models
```

### 2. Configure with CMake

```powershell
cd windows
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake
```

### 3. Build

```powershell
cmake --build . --config Release
```

### 4. Package (MSI + MSIX)

```powershell
.\build.ps1
```

## Configuration

Settings are stored in `%APPDATA%\AgentRedactor\settings.json`.

### Default Profile Setup

1. Open the GUI from the system tray (double-click) or launch the exe.
2. Click **Add** to create a new profile.
3. Fill in:
   - **Alias**: e.g., `openclaw`
   - **Port**: e.g., `8081`
   - **Upstream URL**: e.g., `https://openrouter.ai/api/v1`
   - **API Key**: your real upstream API key
4. Check/uncheck PII categories, add regex patterns and keywords.
5. Click **Save Profile**.
6. The proxy starts automatically on the configured port.

### IDE / Agent Configuration

In your coding agent or IDE, set the API base URL to:

```
http://localhost:8081
```

Use any dummy API key (the real key is injected by the proxy).

## Redaction Pipeline

For each outgoing request:
1. **OpenAI Model** (optional): Detects PII entities and replaces them with `<<REDACTED_PII_n>>`
2. **Regex Engine**: Matches custom patterns and replaces with `<<REDACTED_REGEX_n>>`
3. **Keyword Engine**: Matches keywords and replaces with `<<REDACTED_KEYWORD_n>>`

For each incoming response:
1. All labels are restored to their original values in reverse order.
2. SSE streams are accumulated and reconstructed to handle split labels.

## GUI Layout

- **Left Panel**: List of API key aliases.
- **Right Panel** (per selected alias):
  - Profile settings (alias, port, URL, key)
  - OpenAI model enable/disable
  - PII category checkboxes
  - Regex patterns editor
  - Keywords editor
  - Statistics display
  - Colored log viewer

## License

Proprietary — Agent Redactor Project.
