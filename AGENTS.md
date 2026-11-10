# Agent Redactor — Agent Instructions

This repository contains platform-specific code under `windows/`, `linux/`, and `macos/`.
Follow the appropriate `AGENTS.md` for the part of the codebase you are changing:

- **Windows work** → follow [`windows/AGENTS.md`](windows/AGENTS.md)
- **Linux work** → follow [`linux/AGENTS.md`](linux/AGENTS.md)
- **macOS work** → follow [`macos/AGENTS.md`](macos/AGENTS.md); engine/CLI build notes live in [`macos/README.md`](macos/README.md); shared-engine changes follow all applicable platform rules and must be tested on every affected platform

If a change touches multiple platforms, follow all applicable sets of instructions.

## Deferred release improvements

See [`docs/release-improvements.md`](docs/release-improvements.md) for proposed release-pipeline and cross-platform product improvements. This list is planning only: **ask the user before implementing any item on it**.
