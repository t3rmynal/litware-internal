# LitWare CS2 Internal

---

## ⚠️ Still in development. Features that are unfinished/don't work or needs to be fixed: (Aimbot) Visibility Check; Third Person View 

**Внутренний чит для Counter-Strike 2.** Инжектируется в `cs2.exe`, перехватывает DirectX 11 Present, рисует ImGui оверлей прямо в игре — без внешнего оверлея и задержки ввода.

| | |
|---|---|
| платформа | Windows x64 |
| сборка | Visual Studio 2022 |
| зависимости | Steam (gameoverlayrenderer64.dll), ImGui, MinHook |

---

## Сборка

1. `git clone --recurse-submodules` (submodules обязательны)
2. Открыть `litware-dll/litware-dll.vcxproj` в Visual Studio
3. Build → Release | x64
4. DLL: `litware-dll/bin/Release/litware-dll.dll`

Для Electron-меню: `cd electron-menu` → `npm run dist` → `litware-menu.exe` рядом с DLL (или exe встроен в DLL при сборке).

---

## Управление

| Клавиша | Действие |
|---------|----------|
| INSERT | Меню |
| END | Выгрузка |

---

## Конфиги

`%APPDATA%\litware\`

---

## Оффсеты

`litware-dll/src/core/offsets.h` — синхронизированы с [cs2-dumper](https://github.com/a2x/cs2-dumper). После обновления игры обновляй оффсеты.

---

## Лицензия

GPL-3.0. Educational only. VAC bans possible.

---

---

# LitWare CS2 Internal (English)

Internal cheat for Counter-Strike 2. Injects into `cs2.exe`, hooks DirectX 11 Present, renders ImGui overlay in-game.

| | |
|---|---|
| Platform | Windows x64 |
| Build | Visual Studio 2022 |
| Dependencies | Steam (gameoverlayrenderer64.dll), ImGui, MinHook |

---

## Build

1. `git clone --recurse-submodules` (submodules required)
2. Open `litware-dll/litware-dll.vcxproj` in Visual Studio
3. Build → Release | x64
4. Output: `litware-dll/bin/Release/litware-dll.dll`

For Electron menu: `cd electron-menu` → `npm run dist` → place `litware-menu.exe` next to DLL (or exe is embedded in DLL on build).

---

## Controls

| Key | Action |
|-----|--------|
| INSERT | Menu |
| END | Unload |

---

## Configs

`%APPDATA%\litware\`

---

## Offsets

`litware-dll/src/core/offsets.h` — synced with [cs2-dumper](https://github.com/a2x/cs2-dumper). Update offsets after game updates.

---

## License

MIT. Educational only. VAC bans possible.
