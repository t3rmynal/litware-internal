#pragma once

typedef void (*ElectronBridgeApplyFn)(const char* key, const char* value);

// запускает websocket сервер и поток обработки
void ElectronBridge_Start(ElectronBridgeApplyFn apply_fn);

// шлёт состояние меню (открыто/закрыто)
void ElectronBridge_SendMenuOpen(bool open);

// шлёт уведомление в electron (текст)
void ElectronBridge_SendNotification(const char* text);

// поднимает electron окно поверх cs2 (вызывать из dll — foreground доступ гарантирован)
void ElectronBridge_BringToFront();

// шлёт overlay_visible: true/false (alt-tab показ/скрытие)
void ElectronBridge_SendVisibility(bool visible);

// извлекает exe из ресурса или находит рядом с dll и запускает
void ElectronBridge_LaunchMenu(void);

void ElectronBridge_Stop(void);
