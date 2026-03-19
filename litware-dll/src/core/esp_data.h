#pragma once
#include <cstdint>

struct ESPEntry {
    bool  valid;
    bool  visible;
    bool  flashed;
    bool  planting;
    bool  scoped;
    bool  spotted;
    bool  defusing;
    bool  hasBomb;
    bool  hasKits;
    int   ent_index;
    uintptr_t pawn;
    uintptr_t controller;
    float head_x, head_y;
    float head_fx, head_fy;
    float head_ox, head_oy, head_oz;  // ???????????
    float origin_x, origin_y, origin_z;
    float feet_x, feet_y;
    float box_l, box_t, box_r, box_b;
    int   health;
    int   team;
    float distance;   // ???????????
    float yaw;        // ???????????
    char  name[128];
};

constexpr int ESP_MAX_PLAYERS = 64;
inline ESPEntry  g_esp_players[ESP_MAX_PLAYERS];
inline int       g_esp_count      = 0;
inline int       g_esp_local_team = 0;
inline uintptr_t g_esp_local_pawn = 0;
inline int       g_esp_screen_w   = 1920;
inline int       g_esp_screen_h   = 1080;
