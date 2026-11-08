#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "${DIR}/bin"
swiftc -O "${DIR}/main.swift" -o "${DIR}/bin/agentredactor-gui-helper" \
    -framework AppKit -framework ApplicationServices
