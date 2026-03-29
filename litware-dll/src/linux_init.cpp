#include "platform/compat.h"
#include "debug.h"
#include "entry.h"
#include "hooks/render_hook.h"
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

extern "C" {

static void* g_this_handle = nullptr;

// Entry thread for the cheat
static void* EntryThread(void*) {
    BootstrapLog("[litware] Entry thread started");
    DebugLog("[litware] Entry thread started");

    try {
        entry();
    } catch (...) {
        BootstrapLog("[litware] CRASH in entry()");
        DebugLog("[litware] CRASH in entry()");
    }

    BootstrapLog("[litware] Entry thread finished");
    DebugLog("[litware] Entry thread finished");
    return nullptr;
}

// Cleanup thread for unload
static void* UnloadThread(void*) {
    usleep(800000);  // 800ms delay

    render_hook::Shutdown();

    if (g_this_handle) {
        dlclose(g_this_handle);
    }

    pthread_exit(nullptr);
}

// Constructor: called when .so is loaded via LD_PRELOAD
__attribute__((constructor))
static void LitWareInit() {
    BootstrapLog("[litware] LD_PRELOAD constructor");
    DebugLog("[litware] LD_PRELOAD constructor");

    // Capture handle to self for later unload
    g_this_handle = dlopen(nullptr, RTLD_NOW);

    // Start entry thread
    pthread_t tid;
    if (pthread_create(&tid, nullptr, EntryThread, nullptr) == 0) {
        pthread_detach(tid);
    } else {
        BootstrapLog("[litware] Failed to create entry thread");
    }
}

// Destructor: called when .so is unloaded
__attribute__((destructor))
static void LitWareFini() {
    BootstrapLog("[litware] destructor - cleanup");
    DebugLog("[litware] destructor - cleanup");

    // Spawn unload thread
    pthread_t tid;
    if (pthread_create(&tid, nullptr, UnloadThread, nullptr) == 0) {
        pthread_detach(tid);
    } else {
        BootstrapLog("[litware] Failed to create unload thread");
        render_hook::Shutdown();
    }
}

} // extern "C"
