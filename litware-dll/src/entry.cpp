#include "entry.h"
#include "bypass.h"
#include "hooks/render_hook.h"
#include "core/electron_bridge.h"
#include "debug.h"
#include <Windows.h>

void entry() {
    BootstrapLog("[litware] entry() start");
    DebugLog("[litware] entry() start");

    // ws сервер стартует сразу — electron подключится как только запустится
    ElectronBridge_Start(nullptr);

    // запускаем electron пока грузятся хуки
    ElectronBridge_LaunchMenu();

    // ждём overlay renderer (steam)
    for (int i = 0; i < 50; ++i) {
        if (GetModuleHandleA("gameoverlayrenderer64.dll") != nullptr)
            break;
        Sleep(200);
    }

    if (!bypass::Initialize()) {
        BootstrapLog("[litware] bypass::Initialize failed (non-fatal)");
        DebugLog("[litware] bypass::Initialize failed (non-fatal)");
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (render_hook::Initialize()) break;
        BootstrapLog("[litware] render_hook attempt %d failed, retry in 3s", attempt + 1);
        Sleep(3000);
        if (attempt == 2) {
            BootstrapLog("[litware] render_hook::Initialize FAILED after 3 attempts");
            DebugLog("[litware] render_hook::Initialize FAILED");
            return;
        }
    }

    BootstrapLog("[litware] hook active, INSERT for menu");

    // уведомление — electron уже должен был подключиться за время инициализации
    ElectronBridge_SendNotification("Cheat injected successfully! Enjoy.");
    Sleep(300);
    ElectronBridge_BringToFront();

    bypass::PatchSecureServerFlag();
}
