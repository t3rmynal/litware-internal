#pragma once
#include <cstdint>
#include <Windows.h>

// direct ptrs, no rpm. inject into cs2

namespace memory {

inline uintptr_t GetModule(const char* name) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(name));
}

template<typename T>
inline T Read(uintptr_t addr) {
    if (!addr) return T{};
    return *reinterpret_cast<const T*>(addr);
}

template<typename T>
inline bool Write(uintptr_t addr, const T& value) {
    if (!addr) return false;
    *reinterpret_cast<T*>(addr) = value;
    return true;
}

}
