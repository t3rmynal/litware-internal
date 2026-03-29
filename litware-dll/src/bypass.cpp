#include "bypass.h"
#include "debug.h"
#include "platform/compat.h"
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <MinHook.h>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")
#else
#include "platform/linux/hook.h"
#include "platform/linux/proc_maps.h"
#include <dlfcn.h>
#endif

namespace {

using BSecureAllowedFn = int (*)(char*, int, int);
static BSecureAllowedFn g_origBSecureAllowed = nullptr;

static int HookBSecureAllowed(char* report_buf, int buf_size, int flags) {
    (void)report_buf;
    (void)buf_size;
    (void)flags;
    return 1;
}

static void* PatternScan(uintptr_t base, size_t size, const char* pat, const char* mask) {
    if (!base || !pat || !mask) return nullptr;
    auto* b = reinterpret_cast<const uint8_t*>(base);
    const size_t pl = strlen(mask);
    for (size_t i = 0; i + pl <= size; ++i) {
        bool ok = true;
        for (size_t j = 0; j < pl && ok; ++j)
            if (mask[j] != '?' && b[i + j] != static_cast<uint8_t>(pat[j]))
                ok = false;
        if (ok) return const_cast<void*>(static_cast<const void*>(b + i));
    }
    return nullptr;
}

static bool g_secureFlagPatched = false;

}

namespace bypass {

bool Initialize() {
#ifdef _WIN32
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) {
        exe = GetModuleHandleA("cs2.exe");
    }
    if (!exe) {
        DebugLog("[bypass] cs2.exe not found");
        return false;
    }

    void* pBSecureAllowed = GetProcAddress(exe, "BSecureAllowed");
    if (!pBSecureAllowed) {
        DebugLog("[bypass] BSecureAllowed export not found");
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        DebugLog("[bypass] MH_Initialize failed (may already be initialized)");
    }

    MH_STATUS st = MH_CreateHook(pBSecureAllowed, &HookBSecureAllowed,
        reinterpret_cast<void**>(&g_origBSecureAllowed));
    if (st != MH_OK) {
        DebugLog("[bypass] MH_CreateHook BSecureAllowed failed: %d", static_cast<int>(st));
        return false;
    }

    if (MH_EnableHook(pBSecureAllowed) != MH_OK) {
        DebugLog("[bypass] MH_EnableHook BSecureAllowed failed");
        return false;
    }

    DebugLog("[bypass] BSecureAllowed hooked OK");
    return true;
#else
    // Linux: BSecureAllowed may not exist in native CS2 binary
    DebugLog("[bypass] BSecureAllowed bypass not available on Linux");
    return false;
#endif
}

void PatchSecureServerFlag() {
    if (g_secureFlagPatched) return;

#ifdef _WIN32
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) return;

    static const char PAT[] = "\xC6\x05\x00\x00\x00\x00\x01";
    static const char MSK[] = "xx????x";
    void* hit = PatternScan((uintptr_t)client, 0x10000000, PAT, MSK);
    if (!hit) {
        return;
    }

    __try {
        DWORD oldProt;
        uint8_t* instr = static_cast<uint8_t*>(hit);
        if (VirtualProtect(instr, 7, PAGE_EXECUTE_READWRITE, &oldProt)) {
            instr[6] = 0;
            VirtualProtect(instr, 7, oldProt, &oldProt);
            g_secureFlagPatched = true;
            DebugLog("[bypass] secure-server write patched (mov 1 -> 0)");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("[bypass] PatchSecureServerFlag: exception");
    }
#else
    // Linux: this patch pattern is Windows-specific (MSVC x86-64)
    DebugLog("[bypass] PatchSecureServerFlag not available on Linux");
#endif
}

}
