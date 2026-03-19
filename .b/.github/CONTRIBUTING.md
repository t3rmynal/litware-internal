# Contributing to cs2-litware-internal

## Getting Started
- Fork the repo and create your branch from `main`
- Build with MSVC (Visual Studio 2022, x64 Release)
- Test in-game before submitting a PR

## Reporting Issues
Use the issue templates:
- **Bug Report** — crashes, broken features, incorrect behavior
- **Offset Update** — when a CS2 update breaks functionality
- **Feature Request** — suggestions for new features

## Code Style
- C++ with Windows SDK, no STL containers in hot paths
- Offsets live in `litware-dll/src/core/offsets.h`
- All rendering happens in `render_hook.cpp`
- Keep functions focused and avoid bloating a single file further

## Submitting a PR
- Target the `main` branch
- Fill out the PR template
- One feature/fix per PR — keep diffs small and reviewable
