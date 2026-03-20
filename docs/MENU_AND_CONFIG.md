# Меню и конфиг (LitWare)

## Структура Electron

- `src/App.tsx` — оверлей (фиксированный слой) + окно меню с `menuOffset` (перетаскивание не двигает HUD).
- `src/components/MenuWindow.tsx` — каркас: TitleBar → **TopNav** (главные вкладки) → **VisualSubNav** (только Visuals) → контент → **MenuFooter**.
- `src/components/layout/` — `TopNav`, `VisualSubNav`, `MenuFooter`.
- `src/lib/dllKeys.ts` — маппинг полей store → строковые ключи для `ApplyConfigKeyFromElectron` в DLL.

## Расширенный конфиг Visuals

Поля в `types/config.ts` / `store` отражают уже существующие ключи в `render_hook.cpp` (`LoadConfigKeyEsp`, `LoadConfigKeyChams`). Новые опции UI (толщина бокса, дистанция, chams, градиенты HP/ammo и т.д.) синхронизируются с DLL через `pushSectionToDll`.

## DLL / рефакторинг `render_hook.cpp`

Логика загрузки ключей уже разнесена по функциям `LoadConfigKeyEsp`, `LoadConfigKeyChams`, `LoadConfigKeyAimbot`, … Вынесение в отдельные `.cpp` файлы возможно при добавлении соответствующих единиц компиляции в `litware-dll.vcxproj`.
