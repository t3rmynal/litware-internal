# LitWare CS2 Internal

internal чит для cs2. инжект в cs2.exe, хукает present, рисует оверлей прямо в игре.

| | |
|---|---|
| платформа | windows x64 |
| сборка | visual studio 2022 |
| зависимости | steam (gameoverlayrenderer64.dll), imgui, minhook |

---

## сборка

открываешь `litware-dll/litware-dll.vcxproj` в студии, жмёшь билд. изи бриджи. на выходе только dll в `litware-dll/bin/Release/`.

> submodules без которых крашит: `git clone --recurse-submodules`

---

## управление

| клавиша | действие |
|---------|----------|
| insert | меню |
| end | выгрузка |

---

## конфиги

`%APPDATA%\litware\`

---

## оффсеты

`litware-dll/src/core/offsets.h`, синхронизированы с [cs2-dumper](https://github.com/a2x/cs2-dumper). после обновления игры обнови offsets.

---

## electron меню (опционально)

если нужен ui на react — собери `electron-menu` отдельно (`npm run dist`), положи `litware-menu.exe` рядом с dll. без exe dll работает как обычно, меню просто не откроется.

---

GPL-3.0. educational only. vac баны возможны.
