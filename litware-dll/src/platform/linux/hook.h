#pragma once

#ifdef __linux__

#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>

namespace lhook {

enum Status {
    OK = 0,
    ALREADY_HOOKED = 1,
    MPROTECT_FAIL = 2,
    ALLOC_FAIL = 3,
    INVALID_ARG = 4,
};

// x86-64 inline hook implementation
class InlineHook {
private:
    struct Hook {
        void* target;
        void* detour;
        void* original;
        uint8_t original_bytes[16];
        size_t original_size;
        bool enabled;
    };

    static std::vector<Hook>& GetHooks() {
        static std::vector<Hook> hooks;
        return hooks;
    }

    static std::mutex& GetMutex() {
        static std::mutex mutex;
        return mutex;
    }

    static size_t GetPageSize() {
        static size_t page_size = sysconf(_SC_PAGE_SIZE);
        return page_size;
    }

    static uintptr_t AlignDown(uintptr_t addr) {
        return addr & ~(GetPageSize() - 1);
    }

    static uintptr_t AlignUp(uintptr_t addr) {
        return (addr + GetPageSize() - 1) & ~(GetPageSize() - 1);
    }

    // Minimal x86-64 instruction length decoder
    // For hook purposes, we need at least 5 bytes to fit our jmp instruction
    static size_t GetInstructionLength(const uint8_t* code, size_t max_len) {
        if (!code || max_len == 0) return 0;

        uint8_t b = code[0];

        // Single byte instructions
        if (b == 0x90) return 1; // nop
        if (b == 0xCC) return 1; // int3
        if ((b & 0xF0) == 0x40 && max_len > 1) {
            // REX prefix - multi-byte instruction
            b = code[1];
            if (b == 0x89 || b == 0x8B) return 3; // mov r/m64
            if (b == 0xFF) return 3; // jmp/call
            return 2;
        }
        if (b == 0x55) return 1; // push rbp
        if (b == 0x5D) return 1; // pop rbp
        if (b == 0xC3) return 1; // ret
        if (b == 0xE8 && max_len >= 5) return 5; // call rel32
        if (b == 0xE9 && max_len >= 5) return 5; // jmp rel32
        if (b == 0xFF && max_len > 1) {
            b = code[1];
            if ((b & 0x38) == 0x20) return 3; // jmp [r/m64]
            if ((b & 0x38) == 0x10) return 3; // call [r/m64]
            return 3;
        }
        if ((b & 0xF0) == 0x50) return 1; // push/pop
        if (b == 0x48 && max_len > 1) {
            b = code[1];
            if (b == 0x89 || b == 0x8B) return 3; // mov r64, r/m64
            if (b == 0xC7) return 7; // mov r/m64, imm64
            return 3;
        }
        if (b >= 0x70 && b <= 0x7F && max_len >= 2) return 2; // jcc rel8

        // Default: assume 3 bytes (covers many x86-64 instructions)
        return 3;
    }

public:
    // Create a hook: target -> detour, save original pointer
    static Status Create(void* target, void* detour, void** original) {
        if (!target || !detour) return INVALID_ARG;

        std::lock_guard<std::mutex> lock(GetMutex());

        // Check if already hooked
        for (const auto& h : GetHooks()) {
            if (h.target == target) return ALREADY_HOOKED;
        }

        auto& hooks = GetHooks();

        // Save original bytes (minimum 5 bytes for jmp instruction)
        uint8_t original_bytes[16] = {0};
        size_t copy_size = 0;
        {
            size_t pos = 0;
            while (pos < 5 && pos < 16) {
                size_t insn_len = GetInstructionLength((uint8_t*)target + pos, 16 - pos);
                if (insn_len == 0) break;
                pos += insn_len;
            }
            copy_size = pos;
            if (copy_size < 5 || copy_size > 15) {
                // Can't safely hook - not enough space for jmp
                return ALLOC_FAIL;
            }
            memcpy(original_bytes, target, copy_size);
        }

        // Allocate trampoline near target (within ±2GB for rel32 jmp)
        // Try addresses near the target in 64MB increments
        void* trampoline = MAP_FAILED;
        uintptr_t target_addr = (uintptr_t)target;
        for (int i = 1; i <= 32 && trampoline == MAP_FAILED; ++i) {
            uintptr_t hint_above = target_addr + (uintptr_t)i * 64 * 1024 * 1024;
            uintptr_t hint_below = target_addr - (uintptr_t)i * 64 * 1024 * 1024;
            trampoline = mmap((void*)hint_above, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (trampoline == MAP_FAILED) {
                trampoline = mmap((void*)hint_below, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            }
        }
        // Fallback: anywhere (may fail if >2GB away - detected below)
        if (trampoline == MAP_FAILED) {
            trampoline = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
        if (!trampoline || trampoline == MAP_FAILED) {
            return ALLOC_FAIL;
        }

        // Verify trampoline is within ±2GB of target for rel32
        intptr_t dist = (intptr_t)trampoline - (intptr_t)target;
        if (dist > 0x7FFFFFFF || dist < -0x7FFFFFFF) {
            munmap(trampoline, 4096);
            return ALLOC_FAIL;
        }

        // Build trampoline: original bytes + jmp back
        uint8_t* tram = (uint8_t*)trampoline;
        memcpy(tram, original_bytes, copy_size);

        // Write jmp back instruction at end of trampoline
        // jmp rel32: 0xE9 offset(4 bytes)
        tram[copy_size] = 0xE9;
        int32_t jmp_offset = (int32_t)((uintptr_t)target + copy_size - (uintptr_t)(tram + copy_size + 5));
        memcpy(tram + copy_size + 1, &jmp_offset, 4);

        // Make target page writable
        uintptr_t page_start = AlignDown((uintptr_t)target);
        size_t page_size = GetPageSize();
        int prot_result = mprotect((void*)page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
        if (prot_result != 0) {
            munmap(trampoline, 4096);
            return MPROTECT_FAIL;
        }

        // Write jmp instruction at target
        // jmp rel32: 0xE9 offset(4 bytes)
        uint8_t* target_bytes = (uint8_t*)target;
        target_bytes[0] = 0xE9;
        int32_t target_jmp_offset = (int32_t)((uintptr_t)detour - (uintptr_t)target - 5);
        memcpy(target_bytes + 1, &target_jmp_offset, 4);

        // Pad rest with nop if space available
        for (size_t i = 5; i < copy_size; ++i) {
            target_bytes[i] = 0x90; // nop
        }

        // Memory barrier
        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        // Restore original protection
        mprotect((void*)page_start, page_size, PROT_READ | PROT_EXEC);

        // Store hook info
        hooks.push_back({
            target,
            detour,
            trampoline,
            {0},
            copy_size,
            true
        });

        // Copy original bytes to the hook structure
        memcpy(hooks.back().original_bytes, original_bytes, copy_size);

        if (original) {
            *original = trampoline;
        }

        return OK;
    }

    // Enable a hook
    static Status Enable(void* target) {
        std::lock_guard<std::mutex> lock(GetMutex());

        for (auto& h : GetHooks()) {
            if (h.target == target) {
                if (h.enabled) return ALREADY_HOOKED;

                // Re-apply jmp
                uintptr_t page_start = AlignDown((uintptr_t)target);
                mprotect((void*)page_start, GetPageSize(), PROT_READ | PROT_WRITE | PROT_EXEC);

                uint8_t* target_bytes = (uint8_t*)target;
                target_bytes[0] = 0xE9;
                int32_t offset = (int32_t)((uintptr_t)h.detour - (uintptr_t)target - 5);
                memcpy(target_bytes + 1, &offset, 4);

                __atomic_thread_fence(__ATOMIC_SEQ_CST);
                mprotect((void*)page_start, GetPageSize(), PROT_READ | PROT_EXEC);

                h.enabled = true;
                return OK;
            }
        }
        return INVALID_ARG;
    }

    // Disable a hook (restore original bytes)
    static Status Disable(void* target) {
        std::lock_guard<std::mutex> lock(GetMutex());

        for (auto& h : GetHooks()) {
            if (h.target == target) {
                if (!h.enabled) return OK; // Already disabled

                uintptr_t page_start = AlignDown((uintptr_t)target);
                mprotect((void*)page_start, GetPageSize(), PROT_READ | PROT_WRITE | PROT_EXEC);

                memcpy(target, h.original_bytes, h.original_size);

                __atomic_thread_fence(__ATOMIC_SEQ_CST);
                mprotect((void*)page_start, GetPageSize(), PROT_READ | PROT_EXEC);

                h.enabled = false;
                return OK;
            }
        }
        return INVALID_ARG;
    }

    // Disable all hooks
    static void DisableAll() {
        std::lock_guard<std::mutex> lock(GetMutex());

        for (auto& h : GetHooks()) {
            if (h.enabled) {
                uintptr_t page_start = AlignDown((uintptr_t)h.target);
                mprotect((void*)page_start, GetPageSize(), PROT_READ | PROT_WRITE | PROT_EXEC);
                memcpy(h.target, h.original_bytes, h.original_size);
                mprotect((void*)page_start, GetPageSize(), PROT_READ | PROT_EXEC);
                h.enabled = false;
            }
        }

        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    }

    // Cleanup all hooks
    static void Cleanup() {
        DisableAll();
        std::lock_guard<std::mutex> lock(GetMutex());
        auto& hooks = GetHooks();
        for (auto& h : hooks) {
            if (h.original) {
                munmap(h.original, 4096);
                h.original = nullptr;
            }
        }
        hooks.clear();
    }
};

// Public API
inline Status create(void* target, void* detour, void** original) {
    return InlineHook::Create(target, detour, original);
}

inline Status enable(void* target) {
    return InlineHook::Enable(target);
}

inline Status disable(void* target) {
    return InlineHook::Disable(target);
}

inline void disable_all() {
    InlineHook::DisableAll();
}

inline void cleanup() {
    InlineHook::Cleanup();
}

} // namespace lhook

// === MinHook Compatibility Shims ===
typedef int MH_STATUS;
#define MH_OK       0
#define MH_ALL_HOOKS (void*)-1

inline MH_STATUS MH_Initialize() {
    return MH_OK;
}

inline MH_STATUS MH_Uninitialize() {
    lhook::cleanup();
    return MH_OK;
}

inline MH_STATUS MH_CreateHook(void* pTarget, void* pDetour, void** ppOriginal) {
    return (MH_STATUS)lhook::create(pTarget, pDetour, ppOriginal);
}

inline MH_STATUS MH_EnableHook(void* pTarget) {
    if (pTarget == MH_ALL_HOOKS) {
        return MH_OK;
    }
    return (MH_STATUS)lhook::enable(pTarget);
}

inline MH_STATUS MH_DisableHook(void* pTarget) {
    if (pTarget == MH_ALL_HOOKS) {
        lhook::disable_all();
        return MH_OK;
    }
    return (MH_STATUS)lhook::disable(pTarget);
}

#endif // __linux__
