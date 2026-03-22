# LitWare CS2 Internal

## ⚠️ Still in development. Features that are unfinished/don't work: BHOP, Skinchanher

**Внутренний чит для Counter-Strike 2.** Инжектируется в `cs2.exe`, перехватывает DirectX 11 Present, рисует ImGui оверлей прямо в игре — без внешнего оверлея и задержки ввода.

| | |
|---|---|
| **Платформа** | Windows x64 |
| **Движок** | Source 2 |
| **Сборка** | Visual Studio 2022 |
| **Зависимости** | Steam (gameoverlayrenderer64.dll), ImGui, MinHook |

---

## Скриншоты

| Меню | Визуалы |
|:----:|:-------:|
| <img src="screenshots/menu.jpg" width="320"> | <img src="screenshots/visuals_blue.jpg" width="320"> |

---

## Возможности

| Категория | Функции |
|----------|---------|
| **ESP** | Боксы (угловые/округлые/острые), скелет, полоса HP, имена, оружие, боеприпасы, деньги, дистанция, стрелки за экраном |
| **Aimbot** | FOV, сглаживание, голова (кость), проверка тиммейтов, опционально только цель под прицелом |
| **Визуалы** | Без флешки, без дыма, glow, chams (враги/тим/ignore-z), цвет мира/неба, снег, сакура |
| **Движение** | Bunny hop, strafe helper, anti-aim (spin/desync/jitter) |
| **Разное** | Смена FOV, радар, таймер бомбы, список зрителей |
| **Конфиги** | Сохранение/загрузка в `%APPDATA%\litware\` |

**Путь к конфигам:** `%APPDATA%\litware\` (например `C:\Users\<пользователь>\AppData\Roaming\litware\`)

---

## Управление

| Клавиша | Действие |
|---------|----------|
| **INSERT** | Вкл/выкл меню |
| **F1** | Окно хоткеев |
| **END** | Выгрузка DLL |

---

## Сборка

### Требования

- Visual Studio 2022 (x64 workload)
- Запущенный Steam (нужен `gameoverlayrenderer64.dll`)
- Установленная CS2

### Шаги

1. Открыть `litware-dll/litware-dll.vcxproj` в Visual Studio (или `litware-dll.sln` в корне)
2. Выбрать конфигурацию **Release | x64** (или Debug для логов)
3. Сборка → `litware-dll/bin/Release/litware-dll.dll`
4. Инжектить в `cs2.exe` после загрузки главного меню

### Зависимости (submodules)

- `litware-dll/external/imgui` — ImGui
- `litware-dll/external/minhook` — MinHook
- `vendor/omath` — математические утилиты
- `icons/CS2GunIcons.ttf` — иконки оружия

**Клонирование:**
```bash
git clone --recurse-submodules --remote-submodules https://github.com/t3rmynal/cs2-litware-internal.git
```

---

## Оффсеты

Оффсеты в `litware-dll/src/core/offsets.h`, синхронизированы с [cs2-dumper](https://github.com/a2x/cs2-dumper).

После обновления игры: запустить cs2-dumper, обновить `offsets/output/` и `offsets.h`.

---

## Структура проекта

```
├── litware-dll/            # Основная DLL (Litware)
│   ├── src/                # Исходники
│   │   ├── hooks/          # render_hook, хук Present
│   │   ├── core/           # offsets, entity, esp_data
│   │   └── ...
│   ├── external/           # ImGui, MinHook (встроены)
│   └── res/                # Ассеты меню
├── offsets/                # Вывод cs2-dumper
├── vendor/omath/           # Математическая библиотека
└── icons/                  # Шрифты
```

---

## Лицензия

GPL-3.0 — см. [LICENSE](LICENSE).

---

## Дисклеймер

Только в образовательных целях. Использование на свой риск. Возможны баны VAC.

---

---

# LitWare CS2 Internal

**Internal cheat for Counter-Strike 2.** Injects into `cs2.exe`, hooks DirectX 11 Present, renders an ImGui overlay directly in-game — no external overlay, no input lag.

| | |
|---|---|
| **Platform** | Windows x64 |
| **Engine** | Source 2 |
| **Build** | Visual Studio 2022 |
| **Dependencies** | Steam (gameoverlayrenderer64.dll), ImGui, MinHook |

---

## Screenshots

| Menu | Visuals |
|:----:|:-------:|
| <img src="screenshots/menu.jpg" width="320"> | <img src="screenshots/visuals_blue.jpg" width="320"> |

---

## Features

| Category | Features |
|----------|----------|
| **ESP** | Boxes (corner/rounded/sharp), skeleton, health bar, names, weapons, ammo, money, distance, offscreen arrows |
| **Aimbot** | FOV, smoothing, head bone, team check, optional crosshair-only (visibility) |
| **Visuals** | No flash, no smoke, glow, chams (enemy/team/ignore-z), world/sky color, snow, sakura |
| **Movement** | Bunny hop, strafe helper, anti-aim (spin/desync/jitter) |
| **Misc** | FOV changer, radar, bomb timer, spectator list |
| **Config** | Save/load to `%APPDATA%\litware\` |

**Config path:** `%APPDATA%\litware\` (e.g. `C:\Users\<user>\AppData\Roaming\litware\`)

---

## Controls

| Key | Action |
|-----|--------|
| **INSERT** | Toggle menu |
| **F1** | Keybinds window |
| **END** | Unload DLL |

---

## Build

### Prerequisites

- Visual Studio 2022 (x64 workload)
- Steam running (required for `gameoverlayrenderer64.dll`)
- CS2 installed

### Steps

1. Open `litware-dll/litware-dll.vcxproj` in Visual Studio (or `litware-dll.sln` at root)
2. Select **Release | x64** (or Debug for logs)
3. Build → `litware-dll/bin/Release/litware-dll.dll`
4. Inject into `cs2.exe` after the main menu loads

### Dependencies (submodules)

- `litware-dll/external/imgui` — ImGui
- `litware-dll/external/minhook` — MinHook
- `vendor/omath` — math utilities
- `icons/CS2GunIcons.ttf` — weapon icons

**Clone:**
```bash
git clone --recurse-submodules --remote-submodules https://github.com/t3rmynal/cs2-litware-internal.git
```

---

## Offsets

Offsets are kept in `litware-dll/src/core/offsets.h`, synced with [cs2-dumper](https://github.com/a2x/cs2-dumper).

After game updates: run cs2-dumper, refresh `offsets/output/`, and update `offsets.h`.

---

## Project Structure

```
├── litware-dll/            # Main DLL (Litware)
│   ├── src/                # Source
│   │   ├── hooks/          # render_hook, Present hook
│   │   ├── core/           # offsets, entity, esp_data
│   │   └── ...
│   ├── external/          # ImGui, MinHook (bundled)
│   └── res/                # Menu assets
├── offsets/                # cs2-dumper output
├── vendor/omath/           # Math library
└── icons/                  # Fonts
```

---

## License

GPL-3.0 — see [LICENSE](LICENSE).

---

## Disclaimer

Educational use only. Use at your own risk. VAC bans possible.