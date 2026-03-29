# Developing on Linux

Counter-Strike 2 and this project’s output (`litware-dll.dll`) run on **Windows x64**. You cannot build or inject this DLL on native Linux for the Linux game client: it depends on the Windows CS2 process, Win32, and DirectX 11.

This guide covers **using Linux for the repository**—cloning, submodules, editing sources and docs, and integrating with a Windows build machine when you need a binary.

---

## What works on Linux

- Cloning the repo and updating **Git submodules** (ImGui, MinHook, omath).
- Editing C++ sources, headers, offset definitions, and documentation.
- Running your own checks (formatters, static analysis) if you configure them locally.

## What still requires Windows

- **Building** the DLL: Visual Studio 2022, `Release | x64`, per the main [README](../README.md).
- **Running** the cheat: Windows `cs2.exe`, Steam overlay, injection tooling.

---

## Quick start (Linux)

```bash
git clone --recurse-submodules <your-repo-url>
cd LitWare-Internal
```

Replace `<your-repo-url>` with your remote (HTTPS or SSH). Example: `https://github.com/t3rmynal/LitWare-Internal.git`.

If you already cloned without submodules:

```bash
./scripts/setup-dev-linux.sh
```

Or manually:

```bash
git submodule update --init --recursive
```

Make the setup script executable if needed:

```bash
chmod +x scripts/setup-dev-linux.sh
```

---

## Line endings

The repo uses `.gitattributes` so text files get sane line endings across OSes. If you see `^M` in editors or shell errors, configure Git:

```bash
git config core.autocrlf input
```

---

## Editing on Linux, building on Windows

Typical workflows:

1. **Dual boot / second PC** — keep the repo on a shared disk or push a branch from Linux and pull on Windows to build.
2. **WSL2** — edit under Linux; open the same tree from Windows (e.g. `\\wsl$\...`) or sync via Git and build with Visual Studio on Windows.
3. **SSH / CI** — Linux can hold the canonical tree; a Windows runner or manual step produces the DLL.

There is no supported Linux toolchain in this repository that replaces MSVC for the game DLL.

---

## Dependencies on Linux (dev only)

- **Git** — for clone and submodules.
- **Optional** — a C++ editor or LSP; the solution file is for Visual Studio, but any editor can open the sources.

You do **not** need Steam or CS2 on Linux to work on offsets or code, unless you use a separate Windows environment to test.

---

## Troubleshooting

| Issue | What to try |
|--------|-------------|
| Empty `external/` or `vendor/` after clone | Run `./scripts/setup-dev-linux.sh` or `git submodule update --init --recursive`. |
| Permission denied on `setup-dev-linux.sh` | `chmod +x scripts/setup-dev-linux.sh` |
| Submodule URL blocked (firewall) | Use SSH remotes for submodules or fetch over a network that allows GitHub. |

---

## Summary

Linux is fully supported for **checkout and development workflows**; **release builds and in-game use** remain on **Windows x64** as documented in the main README.
