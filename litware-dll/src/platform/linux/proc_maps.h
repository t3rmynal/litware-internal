#pragma once

#ifdef __linux__

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace proc_maps {

struct ModuleInfo {
    uintptr_t base;
    size_t size;
};

class ModuleCache {
private:
    std::unordered_map<std::string, ModuleInfo> cache;
    mutable std::mutex mutex;

    std::string NormalizeName(const std::string& name) const {
        std::string normalized = name;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        return normalized;
    }

    bool ParseProcMaps() {
        std::ifstream maps("/proc/self/maps");
        if (!maps) return false;

        cache.clear();
        std::string line;

        while (std::getline(maps, line)) {
            // Format: address           perms offset  dev   inode pathname
            // 7fff0000-7fff1000 r-xp 00000000 00:00 0     /path/to/lib.so

            if (line.empty()) continue;

            uintptr_t start, end;
            char perms[5] = {0};
            char dev[6] = {0};
            unsigned long inode;
            char pathname[4096] = {0};

            int parsed = sscanf(line.c_str(), "%lx-%lx %4s %*s %5s %lu %4095s",
                                &start, &end, perms, dev, &inode, pathname);

            if (parsed < 5) continue;

            // Only care about executable/readable mappings
            if (perms[0] != 'r') continue;

            // Extract library name from pathname
            if (strlen(pathname) == 0 || pathname[0] != '/') continue;

            // Get just the filename
            std::string fullpath = pathname;
            size_t last_slash = fullpath.find_last_of('/');
            std::string libname = (last_slash != std::string::npos)
                                  ? fullpath.substr(last_slash + 1)
                                  : fullpath;

            // Normalize library name (remove version suffix if any)
            // e.g., "libclient.so" or "libclient.so.1" -> "libclient"
            std::string normalized = libname;
            size_t so_pos = normalized.find(".so");
            if (so_pos != std::string::npos) {
                normalized = normalized.substr(0, so_pos);
            }

            // Skip non-.so files
            if (libname.find(".so") == std::string::npos && pathname[0] != '[') continue;

            // Update cache: keep the lowest base address and track highest end
            auto it = cache.find(normalized);
            if (it == cache.end()) {
                cache[normalized] = {start, end - start};
            } else {
                // Multiple mappings for same lib: use lowest base
                if (start < it->second.base) {
                    size_t highest_end = it->second.base + it->second.size;
                    it->second.base = start;
                    it->second.size = highest_end - start;
                }
            }
        }

        return true;
    }

public:
    uintptr_t GetModuleBase(const std::string& soname) {
        std::lock_guard<std::mutex> lock(mutex);

        // Try up to 2 passes: cached first, then refresh if miss
        for (int pass = 0; pass < 2; ++pass) {
            if (cache.empty() || pass == 1) {
                ParseProcMaps();
            }

            // Try exact match first
            auto it = cache.find(soname);
            if (it != cache.end()) {
                return it->second.base;
            }

            // Try partial match (e.g., "libclient" matches "libclient.so.x")
            for (const auto& [name, info] : cache) {
                if (name.find(soname) != std::string::npos) {
                    return info.base;
                }
            }
        }

        return 0;
    }

    bool GetModuleInfo(const std::string& soname, uintptr_t& base, size_t& size) {
        std::lock_guard<std::mutex> lock(mutex);

        for (int pass = 0; pass < 2; ++pass) {
            if (cache.empty() || pass == 1) {
                ParseProcMaps();
            }

            auto it = cache.find(soname);
            if (it != cache.end()) {
                base = it->second.base;
                size = it->second.size;
                return true;
            }

            for (const auto& [name, info] : cache) {
                if (name.find(soname) != std::string::npos) {
                    base = info.base;
                    size = info.size;
                    return true;
                }
            }
        }

        return false;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        cache.clear();
    }
};

// Global cache instance (Meyer's singleton — one instance across all TUs)
inline ModuleCache& g_cache() {
    static ModuleCache instance;
    return instance;
}

// Public API
inline uintptr_t get_module_base(const char* soname) {
    if (!soname) return 0;
    return g_cache().GetModuleBase(soname);
}

inline bool get_module_info(const char* soname, uintptr_t& base, size_t& size) {
    if (!soname) return false;
    return g_cache().GetModuleInfo(soname, base, size);
}

// === Module Name Translation (Windows .dll → Linux .so) ===
static const char* translate_module_name(const char* win_name) {
    if (!win_name) return nullptr;

    // Map common Windows DLL names to Linux .so names
    if (strcmp(win_name, "client.dll") == 0)
        return "libclient";
    if (strcmp(win_name, "engine2.dll") == 0)
        return "libengine2";
    if (strcmp(win_name, "inputsystem.dll") == 0)
        return "libinputsystem";
    if (strcmp(win_name, "rendersystemdx11.dll") == 0)
        return "librendersystemvulkan";
    if (strcmp(win_name, "scenesystem.dll") == 0)
        return "libscenesystem";
    if (strcmp(win_name, "matchmaking.dll") == 0)
        return "libmatchmaking";
    if (strcmp(win_name, "soundsystem.dll") == 0)
        return "libsoundsystem";
    if (strcmp(win_name, "networksystem.dll") == 0)
        return "libnetworksystem";
    if (strcmp(win_name, "gameoverlayrenderer64.dll") == 0)
        return "gameoverlayrenderer";

    // For names that are already in .so format or unknown, try as-is
    return win_name;
}

} // namespace proc_maps

// === Compatibility shim: GetModuleHandleA ===
// Windows returns HMODULE (void*), Linux returns base address as uintptr_t cast to void*
inline void* GetModuleHandleA(const char* lpModuleName) {
    if (!lpModuleName) {
        // Special case: NULL = current process (cs2 executable)
        // Try to find the main executable in /proc/self/maps
        auto base = proc_maps::get_module_base("cs2");
        if (!base) {
            // Fallback: read /proc/self/exe and find it
            std::ifstream maps("/proc/self/maps");
            std::string line;
            while (std::getline(maps, line)) {
                if (line.find("/cs2") != std::string::npos ||
                    line.find("cs2 ") != std::string::npos) {
                    uintptr_t start;
                    if (sscanf(line.c_str(), "%lx", &start) == 1) {
                        return (void*)start;
                    }
                }
            }
        }
        return (void*)base;
    }

    // Translate Windows DLL name to Linux .so name
    const char* soname = proc_maps::translate_module_name(lpModuleName);
    auto base = proc_maps::get_module_base(soname);
    return (void*)base;
}

#endif // __linux__
