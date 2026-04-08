#include "entry.h"
#include "bypass.h"
#include "hooks/render_hook.h"
#include "debug.h"
#include "platform/compat.h"
#include "platform/linux/proc_maps.h"

void entry() {
    BootstrapLog("[litware] entry() start");
    DebugLog("[litware] entry() start");

    // On Linux, skip overlay wait and proceed directly
    #ifndef __linux__
    // Wait for overlay on Windows
    for (int i = 0; i < 50; ++i) {
        if (GetModuleHandleA("gameoverlayrenderer64.dll") != nullptr)
            break;
        Sleep(200);
    }
    #endif

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
    BootstrapLog("[litware] SUCCESS - hook active, INSERT for menu");
    DebugLog("[litware] render_hook::Initialize OK - hook active");
    bypass::PatchSecureServerFlag();
}
