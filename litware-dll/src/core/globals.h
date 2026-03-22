#pragma once
#include <mutex>
#include <vector>
#include "entity.h"

// Forward declaration
struct CSPlayerPawn;

namespace globals {
    extern std::mutex espMutex;
    
    struct ESPPlayer {
        int index;
        uintptr_t pawn;
        uintptr_t controller;
        int team;
        int health;
        bool alive;
        bool spotted;
        Vec3 origin;
        Vec3 headPos;
        char name[32];
        float distance;
        
        bool planting;
        bool defusing;
        bool flashed;
        bool scoped;
        bool hasBomb;
        bool hasKits;
        
        // Weapon info
        uintptr_t activeWeapon;
    };

    extern std::vector<ESPPlayer> espPlayers;
    
    // Configs extracted from render_hook.cpp
    extern bool g_espEnabled;
    extern bool g_espOnlyVis;
    extern int g_espBoxStyle;
    extern float g_espBoxThick;
    extern float g_espEnemyCol[4];
    extern float g_espTeamCol[4];
    extern bool g_espShowTeam;
    extern bool g_espName;
    extern float g_espNameSize;
    extern bool g_espHealth;
    extern bool g_espHealthText;
    extern int g_espHealthPos;
    extern int g_espHealthStyle;
    extern float g_espHealthGradientCol1[4];
    extern float g_espHealthGradientCol2[4];
    extern bool g_espDist;
    extern float g_espMaxDist;
    extern bool g_espSkeleton;
    extern bool g_espBoneBox;
    extern bool g_espLines;
    extern int g_espLineAnchor;
    extern bool g_espOof;
    extern float g_espOofSize;
    extern float g_skeletonThick;
    extern bool g_espHeadDot;
    extern bool g_espSpotted;
    extern bool g_visCheckEnabled;
    extern bool g_espWeapon;
    extern bool g_espWeaponIcon;
    extern bool g_espAmmo;
    extern int g_espAmmoStyle;
    extern float g_espAmmoCol1[4];
    extern float g_espAmmoCol2[4];
    extern bool g_espAvatar;
    extern bool g_espMoney;
    extern int g_espMoneyPos;
    extern float g_espHeadForward;
    extern float g_espScale;
    extern int g_espPreviewPos;
    extern bool g_noFlash;
    extern bool g_noSmoke;
    extern bool g_noCrosshair;
    extern bool g_noLegs;
    extern bool g_glowEnabled;
    extern float g_glowEnemyCol[4];
    extern float g_glowTeamCol[4];
    extern float g_glowAlpha;
    extern bool g_chamsEnabled;
    extern bool g_chamsEnemyOnly;
    extern bool g_chamsIgnoreZ;
    extern int g_chamsMaterial;
    extern float g_chamsEnemyCol[4];
    extern float g_chamsTeamCol[4];
    extern float g_chamsIgnoreZCol[4];
    extern bool g_chamsScene;
    extern bool g_weaponChamsEnabled;
    extern float g_weaponChamsCol[4];
    extern bool g_aimbotEnabled;
    extern int g_aimbotKey;
    extern float g_aimbotFov;
    extern float g_aimbotSmooth;
    extern bool g_fovCircleEnabled;
    extern float g_fovCircleCol[4];
    extern bool g_aimbotTeamChk;
    extern int g_aimbotWeaponFilter;
    extern bool g_rcsEnabled;
    extern float g_rcsX;
    extern float g_rcsY;
    extern float g_rcsSmooth;
    extern bool g_tbEnabled;
    extern int g_tbKey;
    extern int g_tbDelay;
    extern bool g_tbTeamChk;
    extern bool g_dtEnabled;
    extern int g_dtKey;
    extern bool g_bhopEnabled;
    extern bool g_strafeEnabled;
    extern int g_strafeKey;
    extern bool g_antiAimEnabled;
    extern int g_antiAimType;
    extern float g_antiAimSpeed;
    extern bool g_fovEnabled;
    extern float g_fovValue;
    extern bool g_autostopEnabled;
    extern bool g_snowEnabled;
    extern int g_snowDensity;
    extern bool g_sakuraEnabled;
    extern float g_sakuraCol[4];
    extern bool g_starsEnabled;
    extern bool g_particlesWorld;
    extern float g_particlesWorldRadius;
    extern float g_particlesWorldHeight;
    extern float g_particlesWorldFloor;
    extern float g_particlesWind;
    extern float g_particlesDepthFade;
    extern bool g_handsColorEnabled;
    extern float g_handsColor[4];
    extern bool g_skyColorEnabled;
    extern float g_skyColor[4];
    extern bool g_watermarkEnabled;
    extern bool g_showFpsWatermark;
    extern bool g_spectatorListEnabled;
    extern bool g_hitNotifEnabled;
    extern bool g_killNotifEnabled;
    extern bool g_hitSoundEnabled;
    extern bool g_hitmarkerEnabled;
    extern float g_hitmarkerDuration;
    extern int g_hitmarkerStyle;
    extern bool g_killEffectEnabled;
    extern float g_killEffectDuration;
    extern int g_hitEffectType;
    extern int g_killEffectType;
    extern float g_hitEffectCol[4];
    extern float g_killEffectCol[4];
    extern int g_hitSoundType;
    extern bool g_radarEnabled;
    extern bool g_radarIngame;
    extern float g_radarRange;
    extern float g_radarSize;
    extern bool g_bombTimerEnabled;
    extern bool g_bulletTraceEnabled;
    extern float g_impactCol[4];
    extern bool g_soundEnabled;
    extern float g_soundPuddleScale;
    extern float g_soundPuddleAlpha;
    extern bool g_soundBlipEnemy;
    extern bool g_soundBlipTeam;
    extern float g_soundBlipCol[4];
    extern float g_accentColor[4];
    extern float g_menuOpacity;
    extern float g_uiScale;
    extern int g_menuTheme;
    extern float g_themeTransition;
    extern float g_menuAnimSpeed;
    
    extern bool g_inventoryNameTagEnabled;
    extern char g_inventoryNameTag[32];
    extern bool g_showDebugConsole;
    extern std::vector<std::string> g_debugLogs;
}
