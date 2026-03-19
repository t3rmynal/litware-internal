# LitWare DLL

DLL for CS2. Injects into `cs2.exe`, hooks DirectX 11 Present, renders overlay in-game.

## Build

1. **First** build the menu: `cd electron-menu; npm run dist` (creates `litware-menu.exe`)
2. Open `litware-dll.vcxproj` in Visual Studio 2022
3. Select **Release | x64** (or Debug)
4. Output: `bin/Release/litware-dll.dll` — **DLL only**, exe is embedded inside
5. Inject into `cs2.exe` after main menu loads

## Dependencies (in repo)

- `external/imgui` — ImGui
- `external/minhook` — MinHook
- `../vendor/omath` — math utils

## Controls

- **INSERT** — Toggle menu
- **END** — Unload

## Troubleshooting

1. **Debug build** — logs to `%TEMP%\litware_dll.log`
2. Steam overlay must be enabled
3. Inject **after** CS2 main menu loads
