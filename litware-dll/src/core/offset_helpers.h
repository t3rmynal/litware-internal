#pragma once
#include "offsets.h"
#include <cstdint>
#ifdef _WIN32
#include <Windows.h>
#endif
#ifdef __linux__
#include "../platform/linux/proc_maps.h"
#endif

// вспомогательные адреса
namespace offset_helpers {

#ifdef _WIN32
inline uintptr_t client_dll_base() {
    return reinterpret_cast<uintptr_t>(::GetModuleHandleA("client.dll"));
}
inline uintptr_t engine2_dll_base() {
    return reinterpret_cast<uintptr_t>(::GetModuleHandleA("engine2.dll"));
}
inline uintptr_t inputsystem_dll_base() {
    return reinterpret_cast<uintptr_t>(::GetModuleHandleA("inputsystem.dll"));
}
inline uintptr_t matchmaking_dll_base() {
    return uintptr_t(::GetModuleHandleA("matchmaking.dll"));
}
inline uintptr_t soundsystem_dll_base() {
    return uintptr_t(::GetModuleHandleA("soundsystem.dll"));
}
#elif defined(__linux__)
inline uintptr_t client_dll_base() {
    return proc_maps::get_module_base("libclient");
}
inline uintptr_t engine2_dll_base() {
    return proc_maps::get_module_base("libengine2");
}
inline uintptr_t inputsystem_dll_base() {
    return proc_maps::get_module_base("libinputsystem");
}
inline uintptr_t matchmaking_dll_base() {
    return proc_maps::get_module_base("libmatchmaking");
}
inline uintptr_t soundsystem_dll_base() {
    return proc_maps::get_module_base("libsoundsystem");
}
#endif

inline uintptr_t addr_dw_planted_c4(uintptr_t client) {
    return client ? client + offsets::client::dwPlantedC4 : 0;
}

inline uintptr_t addr_dw_game_rules(uintptr_t client) {
    return client ? client + offsets::client::dwGameRules : 0;
}

inline uintptr_t addr_dw_weapon_c4(uintptr_t client) {
    return client ? client + offsets::client::dwWeaponC4 : 0;
}

inline uintptr_t addr_dw_sensitivity(uintptr_t client) {
    return client ? client + offsets::client::dwSensitivity : 0;
}

inline uintptr_t addr_dw_view_render(uintptr_t client) {
    return client ? client + offsets::client::dwViewRender : 0;
}

inline uintptr_t addr_dw_glow_manager(uintptr_t client) {
    return client ? client + offsets::client::dwGlowManager : 0;
}

inline uintptr_t addr_dw_prediction(uintptr_t client) {
    return client ? client + offsets::client::dwPrediction : 0;
}

inline uintptr_t addr_engine_build_number(uintptr_t engine2) {
    return engine2 ? engine2 + offsets::engine2::dwBuildNumber : 0;
}

inline uintptr_t addr_engine_network_game_client(uintptr_t engine2) {
    return engine2 ? engine2 + offsets::engine2::dwNetworkGameClient : 0;
}

}
