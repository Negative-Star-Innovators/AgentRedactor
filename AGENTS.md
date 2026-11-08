# Agent Redactor — Agent Instructions

This repository contains platform-specific code under `windows/`, `linux/`, and `macos/`.
Follow the appropriate `AGENTS.md` for the part of the codebase you are changing:

- **Windows work** → follow [`windows/AGENTS.md`](windows/AGENTS.md)
- **Linux work** → follow [`linux/AGENTS.md`](linux/AGENTS.md)
- **macOS work** → follow [`macos/AGENTS.md`](macos/AGENTS.md); engine/CLI build notes live in [`macos/README.md`](macos/README.md); shared-engine changes follow both sets of rules above (keep Windows/Linux behavior identical — additive `#ifdef __APPLE__` only)

If a change touches multiple platforms, follow all applicable sets of instructions.
