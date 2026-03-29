#!/usr/bin/env bash
# Initialize submodules after cloning on Linux (or any POSIX shell).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if ! command -v git >/dev/null 2>&1; then
  echo "error: git is required" >&2
  exit 1
fi
git submodule update --init --recursive
echo "LitWare: submodules ready at $ROOT"
