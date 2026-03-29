# LitWare CS2 Internal

Internal cheat for Counter-Strike 2. The DLL is injected into `cs2.exe`, hooks DirectX 11 `Present`, and renders an in-game ImGui overlay.

| | |
|---|---|
| Platform | Windows x64 |
| Build | Visual Studio 2022 |
| Dependencies | Steam (`gameoverlayrenderer64.dll`), ImGui, MinHook, omath (`vendor/omath`) |
| License | MIT |

---

## Developing on Linux

The **`linux-compat`** branch is for editing the repo on Linux (submodules, line endings, docs)—not for running the cheat on native Linux CS2. The DLL targets **Windows x64** only. See [docs/LINUX.md](docs/LINUX.md) for setup (`./scripts/setup-dev-linux.sh`) and workflow notes.

---

## Build

1. `git clone --recurse-submodules` (if submodules are missing, see [docs/LINUX.md](docs/LINUX.md))
2. Open `litware-dll/litware-dll.vcxproj` in Visual Studio
3. Build `Release | x64`
4. Output: `litware-dll/bin/Release/litware-dll.dll`

---

## Screenshots

| 1 | 2 | 3 |
|:--:|:--:|:--:|
| <img src="screenshots/1.png" width="320"> | <img src="screenshots/2.png" width="320"> | <img src="screenshots/3.png" width="320"> |

---

## Controls

| Key | Action |
|-----|--------|
| `INSERT` | Toggle menu |
| `END` | Unload |

---

## Configs

Configs are stored in `%APPDATA%\litware\` as plain-text `*.cfg` files.

---

## Features

- ESP: boxes, health, names, weapons, ammo, money, distance, offscreen arrows
- Aimbot: FOV, smoothing, head bone, team check
- Visuals: no flash, no smoke, glow, sky color, snow, sakura
- Movement: bunny hop, strafe helper
- Misc: FOV changer, radar, bomb timer, spectator list

---

## Offsets

Offsets are kept in `litware-dll/src/core/offsets.h` and refreshed from `cs2-dumper` after game updates.

---

## Notes

- Steam must be running with the Steam Overlay enabled.
- Inject after the game reaches the main menu.

---

Use at your own risk. Game- or platform-side sanctions are possible.
