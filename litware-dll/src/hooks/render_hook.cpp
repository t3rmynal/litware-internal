#include "render_hook.h"
#include "Fonts.h"
#include "res/font.h"
#include "../core/offsets.h"
#include "../core/entity.h"
#include "../core/world_to_screen.h"
#include "../core/esp_data.h"
#include "../debug.h"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <Windows.h>
#include <Psapi.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>
#include <chrono>
#include <random>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "winmm.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
extern HMODULE g_thisModule;

namespace {

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
PresentFn  g_originalPresent = nullptr;
WNDPROC    g_origWndProc = nullptr;

using DrawSceneObjectFn = void(__fastcall*)(void*, void*, void*, int, int, void*, void*, void*);
using DrawSkyboxArrayFn = void(__fastcall*)(void*, void*, void*, int, void*, void*, void*);
using GetWorldFovFn = float(__fastcall*)(void*);
using FirstPersonLegsFn = void*(__fastcall*)(void*, void*, void*, void*, void*);
using CalcWorldSpaceBonesFn = void(__fastcall*)(void*, uint32_t);

static DrawSceneObjectFn g_origDrawSceneObject = nullptr;
static DrawSkyboxArrayFn g_origDrawSkyboxArray = nullptr;
static GetWorldFovFn g_origGetWorldFov = nullptr;
static FirstPersonLegsFn g_origFirstPersonLegs = nullptr;
static bool g_sceneHooksReady = false;
static bool g_clientHooksReady = false;
static bool g_safeMode = false;
static bool g_drawSceneHooked = false;
static bool g_drawSkyHooked = false;
static bool g_worldFovHooked = false;
static bool g_fpLegsHooked = false;
static DWORD g_lastSceneHookAttempt = 0;
static DWORD g_lastClientHookAttempt = 0;
static DWORD g_lastSceneHookNotify = 0;
static DWORD g_lastClientHookNotify = 0;
static DWORD g_lastBoneResolve = 0;
static CalcWorldSpaceBonesFn g_calcWorldSpaceBones = nullptr;

static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_bbWidth = 0;
static UINT g_bbHeight = 0;
static DXGI_FORMAT g_bbFormat = DXGI_FORMAT_UNKNOWN;
static DWORD g_lastRtvFail = 0;
static bool g_imguiInitialized = false;
static bool g_menuOpen = false;
static std::atomic_bool g_unloading{false};
static std::atomic_bool g_cleanupDone{false};
static std::atomic_bool g_pendingUnload{false};
static HWND g_gameHwnd = nullptr;
static uintptr_t g_client = 0;
static uintptr_t g_engine2 = 0;
static Vec3 g_localOrigin{};

static bool g_espEnabled = true;
static bool g_espOnlyVis = false;
static int g_espBoxStyle = 0;
static float g_espBoxThick = 1.5f;
static float g_espEnemyCol[4]{1.f,0.25f,0.25f,1.f};
static float g_espTeamCol[4]{0.25f,0.55f,1.f,1.f};
static bool g_espName = true;
static float g_espNameSize = 13.5f;
static bool g_espHealth = true;
static bool g_espHealthText = true;   // HP number (Weave style)
static int g_espHealthPos = 0;
static int g_espHealthStyle = 0;
static float g_espHealthGradientCol1[4]{0.2f,0.92f,0.51f,1.f};  // green (full)
static float g_espHealthGradientCol2[4]{1.f,0.27f,0.27f,1.f};   // red (empty)
static bool g_espDist = true;
static float g_espMaxDist = 100.f;
static bool g_espSkeleton = false;
static bool g_espLines = false;
static int g_espLineAnchor = 1;  // 0=Top 1=Middle 2=Bottom
static bool g_espOof = false;       // Offscreen arrows (from Pidoraise/Weave)
static float g_espOofSize = 24.f;
static float g_skeletonThick = 1.1f;
struct OofEntry { float x, y; float angle; ImU32 col; };  // angle: arrow points toward player
static OofEntry g_esp_oof[32];
static int g_esp_oof_count = 0;
static bool g_espHeadDot = true;
static bool g_espSpotted = true;
static bool g_visCheckEnabled = true;
static bool g_espWeapon = true;
static bool g_espWeaponIcon = true;
static bool g_espAmmo = true;
static int g_espAmmoStyle = 0;
static float g_espAmmoCol1[4]{0.02f,0.02f,0.04f,1.f};  // black/dark
static float g_espAmmoCol2[4]{0.35f,0.55f,1.f,1.f};   // blue
static bool g_espMoney = true;
static int g_espMoneyPos = 0;  // 0=below box 1=right side
static float g_espHeadForward = 6.f;
static float g_espScale = 1.0f;
static int g_espPreviewPos = 0;  // 0=Right 1=Left 2=Top 3=Bottom
static bool g_noFlash = false;
static bool g_noSmoke = false;
static bool g_noCrosshair = false;
static bool g_noLegs = false;
static DWORD g_lastNoSmokeTick = 0;
static bool g_glowEnabled = false;
static float g_glowEnemyCol[4]{1.f,0.18f,0.18f,1.f};
static float g_glowTeamCol[4]{0.18f,0.5f,1.f,1.f};
static float g_glowAlpha = 1.0f;
static bool g_chamsEnabled = false;
static bool g_chamsEnemyOnly = true;
static bool g_chamsIgnoreZ = false;  // invisible/occluded chams (wall color)
static int g_chamsMaterial = 0;      // 0=white 1=illuminate 2=latex 3=glow 4=glow2 5=metallic
static float g_chamsEnemyCol[4]{1.f,0.2f,0.2f,1.f};
static float g_chamsTeamCol[4]{0.2f,0.5f,1.f,1.f};
static float g_chamsIgnoreZCol[4]{0.4f,0.9f,0.5f,0.8f};
static bool g_chamsScene = true;     // scene-object chams (model tint) via draw hook
static bool g_weaponChamsEnabled = false;
static float g_weaponChamsCol[4]{1.f,0.88f,0.35f,1.f};
static bool g_aimbotEnabled = false;
static int g_aimbotKey = VK_LBUTTON;  // LMB default
static float g_aimbotFov = 5.f;
static float g_aimbotSmooth = 6.f;
static bool g_fovCircleEnabled = false;
static bool g_aimbotTeamChk = true;
static int g_aimbotBone = 0;
static int g_aimbotWeaponFilter = 0;  // 0=All 1=Rifles 2=Snipers 3=Pistols
static bool g_rcsEnabled = false;
static float g_rcsX = 1.0f;
static float g_rcsY = 1.0f;
static float g_rcsSmooth = 6.0f;
static float g_rcsPrevPunchX = 0.f, g_rcsPrevPunchY = 0.f;
static bool g_tbEnabled = false;
static int g_tbKey = 0;
static int g_tbDelay = 50;
static bool g_tbTeamChk = true;
static DWORD g_tbFireTime = 0;
static bool g_tbShouldFire = false;
static bool g_tbJustFired = false;  // Release attack on next frame after firing
static bool g_dtEnabled = false;
static int g_dtKey = 0;
static bool g_bhopEnabled = false;
static bool g_strafeEnabled = false;
static int g_strafeKey = 0;
static bool g_antiAimEnabled = false;
static int g_antiAimType = 0;  // 0=spin, 1=desync, 2=jitter
static float g_antiAimSpeed = 180.f;
static bool g_fovEnabled = false;
static float g_fovValue = 90.f;
static bool g_thirdPerson = false;
static bool g_autostopEnabled = false;  // stop when shooting for accuracy
static float g_tpDist = 120.f;
static float g_tpHeightOffset = 30.f;
static bool g_snowEnabled = false;
static int g_snowDensity = 1;
static bool g_sakuraEnabled = false;
static float g_sakuraCol[4]{1.f,0.55f,0.7f,0.85f};
static bool g_starsEnabled = false;
static bool g_particlesWorld = true;
static float g_particlesWorldRadius = 600.f;
static float g_particlesWorldHeight = 320.f;
static float g_particlesWorldFloor = 40.f;
static float g_particlesWind = 18.f;
static float g_particlesDepthFade = 0.0022f;
static bool g_handsColorEnabled = false;
static float g_handsColor[4]{0.9f,0.9f,0.95f,1.f};
static bool g_skyColorEnabled = false;
static float g_skyColor[4]{0.4f,0.5f,0.8f,1.f};
static bool g_watermarkEnabled = true;
static bool g_showFpsWatermark = true;
static bool g_spectatorListEnabled = true;
static bool g_hitNotifEnabled = true;
static bool g_killNotifEnabled = true;
static bool g_hitSoundEnabled = false;
static bool g_hitmarkerEnabled = false;
static float g_hitmarkerDuration = 0.4f;
static int g_hitmarkerStyle = 0;  // 0=cross 1=X
static DWORD g_lastHitmarkerTime = 0;
static bool g_killEffectEnabled = false;
static float g_killEffectDuration = 0.6f;
static DWORD g_lastKillEffectTime = 0;
static int g_hitEffectType = 0;   // 0=none 1=cross 2=screen flash 3=circle
static int g_killEffectType = 1;  // 0=none 1=burst 2=KILL text
static float g_hitEffectCol[4]{1.f,0.9f,0.2f,0.9f};
static float g_killEffectCol[4]{1.f,0.3f,0.3f,0.95f};
static int g_hitSoundType = 1;
static bool g_radarEnabled = true;  // Overlay radar window (separate from in-game minimap)
static bool g_radarIngame = true;   // Force spot all for in-game minimap (overlay radar removed)
static float g_radarRange = 2000.f;
static float g_radarSize = 180.f;
static bool g_bombTimerEnabled = true;
static bool g_bulletTraceEnabled = true;
static float g_impactCol[4]{0.35f,0.94f,0.47f,0.78f};
static bool g_soundEnabled = true;
static float g_soundPuddleScale = 1.0f;
static float g_soundPuddleAlpha = 1.0f;
static bool g_soundBlipEnemy = true;
static bool g_soundBlipTeam = false;
static float g_soundBlipCol[4]{1.f, 0.f, 0.f, 1.f};  // red default
static bool g_backtrackEnabled = false;
static bool g_backtrackVisual = false;
static int g_backtrackMs = 200;
struct BacktrackRecord{Vec3 head;Vec3 pelvis;Vec3 chest;DWORD time;};
static std::deque<BacktrackRecord> g_backtrack[ESP_MAX_PLAYERS+1];
static float g_accentColor[4]{0.1f,0.55f,1.0f,1.0f};
static float g_menuOpacity = 0.96f;
static float g_uiScale = 1.00f;  // Default UI scale
static int g_activeTab = 0;  // 0-4: Aimbot, Visuals, World, Skins, Misc
static float g_tabAnim[8] = {};
static float g_toggleAnim[128] = {};
static float g_tabIndicatorY = 0.f;
static int g_toggleIdx = 0;
static bool g_keybindsEnabled = true;
static bool g_keybindsWindowOpen = false;
static int g_menuTheme = 0; // 0=Dark Pro, 1=Glassmorphism, 2=Neumorphism
static float g_themeTransition = 0.f;
static float g_menuAnim = 0.f;
static float g_menuAnimSpeed = 12.f;
static DWORD g_telegramNoteStart = 0;  // Telegram notice: 5s on load

struct ThemeColors {
    ImU32 bg, surf, surf2, border;
    ImU32 text, textDim;
    ImU32 toggleBg, toggleFill;
    ImU32 headerBg;
    float round;
};

static ThemeColors GetThemeColors() {
    ThemeColors c{};
    float acc_r = g_accentColor[0], acc_g = g_accentColor[1], acc_b = g_accentColor[2];
    float pulseIntensity = 0.8f + sinf((float)ImGui::GetTime() * 1.5f) * 0.2f;

    if(g_menuTheme == 1) {
        c.bg = IM_COL32(12, 12, 22, 210);
        c.surf = IM_COL32(22, 22, 38, 195);
        c.surf2 = IM_COL32(32, 32, 52, 185);
        c.border = IM_COL32((int)(acc_r*220*pulseIntensity),(int)(acc_g*220*pulseIntensity),(int)(acc_b*220*pulseIntensity),150);
        c.text = IM_COL32(245, 248, 252, 255);
        c.textDim = IM_COL32(165, 175, 195, 210);
        c.toggleBg = IM_COL32(28, 28, 48, 160);
        c.toggleFill = IM_COL32((int)(acc_r*255),(int)(acc_g*255),(int)(acc_b*255),240);
        c.headerBg = IM_COL32(22, 22, 38, 220);
        c.round = 18.f;
    } else if(g_menuTheme == 2) {
        c.bg = IM_COL32(48, 53, 63, 255);
        c.surf = IM_COL32(58, 63, 73, 255);
        c.surf2 = IM_COL32(53, 58, 68, 255);
        c.border = IM_COL32(35, 40, 50, 120);
        c.text = IM_COL32(215, 225, 235, 255);
        c.textDim = IM_COL32(135, 145, 165, 210);
        c.toggleBg = IM_COL32(53, 58, 68, 255);
        c.toggleFill = IM_COL32((int)(acc_r*230),(int)(acc_g*230),(int)(acc_b*230),220);
        c.headerBg = IM_COL32(48, 53, 63, 255);
        c.round = 14.f;
    } else {
        c.bg = IM_COL32(10, 10, 14, 255);
        c.surf = IM_COL32(18, 18, 26, 255);
        c.surf2 = IM_COL32(24, 24, 34, 255);
        c.border = IM_COL32((int)(50+acc_r*20), (int)(50+acc_g*20), (int)(70+acc_b*20), 220);
        c.text = IM_COL32(225, 225, 235, 255);
        c.textDim = IM_COL32(145, 145, 165, 255);
        c.toggleBg = IM_COL32(33, 33, 48, 220);
        c.toggleFill = IM_COL32((int)(acc_r*255),(int)(acc_g*255),(int)(acc_b*255),255);
        c.headerBg = IM_COL32(18, 18, 26, 255);
        c.round = 10.f;
    }
    return c;
}

struct PidoraisePalette {
    ImU32 bgFill;
    ImU32 bgStroke;
    ImU32 tabBg;
    ImU32 tabActive;
    ImU32 elemBg;
    ImU32 elemStroke;
    ImU32 text;
    ImU32 textDim;
    ImU32 textActive;
    ImU32 accent;
};

static PidoraisePalette g_pido;

static inline float Clampf(float v, float lo, float hi);
template<typename T> static inline T Rd(uintptr_t addr);
static void DrawFilledEllipse(ImDrawList* dl, const ImVec2& center, float rx, float ry, ImU32 col, int segments);
static void DrawRotatedQuad(ImDrawList* dl, ImVec2 center, float w, float h, float angle, ImU32 col);

static void UpdatePidoraisePalette(float fade = 1.f) {
    float opacity = Clampf(g_menuOpacity, 0.f, 1.f) * Clampf(fade, 0.f, 1.f);
    int bgA = (int)(opacity * 255.f);
    int elemA = (int)(opacity * 235.f);
    int tabA = (int)(opacity * 245.f);
    int textA = (int)(Clampf(fade, 0.f, 1.f) * 255.f);
    int accA = (int)(Clampf(fade, 0.f, 1.f) * 255.f);
    g_pido.accent = IM_COL32((int)(g_accentColor[0]*255.f),(int)(g_accentColor[1]*255.f),(int)(g_accentColor[2]*255.f),accA);

    if(g_menuTheme == 1){ // Glassmorphism
        g_pido.bgFill = IM_COL32(12,14,22,bgA);
        g_pido.bgStroke = IM_COL32((int)(g_accentColor[0]*220.f),(int)(g_accentColor[1]*220.f),(int)(g_accentColor[2]*220.f),(int)(140.f*fade));
        g_pido.tabBg = IM_COL32(16,18,28,tabA);
        g_pido.tabActive = IM_COL32(22,24,36,(int)(220.f*fade));
        g_pido.elemBg = IM_COL32(20,22,32,elemA);
        g_pido.elemStroke = IM_COL32(40,44,58,(int)(120.f*fade));
        g_pido.text = IM_COL32(235,242,252,textA);
        g_pido.textDim = IM_COL32(160,170,190,(int)(textA*0.8f));
        g_pido.textActive = IM_COL32(255,255,255,textA);
    }else if(g_menuTheme == 2){ // Neumorphism
        g_pido.bgFill = IM_COL32(50,56,66,(int)(opacity*255.f));
        g_pido.bgStroke = IM_COL32(35,40,50,(int)(160.f*fade));
        g_pido.tabBg = IM_COL32(54,60,70,(int)(opacity*255.f));
        g_pido.tabActive = IM_COL32(60,66,78,(int)(opacity*255.f));
        g_pido.elemBg = IM_COL32(56,62,72,(int)(opacity*255.f));
        g_pido.elemStroke = IM_COL32(30,34,42,(int)(120.f*fade));
        g_pido.text = IM_COL32(215,225,235,textA);
        g_pido.textDim = IM_COL32(140,150,170,(int)(textA*0.8f));
        g_pido.textActive = IM_COL32(235,240,250,textA);
    }else{ // Dark Pro
        g_pido.bgFill = IM_COL32(11,11,11,bgA);
        g_pido.bgStroke = IM_COL32(24,26,36,(int)(220.f*fade));
        g_pido.tabBg = IM_COL32(14,14,15,tabA);
        g_pido.tabActive = IM_COL32(20,20,21,(int)(255.f*fade));
        g_pido.elemBg = IM_COL32(11,13,15,elemA);
        g_pido.elemStroke = IM_COL32(28,26,37,(int)(220.f*fade));
        g_pido.text = IM_COL32(205,210,220,textA);
        g_pido.textDim = IM_COL32(125,125,135,(int)(textA*0.85f));
        g_pido.textActive = IM_COL32(255,255,255,textA);
    }
}

static char g_configName[64] = "default";
static int g_configSelected = -1;
static std::vector<std::string> g_configList;

static int g_lastHealth[ESP_MAX_PLAYERS + 1] = {};
static bool g_seenThisFrame[ESP_MAX_PLAYERS + 1] = {};

struct LogEntry{char text[256];ImU32 color;float lifetime,maxlife;int type;};  // type: 0=hit 1=kill
static std::deque<LogEntry>g_logs;
static DWORD g_lastSoundPingTick[ESP_MAX_PLAYERS + 1] = {};
static bool g_visMap[ESP_MAX_PLAYERS + 1] = {};

// Math
static inline float Clampf(float v, float lo, float hi){return v<lo?lo:(v>hi?hi:v);}
static inline ImVec4 LerpV4(ImVec4 a, ImVec4 b, float t){
    return ImVec4(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t);
}
static inline float LerpF(float a,float b,float t){return a+(b-a)*t;}
static inline float AngleDiff(float a,float b){float d=fmodf(a-b+540.f,360.f)-180.f;return d;}
static inline float MenuScale(){return Clampf(g_uiScale, 0.85f, 1.6f);}
static inline ImU32 WithAlpha(ImU32 col, float a){
    int r = (col >> IM_COL32_R_SHIFT) & 0xFF;
    int g = (col >> IM_COL32_G_SHIFT) & 0xFF;
    int b = (col >> IM_COL32_B_SHIFT) & 0xFF;
    int oa = (col >> IM_COL32_A_SHIFT) & 0xFF;
    int na = (int)Clampf((float)oa * a, 0.f, 255.f);
    return IM_COL32(r,g,b,na);
}

static inline uintptr_t ViewAnglesAddr(){
    if(!g_client) return 0;
    uintptr_t input = Rd<uintptr_t>(g_client + offsets::client::dwCSGOInput);
    if(input && input > 0x10000 && input < 0x7FFFFFFFFFFF){
        std::ptrdiff_t vaOff = (std::ptrdiff_t)offsets::client::dwViewAngles - (std::ptrdiff_t)offsets::client::dwCSGOInput;
        return input + vaOff;
    }
    return g_client + offsets::client::dwViewAngles;
}

static Vec2 CalcAngle(Vec3 from,Vec3 to){
    Vec3 d=to-from;
    float len2d=d.length_2d();
    return Vec2{
        -(float)(atan2(d.z,len2d)*(180.f/3.14159265f)),
        (float)(atan2(d.y,d.x)*(180.f/3.14159265f))
    };
}

static ImU32 HealthCol(int hp);
static void DrawCornerBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick);
static void PushNotification(const char*text,ImU32 color);
static void PushLog(const char* text, ImU32 color);
static void PlayHitSound(int type);
static void EnsureSceneHooks();
static void EnsureClientHooks();
static void* PatternScan(HMODULE mod,const char*pat,const char*mask);
static void EnsureCalcWorldSpaceBones();
static void UpdatePawnBones(uintptr_t pawn);

struct MaterialColor {
    uint8_t r,g,b,a;
};
static MaterialColor MakeMatColor(const float col[4]){
    MaterialColor c{};
    c.r=(uint8_t)(Clampf(col[0],0.f,1.f)*255.f);
    c.g=(uint8_t)(Clampf(col[1],0.f,1.f)*255.f);
    c.b=(uint8_t)(Clampf(col[2],0.f,1.f)*255.f);
    c.a=(uint8_t)(Clampf(col[3],0.f,1.f)*255.f);
    return c;
}
static void ApplyChamsMaterial(float col[4]){
    switch(g_chamsMaterial){
        case 1: { // Illuminate
            col[0] = Clampf(col[0]*1.25f + 0.08f, 0.f, 1.f);
            col[1] = Clampf(col[1]*1.25f + 0.08f, 0.f, 1.f);
            col[2] = Clampf(col[2]*1.25f + 0.08f, 0.f, 1.f);
            col[3] = Clampf(col[3]*1.1f, 0.f, 1.f);
        } break;
        case 2: { // Latex (softer)
            col[0] = LerpF(col[0], 0.85f, 0.2f);
            col[1] = LerpF(col[1], 0.85f, 0.2f);
            col[2] = LerpF(col[2], 0.85f, 0.2f);
            col[3] = Clampf(col[3]*0.9f, 0.f, 1.f);
        } break;
        case 3: { // Glow
            col[0] = Clampf(col[0]*1.1f, 0.f, 1.f);
            col[1] = Clampf(col[1]*1.1f, 0.f, 1.f);
            col[2] = Clampf(col[2]*1.1f, 0.f, 1.f);
            col[3] = Clampf(col[3]*1.25f + 0.15f, 0.f, 1.f);
        } break;
        case 4: { // Glow2 (pulsing)
            float pulse = 0.75f + 0.25f * sinf((float)ImGui::GetTime() * 3.0f);
            col[0] = Clampf(col[0]*pulse + 0.08f, 0.f, 1.f);
            col[1] = Clampf(col[1]*pulse + 0.08f, 0.f, 1.f);
            col[2] = Clampf(col[2]*pulse + 0.08f, 0.f, 1.f);
            col[3] = Clampf(col[3]*1.15f, 0.f, 1.f);
        } break;
        case 5: { // Metallic (desaturate)
            float lum = (col[0] + col[1] + col[2]) / 3.f;
            col[0] = LerpF(col[0], lum, 0.45f);
            col[1] = LerpF(col[1], lum, 0.45f);
            col[2] = LerpF(col[2], lum, 0.45f);
            col[3] = Clampf(col[3]*0.95f, 0.f, 1.f);
        } break;
        default: break; // 0=white passthrough
    }
}
static constexpr size_t kSceneObjectStride = 0x68;
static constexpr size_t kSceneObjectColorOffset = 0x40;
static constexpr size_t kSceneObjectMaterialOffset = 0x18;
static constexpr size_t kSceneObjectInfoOffset = 0x48;
static constexpr size_t kSceneObjectInfoIdOffset = 0xB0;

static inline bool IsLikelyPtr(uintptr_t p){
    return (p > 0x10000 && p < 0x00007FFFFFFFFFFF);
}

static const char* SafeMaterialName(uintptr_t mat){
    if(!IsLikelyPtr(mat)) return nullptr;
    __try{
        void** vtbl = *reinterpret_cast<void***>(mat);
        if(!IsLikelyPtr((uintptr_t)vtbl)) return nullptr;
        using Fn = const char*(__fastcall*)(void*);
        Fn fn = reinterpret_cast<Fn>(vtbl[0]); // get_name
        return fn ? fn(reinterpret_cast<void*>(mat)) : nullptr;
    }__except(EXCEPTION_EXECUTE_HANDLER){
        return nullptr;
    }
}

// Memory
template<typename T>
static inline T Rd(uintptr_t addr){
    if(!addr)return T{};__try{return *reinterpret_cast<const T*>(addr);}
    __except(EXCEPTION_EXECUTE_HANDLER){return T{};}
}
template<typename T>
static inline void Wr(uintptr_t addr,T val){
    if(!addr)return;__try{*reinterpret_cast<T*>(addr)=val;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
static void RdStr(uintptr_t addr,char*buf,size_t maxlen){
    if(!addr||!buf||!maxlen){if(buf&&maxlen)buf[0]=0;return;}
    __try{const char*p=reinterpret_cast<const char*>(addr);size_t i=0;
    for(;i<maxlen-1&&p[i];++i)buf[i]=p[i];buf[i]=0;}
    __except(EXCEPTION_EXECUTE_HANDLER){buf[0]=0;}
}

// CS2 bone: 32 bytes (alignas 16), pos at 0
struct BoneData{
    Vec3 pos;
    float scale;
    char _pad[16];  // quat rotation + padding = 32 total
};
static constexpr size_t BONE_STRIDE = 32;

enum BoneIndex : int {
    BONE_PELVIS = 0,
    BONE_SPINE1 = 2,
    BONE_SPINE2 = 3,
    BONE_SPINE3 = 4,
    BONE_NECK   = 5,
    BONE_HEAD   = 6,
    BONE_ARM_UP_L = 8,
    BONE_ARM_LO_L = 9,
    BONE_HAND_L   = 10,
    BONE_ARM_UP_R = 13,
    BONE_ARM_LO_R = 14,
    BONE_HAND_R   = 15,
    BONE_LEG_UP_L = 22,
    BONE_LEG_LO_L = 23,
    BONE_ANKLE_L  = 24,
    BONE_LEG_UP_R = 25,
    BONE_LEG_LO_R = 26,
    BONE_ANKLE_R  = 27
};

static bool GetBonePos(uintptr_t pawn,int bone,Vec3& out){
    uintptr_t bones = 0;
    uintptr_t body = Rd<uintptr_t>(pawn + offsets::base_entity::m_CBodyComponent);
    if(body){
        uintptr_t skel = Rd<uintptr_t>(body + offsets::body_component::m_skeletonInstance);
        if(skel){
            uintptr_t modelState = skel + offsets::skeleton_instance::m_modelState;
            bones = Rd<uintptr_t>(modelState + offsets::model_state::m_pBones);
        }
    }
    // No fallback: CGameSceneNode != CSkeletonInstance, mixing offsets causes invalid reads/crash
    if(!bones) return false;
    uintptr_t boneAddr = bones + (uintptr_t)bone * BONE_STRIDE;
    BoneData bd = Rd<BoneData>(boneAddr);
    out = bd.pos;
    return true;
}

static void EnsureCalcWorldSpaceBones(){
    if(g_calcWorldSpaceBones) return;
    DWORD now = GetTickCount();
    if(now - g_lastBoneResolve < 2000) return;
    g_lastBoneResolve = now;
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    // Primary pattern
    static const char PAT_BONES[] = "\x40\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x81\xEC\xD0";
    static const char MSK_BONES[] = "xxxxxxxxxxxxxxxx";
    void* fn = PatternScan(client, PAT_BONES, MSK_BONES);
    // Fallback patterns for different CS2 builds
    if(!fn){
        static const char PAT2[] = "\x40\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x81\xEC\xE0";
        static const char MSK2[] = "xxxxxxxxxxxxxxxx";
        fn = PatternScan(client, PAT2, MSK2);
    }
    if(!fn){
        static const char PAT3[] = "\x40\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x81\xEC";
        static const char MSK3[] = "xxxxxxxxxxxxxxx";
        fn = PatternScan(client, PAT3, MSK3);
    }
    if(fn){
        g_calcWorldSpaceBones = reinterpret_cast<CalcWorldSpaceBonesFn>(fn);
        DebugLog("[LitWare] CalcWorldSpaceBones found");
        PushNotification("Bone system ready", IM_COL32(80,255,120,255));
    }else{
        DebugLog("[LitWare] CalcWorldSpaceBones NOT found - skeleton will be static");
        PushNotification("Bone pattern not found - skeleton static", IM_COL32(255,200,80,255));
    }
}

static void UpdatePawnBones(uintptr_t pawn){
    if(!pawn) return;
    EnsureCalcWorldSpaceBones();
    if(!g_calcWorldSpaceBones) return;
    uintptr_t body = Rd<uintptr_t>(pawn + offsets::base_entity::m_CBodyComponent);
    if(!body) return;
    uintptr_t skel = Rd<uintptr_t>(body + offsets::body_component::m_skeletonInstance);
    if(!skel) return;
    __try{ g_calcWorldSpaceBones(reinterpret_cast<void*>(skel), 0xFFFFF); }
    __except(EXCEPTION_EXECUTE_HANDLER){}
}

static bool LooksLikeUtf16LE(uintptr_t addr){
    __try{
        const uint8_t* p = reinterpret_cast<const uint8_t*>(addr);
        int zeros = 0;
        int checked = 0;
        for(int i=1;i<16;i+=2){
            if(p[i] == 0) zeros++;
            checked++;
        }
        return checked >= 4 && zeros >= 3;
    }__except(EXCEPTION_EXECUTE_HANDLER){
        return false;
    }
}

static void RdName(uintptr_t addr,char*buf,size_t maxlen){
    if(!addr||!buf||!maxlen){if(buf&&maxlen)buf[0]=0;return;}
    if(LooksLikeUtf16LE(addr)){
        wchar_t wbuf[96]{};
        __try{
            const wchar_t* wp = reinterpret_cast<const wchar_t*>(addr);
            size_t i=0;
            for(;i<sizeof(wbuf)/sizeof(wbuf[0])-1&&wp[i];++i)wbuf[i]=wp[i];
            wbuf[i]=0;
        }__except(EXCEPTION_EXECUTE_HANDLER){
            buf[0]=0;
            return;
        }
        int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, (int)maxlen, nullptr, nullptr);
        if(n > 0) return;
    }
    RdStr(addr, buf, maxlen);
}

static void EnsureModules(){
    if(!g_client)g_client=(uintptr_t)GetModuleHandleA("client.dll");
    if(!g_engine2)g_engine2=(uintptr_t)GetModuleHandleA("engine2.dll");
}

static float GetCurTime(){
    if(!g_client) return 0.f;
    uintptr_t gv = Rd<uintptr_t>(g_client + offsets::client::dwGlobalVars);
    if(!gv) return 0.f;
    return Rd<float>(gv + 0x30);  // CGlobalVars::m_curtime
}

static uintptr_t ResolveHandle(uintptr_t entityList, uint32_t handle){
    if(!entityList||!handle) return 0;
    uintptr_t chunk = Rd<uintptr_t>(entityList + 8*((handle&0x7FFF)>>9) + 16);
    if(!chunk) return 0;
    return Rd<uintptr_t>(chunk + 112*(handle&0x1FF));
}

static std::string ConfigDir(){
    char buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    if(len == 0 || len >= MAX_PATH) return ".";
    std::string dir = std::string(buf) + "\\litware";
    return dir;
}

static std::string ConfigPath(const char* name){
    std::string safe = name && name[0] ? name : "default";
    for(char& c : safe){if(c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='\"'||c=='<'||c=='>'||c=='|')c='_';}
    return ConfigDir() + "\\" + safe + ".cfg";
}

static void RefreshConfigList();

static void EnsureConfigDir(){
    std::error_code ec;
    std::filesystem::create_directories(ConfigDir(), ec);
    RefreshConfigList();
}

static void RefreshConfigList(){
    g_configList.clear();
    std::error_code ec;
    const std::string dir = ConfigDir();
    if(!std::filesystem::exists(dir, ec)) return;
    for(const auto& entry : std::filesystem::directory_iterator(dir, ec)){
        if(entry.is_regular_file()){
            auto path = entry.path();
            if(path.extension() == ".cfg"){
                g_configList.emplace_back(path.stem().string());
            }
        }
    }
    std::sort(g_configList.begin(), g_configList.end());
}

struct WeaponInfo{
    const char* name;
    const char* icon;
    int maxClip;
    ImWchar iconChar;  // CS2GunIcons: 0xE000 + weapon_id, 0 = use text fallback
};

static WeaponInfo WeaponInfoForId(int id){
    switch(id){
        case 1: return {"Deagle","DE",7,0xE001};
        case 2: return {"Dualies","DUAL",30,0xE002};
        case 3: return {"Five-SeveN","57",20,0xE003};
        case 4: return {"Glock","GLOCK",20,0xE004};
        case 7: return {"AK-47","AK",30,0xE007};
        case 8: return {"AUG","AUG",30,0xE008};
        case 9: return {"AWP","AWP",10,0xE009};
        case 10: return {"FAMAS","FAMAS",25,0xE00A};
        case 11: return {"G3SG1","G3",20,0xE00B};
        case 13: return {"Galil","GALIL",35,0xE00D};
        case 14: return {"M249","M249",100,0xE00E};
        case 16: return {"M4A4","M4",30,0xE010};
        case 17: return {"MAC-10","MAC10",30,0xE011};
        case 19: return {"P90","P90",50,0xE013};
        case 24: return {"UMP-45","UMP",25,0xE018};
        case 25: return {"XM1014","XM",7,0xE019};
        case 26: return {"Bizon","BIZON",64,0xE01A};
        case 27: return {"MAG-7","MAG7",5,0xE01B};
        case 28: return {"Negev","NEGEV",150,0xE01C};
        case 29: return {"Sawed-Off","SAWED",7,0xE01D};
        case 30: return {"Tec-9","TEC9",18,0xE01E};
        case 32: return {"P2000","P2K",13,0xE020};
        case 33: return {"MP7","MP7",30,0xE021};
        case 34: return {"MP9","MP9",30,0xE022};
        case 35: return {"Nova","NOVA",8,0xE023};
        case 36: return {"P250","P250",13,0xE024};
        case 38: return {"SCAR-20","SCAR",20,0xE026};
        case 39: return {"SG553","SG",30,0xE027};
        case 40: return {"SSG08","SSG",10,0xE028};
        case 42: return {"Knife","KN",0,0xE02A};
        case 59: return {"Knife","KN",0,0xE03B};
        case 60: return {"M4A1-S","M4S",20,0xE03C};
        case 61: return {"USP-S","USP",12,0xE03D};
        case 63: return {"CZ75","CZ75",12,0xE03F};
        case 64: return {"Revolver","R8",8,0xE040};
        case 80: return {"Zeus","ZEU",1,0xE050};
        case 81: return {"Flash","FL",0,0xE051};
        case 82: return {"HE","HE",0,0xE052};
        case 83: return {"Smoke","SM",0,0xE053};
        case 84: return {"Decoy","DEC",0,0xE054};
        case 86: return {"Molotov","MOL",0,0xE056};
        case 87: return {"Incendiary","INC",0,0xE057};
        case 88: return {"C4","C4",0,0xE058};
        default: return {"Weapon","WPN",0,(ImWchar)(id>=1&&id<=255?0xE000u+(unsigned)id:0)};
    }
}

static uintptr_t GetActiveWeapon(uintptr_t pawn, uintptr_t entityList){
    if(!pawn) return 0;
    uintptr_t ws = Rd<uintptr_t>(pawn + offsets::base_pawn::m_pWeaponServices);
    if(!ws) return 0;
    uint32_t hWeapon = Rd<uint32_t>(ws + offsets::weapon_services::m_hActiveWeapon);
    return ResolveHandle(entityList, hWeapon);
}

static int GetWeaponId(uintptr_t weapon){
    if(!weapon) return 0;
    uintptr_t attr = weapon + offsets::econ_entity::m_AttributeManager;
    uintptr_t item = attr + offsets::attribute_container::m_Item;
    return (int)Rd<uint16_t>(item + offsets::econ_item_view::m_iItemDefinitionIndex);
}

static int GetWeaponClip(uintptr_t weapon){
    if(!weapon) return 0;
    return Rd<int>(weapon + offsets::base_weapon::m_iClip1);
}

static int GetPlayerMoney(uintptr_t controller){
    if(!controller) return 0;
    uintptr_t ms = Rd<uintptr_t>(controller + offsets::controller::m_pInGameMoneyServices);
    if(!ms) return 0;
    return Rd<int>(ms + offsets::money_services::m_iAccount);
}

static void WriteBool(std::ofstream& out, const char* key, bool v){out<<key<<"="<<(v?1:0)<<"\n";}
static void WriteInt(std::ofstream& out, const char* key, int v){out<<key<<"="<<v<<"\n";}
static void WriteFloat(std::ofstream& out, const char* key, float v){out<<key<<"="<<v<<"\n";}
static void WriteColor(std::ofstream& out, const char* key, const float c[4]){
    out<<key<<"="<<c[0]<<","<<c[1]<<","<<c[2]<<","<<c[3]<<"\n";
}
static void WriteColor4(std::ofstream& out, const char* key, const float c[4]){
    WriteColor(out, key, c);
}

static bool ParseBool(const std::string& v){
    return v=="1"||v=="true"||v=="True"||v=="TRUE";
}

static bool ParseInt(const std::string& v, int& out){
    char* end=nullptr;
    long val = std::strtol(v.c_str(), &end, 10);
    if(end==v.c_str()) return false;
    out = (int)val;
    return true;
}

static bool ParseFloat(const std::string& v, float& out){
    char* end=nullptr;
    float val = std::strtof(v.c_str(), &end);
    if(end==v.c_str()) return false;
    out = val;
    return true;
}

static bool ParseColor4(const std::string& v, float out[4]){
    std::stringstream ss(v);
    char c1=0,c2=0,c3=0;
    if(!(ss>>out[0]))return false;
    if(!(ss>>c1))return false;
    if(!(ss>>out[1]))return false;
    if(!(ss>>c2))return false;
    if(!(ss>>out[2]))return false;
    if(!(ss>>c3))return false;
    if(!(ss>>out[3]))return false;
    return true;
}

// Config load helpers ? split to avoid MSVC "invalid nesting of blocks"
static bool LoadConfigKeyEsp(const std::string& key, const std::string& val, bool& ok){
    if(key=="esp_enabled"){ g_espEnabled=ParseBool(val); return true; }
    if(key=="esp_only_vis"){ g_espOnlyVis=ParseBool(val); return true; }
    if(key=="esp_box_style"){ int v; if(ParseInt(val,v)) g_espBoxStyle=v; else ok=false; return true; }
    if(key=="esp_box_thick"){ float v; if(ParseFloat(val,v)) g_espBoxThick=v; else ok=false; return true; }
    if(key=="esp_enemy_col"){ if(!ParseColor4(val,g_espEnemyCol)) ok=false; return true; }
    if(key=="esp_team_col"){ if(!ParseColor4(val,g_espTeamCol)) ok=false; return true; }
    if(key=="esp_name"){ g_espName=ParseBool(val); return true; }
    if(key=="esp_name_size"){ float v; if(ParseFloat(val,v)) g_espNameSize=v; else ok=false; return true; }
    if(key=="esp_scale"){ float v; if(ParseFloat(val,v)) g_espScale=v; else ok=false; return true; }
    if(key=="esp_preview_pos"){ int v; if(ParseInt(val,v)) g_espPreviewPos=v; else ok=false; return true; }
    if(key=="esp_health"){ g_espHealth=ParseBool(val); return true; }
    if(key=="esp_health_pos"){ int v; if(ParseInt(val,v)) g_espHealthPos=v; else ok=false; return true; }
    if(key=="esp_health_style"){ int v; if(ParseInt(val,v)) g_espHealthStyle=v; else ok=false; return true; }
    if(key=="health_grad_col1"){ if(!ParseColor4(val,g_espHealthGradientCol1)) ok=false; return true; }
    if(key=="health_grad_col2"){ if(!ParseColor4(val,g_espHealthGradientCol2)) ok=false; return true; }
    if(key=="esp_ammo_style"){ int v; if(ParseInt(val,v)) g_espAmmoStyle=v; else ok=false; return true; }
    if(key=="esp_ammo_col1"){ if(!ParseColor4(val,g_espAmmoCol1)) ok=false; return true; }
    if(key=="esp_ammo_col2"){ if(!ParseColor4(val,g_espAmmoCol2)) ok=false; return true; }
    if(key=="esp_dist"){ g_espDist=ParseBool(val); return true; }
    if(key=="esp_max_dist"){ float v; if(ParseFloat(val,v)) g_espMaxDist=v; else ok=false; return true; }
    if(key=="esp_skeleton"){ g_espSkeleton=ParseBool(val); return true; }
    if(key=="esp_lines"){ g_espLines=ParseBool(val); return true; }
    if(key=="esp_oof"){ g_espOof=ParseBool(val); return true; }
    if(key=="esp_oof_size"){ float v; if(ParseFloat(val,v)) g_espOofSize=v; else ok=false; return true; }
    if(key=="skeleton_thick"){ float v; if(ParseFloat(val,v)) g_skeletonThick=v; else ok=false; return true; }
    if(key=="esp_head_dot"){ g_espHeadDot=ParseBool(val); return true; }
    if(key=="esp_spotted"){ g_espSpotted=ParseBool(val); return true; }
    if(key=="esp_vis_check"){ g_visCheckEnabled=ParseBool(val); return true; }
    if(key=="esp_weapon"){ g_espWeapon=ParseBool(val); return true; }
    if(key=="esp_weapon_icon"){ g_espWeaponIcon=ParseBool(val); return true; }
    if(key=="esp_ammo"){ g_espAmmo=ParseBool(val); return true; }
    if(key=="esp_money"){ g_espMoney=ParseBool(val); return true; }
    if(key=="esp_head_forward"){ float v; if(ParseFloat(val,v)) g_espHeadForward=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeyChams(const std::string& key, const std::string& val, bool& ok){
    if(key=="no_flash"){ g_noFlash=ParseBool(val); return true; }
    if(key=="no_smoke"){ g_noSmoke=ParseBool(val); return true; }
    if(key=="no_crosshair"){ g_noCrosshair=ParseBool(val); return true; }
    if(key=="no_legs"){ g_noLegs=ParseBool(val); return true; }
    if(key=="glow_enabled"){ g_glowEnabled=ParseBool(val); return true; }
    if(key=="glow_enemy_col"){ if(!ParseColor4(val,g_glowEnemyCol)) ok=false; return true; }
    if(key=="glow_team_col"){ if(!ParseColor4(val,g_glowTeamCol)) ok=false; return true; }
    if(key=="glow_alpha"){ float v; if(ParseFloat(val,v)) g_glowAlpha=v; else ok=false; return true; }
    if(key=="chams_enabled"){ g_chamsEnabled=ParseBool(val); return true; }
    if(key=="chams_enemy_only"){ g_chamsEnemyOnly=ParseBool(val); return true; }
    if(key=="chams_ignorez"){ g_chamsIgnoreZ=ParseBool(val); return true; }
    if(key=="chams_material"){ int v; if(ParseInt(val,v)) g_chamsMaterial=v; else ok=false; return true; }
    if(key=="chams_enemy_col"){ if(!ParseColor4(val,g_chamsEnemyCol)) ok=false; return true; }
    if(key=="chams_team_col"){ if(!ParseColor4(val,g_chamsTeamCol)) ok=false; return true; }
    if(key=="chams_ignorez_col"){ if(!ParseColor4(val,g_chamsIgnoreZCol)) ok=false; return true; }
    if(key=="chams_scene"){ g_chamsScene=ParseBool(val); return true; }
    if(key=="weapon_chams"){ g_weaponChamsEnabled=ParseBool(val); return true; }
    if(key=="weapon_chams_col"){ if(!ParseColor4(val,g_weaponChamsCol)) ok=false; return true; }
    return false;
}
static bool LoadConfigKeyAimbot(const std::string& key, const std::string& val, bool& ok, bool& rcsXSet, bool& rcsYSet){
    if(key=="aimbot_enabled"){ g_aimbotEnabled=ParseBool(val); return true; }
    if(key=="aimbot_key"){ int v; if(ParseInt(val,v)) g_aimbotKey=v; else ok=false; return true; }
    if(key=="aimbot_fov"){ float v; if(ParseFloat(val,v)) g_aimbotFov=v; else ok=false; return true; }
    if(key=="aimbot_smooth"){ float v; if(ParseFloat(val,v)) g_aimbotSmooth=v; else ok=false; return true; }
    if(key=="fov_circle"){ g_fovCircleEnabled=ParseBool(val); return true; }
    if(key=="aimbot_team"){ g_aimbotTeamChk=ParseBool(val); return true; }
    if(key=="aimbot_bone"){ int v; if(ParseInt(val,v)) g_aimbotBone=v; else ok=false; return true; }
    if(key=="rcs_enabled"){ g_rcsEnabled=ParseBool(val); return true; }
    if(key=="rcs_x"){ float v; if(ParseFloat(val,v)){ g_rcsX=v; rcsXSet=true; } else ok=false; return true; }
    if(key=="rcs_y"){ float v; if(ParseFloat(val,v)){ g_rcsY=v; rcsYSet=true; } else ok=false; return true; }
    if(key=="rcs_smooth"){ float v; if(ParseFloat(val,v)) g_rcsSmooth=v; else ok=false; return true; }
    if(key=="rcs_strength"){ float v; if(ParseFloat(val,v)){ if(!rcsXSet&&!rcsYSet){ g_rcsX=v; g_rcsY=v; } } else ok=false; return true; }
    if(key=="tb_enabled"){ g_tbEnabled=ParseBool(val); return true; }
    if(key=="tb_key"){ int v; if(ParseInt(val,v)) g_tbKey=v; else ok=false; return true; }
    if(key=="tb_delay"){ int v; if(ParseInt(val,v)) g_tbDelay=v; else ok=false; return true; }
    if(key=="tb_team"){ g_tbTeamChk=ParseBool(val); return true; }
    if(key=="dt_enabled"){ g_dtEnabled=ParseBool(val); return true; }
    if(key=="dt_key"){ int v; if(ParseInt(val,v)) g_dtKey=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeyMovement(const std::string& key, const std::string& val, bool& ok){
    if(key=="bhop"){ g_bhopEnabled=ParseBool(val); return true; }
    if(key=="strafe_enabled"){ g_strafeEnabled=ParseBool(val); return true; }
    if(key=="strafe_key"){ int v; if(ParseInt(val,v)) g_strafeKey=v; else ok=false; return true; }
    if(key=="anti_aim_enabled"){ g_antiAimEnabled=ParseBool(val); return true; }
    if(key=="anti_aim_type"){ int v; if(ParseInt(val,v)) g_antiAimType=v; else ok=false; return true; }
    if(key=="anti_aim_speed"){ float v; if(ParseFloat(val,v)) g_antiAimSpeed=v; else ok=false; return true; }
    if(key=="fov_enabled"){ g_fovEnabled=ParseBool(val); return true; }
    if(key=="fov_value"){ float v; if(ParseFloat(val,v)) g_fovValue=v; else ok=false; return true; }
    if(key=="third_person"){ g_thirdPerson=ParseBool(val); return true; }
    if(key=="tp_dist"){ float v; if(ParseFloat(val,v)) g_tpDist=v; else ok=false; return true; }
    if(key=="tp_height"){ float v; if(ParseFloat(val,v)) g_tpHeightOffset=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeyVisual(const std::string& key, const std::string& val, bool& ok){
    if(key=="hands_color_enabled"){ g_handsColorEnabled=ParseBool(val); return true; }
    if(key=="hands_color"){ if(!ParseColor4(val,g_handsColor)) ok=false; return true; }
    if(key=="snow"){ g_snowEnabled=ParseBool(val); return true; }
    if(key=="snow_density"){ int v; if(ParseInt(val,v)) g_snowDensity=v; else ok=false; return true; }
    if(key=="sakura"){ g_sakuraEnabled=ParseBool(val); return true; }
    if(key=="sakura_col"){ if(!ParseColor4(val,g_sakuraCol)) ok=false; return true; }
    if(key=="stars"){ g_starsEnabled=ParseBool(val); return true; }
    if(key=="particles_world"){ g_particlesWorld=ParseBool(val); return true; }
    if(key=="particles_world_radius"){ float v; if(ParseFloat(val,v)) g_particlesWorldRadius=v; else ok=false; return true; }
    if(key=="particles_world_height"){ float v; if(ParseFloat(val,v)) g_particlesWorldHeight=v; else ok=false; return true; }
    if(key=="particles_world_floor"){ float v; if(ParseFloat(val,v)) g_particlesWorldFloor=v; else ok=false; return true; }
    if(key=="particles_wind"){ float v; if(ParseFloat(val,v)) g_particlesWind=v; else ok=false; return true; }
    if(key=="particles_depth_fade"){ float v; if(ParseFloat(val,v)) g_particlesDepthFade=v; else ok=false; return true; }
    if(key=="sky_color_enabled"){ g_skyColorEnabled=ParseBool(val); return true; }
    if(key=="sky_color"){ if(!ParseColor4(val,g_skyColor)) ok=false; return true; }
    return false;
}
static bool LoadConfigKeyMisc(const std::string& key, const std::string& val, bool& ok){
    if(key=="watermark"){ g_watermarkEnabled=ParseBool(val); return true; }
    if(key=="watermark_fps"){ g_showFpsWatermark=ParseBool(val); return true; }
        if(key=="spectator_list"){ g_spectatorListEnabled=ParseBool(val); return true; }
        if(key=="keybinds_enabled"){ g_keybindsEnabled=ParseBool(val); return true; }
    if(key=="hit_notif"){ g_hitNotifEnabled=ParseBool(val); return true; }
    if(key=="kill_notif"){ g_killNotifEnabled=ParseBool(val); return true; }
    if(key=="hit_sound"){ g_hitSoundEnabled=ParseBool(val); return true; }
    if(key=="hit_sound_type"){ int v; if(ParseInt(val,v)) g_hitSoundType=v; else ok=false; return true; }
    if(key=="radar"){ g_radarEnabled=ParseBool(val); return true; }
    if(key=="radar_ingame"){ g_radarIngame=ParseBool(val); return true; }
    if(key=="radar_range"){ float v; if(ParseFloat(val,v)) g_radarRange=v; else ok=false; return true; }
    if(key=="radar_size"){ float v; if(ParseFloat(val,v)) g_radarSize=v; else ok=false; return true; }
    if(key=="bomb_timer"){ g_bombTimerEnabled=ParseBool(val); return true; }
    if(key=="bullet_trace"){ g_bulletTraceEnabled=ParseBool(val); return true; }
    if(key=="impact_col"){ if(!ParseColor4(val,g_impactCol)) ok=false; return true; }
    if(key=="sound_indicators"){ g_soundEnabled=ParseBool(val); return true; }
    if(key=="sound_puddle_scale"){ float v; if(ParseFloat(val,v)) g_soundPuddleScale=v; else ok=false; return true; }
    if(key=="sound_puddle_alpha"){ float v; if(ParseFloat(val,v)) g_soundPuddleAlpha=v; else ok=false; return true; }
    if(key=="sound_blip_enemy"){ g_soundBlipEnemy=ParseBool(val); return true; }
    if(key=="sound_blip_team"){ g_soundBlipTeam=ParseBool(val); return true; }
    if(key=="sound_blip_col"){ if(!ParseColor4(val,g_soundBlipCol)) ok=false; return true; }
    if(key=="backtrack"){ g_backtrackEnabled=ParseBool(val); return true; }
    if(key=="backtrack_visual"){ g_backtrackVisual=ParseBool(val); return true; }
    if(key=="backtrack_ms"){ int v; if(ParseInt(val,v)) g_backtrackMs=v; else ok=false; return true; }
    if(key=="accent"){ if(!ParseColor4(val,g_accentColor)) ok=false; return true; }
    if(key=="menu_opacity"){ float v; if(ParseFloat(val,v)) g_menuOpacity=v; else ok=false; return true; }
    if(key=="ui_scale"){ float v; if(ParseFloat(val,v)) g_uiScale=v; else ok=false; return true; }
    if(key=="menu_theme"){ int v; if(ParseInt(val,v)) g_menuTheme=v; else ok=false; return true; }
    if(key=="menu_anim_speed"){ float v; if(ParseFloat(val,v)) g_menuAnimSpeed=v; else ok=false; return true; }
    return false;
}

static void ApplyDefaults(){
    g_espEnabled = true;
    g_espOnlyVis = false;
    g_espBoxStyle = 0;
    g_espBoxThick = 1.5f;
    g_espEnemyCol[0]=1.f; g_espEnemyCol[1]=0.25f; g_espEnemyCol[2]=0.25f; g_espEnemyCol[3]=1.f;
    g_espTeamCol[0]=0.25f; g_espTeamCol[1]=0.55f; g_espTeamCol[2]=1.f; g_espTeamCol[3]=1.f;
    g_espName = true;
    g_espNameSize = 13.5f;
    g_espScale = 1.0f;
    g_espHealth = true;
    g_espHealthPos = 0;
    g_espHealthStyle = 0;
    g_espHealthGradientCol1[0]=0.2f; g_espHealthGradientCol1[1]=0.92f; g_espHealthGradientCol1[2]=0.51f; g_espHealthGradientCol1[3]=1.f;
    g_espHealthGradientCol2[0]=1.f; g_espHealthGradientCol2[1]=0.27f; g_espHealthGradientCol2[2]=0.27f; g_espHealthGradientCol2[3]=1.f;
    g_espAmmoStyle = 0;
    g_espAmmoCol1[0]=0.03f; g_espAmmoCol1[1]=0.03f; g_espAmmoCol1[2]=0.05f; g_espAmmoCol1[3]=1.f;
    g_espAmmoCol2[0]=0.35f; g_espAmmoCol2[1]=0.71f; g_espAmmoCol2[2]=1.f; g_espAmmoCol2[3]=1.f;
    g_espDist = true;
    g_espMaxDist = 100.f;
    g_espSkeleton = false;
    g_espLines = false;
    g_espOof = false;
    g_espOofSize = 24.f;
    g_skeletonThick = 1.1f;
    g_espHeadDot = true;
    g_espSpotted = true;
    g_visCheckEnabled = true;
    g_espWeapon = true;
    g_espWeaponIcon = true;
    g_espAmmo = true;
    g_espMoney = true;
    g_espMoneyPos = 0;  // 0=below box
    g_espHeadForward = 6.f;
    g_noFlash = false;
    g_noSmoke = false;
    g_noCrosshair = false;
    g_noLegs = false;
    g_glowEnabled = false;
    g_glowEnemyCol[0]=1.f; g_glowEnemyCol[1]=0.18f; g_glowEnemyCol[2]=0.18f; g_glowEnemyCol[3]=1.f;
    g_glowTeamCol[0]=0.18f; g_glowTeamCol[1]=0.5f; g_glowTeamCol[2]=1.f; g_glowTeamCol[3]=1.f;
    g_glowAlpha = 1.0f;
    g_chamsEnabled = false;
    g_chamsEnemyOnly = true;
    g_chamsIgnoreZ = false;
    g_chamsMaterial = 0;
    g_chamsEnemyCol[0]=1.f; g_chamsEnemyCol[1]=0.2f; g_chamsEnemyCol[2]=0.2f; g_chamsEnemyCol[3]=1.f;
    g_chamsTeamCol[0]=0.2f; g_chamsTeamCol[1]=0.5f; g_chamsTeamCol[2]=1.f; g_chamsTeamCol[3]=1.f;
    g_chamsIgnoreZCol[0]=1.f; g_chamsIgnoreZCol[1]=0.4f; g_chamsIgnoreZCol[2]=0.9f; g_chamsIgnoreZCol[3]=0.6f;
    g_chamsScene = true;
    g_weaponChamsEnabled = false;
    g_weaponChamsCol[0]=1.f; g_weaponChamsCol[1]=0.88f; g_weaponChamsCol[2]=0.35f; g_weaponChamsCol[3]=1.f;
    g_aimbotEnabled = false;
    g_aimbotKey = VK_LBUTTON;
    g_aimbotFov = 5.f;
    g_aimbotSmooth = 6.f;
    g_aimbotTeamChk = true;
    g_aimbotBone = 0;
    g_rcsEnabled = false;
    g_rcsX = 1.0f;
    g_rcsY = 1.0f;
    g_rcsSmooth = 6.0f;
    g_tbEnabled = false;
    g_tbKey = 0;
    g_tbDelay = 50;
    g_tbTeamChk = true;
    g_dtEnabled = false;
    g_dtKey = 0;
    g_bhopEnabled = false;
    g_strafeEnabled = false;
    g_strafeKey = 0;
    g_antiAimEnabled = false;
    g_antiAimType = 0;
    g_antiAimSpeed = 180.f;
    g_fovEnabled = false;
    g_fovValue = 90.f;
    g_thirdPerson = false;
    g_tpDist = 120.f;
    g_tpHeightOffset = 30.f;
    g_snowEnabled = false;
    g_snowDensity = 1;
    g_sakuraEnabled = false;
    g_sakuraCol[0]=1.f; g_sakuraCol[1]=0.55f; g_sakuraCol[2]=0.7f; g_sakuraCol[3]=0.85f;
    g_starsEnabled = false;
    g_particlesWorld = true;
    g_particlesWorldRadius = 600.f;
    g_particlesWorldHeight = 320.f;
    g_particlesWorldFloor = 40.f;
    g_particlesWind = 18.f;
    g_particlesDepthFade = 0.0022f;
    g_skyColorEnabled = false;
    g_skyColor[0]=0.4f; g_skyColor[1]=0.5f; g_skyColor[2]=0.8f; g_skyColor[3]=1.f;
    g_handsColorEnabled = false;
    g_handsColor[0]=0.9f; g_handsColor[1]=0.9f; g_handsColor[2]=0.95f; g_handsColor[3]=1.f;
    g_watermarkEnabled = true;
    g_showFpsWatermark = true;
    g_spectatorListEnabled = true;
    g_keybindsEnabled = true;
    g_hitNotifEnabled = true;
    g_killNotifEnabled = true;
    g_hitSoundEnabled = false;
    g_hitSoundType = 1;
    g_radarEnabled = true;
    g_radarIngame = false;
    g_radarRange = 2000.f;
    g_radarSize = 180.f;
    g_bombTimerEnabled = true;
    g_bulletTraceEnabled = true;
    g_impactCol[0]=0.35f; g_impactCol[1]=0.94f; g_impactCol[2]=0.47f; g_impactCol[3]=0.78f;
    g_soundEnabled = true;
    g_soundPuddleScale = 1.0f;
    g_soundPuddleAlpha = 1.0f;
    g_soundBlipEnemy = true;
    g_soundBlipTeam = false;
    g_soundBlipCol[0]=1.f; g_soundBlipCol[1]=0.f; g_soundBlipCol[2]=0.f; g_soundBlipCol[3]=1.f;
    g_backtrackEnabled = false;
    g_backtrackVisual = false;
    g_backtrackMs = 200;
    g_accentColor[0]=0.1f; g_accentColor[1]=0.55f; g_accentColor[2]=1.0f; g_accentColor[3]=1.0f;
    g_menuOpacity = 1.0f;
    g_uiScale = 1.10f;
    g_menuTheme = 0;
    g_menuAnimSpeed = 12.f;
}

static bool SaveConfig(const char* name){
    EnsureConfigDir();
    std::ofstream out(ConfigPath(name), std::ios::trunc);
    if(!out.is_open()) return false;
    WriteBool(out, "esp_enabled", g_espEnabled);
    WriteBool(out, "esp_only_vis", g_espOnlyVis);
    WriteInt(out, "esp_box_style", g_espBoxStyle);
    WriteFloat(out, "esp_box_thick", g_espBoxThick);
    WriteColor(out, "esp_enemy_col", g_espEnemyCol);
    WriteColor(out, "esp_team_col", g_espTeamCol);
    WriteBool(out, "esp_name", g_espName);
    WriteFloat(out, "esp_name_size", g_espNameSize);
    WriteFloat(out, "esp_scale", g_espScale);
    WriteInt(out, "esp_preview_pos", g_espPreviewPos);
    WriteBool(out, "esp_health", g_espHealth);
    WriteInt(out, "esp_health_pos", g_espHealthPos);
    WriteInt(out, "esp_health_style", g_espHealthStyle);
    WriteColor(out, "health_grad_col1", g_espHealthGradientCol1);
    WriteColor(out, "health_grad_col2", g_espHealthGradientCol2);
    WriteInt(out, "esp_ammo_style", g_espAmmoStyle);
    WriteColor(out, "esp_ammo_col1", g_espAmmoCol1);
    WriteColor(out, "esp_ammo_col2", g_espAmmoCol2);
    WriteBool(out, "esp_dist", g_espDist);
    WriteFloat(out, "esp_max_dist", g_espMaxDist);
    WriteBool(out, "esp_skeleton", g_espSkeleton);
    WriteBool(out, "esp_lines", g_espLines);
    WriteBool(out, "esp_oof", g_espOof);
    WriteFloat(out, "esp_oof_size", g_espOofSize);
    WriteFloat(out, "skeleton_thick", g_skeletonThick);
    WriteBool(out, "esp_head_dot", g_espHeadDot);
    WriteBool(out, "esp_spotted", g_espSpotted);
    WriteBool(out, "esp_vis_check", g_visCheckEnabled);
    WriteBool(out, "esp_weapon", g_espWeapon);
    WriteBool(out, "esp_weapon_icon", g_espWeaponIcon);
    WriteBool(out, "esp_ammo", g_espAmmo);
    WriteBool(out, "esp_money", g_espMoney);
    WriteFloat(out, "esp_head_forward", g_espHeadForward);
    WriteBool(out, "no_flash", g_noFlash);
    WriteBool(out, "no_smoke", g_noSmoke);
    WriteBool(out, "no_crosshair", g_noCrosshair);
    WriteBool(out, "no_legs", g_noLegs);
    WriteBool(out, "glow_enabled", g_glowEnabled);
    WriteColor(out, "glow_enemy_col", g_glowEnemyCol);
    WriteColor(out, "glow_team_col", g_glowTeamCol);
    WriteFloat(out, "glow_alpha", g_glowAlpha);
    WriteBool(out, "chams_enabled", g_chamsEnabled);
    WriteBool(out, "chams_enemy_only", g_chamsEnemyOnly);
    WriteBool(out, "chams_ignorez", g_chamsIgnoreZ);
    WriteInt(out, "chams_material", g_chamsMaterial);
    WriteColor(out, "chams_enemy_col", g_chamsEnemyCol);
    WriteColor(out, "chams_team_col", g_chamsTeamCol);
    WriteColor(out, "chams_ignorez_col", g_chamsIgnoreZCol);
    WriteBool(out, "chams_scene", g_chamsScene);
    WriteBool(out, "weapon_chams", g_weaponChamsEnabled);
    WriteColor(out, "weapon_chams_col", g_weaponChamsCol);
    WriteBool(out, "aimbot_enabled", g_aimbotEnabled);
    WriteInt(out, "aimbot_key", g_aimbotKey);
    WriteFloat(out, "aimbot_fov", g_aimbotFov);
    WriteFloat(out, "aimbot_smooth", g_aimbotSmooth);
    WriteBool(out, "aimbot_team", g_aimbotTeamChk);
    WriteInt(out, "aimbot_bone", g_aimbotBone);
    WriteBool(out, "rcs_enabled", g_rcsEnabled);
    WriteFloat(out, "rcs_x", g_rcsX);
    WriteFloat(out, "rcs_y", g_rcsY);
    if(fabsf(g_rcsX - g_rcsY) < 0.0001f) WriteFloat(out, "rcs_strength", g_rcsX);
    WriteFloat(out, "rcs_smooth", g_rcsSmooth);
    WriteBool(out, "tb_enabled", g_tbEnabled);
    WriteInt(out, "tb_key", g_tbKey);
    WriteInt(out, "tb_delay", g_tbDelay);
    WriteBool(out, "tb_team", g_tbTeamChk);
    WriteBool(out, "dt_enabled", g_dtEnabled);
    WriteInt(out, "dt_key", g_dtKey);
    WriteBool(out, "bhop", g_bhopEnabled);
    WriteBool(out, "strafe_enabled", g_strafeEnabled);
    WriteInt(out, "strafe_key", g_strafeKey);
    WriteBool(out, "anti_aim_enabled", g_antiAimEnabled);
    WriteInt(out, "anti_aim_type", g_antiAimType);
    WriteFloat(out, "anti_aim_speed", g_antiAimSpeed);
    WriteBool(out, "fov_enabled", g_fovEnabled);
    WriteFloat(out, "fov_value", g_fovValue);
    WriteBool(out, "third_person", g_thirdPerson);
    WriteFloat(out, "tp_dist", g_tpDist);
    WriteFloat(out, "tp_height", g_tpHeightOffset);
    WriteBool(out, "fov_circle", g_fovCircleEnabled);
    WriteBool(out, "hands_color_enabled", g_handsColorEnabled);
    WriteColor(out, "hands_color", g_handsColor);
    WriteBool(out, "snow", g_snowEnabled);
    WriteInt(out, "snow_density", g_snowDensity);
    WriteBool(out, "sakura", g_sakuraEnabled);
    WriteColor(out, "sakura_col", g_sakuraCol);
    WriteBool(out, "stars", g_starsEnabled);
    WriteBool(out, "particles_world", g_particlesWorld);
    WriteFloat(out, "particles_world_radius", g_particlesWorldRadius);
    WriteFloat(out, "particles_world_height", g_particlesWorldHeight);
    WriteFloat(out, "particles_world_floor", g_particlesWorldFloor);
    WriteFloat(out, "particles_wind", g_particlesWind);
    WriteFloat(out, "particles_depth_fade", g_particlesDepthFade);
    WriteBool(out, "sky_color_enabled", g_skyColorEnabled);
    WriteColor(out, "sky_color", g_skyColor);
    WriteBool(out, "watermark", g_watermarkEnabled);
    WriteBool(out, "watermark_fps", g_showFpsWatermark);
    WriteBool(out, "spectator_list", g_spectatorListEnabled);
    WriteBool(out, "keybinds_enabled", g_keybindsEnabled);
    WriteBool(out, "hit_notif", g_hitNotifEnabled);
    WriteBool(out, "kill_notif", g_killNotifEnabled);
    WriteBool(out, "hit_sound", g_hitSoundEnabled);
    WriteInt(out, "hit_sound_type", g_hitSoundType);
    WriteBool(out, "radar", g_radarEnabled);
    WriteBool(out, "radar_ingame", g_radarIngame);
    WriteFloat(out, "radar_range", g_radarRange);
    WriteFloat(out, "radar_size", g_radarSize);
    WriteBool(out, "bomb_timer", g_bombTimerEnabled);
    WriteBool(out, "bullet_trace", g_bulletTraceEnabled);
    WriteColor(out, "impact_col", g_impactCol);
    WriteBool(out, "sound_indicators", g_soundEnabled);
    WriteFloat(out, "sound_puddle_scale", g_soundPuddleScale);
    WriteFloat(out, "sound_puddle_alpha", g_soundPuddleAlpha);
    WriteBool(out, "sound_blip_enemy", g_soundBlipEnemy);
    WriteBool(out, "sound_blip_team", g_soundBlipTeam);
    WriteColor4(out, "sound_blip_col", g_soundBlipCol);
    WriteBool(out, "backtrack", g_backtrackEnabled);
    WriteBool(out, "backtrack_visual", g_backtrackVisual);
    WriteInt(out, "backtrack_ms", g_backtrackMs);
    WriteColor(out, "accent", g_accentColor);
    WriteFloat(out, "menu_opacity", g_menuOpacity);
    WriteFloat(out, "ui_scale", g_uiScale);
    WriteInt(out, "menu_theme", g_menuTheme);
    WriteFloat(out, "menu_anim_speed", g_menuAnimSpeed);
    return true;
}

static bool LoadConfig(const char* name){
    std::ifstream in(ConfigPath(name));
    if(!in.is_open()) return false;
    std::string line;
    bool ok = true;
    bool rcsXSet = false;
    bool rcsYSet = false;
    while(std::getline(in, line)){
        if(line.empty()) continue;
        const auto pos = line.find('=');
        if(pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if(LoadConfigKeyEsp(key, val, ok)) continue;
        if(LoadConfigKeyChams(key, val, ok)) continue;
        if(LoadConfigKeyAimbot(key, val, ok, rcsXSet, rcsYSet)) continue;
        if(LoadConfigKeyMovement(key, val, ok)) continue;
        if(LoadConfigKeyVisual(key, val, ok)) continue;
        LoadConfigKeyMisc(key, val, ok);
    }
    g_menuOpacity = Clampf(g_menuOpacity, 0.3f, 1.0f);
    g_uiScale = Clampf(g_uiScale, 0.85f, 1.6f);
    g_menuAnimSpeed = Clampf(g_menuAnimSpeed, 2.f, 30.f);
    if(g_menuTheme < 0) g_menuTheme = 0;
    if(g_menuTheme > 2) g_menuTheme = 2;
    g_glowAlpha = Clampf(g_glowAlpha, 0.f, 1.f);
    g_skeletonThick = Clampf(g_skeletonThick, 0.5f, 3.5f);
    g_espOofSize = Clampf(g_espOofSize, 8.f, 64.f);
    g_particlesWorldRadius = Clampf(g_particlesWorldRadius, 200.f, 2000.f);
    g_particlesWorldHeight = Clampf(g_particlesWorldHeight, 100.f, 1200.f);
    g_particlesWorldFloor = Clampf(g_particlesWorldFloor, -200.f, 400.f);
    g_particlesWind = Clampf(g_particlesWind, 0.f, 60.f);
    g_particlesDepthFade = Clampf(g_particlesDepthFade, 0.0005f, 0.01f);
    g_soundPuddleScale = Clampf(g_soundPuddleScale, 0.3f, 3.0f);
    g_soundPuddleAlpha = Clampf(g_soundPuddleAlpha, 0.f, 2.0f);
    return ok;
}

static bool GetOofArrowPos(const float* vm, const Vec3& head, int sw, int sh, float& ox, float& oy);

// BuildESPData
static void BuildESPData(){
    g_esp_count=0;g_esp_oof_count=0;g_esp_local_team=0;g_esp_local_pawn=0;
    g_esp_screen_w=1920;g_esp_screen_h=1080;
    g_localOrigin = {};
    // Visibility throttle: only clear cache when doing full read (every 3rd frame)
    static int s_visFrame = 0;
    if((s_visFrame % 3) == 0) std::fill(std::begin(g_visMap), std::end(g_visMap), false);
    s_visFrame++;

    EnsureModules();if(!g_client)return;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
    if(!entityList)return;
    const float*vm=reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix);
    int sw=1920,sh=1080;
    if(g_engine2){int w=Rd<int>(g_engine2+offsets::engine2::dwWindowWidth);
    int h=Rd<int>(g_engine2+offsets::engine2::dwWindowHeight);
    if(w>100&&h>100){sw=w;sh=h;}}
    uintptr_t localPawn=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
    (void)Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController); // localCtrl reserved
    int localTeam=0;Vec3 localOrigin{};
    DWORD nowTick = GetTickCount();
    if(localPawn){localTeam=(int)Rd<uint8_t>(localPawn+offsets::base_entity::m_iTeamNum);
    uintptr_t sc0=Rd<uintptr_t>(localPawn+offsets::base_entity::m_pGameSceneNode);
    if(sc0)localOrigin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);}
    g_localOrigin = localOrigin;
    g_esp_local_team=localTeam;g_esp_local_pawn=localPawn;
    g_esp_screen_w=sw;g_esp_screen_h=sh;
    for(int i=1;i<64&&g_esp_count<ESP_MAX_PLAYERS;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ctrl)continue;
        if(!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
        uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
        uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
        uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
        if(!pawn||pawn==localPawn)continue;
        int team=(int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum);
        int health=Rd<int>(pawn+offsets::base_entity::m_iHealth);if(health<=0)continue;
        bool vis=true;
        if(g_visCheckEnabled){
            static int s_visFrame = 0;
            if((s_visFrame++ % 3) == 0){
                vis=Rd<bool>(pawn+offsets::spotted::m_entitySpottedState+offsets::spotted::m_bSpotted);
                if(i > 0 && i <= ESP_MAX_PLAYERS) g_visMap[i] = vis;
            }else if(i > 0 && i <= ESP_MAX_PLAYERS){
                vis = g_visMap[i];
            }else{
                vis = true;
            }
        }
        if(i > 0 && i <= ESP_MAX_PLAYERS && g_visCheckEnabled) { /* g_visMap updated above */ }
        else if(i > 0 && i <= ESP_MAX_PLAYERS) g_visMap[i] = vis;
        if(g_espOnlyVis&&!vis)continue;
        uintptr_t scn=Rd<uintptr_t>(pawn+offsets::base_entity::m_pGameSceneNode);
        Vec3 origin{};if(scn)origin=Rd<Vec3>(scn+offsets::scene_node::m_vecAbsOrigin);
        Vec3 viewOff=Rd<Vec3>(pawn+offsets::base_pawn::m_vecViewOffset);Vec3 head=origin+viewOff;
        Vec3 headForward=head;
        float yaw=0.f;
        if(scn){
            yaw=Rd<float>(scn+offsets::scene_node::m_angRotation+4);
            float rad=yaw*(3.14159265f/180.f);
            Vec3 fwd{cosf(rad),sinf(rad),0.f};
            headForward = head + fwd * g_espHeadForward;
        }
        float hx,hy,fx,fy,hfx,hfy;
        if(!WorldToScreen(vm,head,sw,sh,hx,hy)){
            if(g_espOof&&(team!=localTeam)&&g_esp_oof_count<32){
                float ox,oy;
                if(GetOofArrowPos(vm,head,sw,sh,ox,oy)){
                    float cx=sw*0.5f, cy=sh*0.5f;
                    float dx=ox-cx, dy=oy-cy;
                    float angle=atan2f(dx, -dy);  // direction from center toward player
                    float*ecol=(team==localTeam)?g_espTeamCol:g_espEnemyCol;
                    g_esp_oof[g_esp_oof_count].x=ox;
                    g_esp_oof[g_esp_oof_count].y=oy;
                    g_esp_oof[g_esp_oof_count].angle=angle;
                    g_esp_oof[g_esp_oof_count].col=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),220);
                    g_esp_oof_count++;
                }
            }
            continue;
        }
        if(!WorldToScreen(vm,headForward,sw,sh,hfx,hfy)){hfx=hx;hfy=hy;}
        if(!WorldToScreen(vm,origin,sw,sh,fx,fy))continue;
        float top=(hy<fy)?hy:fy;float bot=(hy>fy)?hy:fy;float boxH=bot-top;
        if(boxH<2.f)continue;
        top -= boxH*0.25f; boxH *= 1.25f;  // Extend upward for full head coverage (full hitbox)
        float boxW=boxH*0.50f;float cx=(hx+fx)*0.5f;  // Wider for full body/hitbox
        float dist = (origin - localOrigin).length() / 100.f;  // meters
        uintptr_t namePtr=Rd<uintptr_t>(ctrl+offsets::controller::m_sSanitizedPlayerName);
        float flashDur=Rd<float>(pawn+offsets::cs_pawn_base::m_flFlashDuration);
        ESPEntry&e=g_esp_players[g_esp_count++];
        e.valid=true;e.visible=vis;e.flashed=(flashDur>0.1f);
        e.planting=Rd<bool>(pawn+offsets::cs_pawn::m_bIsPlantingViaUse);
        e.scoped=Rd<bool>(pawn+offsets::cs_pawn::m_bIsScoped);
        e.spotted=vis;
        e.ent_index=i;
        e.pawn=pawn;
        e.controller=ctrl;
        e.head_x=hx;e.head_y=hy;e.head_fx=hfx;e.head_fy=hfy;
        e.head_ox=head.x;e.head_oy=head.y;e.head_oz=head.z;
        e.origin_x=origin.x;e.origin_y=origin.y;e.origin_z=origin.z;
        e.feet_x=fx;e.feet_y=fy;
        e.box_l=cx-boxW*0.5f;e.box_r=cx+boxW*0.5f;e.box_t=top;e.box_b=bot;
        e.health=health;e.team=team;e.distance=dist;e.yaw=0.f;
        RdName(namePtr,e.name,sizeof(e.name));
        if((g_backtrackEnabled||g_backtrackVisual)&&e.ent_index>0&&e.ent_index<=ESP_MAX_PLAYERS){
            auto& dq = g_backtrack[e.ent_index];
            Vec3 pelvis{}; GetBonePos(pawn,BONE_PELVIS,pelvis);
            dq.push_front(BacktrackRecord{head, pelvis, Vec3{}, nowTick});
            DWORD maxAge = (DWORD)g_backtrackMs;
            while(!dq.empty() && (DWORD)(nowTick - dq.back().time) > maxAge) dq.pop_back();
            if(dq.size()>32) dq.pop_back();
        }
    }
}

// Spectator list (help-learn / YouGame.Biz): who's spectating you, or who you're spectating
static char g_spectatorNames[10][64];
static int g_spectatorCount = 0;
static char g_spectatingTarget[64] = {};
static bool g_weAreSpectating = false;

static void BuildSpectatorList(){
    g_spectatorCount = 0;
    g_spectatingTarget[0] = '\0';
    g_weAreSpectating = false;
    if(!g_client||!g_spectatorListEnabled) return;
    uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
    if(!entityList) return;
    uintptr_t localPawn = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!localPawn) return;
    int localLife = Rd<uint8_t>(localPawn + offsets::base_entity::m_lifeState);
    uintptr_t localCtrl = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerController);

    if(localLife == 0){  // LIFE_ALIVE - find who's spectating us
        for(int i = 1; i < 64; i++){
            uintptr_t chunk = Rd<uintptr_t>(entityList + 8*((i&0x7FFF)>>9) + 16);
            if(!chunk) continue;
            uintptr_t ctrl = Rd<uintptr_t>(chunk + 112*(i&0x1FF));
            if(!ctrl||ctrl==localCtrl) continue;
            uint32_t obsHandle = Rd<uint32_t>(ctrl + offsets::controller::m_hObserverPawn);
            if(!obsHandle) continue;
            uintptr_t obsPawn = ResolveHandle(entityList, obsHandle);
            if(!obsPawn) continue;
            uintptr_t obsSvc = Rd<uintptr_t>(obsPawn + offsets::base_pawn::m_pObserverServices);
            if(!obsSvc) continue;
            uint32_t targetHandle = Rd<uint32_t>(obsSvc + offsets::observer::m_hObserverTarget);
            if(!targetHandle) continue;
            uintptr_t targetPawn = ResolveHandle(entityList, targetHandle);
            if(targetPawn != localPawn) continue;
            uintptr_t namePtr = Rd<uintptr_t>(ctrl + offsets::controller::m_sSanitizedPlayerName);
            if(g_spectatorCount < 10) RdName(namePtr, g_spectatorNames[g_spectatorCount], 64);
            g_spectatorCount++;
        }
    }else{  // Dead - show who we're spectating
        uintptr_t obsSvc = Rd<uintptr_t>(localPawn + offsets::base_pawn::m_pObserverServices);
        if(obsSvc){
            uint32_t targetHandle = Rd<uint32_t>(obsSvc + offsets::observer::m_hObserverTarget);
            if(targetHandle){
                uintptr_t targetPawn = ResolveHandle(entityList, targetHandle);
                if(targetPawn){
                    g_weAreSpectating = true;
                    for(int i = 1; i < 64; i++){
                        uintptr_t chunk = Rd<uintptr_t>(entityList + 8*((i&0x7FFF)>>9) + 16);
                        if(!chunk) continue;
                        uintptr_t ctrl = Rd<uintptr_t>(chunk + 112*(i&0x1FF));
                        if(!ctrl) continue;
                        uint32_t ph = Rd<uint32_t>(ctrl + offsets::controller::m_hPlayerPawn);
                        if(!ph) continue;
                        uintptr_t pchunk = Rd<uintptr_t>(entityList + 8*((ph&0x7FFF)>>9) + 16);
                        if(!pchunk) continue;
                        uintptr_t pawn = Rd<uintptr_t>(pchunk + 112*(ph&0x1FF));
                        if(pawn == targetPawn){
                            uintptr_t namePtr = Rd<uintptr_t>(ctrl + offsets::controller::m_sSanitizedPlayerName);
                            RdName(namePtr, g_spectatingTarget, sizeof(g_spectatingTarget));
                            break;
                        }
                    }
                }
            }
        }
    }
}

static void DrawSpectatorList(float sw){
    if(!g_spectatorListEnabled) return;
    if(g_spectatorCount == 0 && !g_weAreSpectating) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    ImFont* f = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* fReg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const float pad = 12.f;
    float y = 15.f;
    if(g_watermarkEnabled) y += 48.f;
    ImU32 bgFill = IM_COL32(11, 11, 11, 255);
    ImU32 stroke = IM_COL32(24, 26, 36, 255);
    ImU32 accent = IM_COL32((int)(g_accentColor[0]*255), (int)(g_accentColor[1]*255), (int)(g_accentColor[2]*255), 255);
    ImU32 textCol = IM_COL32(255, 255, 255, 255);
    if(g_weAreSpectating && g_spectatingTarget[0]){
        char buf[80];
        std::snprintf(buf, sizeof(buf), "Spectating: %s", g_spectatingTarget);
        ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, buf);
        float w = ts.x + pad*2.f; w = (std::max)(w, 120.f);
        float x = sw - w - 15.f;
        dl->AddRectFilled({x, y}, {x+w, y+28.f}, bgFill, 6.f);
        dl->AddRect({x, y}, {x+w, y+28.f}, stroke, 6.f, 0, 1.f);
        dl->AddText(f, f->FontSize, {x+pad, y+6.f}, accent, "Spectating:");
        float nx = x + pad + f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, "Spectating: ").x;
        dl->AddText(f, f->FontSize, {nx, y+6.f}, textCol, g_spectatingTarget);
        return;
    }
    if(g_spectatorCount <= 0) return;
    float lineH = 20.f;
    float boxH = pad*2.f + (float)g_spectatorCount * lineH;
    float maxW = 80.f;
    for(int i = 0; i < g_spectatorCount; i++){
        ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, g_spectatorNames[i]);
        if(ts.x > maxW) maxW = ts.x;
    }
    float boxW = maxW + pad*2.f; boxW = (std::max)(boxW, 100.f);
    float x = sw - boxW - 15.f;
    dl->AddRectFilled({x, y}, {x+boxW, y+boxH}, bgFill, 6.f);
    dl->AddRect({x, y}, {x+boxW, y+boxH}, stroke, 6.f, 0, 1.f);
    dl->AddRectFilled({x+12.f, y+22.f}, {x+boxW-12.f, y+24.f}, accent, 3.f);
    dl->AddText(f, f->FontSize, {x+pad, y+4.f}, accent, "Spectators");
    for(int i = 0; i < g_spectatorCount; i++){
        dl->AddText(fReg, fReg->FontSize - 1.f, {x+pad, y+pad+18.f+(float)i*lineH}, textCol, g_spectatorNames[i]);
    }
}

static void ProcessHitEvents(){
    std::fill(std::begin(g_seenThisFrame), std::end(g_seenThisFrame), false);
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry& e = g_esp_players[i];
        if(!e.valid) continue;
        if(e.ent_index <= 0 || e.ent_index > ESP_MAX_PLAYERS) continue;
        if(e.team == g_esp_local_team) continue;
        g_seenThisFrame[e.ent_index] = true;
        int prev = g_lastHealth[e.ent_index];
        if(prev > 0 && e.health < prev){
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Hit %s for %d", e.name[0]?e.name:"Enemy", (prev - e.health));
            if(g_hitNotifEnabled) PushNotification(buf, IM_COL32(240,180,60,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(240,180,60,255); le.maxlife=4.f; le.lifetime=4.f; le.type=0;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            PlayHitSound(g_hitSoundType);
            if(g_hitmarkerEnabled) g_lastHitmarkerTime = GetTickCount();
        }
        if(prev > 0 && e.health <= 0){
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Killed %s", e.name[0]?e.name:"Enemy");
            if(g_killNotifEnabled) PushNotification(buf, IM_COL32(140,100,255,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(140,100,255,255); le.maxlife=4.f; le.lifetime=4.f; le.type=1;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            if(g_killEffectEnabled) g_lastKillEffectTime = GetTickCount();
        }
        g_lastHealth[e.ent_index] = e.health;
    }
    for(int idx=1; idx<=ESP_MAX_PLAYERS; ++idx){
        if(!g_seenThisFrame[idx]) g_lastHealth[idx] = 0;
    }
}

static void RunNoFlash(){
    if(!g_noFlash||!g_client)return;
    __try{
        uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
        if(!lp||!IsLikelyPtr(lp))return;
        float dur=Rd<float>(lp+offsets::cs_pawn_base::m_flFlashDuration);
        if(dur>0.01f) Wr<float>(lp+offsets::cs_pawn_base::m_flFlashDuration,0.f);
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunNoSmoke(){
    if(!g_noSmoke||!g_client) return;
    DWORD now = GetTickCount();
    if(now - g_lastNoSmokeTick < 200) return;
    g_lastNoSmokeTick = now;
    // 85% transparency = 15% opacity (255 * 0.15 = 38)
    // Writing m_bSmokeEffectSpawned=0 crashes, so we use scene node alpha
    constexpr uint8_t SMOKE_ALPHA_85PCT_TRANSPARENT = 38;
    __try{
        uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
        for(int i=1;i<512;i++){
            uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
            uintptr_t ent=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ent||!IsLikelyPtr(ent))continue;
            __try{
                uint8_t spawned=Rd<uint8_t>(ent+offsets::smoke_projectile::m_bSmokeEffectSpawned);
                if(!spawned)continue;
                uintptr_t scn = Rd<uintptr_t>(ent+offsets::base_entity::m_pGameSceneNode);
                if(!scn||!IsLikelyPtr(scn))continue;
                // Validate scene node is in reasonable range before writing
                Wr<uint8_t>(scn + 0x53, SMOKE_ALPHA_85PCT_TRANSPARENT);
            }__except(EXCEPTION_EXECUTE_HANDLER){}
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunGlow(){
    static bool s_wasActive = false;
    if(!g_client) return;
    bool needGlowPass = g_glowEnabled || (g_chamsEnabled && !g_chamsScene);
    if(!needGlowPass){
        if(s_wasActive){
            uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
            if(entityList){
                for(int i=1;i<64;i++){
                    uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
                    uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ctrl)continue;
                    uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
                    uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
                    uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
                    if(!pawn)continue;
                    uintptr_t glowProp=pawn+offsets::model_entity::m_Glow;
                    Wr<uint8_t>(glowProp+offsets::glow_prop::m_bGlowing,0);
                }
            }
        }
        s_wasActive = false;
        return;
    }
    s_wasActive = true;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t localPawn=g_esp_local_pawn;int localTeam=g_esp_local_team;
    for(int i=1;i<64;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));
        if(!ctrl||!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
        uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
        uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
        uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
        if(!pawn||pawn==localPawn)continue;
        int health=Rd<int>(pawn+offsets::base_entity::m_iHealth);if(health<=0)continue;
        int team=(int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum);
        bool isTeam = (team == localTeam);
        bool applyChams = g_chamsEnabled && !g_chamsScene && (!g_chamsEnemyOnly || !isTeam);
        bool applyGlow = g_glowEnabled;
        bool apply = applyChams || applyGlow;
        uintptr_t glowProp=pawn+offsets::model_entity::m_Glow;
        if(apply){
            float* col = nullptr;
            if(applyChams){
                if(!isTeam && g_chamsIgnoreZ && i > 0 && i <= ESP_MAX_PLAYERS && !g_visMap[i]){
                    col = g_chamsIgnoreZCol;
                }else{
                    col = isTeam ? g_chamsTeamCol : g_chamsEnemyCol;
                }
            }else{
                col = isTeam ? g_glowTeamCol : g_glowEnemyCol;
            }
            float tmp[4]{col[0], col[1], col[2], col[3]};
            if(applyChams){
                ApplyChamsMaterial(tmp);
            }else{
                tmp[3] = Clampf(tmp[3] * g_glowAlpha, 0.f, 1.f);
            }
            MaterialColor gc = MakeMatColor(tmp);
            Wr<MaterialColor>(glowProp+offsets::glow_prop::m_glowColorOverride,gc);
            Wr<uint8_t>(glowProp+offsets::glow_prop::m_bGlowing,1);
        }else{
            Wr<uint8_t>(glowProp+offsets::glow_prop::m_bGlowing,0);
        }
    }
}

// In-game radar hack: force m_bSpotted so enemies appear on the built-in minimap
static void RunRadarHack(){
    if(!g_radarIngame||!g_client)return;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t localPawn=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!localPawn)return;
    int localTeam=(int)Rd<uint8_t>(localPawn+offsets::base_entity::m_iTeamNum);
    for(int i=1;i<64;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));
        if(!ctrl||!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
        uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
        uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
        uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
        if(!pawn||pawn==localPawn)continue;
        int health=Rd<int>(pawn+offsets::base_entity::m_iHealth);if(health<=0)continue;
        int team=(int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum);
        if(team==localTeam)continue;  // Only force spot enemies
        uintptr_t spotBase=pawn+offsets::spotted::m_entitySpottedState;
        Wr<uint8_t>(spotBase+offsets::spotted::m_bSpotted, 1);
        Wr<uint32_t>(spotBase+offsets::spotted::m_bSpottedByMask, 0xFFFFFFFF);
        Wr<uint32_t>(spotBase+offsets::spotted::m_bSpottedByMask+4, 0xFFFFFFFF);
    }
}

static void RunAutostop(){
    if(!g_autostopEnabled||!g_client)return;
    if(!(GetAsyncKeyState(g_aimbotKey)&0x8000))return;  // Only when holding aim key
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    __try{
        Vec3 vel=Rd<Vec3>(lp+offsets::base_entity::m_vecVelocity);
        float spd=sqrtf(vel.x*vel.x+vel.y*vel.y+vel.z*vel.z);
        if(spd>5.f) Wr<Vec3>(lp+offsets::base_entity::m_vecVelocity, Vec3{0.f,0.f,0.f});
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunBHop(){
    if(!g_bhopEnabled||!g_client)return;
    if(!(GetAsyncKeyState(VK_SPACE)&0x8000))return;  // Only when holding space
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    uint32_t flags=Rd<uint32_t>(lp+offsets::base_entity::m_fFlags);
    static bool wasOnGround = false;
    bool onGround = (flags & 1) != 0;

    if(onGround){
        // On ground - press jump for next bhop
        Wr<int>(g_client+offsets::buttons::jump, 65537);  // 65537 = pressed
        wasOnGround = true;
    }else{
        // In air - release jump after a frame delay to avoid double-tap
        if(wasOnGround){
            wasOnGround = false;
        }
        Wr<int>(g_client+offsets::buttons::jump, 0);   // 0 = released
    }
}

static void RunAntiAim(){
    if(!g_antiAimEnabled||!g_client) return;
    uintptr_t vaAddr = ViewAnglesAddr();
    if(!vaAddr) return;

    float yaw = Rd<float>(vaAddr+4);
    (void)Rd<float>(vaAddr);   // pitch - reserved
    (void)Rd<float>(vaAddr+8); // roll - reserved

    static float antiAimAngle = 0.f;
    float deltaTime = 0.016f; // ~60fps

    if(g_antiAimType == 0){ // Spin
        antiAimAngle += (g_antiAimSpeed * deltaTime);
        if(antiAimAngle > 180.f) antiAimAngle -= 360.f;
        Wr<float>(vaAddr+4, antiAimAngle);
    }
    else if(g_antiAimType == 1){ // Desync (flip between left/right)
        static DWORD desyncTime = 0;
        DWORD now = GetTickCount();
        if(now - desyncTime > 100){
            antiAimAngle = (antiAimAngle > 0.f) ? -45.f : 45.f;
            desyncTime = now;
        }
        Wr<float>(vaAddr+4, yaw + antiAimAngle);
    }
    else if(g_antiAimType == 2){ // Jitter
        antiAimAngle = (sinf((float)GetTickCount()*0.01f)*30.f);
        Wr<float>(vaAddr+4, yaw + antiAimAngle);
    }
}

static void RunFOVChanger(){
    if(!g_fovEnabled||!g_client)return;
    if(g_origGetWorldFov) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    bool scoped=Rd<bool>(lp+offsets::cs_pawn::m_bIsScoped);if(scoped)return;
    uintptr_t camSvc=Rd<uintptr_t>(lp+offsets::base_pawn::m_pCameraServices);if(!camSvc)return;
    Wr<float>(camSvc+offsets::camera::m_iFOV,g_fovValue);
}

static void RunThirdPerson(){
    if(!g_client)return;
    __try{
        uintptr_t input=Rd<uintptr_t>(g_client+offsets::client::dwCSGOInput);
        uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
        if(!lp || lp <= 0x10000) return;
        int life = Rd<uint8_t>(lp + offsets::base_entity::m_lifeState);
        bool allowThird = g_thirdPerson && (life == 0);
        uintptr_t vaAddr=ViewAnglesAddr();
        if(input && input > 0x10000 && input < 0x7FFFFFFFFFFF){
            __try{
                Wr<uint8_t>(input+offsets::csgo_input::m_in_thirdperson, allowThird ? 1 : 0);
            }__except(EXCEPTION_EXECUTE_HANDLER){}

            if(allowThird){
                __try{
                    float pitch=Rd<float>(vaAddr);
                    float yaw=Rd<float>(vaAddr+4);
                    float pitchOffset = (g_tpHeightOffset / 100.f) * 8.f;
                    Wr<float>(input+offsets::csgo_input::m_third_person_angles, pitch - pitchOffset);
                    Wr<float>(input+offsets::csgo_input::m_third_person_angles+4, yaw);
                    Wr<float>(input+offsets::csgo_input::m_third_person_angles+8, 0.0f);
                }__except(EXCEPTION_EXECUTE_HANDLER){}
            }
        }

        __try{
            uintptr_t obs=Rd<uintptr_t>(lp+offsets::base_pawn::m_pObserverServices);
            if(obs && obs > 0x10000 && obs < 0x7FFFFFFFFFFF){
                if(allowThird){
                    Wr<uint8_t>(obs+offsets::observer::m_iObserverMode, 3);  // OBS_MODE_CHASE
                    Wr<uint8_t>(obs+offsets::observer::m_bForcedObserverMode, 1);
                    float dist = Clampf(g_tpDist, 50.f, 200.f);
                    Wr<float>(obs+offsets::observer::m_flObserverChaseDistance, dist);
                }else{
                    Wr<uint8_t>(obs+offsets::observer::m_bForcedObserverMode, 0);
                    Wr<uint8_t>(obs+offsets::observer::m_iObserverMode, 0);
                }
            }
            uintptr_t camSvc = Rd<uintptr_t>(lp + offsets::base_pawn::m_pCameraServices);
            if(camSvc && camSvc > 0x10000 && camSvc < 0x7FFFFFFFFFFF){
                if(allowThird){
                    float yaw=Rd<float>(vaAddr+4);
                    Wr<float>(camSvc + offsets::camera::m_thirdPersonHeading, yaw);
                }
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){}
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunRCS(){
    if(!g_rcsEnabled||!g_client)return;
    if(g_menuOpen)return;
    if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000)){g_rcsPrevPunchX=0.f;g_rcsPrevPunchY=0.f;return;}  // Only when shooting
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int shots=Rd<int>(lp+offsets::cs_pawn::m_iShotsFired);
    if(shots<2){g_rcsPrevPunchX=0.f;g_rcsPrevPunchY=0.f;return;}

    float punchX=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle);
    float punchY=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle+4);

    float dx=(punchX-g_rcsPrevPunchX)*g_rcsX;
    float dy=(punchY-g_rcsPrevPunchY)*g_rcsY;

    uintptr_t vaAddr=ViewAnglesAddr();
    float pitch=Rd<float>(vaAddr);float yaw=Rd<float>(vaAddr+4);
    float mult=2.0f;
    float smooth=Clampf(g_rcsSmooth,1.f,50.f);
    pitch-=(dx*mult)/smooth;
    yaw-=(dy*mult)/smooth;
    pitch=Clampf(pitch,-89.f,89.f);
    if(yaw>180.f)yaw-=360.f;else if(yaw<-180.f)yaw+=360.f;
    Wr<float>(vaAddr,pitch);
    Wr<float>(vaAddr+4,yaw);
    g_rcsPrevPunchX=punchX;g_rcsPrevPunchY=punchY;
}

// Auto Strafe (Darkside/help-learn): optimal turn = asin(sv_airaccel/speed) for max gain
static void RunStrafeHelper(){
    if(!g_strafeEnabled||!g_client) return;
    if(g_strafeKey!=0&&!(GetAsyncKeyState(g_strafeKey)&0x8000)) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn); if(!lp) return;
    if(Rd<uint32_t>(lp+offsets::base_entity::m_fFlags)&1) return;  // On ground - no strafe
    bool left=(GetAsyncKeyState('A')&0x8000)!=0;
    bool right=(GetAsyncKeyState('D')&0x8000)!=0;
    if(!left&&!right) return;
    uintptr_t vaAddr=ViewAnglesAddr();
    float curYaw=Rd<float>(vaAddr+4);
    Vec3 vel=Rd<Vec3>(lp+offsets::base_entity::m_vecVelocity);
    float speed2d=sqrtf(vel.x*vel.x+vel.y*vel.y);
    // Darkside formula: sv_airaccelerate default 12, optimal turn ~2*asin(12/speed) degrees
    const float sv_airaccel=12.f;
    float optimalTurn=15.f;
    if(speed2d>5.f){
        float ratio=Clampf(sv_airaccel/(speed2d+1.f),0.01f,1.f);
        optimalTurn=2.f*57.2958f*asinf(ratio);  // radians to degrees
        optimalTurn=Clampf(optimalTurn,2.f,25.f);
    }
    float turnAmount=(left?-optimalTurn:0.f)+(right?optimalTurn:0.f);
    float newYaw=curYaw+turnAmount;
    if(newYaw>180.f)newYaw-=360.f; else if(newYaw<-180.f)newYaw+=360.f;
    Wr<float>(vaAddr+4,newYaw);
}

static void RunTriggerBot(){
    if(!g_tbEnabled||!g_client)return;
    if(g_tbKey!=0&&!(GetAsyncKeyState(g_tbKey)&0x8000)){g_tbShouldFire=false;return;}
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int entIdx=Rd<int>(lp+offsets::cs_pawn::m_iIDEntIndex);if(entIdx<=0){g_tbShouldFire=false;return;}
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((entIdx&0x7FFF)>>9)+16);if(!pchunk)return;
    uintptr_t targPawn=Rd<uintptr_t>(pchunk+112*(entIdx&0x1FF));if(!targPawn){g_tbShouldFire=false;return;}
    int targTeam=(int)Rd<uint8_t>(targPawn+offsets::base_entity::m_iTeamNum);
    int targHealth=Rd<int>(targPawn+offsets::base_entity::m_iHealth);
    if(targHealth<=0){g_tbShouldFire=false;return;}
    if(g_tbTeamChk&&targTeam==g_esp_local_team){g_tbShouldFire=false;return;}
    if(!g_tbShouldFire){g_tbShouldFire=true;g_tbFireTime=GetTickCount()+(DWORD)g_tbDelay;}
    if(GetTickCount()>=g_tbFireTime){
        Wr<int>(g_client+offsets::buttons::attack,65537);
        g_tbShouldFire=false;
        g_tbJustFired=true;  // Release on next frame
    }
}

static void ReleaseTriggerAttack(){
    if(!g_client||!g_tbEnabled)return;
    if(g_tbJustFired){
        Wr<int>(g_client+offsets::buttons::attack,256);
        g_tbJustFired=false;
        return;
    }
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int entIdx=Rd<int>(lp+offsets::cs_pawn::m_iIDEntIndex);
    bool keyHeld=(g_tbKey==0)||(GetAsyncKeyState(g_tbKey)&0x8000);
    if(entIdx<=0||!keyHeld)Wr<int>(g_client+offsets::buttons::attack,256);
}

static void RunAimbot(){
    if(!g_aimbotEnabled||!g_client)return;
    if(!(GetAsyncKeyState(g_aimbotKey)&0x8000))return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    uintptr_t vaAddr=ViewAnglesAddr();if(!vaAddr)return;
    uintptr_t sc0=Rd<uintptr_t>(lp+offsets::base_entity::m_pGameSceneNode);Vec3 localOrigin{};
    if(sc0)localOrigin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);
    Vec3 eyePos=localOrigin+Rd<Vec3>(lp+offsets::base_pawn::m_vecViewOffset);
    float curPitch=Rd<float>(vaAddr);float curYaw=Rd<float>(vaAddr+4);
    float bestDist=g_aimbotFov;Vec3 bestPoint{};bool found=false;
    DWORD nowTick = GetTickCount();
    DWORD maxAge = (DWORD)g_backtrackMs;
    auto evalPoint = [&](const Vec3& p){
        Vec2 aimAngle=CalcAngle(eyePos,p);
        float dPitch=fabsf(AngleDiff(aimAngle.x,curPitch));
        float dYaw=fabsf(AngleDiff(aimAngle.y,curYaw));
        float fovDist=sqrtf(dPitch*dPitch+dYaw*dYaw);
        if(fovDist<bestDist){bestDist=fovDist;bestPoint=p;found=true;}
    };
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry&e=g_esp_players[i];if(!e.valid)continue;
        if(g_aimbotTeamChk&&e.team==g_esp_local_team)continue;
        if(e.distance>g_espMaxDist)continue;
        Vec3 origin{e.origin_x,e.origin_y,e.origin_z};
        Vec3 headWorld{e.head_ox,e.head_oy,e.head_oz};
        Vec3 viewOff=headWorld-origin;
        Vec3 aimPoint=origin+viewOff;
        if(g_backtrackEnabled && e.ent_index>0 && e.ent_index<=ESP_MAX_PLAYERS){
            auto& dq = g_backtrack[e.ent_index];
            for(const auto& rec : dq){
                if((DWORD)(nowTick - rec.time) > maxAge) continue;
                evalPoint(rec.head);
            }
        }
        if(g_aimbotBone==1 || g_aimbotBone==2){
            Vec3 bonePos{};
            int boneId = (g_aimbotBone==1) ? BONE_NECK : BONE_SPINE3;
            if(GetBonePos(e.pawn, boneId, bonePos)) aimPoint = bonePos;
            else {
                float boneFactor=(g_aimbotBone==1)?0.75f:0.5f;
                aimPoint=origin+viewOff*boneFactor;
            }
            evalPoint(aimPoint);
        }else if(g_aimbotBone==3){
            static const int bones[] = {BONE_HEAD,BONE_NECK,BONE_SPINE3,BONE_SPINE2,BONE_PELVIS};
            bool any=false;
            for(int b: bones){
                Vec3 bp{};
                if(!GetBonePos(e.pawn,b,bp)) continue;
                evalPoint(bp);
                any=true;
            }
            if(!any) evalPoint(aimPoint);
        }else{
            evalPoint(aimPoint);
        }
    }
    if(!found)return;
    Vec2 targetAngle=CalcAngle(eyePos,bestPoint);
    float smooth=Clampf(g_aimbotSmooth,1.f,50.f);
    float dp=AngleDiff(targetAngle.x,curPitch);
    float dy=AngleDiff(targetAngle.y,curYaw);
    float newPitch=curPitch+(dp/smooth);
    float newYaw=curYaw+(dy/smooth);
    newPitch=Clampf(newPitch,-89.f,89.f);
    if(newYaw>180.f)newYaw-=360.f;else if(newYaw<-180.f)newYaw+=360.f;
    Wr<float>(vaAddr,newPitch);Wr<float>(vaAddr+4,newYaw);
}

static void RunDoubleTap(){
    if(!g_dtEnabled||!g_client) return;
    if(!(GetAsyncKeyState(g_dtKey)&0x8000)) return;
    Wr<int>(g_client+offsets::buttons::attack,65537);
}

struct Particle{
    float x,y,vx,vy,size,lifetime,maxlife,phase;
    float rot, spin;
    ImU32 color;int type;
    Vec3 worldPos;bool is3D;
    Vec3 worldVel;
};
static std::vector<Particle>g_particles;
static std::mt19937 g_rng{12345u};
static float Randf(float lo,float hi){
    std::uniform_real_distribution<float>d(lo,hi);return d(g_rng);
}

struct Notification{char text[256];ImU32 color;float lifetime,maxlife,yOff,xOff;};
static std::deque<Notification>g_notifs;
struct MenuNote{char text[96];ImU32 color;};
static std::deque<MenuNote> g_menuNotes;

struct BulletTrace{Vec3 start,end;float lifetime,maxlife;ImU32 color;};
static std::deque<BulletTrace>g_traces;
static int g_lastShotsFired = 0;

struct SoundPing{Vec3 pos;float lifetime,maxlife;float radius;float height;};  // height for 3D vertical marker
static std::deque<SoundPing>g_soundPings;

static bool g_bombActive=false;
static int g_bombSite=-1;
static Vec3 g_bombPos{};
static float g_bombExplodeTime=0.f;
static bool g_bombDefusing=false;
static float g_bombDefuseEnd=0.f;
static float g_lastBombScan=0.f;

static void PushNotification(const char*text,ImU32 color){
    if(!text||!text[0])return;
    Notification n{};
    std::snprintf(n.text,sizeof(n.text),"%s",text);
    n.color=color;
    n.maxlife=2.5f;n.lifetime=2.5f;n.yOff=0.f;
    n.xOff=(float)g_esp_screen_w + 200.f;
    g_notifs.push_back(n);
    if(g_notifs.size()>8)g_notifs.pop_front();
    MenuNote m{};
    std::snprintf(m.text,sizeof(m.text),"%s",text);
    m.color=color;
    g_menuNotes.push_back(m);
    if(g_menuNotes.size()>10) g_menuNotes.pop_front();
}

static void PlayHitSound(int type){
    if(!g_hitSoundEnabled||type==0)return;
    switch(type){
        case 1: Beep(1000,40); break;
        case 2: Beep(1400,30); break;
        case 3: Beep(800,60); break;
        default: Beep(1100,35); break;
    }
}

static void PushLog(const char* text, ImU32 color){
    if(!text||!text[0]) return;
    LogEntry e{};
    std::snprintf(e.text,sizeof(e.text),"%s",text);
    e.color=color;
    e.maxlife=4.0f; e.lifetime=4.0f;
    g_logs.push_back(e);
    if(g_logs.size()>8) g_logs.pop_front();
}

static Vec3 AngleToForward(float pitch,float yaw){
    float p = pitch*(3.14159265f/180.f);
    float y = yaw*(3.14159265f/180.f);
    float cp = cosf(p), sp = sinf(p);
    float cy = cosf(y), sy = sinf(y);
    return {cp*cy, cp*sy, -sp};
}

static void UpdateBulletTraces(){
    if(!g_bulletTraceEnabled||!g_client) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int shots=Rd<int>(lp+offsets::cs_pawn::m_iShotsFired);
    if(shots>g_lastShotsFired){
        uintptr_t sc0=Rd<uintptr_t>(lp+offsets::base_entity::m_pGameSceneNode);
        Vec3 origin{};if(sc0)origin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);
        Vec3 eye=origin+Rd<Vec3>(lp+offsets::base_pawn::m_vecViewOffset);
        uintptr_t vaAddr=ViewAnglesAddr();
        float pitch=Rd<float>(vaAddr);float yaw=Rd<float>(vaAddr+4);
        Vec3 fwd=AngleToForward(pitch,yaw);
        BulletTrace t{};t.start=eye;t.end=eye+fwd*3000.f;t.maxlife=1.0f;t.lifetime=1.0f;
        t.color=IM_COL32((int)(g_impactCol[0]*255),(int)(g_impactCol[1]*255),(int)(g_impactCol[2]*255),(int)(g_impactCol[3]*255));
        g_traces.push_back(t);
        if(g_traces.size()>24) g_traces.pop_front();
    }
    g_lastShotsFired=shots;
}

static void DrawBulletTraces(float dt){
    if(!g_bulletTraceEnabled){g_traces.clear();return;}
    if(g_traces.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    if(!vm) return;
    for(auto& t: g_traces){
        t.lifetime-=dt;
        float a=Clampf(t.lifetime/t.maxlife,0.f,1.f);
        float sx,sy,ex,ey;
        if(WorldToScreen(vm,t.start,g_esp_screen_w,g_esp_screen_h,sx,sy)&&WorldToScreen(vm,t.end,g_esp_screen_w,g_esp_screen_h,ex,ey)){
            ImU32 col=IM_COL32((t.color>>IM_COL32_R_SHIFT)&0xFF,(t.color>>IM_COL32_G_SHIFT)&0xFF,(t.color>>IM_COL32_B_SHIFT)&0xFF,(int)(255*a));
            dl->AddLine({sx,sy},{ex,ey},col,1.3f);
        }
    }
    g_traces.erase(std::remove_if(g_traces.begin(),g_traces.end(),[](const BulletTrace& t){return t.lifetime<=0.f;}),g_traces.end());
}

static void UpdateSoundPings(){
    if(!g_soundEnabled||!g_client) return;
    if(!g_soundBlipEnemy && !g_soundBlipTeam) return;
    DWORD nowTick = GetTickCount();
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry& e = g_esp_players[i];
        if(!e.valid) continue;
        bool isEnemy = (e.team != g_esp_local_team);
        if(isEnemy && !g_soundBlipEnemy) continue;
        if(!isEnemy && !g_soundBlipTeam) continue;
        Vec3 vel = Rd<Vec3>(e.pawn + offsets::base_entity::m_vecVelocity);
        float spd = sqrtf(vel.x*vel.x+vel.y*vel.y+vel.z*vel.z);
        if(spd>80.f && e.ent_index>0 && e.ent_index<=ESP_MAX_PLAYERS){
            if(nowTick - g_lastSoundPingTick[e.ent_index] < 350) continue;
            g_lastSoundPingTick[e.ent_index] = nowTick;
            float scale = Clampf(g_soundPuddleScale, 0.3f, 3.0f);
            float size = (18.f + Clampf(spd/250.f, 0.f, 1.f) * 18.f) * scale;
            SoundPing p{};p.pos={e.origin_x,e.origin_y,e.origin_z};p.maxlife=0.5f;p.lifetime=0.5f;p.radius=size;p.height=0.f;
            g_soundPings.push_back(p);
            if(g_soundPings.size()>32) g_soundPings.pop_front();
        }
    }
}

static void DrawSoundPings(float dt){
    if(!g_soundEnabled){g_soundPings.clear();return;}
    if(g_soundPings.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    if(!vm) return;
    int colR=(int)(g_soundBlipCol[0]*255), colG=(int)(g_soundBlipCol[1]*255), colB=(int)(g_soundBlipCol[2]*255);
    for(auto& p: g_soundPings){
        p.lifetime-=dt;
        float life = Clampf(p.lifetime/p.maxlife,0.f,1.f);
        float t = 1.f - life;
        float fadeIn = Clampf(t/0.08f, 0.f, 1.f);
        float fadeOut = Clampf(life/0.2f, 0.f, 1.f);
        float alpha = fadeIn * fadeOut * Clampf(g_soundPuddleAlpha, 0.f, 2.0f);
        alpha = Clampf(alpha, 0.f, 1.f);
        float grow = LerpF(0.3f, 1.f, t);
        float rad = p.radius * grow;
        const int segs = 36;
        ImVec2 pts[segs+1];
        int validCount = 0;
        for(int i=0;i<=segs;i++){
            float ang = (float)i * (6.283185f / (float)segs);
            Vec3 wp{p.pos.x + rad*cosf(ang), p.pos.y + rad*sinf(ang), p.pos.z};
            float sx, sy;
            if(WorldToScreen(vm, wp, g_esp_screen_w, g_esp_screen_h, sx, sy)){
                pts[validCount++] = {sx, sy};
            }
        }
        if(validCount < 3) continue;
        for(int ring = 4; ring >= 0; ring--){
            float rMul = 0.25f + (float)ring * 0.2f;
            int a = (int)((80 - ring*12) * alpha);
            if(a < 8) continue;
            ImU32 ringCol = IM_COL32(colR, colG, colB, a);
            ImVec2 rPts[segs+1];
            int rCnt = 0;
            for(int i=0;i<=segs;i++){
                float ang = (float)i * (6.283185f / (float)segs);
                Vec3 wp{p.pos.x + rad*rMul*cosf(ang), p.pos.y + rad*rMul*sinf(ang), p.pos.z};
                float sx, sy;
                if(WorldToScreen(vm, wp, g_esp_screen_w, g_esp_screen_h, sx, sy)){
                    rPts[rCnt++] = {sx, sy};
                }
            }
            if(rCnt >= 3){
                for(int j=1;j<rCnt;j++) dl->AddLine(rPts[j-1], rPts[j], ringCol, 1.2f);
                if(rCnt>1) dl->AddLine(rPts[rCnt-1], rPts[0], ringCol, 1.2f);
            }
        }
        ImU32 fillCol = IM_COL32(colR, colG, colB, (int)(40*alpha));
        dl->AddConvexPolyFilled(pts, validCount, fillCol);
        ImU32 strokeCol = IM_COL32(colR, colG, colB, (int)(200*alpha));
        for(int j=1;j<validCount;j++) dl->AddLine(pts[j-1], pts[j], strokeCol, 1.5f);
        if(validCount>1) dl->AddLine(pts[validCount-1], pts[0], strokeCol, 1.5f);
    }
    g_soundPings.erase(std::remove_if(g_soundPings.begin(),g_soundPings.end(),[](const SoundPing& p){return p.lifetime<=0.f;}),g_soundPings.end());
}

static void DrawLogs(float dt,float sw,float sh){
    if(g_logs.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImFont* mainFont=font::lexend_bold?font::lexend_bold:ImGui::GetFont();
    ImFont* iconFont=font::icomoon;
    float cx=sw*0.5f,cy=sh*0.5f,y=cy+28.f;
    static const char hitIcon[] = "\xee\x80\x81";   // crosshair (icomoon PUA)
    static const char killIcon[] = "\xee\x80\x82";  // skull (icomoon PUA)
    for(auto& l: g_logs){
        l.lifetime-=dt;
        float a=Clampf(l.lifetime/l.maxlife,0.f,1.f);
        ImU32 col=IM_COL32((l.color>>IM_COL32_R_SHIFT)&0xFF,(l.color>>IM_COL32_G_SHIFT)&0xFF,(l.color>>IM_COL32_B_SHIFT)&0xFF,(int)(255*a));
        ImVec2 ts=mainFont->CalcTextSizeA(14.f,FLT_MAX,0.f,l.text);
        float x=cx-ts.x*0.5f;
        float iconW=0.f;
        if(iconFont){
            const char* icon=(l.type==1)?killIcon:hitIcon;
            ImVec2 is=iconFont->CalcTextSizeA(14.f,FLT_MAX,0.f,icon);
            iconW=is.x+4.f;
            x-=iconW*0.5f;
            dl->AddText(iconFont,14.f,{x+1.f,y+1.f},IM_COL32(0,0,0,(int)(180*a)),icon);
            dl->AddText(iconFont,14.f,{x,y},col,icon);
        }
        x+=iconW;
        dl->AddText(mainFont,14.f,{x+1.f,y+1.f},IM_COL32(0,0,0,(int)(180*a)),l.text);
        dl->AddText(mainFont,14.f,{x,y},col,l.text);
        y+=18.f;
    }
    g_logs.erase(std::remove_if(g_logs.begin(),g_logs.end(),[](const LogEntry& l){return l.lifetime<=0.f;}),g_logs.end());
}

// Bomb timer only shown when player is within this distance (avoids stale data when entity is dormant)
static const float g_bombTimerMaxDist = 1500.f;

static void UpdateBombInfo(){
    if(!g_bombTimerEnabled||!g_client) return;
    float now = GetCurTime();
    if(now - g_lastBombScan < 0.2f) return;
    g_lastBombScan = now;
    g_bombActive=false;g_bombSite=-1;g_bombExplodeTime=0.f;g_bombDefusing=false;g_bombDefuseEnd=0.f;g_bombPos={};
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    for(int i=1;i<1024;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ent=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ent)continue;
        bool ticking=Rd<bool>(ent+offsets::planted_c4::m_bBombTicking);
        if(!ticking) continue;
        float blow=Rd<float>(ent+offsets::planted_c4::m_flC4Blow);
        if(blow<=0.f) continue;
        uintptr_t scn = Rd<uintptr_t>(ent+offsets::base_entity::m_pGameSceneNode);
        if(scn) g_bombPos = Rd<Vec3>(scn+offsets::scene_node::m_vecAbsOrigin);
        // Only consider active when player is near bomb (far entities are dormant, timer data is stale)
        float dx = g_bombPos.x - g_localOrigin.x, dy = g_bombPos.y - g_localOrigin.y;
        if(dx*dx + dy*dy > g_bombTimerMaxDist*g_bombTimerMaxDist) continue;
        g_bombActive=true;
        g_bombSite=Rd<int>(ent+offsets::planted_c4::m_nBombSite);
        g_bombExplodeTime=blow;
        g_bombDefusing=Rd<bool>(ent+offsets::planted_c4::m_bBeingDefused);
        if(g_bombDefusing) g_bombDefuseEnd=Rd<float>(ent+offsets::planted_c4::m_flDefuseCountDown);
        break;
    }
}

static void DrawBombTimer(float sw){
    if(!g_bombTimerEnabled||!g_bombActive) return;
    float now = GetCurTime();
    float tLeft = g_bombExplodeTime - now;
    if(tLeft <= 0.f) return;  // bomb exploded, hide timer
    if(tLeft > 42.f) return;  // invalid/stale data (bomb is 40s max)
    char buf[128];
    if(g_bombDefusing){
        float dLeft = g_bombDefuseEnd - now;
        if(dLeft < 0.f) dLeft = 0.f;
        std::snprintf(buf,sizeof(buf),"Bomb %c | %.1fs | Defuse %.1fs", g_bombSite==1?'B':'A', tLeft, dLeft);
    }else{
        std::snprintf(buf,sizeof(buf),"Bomb %c | %.1fs", g_bombSite==1?'B':'A', tLeft);
    }
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImVec2 ts=ImGui::CalcTextSize(buf);
    float pad=6.f;
    ImVec2 pos{sw*0.5f-ts.x*0.5f-pad,30.f};
    dl->AddRectFilled(pos,{pos.x+ts.x+pad*2.f,pos.y+ts.y+pad*2.f},IM_COL32(20,20,25,200),6.f);
    dl->AddText({pos.x+pad,pos.y+pad},IM_COL32(255,180,90,255),buf);
}

static bool IsInputMessage(UINT msg){
    if(msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST) return true;
    if(msg>=WM_KEYFIRST && msg<=WM_KEYLAST) return true;
    switch(msg){
        case WM_INPUT:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        default:
            return false;
    }
}

static LRESULT CALLBACK HookWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(g_imguiInitialized&&g_menuOpen){
        if(ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp)) return true;
        if(IsInputMessage(msg)) return true;
    }
    if(!g_origWndProc)return DefWindowProcA(hwnd,msg,wp,lp);
    return CallWindowProcA(g_origWndProc,hwnd,msg,wp,lp);
}

static const char* KeyName(int vk){
    static char buf[32];
    if(vk==0)return "None";
    if(vk==VK_LBUTTON)return "LMB";
    if(vk==VK_RBUTTON)return "RMB";
    if(vk==VK_MBUTTON)return "MMB";
    if(vk==VK_XBUTTON1)return "X1";
    if(vk==VK_XBUTTON2)return "X2";
    UINT sc=MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if(GetKeyNameTextA((LONG)(sc<<16), buf, sizeof(buf))) return buf;
    std::snprintf(buf,sizeof(buf),"VK_%d",vk);
    return buf;
}

static bool KeyBindWidget(const char* label, int* key){
    static int* capture=nullptr;
    ImGui::PushID(label);
    bool changed=false;
    const bool active = (capture==key);
    const bool hovered = ImGui::IsMouseHoveringRect(ImGui::GetCursorScreenPos(),
        {ImGui::GetCursorScreenPos().x+90.f, ImGui::GetCursorScreenPos().y+28.f});

    ThemeColors theme = GetThemeColors();
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* btn = active ? "..." : KeyName(*key);

    // Button background
    ImU32 btnBg = active ? IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),200)
                        : (hovered ? IM_COL32(50,50,70,180) : IM_COL32(35,35,50,150));
    dl->AddRectFilled(btnPos, {btnPos.x+90.f, btnPos.y+26.f}, btnBg, 5.f);
    dl->AddRect(btnPos, {btnPos.x+90.f, btnPos.y+26.f},
        IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),150), 5.f, 0, 1.5f);

    ImGui::Button(btn, ImVec2(80,0));
    if(ImGui::IsItemClicked()){
        capture = key;
    }
    if(active){
        for(int vk=1; vk<256; ++vk){
            if(GetAsyncKeyState(vk)&1){
                if(vk==VK_ESCAPE)*key=0; else *key=vk;
                capture=nullptr; changed=true; break;
            }
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, hovered ? theme.text : theme.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return changed;
}

static bool ToggleSwitch(const char* label, bool* v, int idx){
    float& anim = g_toggleAnim[idx % 128];
    ImGuiIO& io = ImGui::GetIO();
    anim = LerpF(anim, *v ? 1.f : 0.f, io.DeltaTime * 18.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ThemeColors theme = GetThemeColors();
    float h=22.f, w=48.f, r=h*0.5f;

    // Glow effect on enabled state
    float glowIntensity = 0.5f + sinf((float)ImGui::GetTime()*3.0f)*0.3f;
    if(*v && g_menuTheme==1) { // Glassmorphism glow
        dl->AddRectFilled(p,{p.x+w,p.y+h},IM_COL32((int)(g_accentColor[0]*255.f*glowIntensity),(int)(g_accentColor[1]*255.f*glowIntensity),(int)(g_accentColor[2]*255.f*glowIntensity),50),r);
    }

    // Background track with smooth color transition
    ImU32 trackCol = IM_COL32(
        (int)LerpF((float)((theme.toggleBg>>0)&0xFF), (float)((theme.toggleFill>>0)&0xFF), anim),
        (int)LerpF((float)((theme.toggleBg>>8)&0xFF), (float)((theme.toggleFill>>8)&0xFF), anim),
        (int)LerpF((float)((theme.toggleBg>>16)&0xFF), (float)((theme.toggleFill>>16)&0xFF), anim),
        (int)LerpF(140.f, 240.f, anim));

    dl->AddRectFilled(p,{p.x+w,p.y+h},trackCol,r);

    // Border glow
    ImU32 borderCol = IM_COL32((int)(g_accentColor[0]*255.f*0.8f),(int)(g_accentColor[1]*255.f*0.8f),(int)(g_accentColor[2]*255.f*0.8f),(int)(150.f*anim));
    dl->AddRect(p,{p.x+w,p.y+h},borderCol,r,0,2.f);

    // Shadow layers per theme
    if(g_menuTheme==2) { // Neumorphism shadow
        dl->AddRectFilled({p.x+2.f,p.y+2.f},{p.x+w-2.f,p.y+h-2.f},IM_COL32(0,0,0,40),r);
    }

    // Knob with enhanced animation
    float knobX = p.x+r+anim*(w-h);
    dl->AddCircleFilled({knobX,p.y+r},r-3.f,IM_COL32(248,250,255,255),20);

    // Knob border effects
    if(g_menuTheme==2) { // Neumorphism
        dl->AddCircle({knobX,p.y+r},r-3.f,IM_COL32(0,0,0,60),20,2.2f);
    } else if(g_menuTheme==1) { // Glassmorphism
        dl->AddCircle({knobX,p.y+r},r-3.f,IM_COL32((int)(g_accentColor[0]*255.f),(int)(g_accentColor[1]*255.f),(int)(g_accentColor[2]*255.f),(int)(100.f*anim)),20,1.5f);
    } else { // Dark Pro - accent glow
        dl->AddCircle({knobX,p.y+r},r-3.f,IM_COL32((int)(g_accentColor[0]*255.f),(int)(g_accentColor[1]*255.f),(int)(g_accentColor[2]*255.f),(int)(80.f*anim)),20,1.8f);
    }

    ImGui::InvisibleButton(label,{w+8.f,h});
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    if(clicked) *v=!*v;

    ImGui::SameLine();
    ImVec4 textCol = hovered ? ImVec4((float)((theme.text>>0)&0xFF)/255.f,(float)((theme.text>>8)&0xFF)/255.f,(float)((theme.text>>16)&0xFF)/255.f,1.f) :
                              ImVec4((float)((theme.textDim>>0)&0xFF)/255.f,(float)((theme.textDim>>8)&0xFF)/255.f,(float)((theme.textDim>>16)&0xFF)/255.f,0.7f);
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    const char* end = strstr(label, "##");
    if(end) ImGui::TextUnformatted(label, end);
    else ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    return false;
}

#pragma warning(push)
#pragma warning(disable:4505)
static void SectionHeader(const char* text){
    ImGui::Spacing();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ThemeColors theme = GetThemeColors();
    ImU32 acc = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),240);
    float lineY = p.y + ts.y*0.5f;
    float glowIntensity = 0.6f + sinf((float)ImGui::GetTime()*2.0f)*0.4f;

    if(g_menuTheme==2) { // Neumorphism - enhanced embossed effect
        // Outer shadow (more dramatic)
        dl->AddRectFilled({p.x,lineY-1.5f},{p.x+ts.x+60.f,lineY+1.5f},IM_COL32(8,10,14,100),2.f);
        // Main line
        dl->AddRectFilled({p.x,lineY-0.8f},{p.x+ts.x+60.f,lineY+0.8f},IM_COL32(70,90,120,150),1.f);
        // Highlight (enhanced)
        dl->AddRectFilled({p.x,lineY-0.3f},{p.x+ts.x+60.f,lineY+0.3f},IM_COL32(150,170,200,100),0.5f);
    } else if(g_menuTheme==1) { // Glassmorphism - enhanced glow effect
        float glow = sinf((float)ImGui::GetTime()*2.0f)*25.f+55.f;
        dl->AddRectFilledMultiColor({p.x,lineY-0.8f},{p.x+ts.x+70.f,lineY+0.8f},
            acc,IM_COL32((int)(g_accentColor[0]*255.f),(int)(g_accentColor[1]*255.f),(int)(g_accentColor[2]*255.f),(int)glow),
            IM_COL32(0,0,0,0),acc);
        // Glow layer (stronger)
        dl->AddRectFilled({p.x-3.f,lineY-1.2f},{p.x+ts.x+67.f,lineY+1.2f},
            IM_COL32((int)(g_accentColor[0]*200.f),(int)(g_accentColor[1]*200.f),(int)(g_accentColor[2]*200.f),(int)(glow*0.5f)),3.f);
    } else { // Dark Pro - enhanced gradient with glow
        dl->AddRectFilledMultiColor({p.x,lineY-0.7f},{p.x+ts.x+65.f,lineY+0.7f},
            acc,IM_COL32((int)(g_accentColor[0]*255.f*(0.7f+glowIntensity*0.3f)),(int)(g_accentColor[1]*255.f*(0.7f+glowIntensity*0.3f)),(int)(g_accentColor[2]*255.f*(0.7f+glowIntensity*0.3f)),200),
            IM_COL32(0,0,0,0),IM_COL32(0,0,0,0));
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(g_accentColor[0],g_accentColor[1],g_accentColor[2],1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Enhanced combo with theme styling
static bool StyledCombo(const char* label, int* current_item, const char* const items[], int items_count) {
    ThemeColors theme = GetThemeColors();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f);

    float glowIntensity = 0.5f + sinf((float)ImGui::GetTime() * 2.f) * 0.2f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(35.f/255.f, 35.f/255.f, 50.f/255.f, 160.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(45.f/255.f, 45.f/255.f, 65.f/255.f, 200.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4((g_accentColor[0]*0.4f), (g_accentColor[1]*0.4f), (g_accentColor[2]*0.4f), 0.7f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4((float)((theme.bg>>0)&0xFF)/255.f, (float)((theme.bg>>8)&0xFF)/255.f, (float)((theme.bg>>16)&0xFF)/255.f, 230.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(g_accentColor[0]*0.6f, g_accentColor[1]*0.6f, g_accentColor[2]*0.6f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(g_accentColor[0]*0.8f, g_accentColor[1]*0.8f, g_accentColor[2]*0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(g_accentColor[0]*glowIntensity, g_accentColor[1]*glowIntensity, g_accentColor[2]*glowIntensity, 0.8f));

    bool changed = ImGui::Combo(label, current_item, items, items_count, 5);

    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(4);

    return changed;
}

// Simplified slider for new menu
static bool StyledSliderFloat(const char* label, const char* format, float* v, float v_min, float v_max) {
    ImGui::PushItemWidth(-60.f);
    bool changed = ImGui::SliderFloat(label, v, v_min, v_max, format);
    ImGui::PopItemWidth();
    return changed;
}

// Enhanced slider for integers with glow effect
static bool StyledSliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") {
    ThemeColors theme = GetThemeColors();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);

    float glowIntensity = 0.5f + sinf((float)ImGui::GetTime() * 2.f) * 0.3f;
    ImVec4 glowCol(g_accentColor[0]*glowIntensity, g_accentColor[1]*glowIntensity, g_accentColor[2]*glowIntensity, 0.6f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(40.f/255.f, 40.f/255.f, 55.f/255.f, 160.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(50.f/255.f, 50.f/255.f, 70.f/255.f, 200.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(g_accentColor[0]*1.2f, g_accentColor[1]*1.2f, g_accentColor[2]*1.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, glowCol);

    bool changed = ImGui::SliderInt(label, v, v_min, v_max, format);

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);

    return changed;
}

// Enhanced color picker with theme styling and glow
static bool StyledColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0) {
    ThemeColors theme = GetThemeColors();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);

    float glowIntensity = 0.6f + sinf((float)ImGui::GetTime() * 2.5f) * 0.4f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(35.f/255.f, 35.f/255.f, 50.f/255.f, 160.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(50.f/255.f, 50.f/255.f, 70.f/255.f, 200.f/255.f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col[0], col[1], col[2], col[3]*0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col[0]*1.2f, col[1]*1.2f, col[2]*1.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(g_accentColor[0]*glowIntensity, g_accentColor[1]*glowIntensity, g_accentColor[2]*glowIntensity, 0.8f));

    bool changed = ImGui::ColorEdit4(label, col, flags);

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);

    return changed;
}

static const char* LabelTextEnd(const char* label){
    const char* end = label;
    while(*end && !(end[0]=='#' && end[1]=='#')) ++end;
    return end;
}

struct ScrollAnim {
    float target = 0.f;
    bool init = false;
};
static std::unordered_map<ImGuiID, ScrollAnim> g_scrollAnims;

static void SmoothScrollCurrentWindow(float speed = 45.f, float damping = 14.f){
    float maxY = ImGui::GetScrollMaxY();
    if(maxY <= 0.f) return;
    ImGuiID id = ImGui::GetID("##smooth_scroll");
    auto& st = g_scrollAnims[id];
    float cur = ImGui::GetScrollY();
    if(!st.init){ st.target = cur; st.init = true; }

    ImGuiIO& io = ImGui::GetIO();
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)){
        if(io.MouseWheel != 0.f){
            st.target = Clampf(st.target - io.MouseWheel * speed, 0.f, maxY);
        }else if(ImGui::IsMouseDragging(0)){
            st.target = cur;
        }
    }
    st.target = Clampf(st.target, 0.f, maxY);
    float t = 1.0f - expf(-damping * io.DeltaTime);
    float next = LerpF(cur, st.target, t);
    ImGui::SetScrollY(next);
}

static void PidoSection(const char* title){
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(g_pido.textDim));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
}

static bool BeginPidoChild(const char* id, const ImVec2& size){
    float scale = MenuScale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * scale, 10.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f * scale, 10.f * scale));
    bool open = ImGui::BeginChild(id, size, false);
    ImGui::SetWindowFontScale(scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 s = ImGui::GetWindowSize();
    dl->AddRectFilled(p, {p.x+s.x, p.y+s.y}, g_pido.bgFill, 6.f * scale);
    dl->AddRect(p, {p.x+s.x, p.y+s.y}, IM_COL32(0,0,0,55), 6.f * scale);
    return open;
}

static void EndPidoChild(){
    SmoothScrollCurrentWindow(45.f * MenuScale(), 14.f);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

static bool PidoTab(const char* icon, const char* label, const char* desc, bool selected){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(180.f * s, 50.f * s);
    ImGui::InvisibleButton("##tab", size);
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = selected ? g_pido.tabActive : (hovered ? IM_COL32(18,18,22,255) : IM_COL32(0,0,0,0));
    if((bg >> 24) != 0) dl->AddRectFilled(pos, {pos.x+size.x, pos.y+size.y}, bg, 4.f * s);
    if(selected) dl->AddRectFilled({pos.x+3.f*s, pos.y+6.f*s}, {pos.x+5.f*s, pos.y+size.y-6.f*s}, g_pido.accent, 2.f * s);

    ImU32 iconCol = selected ? g_pido.accent : (hovered ? g_pido.textActive : g_pido.textDim);
    ImU32 textCol = selected ? g_pido.textActive : (hovered ? g_pido.textActive : g_pido.textDim);

    if(font::icomoon){
        dl->AddText(font::icomoon, 18.f * s, {pos.x+12.f*s, pos.y+size.y*0.5f-9.f*s}, iconCol, icon);
    }
    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (size.y - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+36.f*s, labelY}, textCol, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+36.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return pressed;
}

static bool PidoToggle(const char* label, const char* desc, bool* v){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::InvisibleButton("##row", ImVec2(width, height));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    if(pressed) *v = !*v;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, bg, 4.f * s);
    dl->AddRect(pos, {pos.x+width, pos.y+height}, g_pido.elemStroke, 4.f * s);
    if(*v) dl->AddRectFilled({pos.x+3.f*s, pos.y+6.f*s}, {pos.x+5.f*s, pos.y+height-6.f*s}, g_pido.accent, 2.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    float tW = 36.f * s, tH = 18.f * s, r = tH*0.5f;
    ImVec2 tPos{pos.x+width - tW - 10.f*s, pos.y + (height - tH)*0.5f};
    ImU32 tBg = *v ? g_pido.accent : IM_COL32(30,30,30,255);
    dl->AddRectFilled(tPos, {tPos.x+tW, tPos.y+tH}, tBg, r);
    float knobX = tPos.x + r + (*v ? (tW - tH) : 0.f);
    dl->AddCircleFilled({knobX, tPos.y + r}, r-2.f, IM_COL32(245,245,250,255), 20);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return pressed;
}

static bool PidoSliderFloat(const char* label, const char* desc, float* v, float v_min, float v_max, const char* format = "%.1f"){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(bbMin, bbMax, bg, 4.f * s);
    dl->AddRect(bbMin, bbMax, g_pido.elemStroke, 4.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    float sliderW = (std::min)(160.f * s, width * 0.45f);
    ImGui::SetCursorScreenPos({bbMax.x - sliderW - 10.f*s, bbMin.y + (height - 20.f*s)*0.5f});
    ImGui::PushItemWidth(sliderW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    bool changed = ImGui::SliderFloat("##slider", v, v_min, v_max, format);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return changed;
}

static bool PidoSliderInt(const char* label, const char* desc, int* v, int v_min, int v_max, const char* format = "%d"){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(bbMin, bbMax, bg, 4.f * s);
    dl->AddRect(bbMin, bbMax, g_pido.elemStroke, 4.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    float sliderW = (std::min)(160.f * s, width * 0.45f);
    ImGui::SetCursorScreenPos({bbMax.x - sliderW - 10.f*s, bbMin.y + (height - 20.f*s)*0.5f});
    ImGui::PushItemWidth(sliderW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    bool changed = ImGui::SliderInt("##slider", v, v_min, v_max, format);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return changed;
}

static bool PidoCombo(const char* label, const char* desc, int* current_item, const char* const items[], int items_count){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(bbMin, bbMax, bg, 4.f * s);
    dl->AddRect(bbMin, bbMax, g_pido.elemStroke, 4.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    float comboW = (std::min)(170.f * s, width * 0.5f);
    ImGui::SetCursorScreenPos({bbMax.x - comboW - 10.f*s, bbMin.y + (height - 24.f*s)*0.5f});
    ImGui::PushItemWidth(comboW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(18.f/255.f,18.f/255.f,18.f/255.f,0.95f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(g_pido.tabActive));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    bool changed = ImGui::Combo("##combo", current_item, items, items_count);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return changed;
}

static bool PidoColorEdit4(const char* label, const char* desc, float col[4], ImGuiColorEditFlags flags = 0){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(bbMin, bbMax, bg, 4.f * s);
    dl->AddRect(bbMin, bbMax, g_pido.elemStroke, 4.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    ImGui::SetCursorScreenPos({bbMax.x - 46.f*s, bbMin.y + (height - 22.f*s)*0.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    bool changed = ImGui::ColorEdit4("##col", col, flags | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::PopStyleVar();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return changed;
}

static bool PidoInputText(const char* label, const char* desc, char* buf, size_t bufSize){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(bbMin, bbMax, bg, 4.f * s);
    dl->AddRect(bbMin, bbMax, g_pido.elemStroke, 4.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    float inputW = (std::min)(200.f * s, width * 0.55f);
    ImGui::SetCursorScreenPos({bbMax.x - inputW - 10.f*s, bbMin.y + (height - 24.f*s)*0.5f});
    ImGui::PushItemWidth(inputW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    bool changed = ImGui::InputText("##input", buf, bufSize, ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return changed;
}

static bool PidoKeybind(const char* label, const char* desc, int* key){
    static int* capture = nullptr;
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 50.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(15,17,20,255) : g_pido.elemBg;
    dl->AddRectFilled(bbMin, bbMax, bg, 4.f * s);
    dl->AddRect(bbMin, bbMax, g_pido.elemStroke, 4.f * s);

    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+6.f*s : pos.y + (height - bold->FontSize*s)*0.5f;
    dl->AddText(bold, 14.f * s, {pos.x+10.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 12.f * s, {pos.x+10.f*s, pos.y+26.f*s}, g_pido.textDim, desc);

    float btnW = 80.f * s, btnH = 26.f * s;
    ImGui::SetCursorScreenPos({bbMax.x - btnW - 10.f*s, bbMin.y + (height - btnH)*0.5f});
    const char* btn = (capture == key) ? "..." : KeyName(*key);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(35.f/255.f,35.f/255.f,50.f/255.f,1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    if(ImGui::Button(btn, ImVec2(btnW, btnH))){
        capture = key;
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if(capture == key){
        for(int vk=1; vk<256; ++vk){
            if(GetAsyncKeyState(vk)&1){
                if(vk==VK_ESCAPE) *key = 0; else *key = vk;
                capture = nullptr;
                break;
            }
        }
    }

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::PopID();
    return false;
}

static bool PidoButton(const char* label, const ImVec2& size){
    float s = MenuScale();
    ImVec2 sz = size;
    if(sz.x > 0.f) sz.x *= s;
    if(sz.y > 0.f) sz.y *= s;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(g_pido.tabActive));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    bool pressed = ImGui::Button(label, sz);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return pressed;
}

static void DrawFakeESPPreview(const ImVec2& size, int pos){
    (void)pos;
    ImGui::BeginChild("##esp_preview", size, true);
    ImDrawList*dl=ImGui::GetWindowDrawList();
    ImVec2 p=ImGui::GetWindowPos();
    ImVec2 s=ImGui::GetWindowSize();
    ImVec2 c{p.x+s.x*0.5f,p.y+s.y*0.55f};
    float boxH=s.y*0.6f,boxW=boxH*0.42f;
    ImU32 eCol=IM_COL32((int)(g_espEnemyCol[0]*255),(int)(g_espEnemyCol[1]*255),(int)(g_espEnemyCol[2]*255),255);
    ImU32 tCol=IM_COL32((int)(g_espTeamCol[0]*255),(int)(g_espTeamCol[1]*255),(int)(g_espTeamCol[2]*255),255);
    auto drawBox=[&](ImVec2 center,ImU32 col){
        float l=center.x-boxW*0.5f,r=center.x+boxW*0.5f,t=center.y-boxH*0.5f,b=center.y+boxH*0.5f;
        if(g_espBoxStyle==3){
            ImU32 accent=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),220);
            dl->AddRectFilled({l,t},{r,b},IM_COL32(15,15,20,180),2.f);
            dl->AddRect({l,t},{r,b},IM_COL32(0,0,0,200),2.f,0,g_espBoxThick+1.f);
            dl->AddRect({l,t},{r,b},col,2.f,0,g_espBoxThick);
            dl->AddRectFilled({l,t},{r,t+3.f},accent,2.f,ImDrawFlags_RoundCornersTop);
        }else if(g_espBoxStyle==2){
            dl->AddRectFilled({l,t},{r,b},IM_COL32(20,20,28,120),2.f);
            DrawCornerBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==1){
            dl->AddRect({l,t},{r,b},col,0.f,0,g_espBoxThick);
        }else{
            DrawCornerBox(dl,l,t,r,b,col,g_espBoxThick);
        }
        if(g_espHeadDot){
            float dotR = 7.f;
            dl->AddCircle({center.x, t+6.f},dotR,IM_COL32(0,0,0,180),16,1.5f);
            dl->AddCircleFilled({center.x, t+6.f},dotR*0.55f,col,12);
            dl->AddCircle({center.x, t+6.f},dotR,col,16,1.0f);
        }
        if(g_espHealth){
            float barW=4.f;float bx=l-6.f-barW;
            dl->AddRectFilled({bx,t},{bx+barW,b},IM_COL32(15,15,20,200),2.f);
            dl->AddRectFilled({bx,t+boxH*0.3f},{bx+barW,b},HealthCol(70),2.f);
        }
    };
    drawBox({c.x-60.f,c.y}, eCol);
    drawBox({c.x+60.f,c.y}, eCol);
    drawBox({c.x,c.y+10.f}, tCol);
    ImGui::EndChild();
}

static void DrawMenu(){
    ImGuiIO&io=ImGui::GetIO();
    float target = g_menuOpen ? 1.f : 0.f;
    float speed = Clampf(g_menuAnimSpeed, 2.f, 30.f);
    float t = 1.0f - expf(-speed * io.DeltaTime);
    g_menuAnim = LerpF(g_menuAnim, target, t);
    g_menuAnim = Clampf(g_menuAnim, 0.f, 1.f);
    if(!g_menuOpen && g_menuAnim < 0.01f) return;

    UpdatePidoraisePalette(g_menuAnim);

    static int page = g_activeTab;
    static float tabAlpha = 0.f;

    float menuScale = MenuScale();
    float animScale = LerpF(0.96f, 1.f, g_menuAnim);
    float menuW = 800.f * menuScale * animScale;
    float menuH = 520.f * menuScale * animScale;
    float sw=(float)g_esp_screen_w, sh=(float)g_esp_screen_h;
    if(sw<100.f)sw=1920.f; if(sh<100.f)sh=1080.f;

    ImGui::SetNextWindowSize(ImVec2(menuW,menuH), ImGuiCond_Always);
    float slideY = (1.f - g_menuAnim) * 12.f * menuScale;
    float menuX = (sw - menuW) * 0.5f;
    float menuY = (sh - menuH) * 0.5f + slideY;
    menuX = Clampf(menuX, 0.f, sw - menuW);
    menuY = Clampf(menuY, 0.f, sh - menuH);
    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_menuAnim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0,0,0,0));

    if(!ImGui::Begin("##menu",nullptr,flags)){
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        return;
    }

    if(font::lexend_bold) ImGui::PushFont(font::lexend_bold);

    ImDrawList*dl=ImGui::GetWindowDrawList();
    ImVec2 pos=ImGui::GetWindowPos();
    ImVec2 size=ImGui::GetWindowSize();

    const float s = menuScale;
    ImGui::SetWindowFontScale(s);
    const float sidebarW = 200.f * s;
    const float menuRounding = 8.f * s;
    const float pad = 10.f * s;
    const float headerH = 56.f * s;

    dl->PushClipRectFullScreen();
    float shadowBase = 80.f * g_menuAnim;
    for(int i=1;i<=4;i++){
        float spread = i * 6.f * s;
        ImU32 shCol = IM_COL32(0,0,0,(int)(shadowBase/(float)i));
        dl->AddRectFilled({pos.x-spread, pos.y-spread}, {pos.x+size.x+spread, pos.y+size.y+spread}, shCol, menuRounding + spread);
    }
    dl->PopClipRect();

    dl->AddRectFilled(pos, {pos.x+size.x, pos.y+size.y}, g_pido.bgFill, menuRounding);
    dl->AddRectFilled(pos, {pos.x+sidebarW, pos.y+size.y}, g_pido.tabBg, menuRounding, ImDrawFlags_RoundCornersLeft);
    dl->AddLine({pos.x+sidebarW, pos.y}, {pos.x+sidebarW, pos.y+size.y}, g_pido.bgStroke, 1.f);
    dl->AddRect(pos, {pos.x+size.x, pos.y+size.y}, g_pido.bgStroke, menuRounding);

    // Sidebar header with accent
    dl->AddRectFilled(pos, {pos.x+sidebarW, pos.y+headerH}, g_pido.tabActive, menuRounding, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRectFilledMultiColor({pos.x+sidebarW, pos.y}, {pos.x+size.x, pos.y+headerH},
        IM_COL32(255,255,255,(int)(18*g_menuAnim)), IM_COL32(255,255,255,(int)(8*g_menuAnim)),
        IM_COL32(0,0,0,0), IM_COL32(0,0,0,0));
    dl->AddRectFilled({pos.x+12.f*s, pos.y+headerH-3.f*s}, {pos.x+sidebarW-12.f*s, pos.y+headerH-1.f*s}, g_pido.accent, 3.f*s);

    if(font::lexend_bold){
        dl->AddText(font::lexend_bold, 22.f*s, {pos.x+16.f*s, pos.y+10.f*s}, g_pido.accent, "LITWARE");
    }

    ImGui::SetCursorPos({6.f*s, headerH + 6.f*s});
    ImGui::BeginGroup();
    // Order: Aimbot, Visuals, World, Skins, Misc. Icons: Aimbot=crosshair(c), Visuals=eye(b), World=flask(f)
    const char* tabLabels[] = {"Aimbot","Visuals","World","Skins","Misc"};
    const char* tabDescs[] = {"","","","",""};
    static const char tabIcons[] = { 'c', 'b', 'f', 'o', 'e' };  // Aimbot=crosshair, Visuals=eye, World=flask
    for(int i=0;i<5;++i){
        char iconBuf[2] = { tabIcons[i], '\0' };
        if(PidoTab(iconBuf, tabLabels[i], tabDescs[i], page==i)) page = i;
    }
    ImGui::EndGroup();

    tabAlpha = LerpF(tabAlpha, (page==g_activeTab)?1.f:0.f, io.DeltaTime*15.f);
    if(tabAlpha < 0.01f) g_activeTab = page;

    float slide = (1.f - tabAlpha) * 100.f * s;
    float contentX = sidebarW + pad;
    float contentY = pad + slide;
    float contentW = size.x - sidebarW - pad*2.f;
    float contentH = size.y - pad*2.f;
    float childW = (contentW - pad) * 0.5f;
    float rightX = contentX + childW + pad;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_menuAnim * tabAlpha);

    if(g_activeTab==0){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoChild("##aim_left", ImVec2(contentW, contentH));
        PidoSection("Aimbot");
        PidoToggle("Enable","", &g_aimbotEnabled);
        if(g_aimbotEnabled){
        PidoSection("Triggerbot");
        PidoToggle("Enable##tb","", &g_tbEnabled);
        PidoSliderInt("Delay (ms)","", &g_tbDelay, 0, 300);
        PidoKeybind("Trigger key","", &g_tbKey);
        PidoSection("Recoil Control");
        PidoToggle("Enable##rcs","", &g_rcsEnabled);
        PidoSliderFloat("X axis","", &g_rcsX, 0.f, 2.f, "%.2f");
        PidoSliderFloat("Y axis","", &g_rcsY, 0.f, 2.f, "%.2f");
        PidoSliderFloat("Smooth","", &g_rcsSmooth, 1.f, 20.f, "%.1f");
        }
        EndPidoChild();

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoChild("##aim_right", ImVec2(childW, contentH));
        if(g_aimbotEnabled){
        PidoSection("Aimbot");
        PidoToggle("Enable##aim","", &g_aimbotEnabled);
        PidoSliderFloat("FOV","", &g_aimbotFov, 1.f, 30.f, "%.1f");
        PidoSliderFloat("Smooth","", &g_aimbotSmooth, 1.f, 20.f, "%.1f");
        const char* bones[]={"Head","Neck","Chest","Multi"};
        PidoCombo("Bone","", &g_aimbotBone, bones, IM_ARRAYSIZE(bones));
        PidoKeybind("Aimbot key","", &g_aimbotKey);
        PidoSection("Backtrack");
        PidoToggle("Backtrack","", &g_backtrackEnabled);
        PidoSliderInt("Ms","", &g_backtrackMs, 50, 400);
        PidoToggle("Autostop","", &g_autostopEnabled);
        PidoSection("Legitbot");
        PidoToggle("FOV circle","", &g_fovCircleEnabled);
        PidoSliderFloat("Aim FOV","", &g_aimbotFov, 1.f, 90.f, "%.1f");
        PidoSliderFloat("Smoothing","", &g_aimbotSmooth, 1.f, 30.f, "%.1f");
        }
        EndPidoChild();
    }else if(g_activeTab==1){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoChild("##vis_left", ImVec2(childW, contentH));
        PidoSection("ESP");
        PidoToggle("Enable##esp","", &g_espEnabled);
        if(g_safeMode){ ImGui::SameLine(); if(ImGui::Button("Retry##esp2")) g_safeMode=false; }
        PidoToggle("Only visible","", &g_espOnlyVis);
        const char* boxItems[]={"Corner","Full","Corner Fill","Dark Pro","Outline","Coal","Outline Coal"};
        PidoCombo("Box","", &g_espBoxStyle, boxItems, IM_ARRAYSIZE(boxItems));
        PidoColorEdit4("Enemy","", g_espEnemyCol);
        PidoColorEdit4("Team","", g_espTeamCol);
        PidoToggle("Name","", &g_espName);
        PidoToggle("Health","", &g_espHealth);
        if(g_espHealth){ const char* hStyles[]={"Gradient","Solid"}; PidoCombo("Health style","", &g_espHealthStyle, hStyles, 2); if(g_espHealthStyle==0){ PidoColorEdit4("HP full","", g_espHealthGradientCol1); PidoColorEdit4("HP empty","", g_espHealthGradientCol2); } }
        PidoToggle("Distance","", &g_espDist);
        PidoToggle("Head dot","", &g_espHeadDot);
        PidoToggle("OOF arrows","", &g_espOof);
        if(g_espOof) PidoSliderFloat("OOF size","", &g_espOofSize, 12.f, 48.f, "%.0f");
        PidoToggle("Lines","", &g_espLines);
        if(g_espLines){ const char* lineAnchors[]={"Top","Middle","Bottom"}; PidoCombo("Line anchor","", &g_espLineAnchor, lineAnchors, 3); }
        PidoToggle("Weapon","", &g_espWeapon);
        PidoToggle("Weapon icon","", &g_espWeaponIcon);
        PidoToggle("Ammo bar","", &g_espAmmo);
        if(g_espAmmo){ const char* aStyles[]={"Gradient","Solid"}; PidoCombo("Ammo style","", &g_espAmmoStyle, aStyles, 2); if(g_espAmmoStyle==0){ PidoColorEdit4("Ammo empty","", g_espAmmoCol1); PidoColorEdit4("Ammo full","", g_espAmmoCol2); } }
        PidoToggle("Money","", &g_espMoney);
        PidoSliderFloat("Box thick","", &g_espBoxThick, 0.5f, 4.f, "%.1f");
        if(g_espName) PidoSliderFloat("Name size","", &g_espNameSize, 10.f, 18.f, "%.1f");
        EndPidoChild();

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoChild("##vis_right", ImVec2(childW, contentH));
        PidoSection("Preview");
        DrawFakeESPPreview(ImVec2(childW-20.f*s, 120.f*s), 0);
        ImGui::Spacing();
        PidoSection("Effects");
        PidoToggle("No flash","", &g_noFlash);
        PidoToggle("No smoke","", &g_noSmoke);
        PidoToggle("Glow","", &g_glowEnabled);
        if(g_glowEnabled){
            PidoColorEdit4("Glow enemy","", g_glowEnemyCol);
            PidoColorEdit4("Glow team","", g_glowTeamCol);
            PidoSliderFloat("Glow alpha","", &g_glowAlpha, 0.2f, 1.0f, "%.2f");
        }
        // PidoSection("Chams"); // temporarily disabled
        // PidoToggle("Enable##chams","", &g_chamsEnabled);
        // if(g_chamsEnabled){ ... }
        ImGui::Spacing();
        PidoSection("Sound");
        PidoToggle("Sound pings","", &g_soundEnabled);
        if(g_soundEnabled){
            PidoSliderFloat("Scale","", &g_soundPuddleScale, 0.5f, 2.5f, "%.2f");
            PidoSliderFloat("Alpha","", &g_soundPuddleAlpha, 0.2f, 1.5f, "%.2f");
            PidoToggle("Enemy","", &g_soundBlipEnemy);
            PidoToggle("Teammates","", &g_soundBlipTeam);
            PidoColorEdit4("Color","", g_soundBlipCol);
        }
        EndPidoChild();
    }else if(g_activeTab==2){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoChild("##world_full", ImVec2(contentW, contentH));
        PidoSection("World");
        PidoSection("Particles");
        PidoToggle("Snow","", &g_snowEnabled);
        if(g_snowEnabled) PidoSliderInt("Density","", &g_snowDensity, 0, 2);
        PidoToggle("Sakura","", &g_sakuraEnabled);
        if(g_sakuraEnabled) PidoColorEdit4("Sakura","", g_sakuraCol);
        PidoToggle("Stars","", &g_starsEnabled);
        PidoToggle("3D world","", &g_particlesWorld);
        if(g_particlesWorld){
            PidoSliderFloat("World radius","", &g_particlesWorldRadius, 200.f, 2000.f, "%.0f");
            PidoSliderFloat("World height","", &g_particlesWorldHeight, 100.f, 1200.f, "%.0f");
            PidoSliderFloat("World floor","", &g_particlesWorldFloor, -200.f, 200.f, "%.0f");
            PidoSliderFloat("Wind","", &g_particlesWind, 0.f, 60.f, "%.0f");
            PidoSliderFloat("Depth fade","", &g_particlesDepthFade, 0.0005f, 0.01f, "%.4f");
        }
        EndPidoChild();
    }else if(g_activeTab==3){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoChild("##skins_full", ImVec2(contentW, contentH));
        PidoSection("Skins");
        ImGui::TextDisabled("Coming soon...");
        EndPidoChild();
    }else if(g_activeTab==4){
        static int s_lastConfigTabFrame = -1;
        if(s_lastConfigTabFrame != ImGui::GetFrameCount()){ s_lastConfigTabFrame = ImGui::GetFrameCount(); RefreshConfigList(); }
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoChild("##misc_left", ImVec2(childW, contentH));
        PidoSection("Movement");
        PidoToggle("Bunny hop","", &g_bhopEnabled);
        PidoToggle("Strafe helper","", &g_strafeEnabled);
        PidoSection("Radar");
        PidoToggle("In-game radar","", &g_radarIngame);
        PidoSection("HUD");
        PidoToggle("Bomb timer","", &g_bombTimerEnabled);
        PidoToggle("Watermark","", &g_watermarkEnabled);
        PidoToggle("Spectator list","", &g_spectatorListEnabled);
        PidoToggle("Keybinds","", &g_keybindsEnabled);
        EndPidoChild();

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoChild("##misc_right", ImVec2(childW, contentH));
        PidoSection("View");
        PidoToggle("FOV changer","", &g_fovEnabled);
        PidoSliderFloat("FOV","", &g_fovValue, 70.f, 130.f, "%.0f");
        PidoSection("Config");
        const char* themes[] = {"Dark Pro","Glass","Neumo"};
        PidoCombo("Menu theme","", &g_menuTheme, themes, IM_ARRAYSIZE(themes));
        PidoSliderFloat("Menu opacity","", &g_menuOpacity, 0.6f, 1.0f, "%.2f");
        PidoSliderFloat("Menu anim","", &g_menuAnimSpeed, 4.f, 20.f, "%.1f");
        PidoColorEdit4("Accent color","", g_accentColor);
        PidoSliderFloat("Esp scale","", &g_espScale, 0.7f, 1.5f, "%.2f");
        PidoSliderFloat("Menu scale","", &g_uiScale, 0.85f, 1.6f, "%.2f");
        PidoInputText("Config name","", g_configName, sizeof(g_configName));
        static int g_configListSel = -1;
        const char* cfgPreview = g_configList.empty() ? "(no configs)" : (g_configListSel >= 0 && g_configListSel < (int)g_configList.size() ? g_configList[g_configListSel].c_str() : "Select config...");
        if(ImGui::BeginCombo("Config list", cfgPreview, ImGuiComboFlags_None)){
            for(size_t i = 0; i < g_configList.size(); ++i){
                bool sel = (g_configListSel == (int)i);
                if(ImGui::Selectable(g_configList[i].c_str(), sel)){
                    g_configListSel = (int)i;
                    strncpy_s(g_configName, sizeof(g_configName), g_configList[i].c_str(), _TRUNCATE);
                }
                if(sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if(ImGui::Button("Refresh##cfg")){ RefreshConfigList(); }
        if(PidoButton("Save", ImVec2(0, 0))){ if(SaveConfig(g_configName)) RefreshConfigList(); }
        ImGui::SameLine();
        if(PidoButton("Load", ImVec2(0, 0))) LoadConfig(g_configName);
        ImGui::SameLine();
        if(PidoButton("Reset", ImVec2(0, 0))) ApplyDefaults();
        EndPidoChild();
    }

    ImGui::PopStyleVar();

    if(font::lexend_bold) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

static void DrawKeybindsWindow(){
    if(!g_keybindsEnabled || !g_keybindsWindowOpen) return;
    ImGui::SetNextWindowSize({320.f,300.f},ImGuiCond_Always);
    ImGui::Begin("Keybinds",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
    KeyBindWidget("Aimbot key",&g_aimbotKey);
    KeyBindWidget("Triggerbot key",&g_tbKey);
    KeyBindWidget("DT key",&g_dtKey);
    KeyBindWidget("Strafe key",&g_strafeKey);
    ImGui::End();
}

static void UpdateAndDrawParticles(float dt,float sw,float sh){
    ImDrawList*dl=ImGui::GetBackgroundDrawList();if(!dl)return;
    static float snowAcc=0.f,sakuraAcc=0.f,starAcc=0.f;
    int snowRate=(g_snowDensity==0?25:(g_snowDensity==1?60:120));
    if(g_snowEnabled){snowAcc+=dt*(float)snowRate;}
    if(g_sakuraEnabled){sakuraAcc+=dt*40.f;}
    if(g_starsEnabled){starAcc+=dt*12.f;}
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    bool use3D = (vm != nullptr) && g_particlesWorld;
    float worldRadius = g_particlesWorldRadius;
    float worldHeight = g_particlesWorldHeight;
    float worldFloor = g_particlesWorldFloor;
    float wind = g_particlesWind;
    Vec3 windVec{wind, wind*0.35f, 0.f};

    auto spawnWorld=[&](Particle& p){
        float ang = Randf(0.f, 6.2831853f);
        float rad = sqrtf(Randf(0.f, 1.f)) * worldRadius;
        float ox = cosf(ang) * rad;
        float oy = sinf(ang) * rad;
        p.worldPos = {g_localOrigin.x + ox, g_localOrigin.y + oy, g_localOrigin.z + worldFloor + Randf(0.f, worldHeight)};
        p.rot = Randf(0.f, 6.2831853f);
        p.phase = Randf(0.f, 6.2831853f);
        if(p.type==2){
            p.worldVel = {0.f,0.f,0.f};
        }else if(p.type==1){
            p.worldVel = {Randf(-8.f,8.f), Randf(-8.f,8.f), Randf(-45.f,-15.f)};
        }else{
            p.worldVel = {Randf(-5.f,5.f), Randf(-5.f,5.f), Randf(-70.f,-30.f)};
        }
    };

    auto spawn=[&](int count,int type){
        for(int i=0;i<count;i++){
            Particle p{};
            p.size=Randf(1.5f,3.5f);
            p.maxlife=Randf(4.f,9.f);
            p.lifetime=p.maxlife;
            p.phase=Randf(0.f,6.28f);
            p.rot=Randf(0.f,6.28f);
            p.spin=Randf(-1.5f,1.5f);
            p.type=type;
            if(type==0)p.color=IM_COL32(255,255,255,200);
            else if(type==1)p.color=IM_COL32((int)(g_sakuraCol[0]*255),(int)(g_sakuraCol[1]*255),(int)(g_sakuraCol[2]*255),(int)(g_sakuraCol[3]*255));
            else p.color=IM_COL32(210,210,230,200);
            if(use3D){
                p.is3D=true;
                spawnWorld(p);
            }else{
                p.is3D=false;
                p.x=Randf(0.f,sw);
                p.y=Randf(-20.f,0.f);
                p.vx=Randf(-10.f,10.f);
                p.vy=Randf(40.f,120.f);
            }
            g_particles.push_back(p);
        }
    };
    while(snowAcc>=1.f){spawn(1,0);snowAcc-=1.f;}
    while(sakuraAcc>=1.f){spawn(1,1);sakuraAcc-=1.f;}
    while(starAcc>=1.f){spawn(1,2);starAcc-=1.f;}
    for(auto& p: g_particles){
        p.lifetime-=dt;
        if(p.is3D){
            p.phase+=dt;
            p.rot += p.spin * dt;
            float sway = (p.type==1 ? 12.f : 6.f) * sinf(p.phase + p.rot);
            p.worldPos.x += (p.worldVel.x + windVec.x + sway) * dt;
            p.worldPos.y += (p.worldVel.y + windVec.y + (p.type==1 ? -sway * 0.4f : 0.f)) * dt;
            p.worldPos.z += p.worldVel.z * dt;
            if(p.type==0){p.worldVel.z -= 8.f * dt;}
            if(p.type==1){p.worldVel.z -= 5.f * dt;}
            if(p.type==2){/* twinkle only */}

            float baseZ = g_localOrigin.z + worldFloor;
            float topZ = baseZ + worldHeight;
            if(p.worldPos.z < baseZ){
                p.worldPos.z = topZ + Randf(0.f, worldHeight*0.15f);
                p.worldPos.x = g_localOrigin.x;
                p.worldPos.y = g_localOrigin.y;
                spawnWorld(p);
            }
            float dx = p.worldPos.x - g_localOrigin.x;
            float dy = p.worldPos.y - g_localOrigin.y;
            float dist2 = dx*dx + dy*dy;
            float maxR = worldRadius * 1.35f;
            if(dist2 > maxR*maxR){
                spawnWorld(p);
            }
        }else{
            p.x+=p.vx*dt;p.y+=p.vy*dt;
            if(p.type==1){p.vx+=sinf(p.phase+p.lifetime)*5.f*dt;}
            if(p.type==2){p.phase+=dt;}
        }
    }
    g_particles.erase(std::remove_if(g_particles.begin(),g_particles.end(),[&](const Particle& p){
        if(p.lifetime<=0.f) return true;
        if(!p.is3D && (p.x<-20.f||p.x>sw+20.f||p.y>sh+20.f)) return true;
        return false;
    }),g_particles.end());
    for(const auto& p: g_particles){
        float x=p.x,y=p.y;
        if(p.is3D){
            if(!vm) continue;
            if(!WorldToScreen(vm,p.worldPos,g_esp_screen_w,g_esp_screen_h,x,y)) continue;
        }
        float life = Clampf(p.lifetime/p.maxlife,0.f,1.f);
        float depth = 1.f;
        if(p.is3D){
            Vec3 d{p.worldPos.x - g_localOrigin.x, p.worldPos.y - g_localOrigin.y, p.worldPos.z - g_localOrigin.z};
            float dist = d.length();
            depth = 1.f / (1.f + dist * g_particlesDepthFade);
            depth = Clampf(depth, 0.25f, 1.2f);
        }
        float alpha = Clampf(life * depth, 0.f, 1.f);
        float size = p.size * LerpF(0.65f, 1.4f, depth);
        if(p.type==0){
            ImU32 soft = WithAlpha(p.color, alpha*0.35f);
            ImU32 core = WithAlpha(p.color, alpha);
            dl->AddCircleFilled({x,y},size*1.6f,soft,12);
            dl->AddCircleFilled({x,y},size,core,12);
        }else if(p.type==1){
            ImU32 petal = WithAlpha(p.color, alpha*0.9f);
            DrawRotatedQuad(dl, {x,y}, size*2.2f, size*1.2f, p.rot, petal);
            DrawRotatedQuad(dl, {x,y}, size*1.4f, size*0.8f, p.rot + 0.8f, WithAlpha(p.color, alpha*0.55f));
        }else{
            float tw = 0.4f + 0.6f * (sinf(p.phase*2.f + p.rot)*0.5f + 0.5f);
            ImU32 col = WithAlpha(p.color, alpha * tw);
            dl->AddCircleFilled({x,y},size*0.7f,col,10);
            dl->AddLine({x-size,y},{x+size,y},WithAlpha(p.color, alpha*0.35f),1.f);
            dl->AddLine({x,y-size},{x,y+size},WithAlpha(p.color, alpha*0.35f),1.f);
        }
    }
}

static void DrawHitmarker(float sw, float sh){
    if(!g_hitmarkerEnabled || !g_lastHitmarkerTime) return;
    DWORD elapsed = GetTickCount() - g_lastHitmarkerTime;
    float durMs = g_hitmarkerDuration * 1000.f;
    if(elapsed >= (DWORD)durMs) { g_lastHitmarkerTime = 0; return; }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    float t = 1.f - (float)elapsed / durMs;
    float alpha = t;
    float len = 8.f + 6.f * t;
    float cx = sw * 0.5f, cy = sh * 0.5f;
    ImU32 col = IM_COL32(255,255,255,(int)(220*alpha));
    ImU32 outline = IM_COL32(0,0,0,(int)(120*alpha));
    if(g_hitmarkerStyle == 0){
        dl->AddLine({cx-len,cy},{cx+len,cy},outline,2.5f); dl->AddLine({cx-len,cy},{cx+len,cy},col,1.5f);
        dl->AddLine({cx,cy-len},{cx,cy+len},outline,2.5f); dl->AddLine({cx,cy-len},{cx,cy+len},col,1.5f);
    }else{
        dl->AddLine({cx-len*0.7f,cy-len*0.7f},{cx+len*0.7f,cy+len*0.7f},outline,2.5f);
        dl->AddLine({cx-len*0.7f,cy-len*0.7f},{cx+len*0.7f,cy+len*0.7f},col,1.5f);
        dl->AddLine({cx-len*0.7f,cy+len*0.7f},{cx+len*0.7f,cy-len*0.7f},outline,2.5f);
        dl->AddLine({cx-len*0.7f,cy+len*0.7f},{cx+len*0.7f,cy-len*0.7f},col,1.5f);
    }
}

static void DrawKillEffect(float sw, float sh){
    if(!g_killEffectEnabled || !g_lastKillEffectTime) return;
    DWORD elapsed = GetTickCount() - g_lastKillEffectTime;
    float durMs = g_killEffectDuration * 1000.f;
    if(elapsed >= (DWORD)durMs) { g_lastKillEffectTime = 0; return; }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    float t = 1.f - (float)elapsed / durMs;
    int alpha = (int)(80 * t);
    dl->AddRectFilled({0,0},{sw,sh}, IM_COL32(180,60,80,alpha));
}

static void DrawNotifications(float dt,float sw,float sh){
    if(g_notifs.empty())return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float yBase=sh*0.20f;
    int idx=0;
    for(auto& n: g_notifs){
        n.lifetime-=dt;
        float targetX = sw - 250.f;
        n.xOff = LerpF(n.xOff, targetX, dt*10.f);
        float t=Clampf(n.lifetime/n.maxlife,0.f,1.f);
        float alpha=Clampf(1.f-(t*0.4f),0.f,1.f)*Clampf(n.lifetime/0.3f,0.f,1.f);
        float x=n.xOff;
        float y=yBase + idx*26.f;
        float w=240.f;
        float h=22.f;
        ImU32 col=IM_COL32((n.color>>IM_COL32_R_SHIFT)&0xFF,(n.color>>IM_COL32_G_SHIFT)&0xFF,(n.color>>IM_COL32_B_SHIFT)&0xFF,(int)(255*alpha));
        ImU32 bg=IM_COL32(18,18,24,(int)(200*alpha));
        dl->AddRectFilled({x,y},{x+w,y+h},bg,6.f);
        dl->AddRectFilled({x,y},{x+3.f,y+h},col,6.f,ImDrawFlags_RoundCornersLeft);
        dl->AddText({x+10.f,y+3.f},IM_COL32(0,0,0,(int)(160*alpha)),n.text);
        dl->AddText({x+9.f,y+2.f},col,n.text);
        idx++;
    }
    g_notifs.erase(std::remove_if(g_notifs.begin(), g_notifs.end(), [](const Notification& n){return n.lifetime<=0.f;}), g_notifs.end());
}

// Telegram channel notification - menu-styled, 5 sec on load
static void DrawTelegramNote(float sw, float sh){
    DWORD now = GetTickCount();
    if(g_telegramNoteStart == 0) g_telegramNoteStart = now;
    if(now - g_telegramNoteStart > 5000) return;
    float fade = 1.f - Clampf((float)(now - g_telegramNoteStart) / 4500.f, 0.f, 1.f) * 0.3f;  // slight fade near end
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    ImFont* bold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* reg = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    const char* line1 = "Don't forget to subscribe to telegram channel:";
    const char* line2 = "t.me/luvr5rpp";
    ImVec2 t1 = bold->CalcTextSizeA(14.f, FLT_MAX, 0.f, line1);
    ImVec2 t2 = bold->CalcTextSizeA(16.f, FLT_MAX, 0.f, line2);
    float pad = 20.f, w = (std::max)(t1.x, t2.x) + pad * 2.f, h = t1.y + t2.y + pad * 3.f;
    ImVec2 pos{ sw * 0.5f - w * 0.5f, sh * 0.35f - h * 0.5f };
    ImU32 bg = IM_COL32(18, 18, 26, (int)(240 * fade));
    ImU32 border = IM_COL32((int)(g_accentColor[0] * 255), (int)(g_accentColor[1] * 255), (int)(g_accentColor[2] * 255), (int)(180 * fade));
    ImU32 txt = IM_COL32(225, 225, 235, (int)(255 * fade));
    ImU32 acc = IM_COL32((int)(g_accentColor[0] * 255), (int)(g_accentColor[1] * 255), (int)(g_accentColor[2] * 255), (int)(255 * fade));
    dl->AddRectFilled(pos, { pos.x + w, pos.y + h }, bg, 10.f);
    dl->AddRect(pos, { pos.x + w, pos.y + h }, border, 10.f, 0, 2.f);
    dl->AddText(reg, 14.f, { pos.x + pad, pos.y + pad }, txt, line1);
    dl->AddText(bold, 16.f, { pos.x + pad, pos.y + pad + t1.y + 8.f }, acc, line2);
}

// Pidoraise-style watermark: info bar with cheat | fps | time
static void DrawWatermark(float sw){
    if(!g_watermarkEnabled)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImFont* wmFont=font::lexend_bold?font::lexend_bold:ImGui::GetFont();
    ImGuiIO& io=ImGui::GetIO();
    float fps=io.Framerate;
    SYSTEMTIME st{};GetLocalTime(&st);
    char fpsBuf[32], timeBuf[32];
    std::snprintf(fpsBuf,sizeof(fpsBuf),"%.0ffps",fps);
    std::snprintf(timeBuf,sizeof(timeBuf),"%02d:%02d",st.wHour,st.wMinute);
    const char* cheatName="LitWare";
    const char* items[]={cheatName, fpsBuf, timeBuf};
    const int nItems=g_showFpsWatermark?3:2;
    float totalW=0.f, maxH=0.f;
    for(int i=0;i<nItems;++i){
        ImVec2 s=wmFont->CalcTextSizeA(wmFont->FontSize,FLT_MAX,0.f,items[i]);
        totalW+=s.x; if(i<nItems-1) totalW+=wmFont->CalcTextSizeA(wmFont->FontSize,FLT_MAX,0.f," | ").x;
        if(s.y>maxH) maxH=s.y;
    }
    float pad=12.f, spacing=12.f;
    float barW=totalW+pad*2.f+ (nItems>1 ? 2.f*spacing*(float)(nItems-1) : 0.f);  // totalW has items+sep; add spacing around seps
    barW=(std::max)(barW,140.f);
    float barH=maxH+pad*2.f;
    ImVec2 pos{sw-barW-15.f,15.f};
    ImVec2 size{barW,barH};
    // Use accent color and theme-based styling
    ImU32 bgFill=IM_COL32(11,11,11,255);
    ImU32 stroke=IM_COL32(24,26,36,255);
    ImU32 accent=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    ImU32 textCol=IM_COL32(255,255,255,255);
    dl->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},bgFill,6.f);
    dl->AddRect(pos,{pos.x+size.x,pos.y+size.y},stroke,6.f,0,1.f);
    float curX=pos.x+pad;
    float centerY=pos.y+size.y*0.5f;
    for(int i=0;i<nItems;++i){
        ImVec2 ts=wmFont->CalcTextSizeA(wmFont->FontSize,FLT_MAX,0.f,items[i]);
        float y=centerY-ts.y*0.5f;
        ImU32 col=(i==0)?accent:textCol;
        dl->AddText(wmFont,wmFont->FontSize,{curX,y},col,items[i]);
        curX+=ts.x;
        if(i<nItems-1){
            curX+=spacing;
            ImVec2 sepSz=wmFont->CalcTextSizeA(wmFont->FontSize,FLT_MAX,0.f," | ");
            dl->AddText(wmFont,wmFont->FontSize,{curX,y},textCol," | ");
            curX+=sepSz.x+spacing;
        }
    }
}

static void DrawNoCrosshair(float sw,float sh){
    if(!g_noCrosshair) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float cx=sw*0.5f, cy=sh*0.5f;
    dl->AddCircleFilled({cx,cy},20.f,IM_COL32(0,0,0,255),24);
}

static void DrawFovCircle(float sw, float sh){
    if(!g_fovCircleEnabled||!g_aimbotEnabled) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float cx=sw*0.5f, cy=sh*0.5f;
    float fovRad = g_aimbotFov * (3.14159265f/180.f);
    float radius = (std::min)(sw,sh) * 0.5f * tanf(fovRad);
    if(radius < 5.f) radius = 5.f;
    if(radius > 400.f) radius = 400.f;
    ImU32 col = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),80);
    dl->AddCircle({cx,cy}, radius, col, 64, 1.2f);
}

static ImU32 HealthCol(int hp){
    float t=Clampf(hp/100.f,0.f,1.f);
    return IM_COL32((int)(255*(1.f-t)),(int)(210*t),(int)(50*t),255);
}

static void DrawCornerBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    float lx=(r-l)*0.22f,ly=(b-t)*0.22f;
    dl->AddLine({l,t},{l+lx,t},col,thick);dl->AddLine({l,t},{l,t+ly},col,thick);
    dl->AddLine({r,t},{r-lx,t},col,thick);dl->AddLine({r,t},{r,t+ly},col,thick);
    dl->AddLine({l,b},{l+lx,b},col,thick);dl->AddLine({l,b},{l,b-ly},col,thick);
    dl->AddLine({r,b},{r-lx,b},col,thick);dl->AddLine({r,b},{r,b-ly},col,thick);
}
// OOF: get arrow position at screen edge (angle computed by caller from ox,oy)
static bool GetOofArrowPos(const float* vm, const Vec3& head, int sw, int sh, float& ox, float& oy){
    float w = vm[12]*head.x + vm[13]*head.y + vm[14]*head.z + vm[15];
    if(w > -0.001f && w < 0.001f) return false;
    float invW = 1.f / (w < 0.001f ? 0.001f : w);
    float x = (vm[0]*head.x + vm[1]*head.y + vm[2]*head.z + vm[3]) * invW;
    float y = (vm[4]*head.x + vm[5]*head.y + vm[6]*head.z + vm[7]) * invW;
    float sx = (sw*0.5f) + (x * sw*0.5f);
    float sy = (sh*0.5f) - (y * sh*0.5f);
    float cx = sw*0.5f, cy = sh*0.5f;
    float dx = sx - cx, dy = sy - cy;
    if(fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return false;
    float t = 1e9f;
    float margin = 30.f;
    if(dx > 0.001f) t = (sw - margin - cx) / dx; else if(dx < -0.001f) t = (margin - cx) / dx;
    if(dy > 0.001f) { float ty = (sh - margin - cy) / dy; if(ty < t) t = ty; }
    else if(dy < -0.001f) { float ty = (margin - cy) / dy; if(ty < t) t = ty; }
    if(t < 0) t = 0;
    ox = cx + dx * t;
    oy = cy + dy * t;
    return true;
}

// Coal box: corners 1/4 of box size (Andromeda/Weave style)
static void DrawCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    float lx=(r-l)*0.25f,ly=(b-t)*0.25f;
    dl->AddLine({l,t},{l+lx,t},col,thick);dl->AddLine({l,t},{l,t+ly},col,thick);
    dl->AddLine({r,t},{r-lx,t},col,thick);dl->AddLine({r,t},{r,t+ly},col,thick);
    dl->AddLine({l,b},{l+lx,b},col,thick);dl->AddLine({l,b},{l,b-ly},col,thick);
    dl->AddLine({r,b},{r-lx,b},col,thick);dl->AddLine({r,b},{r,b-ly},col,thick);
}
// Outline box: triple outline (Weave/Pidoraise style)
static void DrawOutlineBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    dl->AddRect({l-1.f,t-1.f},{r+1.f,b+1.f},IM_COL32(0,0,0,255),0.f,0,thick+1.f);
    dl->AddRect({l+1.f,t+1.f},{r-1.f,b-1.f},IM_COL32(0,0,0,255),0.f,0,thick);
    dl->AddRect({l,t},{r,b},col,0.f,0,thick);
}
// Outline coal: coal box with black outline (Andromeda style)
static void DrawOutlineCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    ImU32 black=IM_COL32(0,0,0,255);
    DrawCoalBox(dl,l,t,r,b,black,thick+1.f);
    DrawCoalBox(dl,l+1.f,t+1.f,r-1.f,b-1.f,col,thick);
    DrawCoalBox(dl,l+2.f,t+2.f,r-2.f,b-2.f,black,thick);
}

static void DrawFilledEllipse(ImDrawList* dl, const ImVec2& center, float rx, float ry, ImU32 col, int segments = 24){
    if(!dl || rx <= 0.f || ry <= 0.f) return;
    const int maxSeg = 64;
    int n = segments < 6 ? 6 : (segments > maxSeg ? maxSeg : segments);
    ImVec2 pts[maxSeg];
    float step = 6.2831853f / (float)n;
    for(int i=0;i<n;i++){
        float a = step * i;
        pts[i] = {center.x + cosf(a)*rx, center.y + sinf(a)*ry};
    }
    dl->AddConvexPolyFilled(pts, n, col);
}

static void DrawRotatedQuad(ImDrawList* dl, ImVec2 center, float w, float h, float angle, ImU32 col){
    if(!dl || w <= 0.f || h <= 0.f) return;
    float c = cosf(angle), s = sinf(angle);
    float hw = w * 0.5f, hh = h * 0.5f;
    ImVec2 pts[4] = {
        {center.x + (-hw*c - -hh*s), center.y + (-hw*s + -hh*c)},
        {center.x + ( hw*c - -hh*s), center.y + ( hw*s + -hh*c)},
        {center.x + ( hw*c -  hh*s), center.y + ( hw*s +  hh*c)},
        {center.x + (-hw*c -  hh*s), center.y + (-hw*s +  hh*c)}
    };
    dl->AddConvexPolyFilled(pts, 4, col);
}

static bool DrawSkeletonBones(ImDrawList*dl,const ESPEntry& e,ImU32 col){
    if(!dl||!g_client||!e.pawn) return false;
    UpdatePawnBones(e.pawn);
    const float* vm = reinterpret_cast<const float*>(g_client + offsets::client::dwViewMatrix);
    if(!vm) return false;
    Vec3 head{}, neck{}, spine1{}, spine2{}, spine3{}, pelvis{};
    Vec3 armUpL{}, armLoL{}, handL{}, armUpR{}, armLoR{}, handR{};
    Vec3 legUpL{}, legLoL{}, ankleL{}, legUpR{}, legLoR{}, ankleR{};
    bool hHead=GetBonePos(e.pawn,BONE_HEAD,head);
    bool hNeck=GetBonePos(e.pawn,BONE_NECK,neck);
    bool hSp1=GetBonePos(e.pawn,BONE_SPINE1,spine1);
    bool hSp2=GetBonePos(e.pawn,BONE_SPINE2,spine2);
    bool hSp3=GetBonePos(e.pawn,BONE_SPINE3,spine3);
    bool hPel=GetBonePos(e.pawn,BONE_PELVIS,pelvis);
    bool hArmUL=GetBonePos(e.pawn,BONE_ARM_UP_L,armUpL);
    bool hArmLL=GetBonePos(e.pawn,BONE_ARM_LO_L,armLoL);
    bool hHandL=GetBonePos(e.pawn,BONE_HAND_L,handL);
    bool hArmUR=GetBonePos(e.pawn,BONE_ARM_UP_R,armUpR);
    bool hArmLR=GetBonePos(e.pawn,BONE_ARM_LO_R,armLoR);
    bool hHandR=GetBonePos(e.pawn,BONE_HAND_R,handR);
    bool hLegUL=GetBonePos(e.pawn,BONE_LEG_UP_L,legUpL);
    bool hLegLL=GetBonePos(e.pawn,BONE_LEG_LO_L,legLoL);
    bool hAnkL=GetBonePos(e.pawn,BONE_ANKLE_L,ankleL);
    bool hLegUR=GetBonePos(e.pawn,BONE_LEG_UP_R,legUpR);
    bool hLegLR=GetBonePos(e.pawn,BONE_LEG_LO_R,legLoR);
    bool hAnkR=GetBonePos(e.pawn,BONE_ANKLE_R,ankleR);
    auto validPos = [](const Vec3& v){
        if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
        if(fabsf(v.x) > 100000.f || fabsf(v.y) > 100000.f || fabsf(v.z) > 100000.f) return false;
        if(fabsf(v.x) < 0.001f && fabsf(v.y) < 0.001f && fabsf(v.z) < 0.001f) return false;
        return true;
    };
    hHead = hHead && validPos(head);
    hNeck = hNeck && validPos(neck);
    hSp1 = hSp1 && validPos(spine1);
    hSp2 = hSp2 && validPos(spine2);
    hSp3 = hSp3 && validPos(spine3);
    hPel = hPel && validPos(pelvis);
    hArmUL = hArmUL && validPos(armUpL);
    hArmLL = hArmLL && validPos(armLoL);
    hHandL = hHandL && validPos(handL);
    hArmUR = hArmUR && validPos(armUpR);
    hArmLR = hArmLR && validPos(armLoR);
    hHandR = hHandR && validPos(handR);
    hLegUL = hLegUL && validPos(legUpL);
    hLegLL = hLegLL && validPos(legLoL);
    hAnkL = hAnkL && validPos(ankleL);
    hLegUR = hLegUR && validPos(legUpR);
    hLegLR = hLegLR && validPos(legLoR);
    hAnkR = hAnkR && validPos(ankleR);
    if(!(hHead && hNeck && hPel)) return false;
    bool drew=false;
    float thick = Clampf(g_skeletonThick, 0.5f, 3.5f);
    auto line=[&](bool ha,const Vec3& a,bool hb,const Vec3& b){
        if(!ha||!hb) return;
        float ax,ay,bx,by;
        if(WorldToScreen(vm,a,g_esp_screen_w,g_esp_screen_h,ax,ay)&&WorldToScreen(vm,b,g_esp_screen_w,g_esp_screen_h,bx,by)){
            if(!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by)) return;
            dl->AddLine({ax,ay},{bx,by},col,thick);
            drew=true;
        }
    };
    line(hHead,head,hNeck,neck);
    line(hNeck,neck,hSp3,spine3);
    line(hSp3,spine3,hSp2,spine2);
    line(hSp2,spine2,hSp1,spine1);
    line(hSp1,spine1,hPel,pelvis);
    line(hSp3,spine3,hArmUL,armUpL);
    line(hArmUL,armUpL,hArmLL,armLoL);
    line(hArmLL,armLoL,hHandL,handL);
    line(hSp3,spine3,hArmUR,armUpR);
    line(hArmUR,armUpR,hArmLR,armLoR);
    line(hArmLR,armLoR,hHandR,handR);
    line(hPel,pelvis,hLegUL,legUpL);
    line(hLegUL,legUpL,hLegLL,legLoL);
    line(hLegLL,legLoL,hAnkL,ankleL);
    line(hPel,pelvis,hLegUR,legUpR);
    line(hLegUR,legUpR,hLegLR,legLoR);
    line(hLegLR,legLoR,hAnkR,ankleR);
    return drew;
}

static void DrawESP(){
    if(!g_espEnabled||g_esp_count<=0)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    uintptr_t entityList = g_client ? Rd<uintptr_t>(g_client+offsets::client::dwEntityList) : 0;
    DWORD btNow = g_backtrackVisual ? GetTickCount() : 0;
    float btMaxAge = g_backtrackVisual ? (float)g_backtrackMs : 0.f;
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry&e=g_esp_players[i];if(!e.valid||e.distance>g_espMaxDist)continue;
        bool enemy=(e.team!=g_esp_local_team);float*ecol=enemy?g_espEnemyCol:g_espTeamCol;
        float alpha=e.visible?1.f:0.5f;float bl=e.box_l,bt2=e.box_t,br=e.box_r,bb=e.box_b;
        float bw=br-bl,bh=bb-bt2,cx=(bl+br)*0.5f;
        ImU32 boxCol=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(alpha*255));
        ImU32 dimCol=IM_COL32(160,160,170,(int)(180*alpha));
        ImU32 accent=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(200*alpha));
        float belowY = bb + 3.f;
        if(g_backtrackVisual && vm && e.ent_index>0 && e.ent_index<=ESP_MAX_PLAYERS){
            auto& dq = g_backtrack[e.ent_index];
            for(const auto& rec : dq){
                float age = static_cast<float>(btNow - rec.time);
                if(age < 0.f || age > btMaxAge) continue;
                // Skip records with invalid positions
                if(fabsf(rec.head.x) < 1.f && fabsf(rec.head.y) < 1.f && fabsf(rec.head.z) < 1.f) continue;
                if(fabsf(rec.pelvis.x) < 1.f && fabsf(rec.pelvis.y) < 1.f && fabsf(rec.pelvis.z) < 1.f) continue;
                float a = 1.f - (age / btMaxAge);
                float hx,hy,px,py;
                bool hOnScreen = WorldToScreen(vm, rec.head, g_esp_screen_w, g_esp_screen_h, hx, hy);
                bool pOnScreen = WorldToScreen(vm, rec.pelvis, g_esp_screen_w, g_esp_screen_h, px, py);
                ImU32 col = IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(120*a));
                if(hOnScreen && pOnScreen){
                    dl->AddLine({hx,hy},{px,py},col,2.0f*a+1.0f);
                    dl->AddCircleFilled({hx,hy}, 4.f*a+2.f, col, 8);
                    dl->AddCircleFilled({px,py}, 5.f*a+2.5f, col, 8);
                }else if(hOnScreen){
                    dl->AddCircleFilled({hx,hy}, 4.f*a+2.f, col, 8);
                }else if(pOnScreen){
                    dl->AddCircleFilled({px,py}, 5.f*a+2.5f, col, 8);
                }
            }
        }
        // Shadow + glow layers (ESP)
        dl->AddRectFilled({bl+3.f,bt2+3.f},{br+3.f,bb+3.f},IM_COL32(0,0,0,(int)(45*alpha)));
        dl->AddRectFilled({bl+2.f,bt2+2.f},{br+2.f,bb+2.f},IM_COL32(0,0,0,(int)(50*alpha)));
        for(int g=4;g>=1;g--){
            int r=(boxCol>>IM_COL32_R_SHIFT)&0xFF,g_=(boxCol>>IM_COL32_G_SHIFT)&0xFF,b=(boxCol>>IM_COL32_B_SHIFT)&0xFF;
            int ga=(g==4)?50:(g==3)?35:(g==2)?20:10;
            dl->AddRect({bl-(float)g,bt2-(float)g},{br+(float)g,bb+(float)g},IM_COL32(r,g_,b,(int)(ga*alpha)),0.f,0,1.5f);
        }
        if(g_espBoxStyle==3){
            dl->AddRectFilled({bl,bt2},{br,bb},IM_COL32(14,14,18,(int)(120*alpha)),3.f);
            dl->AddRect({bl,bt2},{br,bb},IM_COL32(0,0,0,(int)(220*alpha)),3.f,0,g_espBoxThick+1.0f);
            dl->AddRect({bl,bt2},{br,bb},boxCol,3.f,0,g_espBoxThick);
            dl->AddRectFilled({bl,bt2},{br,bt2+3.f},accent,3.f,ImDrawFlags_RoundCornersTop);
        }
        else if(g_espBoxStyle==0){
            DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),g_espBoxThick+1.0f);
            DrawCornerBox(dl,bl,bt2,br,bb,boxCol,g_espBoxThick);
        }
        else if(g_espBoxStyle==1){
            dl->AddRect({bl,bt2},{br,bb},IM_COL32(0,0,0,(int)(200*alpha)),0.f,0,g_espBoxThick+1.0f);
            dl->AddRect({bl,bt2},{br,bb},boxCol,0.f,0,g_espBoxThick);
        }
        else if(g_espBoxStyle==2){
            dl->AddRectFilled({bl,bt2},{br,bb},IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(35*alpha)));
            DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),g_espBoxThick+1.0f);
            DrawCornerBox(dl,bl,bt2,br,bb,boxCol,g_espBoxThick);
        }
        else if(g_espBoxStyle==4){
            DrawOutlineBox(dl,bl,bt2,br,bb,boxCol,g_espBoxThick);
        }
        else if(g_espBoxStyle==5){
            DrawCoalBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),g_espBoxThick+1.0f);
            DrawCoalBox(dl,bl,bt2,br,bb,boxCol,g_espBoxThick);
        }
        else if(g_espBoxStyle==6){
            DrawOutlineCoalBox(dl,bl,bt2,br,bb,boxCol,g_espBoxThick);
        }
        if(g_espHealth&&e.health>0){
            float fill=Clampf((float)e.health/100.f,0.f,1.f),barW=5.f,barOff=8.f,barRound=4.f;
            ImU32 hbCol=HealthCol(e.health);
            if(g_espHealthStyle==1)hbCol=IM_COL32(60,200,120,(int)(220*alpha));
            if(g_espHealthStyle==2)hbCol=boxCol;
            ImU32 bgDark=IM_COL32(8,8,12,(int)(220*alpha));
            ImU32 borderCol=IM_COL32(40,40,50,(int)(180*alpha));
            float bx=0.f, byBar=0.f;
            if(g_espHealthPos==0){
                bx=bl-barOff-barW;
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    dl->AddRectFilled({bx-o-1.f,bt2-o-1.f},{bx+barW+o+1.f,bb+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bx-1.f,bt2-1.f},{bx+barW+1.f,bb+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bx,bt2},{bx+barW,bb},bgDark,barRound);
                if(g_espHealthStyle==0){
                    ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
                    ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
                    dl->AddRectFilledMultiColor({bx,bt2+bh*(1.f-fill)},{bx+barW,bb}, c1,c1, c2,c2);
                }else{
                    dl->AddRectFilled({bx,bt2+bh*(1.f-fill)},{bx+barW,bb},hbCol,barRound);
                }
                byBar=bt2+bh*(1.f-fill);
            }else if(g_espHealthPos==2){
                bx=br+barOff;
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    dl->AddRectFilled({bx-o-1.f,bt2-o-1.f},{bx+barW+o+1.f,bb+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bx-1.f,bt2-1.f},{bx+barW+1.f,bb+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bx,bt2},{bx+barW,bb},bgDark,barRound);
                if(g_espHealthStyle==0){
                    ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
                    ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
                    dl->AddRectFilledMultiColor({bx,bt2+bh*(1.f-fill)},{bx+barW,bb}, c1,c1, c2,c2);
                }else{
                    dl->AddRectFilled({bx,bt2+bh*(1.f-fill)},{bx+barW,bb},hbCol,barRound);
                }
                byBar=bt2+bh*(1.f-fill);
            }else if(g_espHealthPos==1){
                float by=bt2-barOff-barW;
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    dl->AddRectFilled({bl-o-1.f,by-o-1.f},{br+o+1.f,by+barW+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bl-1.f,by-1.f},{br+1.f,by+barW+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bl,by},{br,by+barW},bgDark,barRound);
                if(g_espHealthStyle==0){
                    ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
                    ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
                    dl->AddRectFilledMultiColor({bl,by},{bl+bw*fill,by+barW}, c2,c1, c1,c2);
                }else{
                    dl->AddRectFilled({bl,by},{bl+bw*fill,by+barW},hbCol,barRound);
                }
            }
            if(e.health<100){
                char hpBuf[16]; std::snprintf(hpBuf,sizeof(hpBuf),"%d",e.health);
                ImFont* font=ImGui::GetFont();
                float fsz=(g_espHealthPos==1)?10.f:g_espNameSize*0.85f;
                ImVec2 ts=font->CalcTextSizeA(fsz,FLT_MAX,0.f,hpBuf);
                float tx=bx+(g_espHealthPos==0?barW+2.f:(g_espHealthPos==2?-ts.x-2.f:bl+bw*0.5f-ts.x*0.5f));
                float ty=(g_espHealthPos==1)?bt2-barOff-barW-ts.y-1.f:byBar-ts.y*0.5f;
                dl->AddText(font,fsz,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(180*alpha)),hpBuf);
                dl->AddText(font,fsz,{tx,ty},IM_COL32(255,255,255,(int)(220*alpha)),hpBuf);
            }
        }
        if(g_espHeadDot){
            float dotR = bw*0.16f;
            if(dotR < 7.f) dotR = 7.f;
            for(int g=4;g>=1;g--){
                float o=(float)g; int r_=(boxCol>>IM_COL32_R_SHIFT)&0xFF,g_=(boxCol>>IM_COL32_G_SHIFT)&0xFF,b=(boxCol>>IM_COL32_B_SHIFT)&0xFF;
                dl->AddCircle({e.head_fx,e.head_fy},dotR+o,IM_COL32(r_,g_,b,(int)(35*alpha/(float)g)),16,1.2f);
            }
            dl->AddCircle({e.head_fx,e.head_fy},dotR,IM_COL32(0,0,0,(int)(180*alpha)),16,1.5f);
            dl->AddCircleFilled({e.head_fx,e.head_fy},dotR*0.55f,
                IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(200*alpha)),12);
            dl->AddCircle({e.head_fx,e.head_fy},dotR,boxCol,16,1.0f);
        }
        if(g_espLines){
            float sx=(float)g_esp_screen_w*0.5f,sh=(float)g_esp_screen_h;
            float sy = (g_espLineAnchor==0) ? 0.f : ((g_espLineAnchor==2) ? sh : sh*0.5f);
            dl->AddLine({sx,sy},{e.feet_x,e.feet_y},IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(100*alpha)),0.8f);
        }
        if(g_espSkeleton){
            ImU32 scol=(g_espBoxStyle==3)
                ? IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(200*alpha))
                : IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(180*alpha));
            bool drew = DrawSkeletonBones(dl, e, scol);
            if(!drew){
                float top=e.box_t, bottom=e.box_b, center=cx;
                float h=bottom-top;
                float shoulderY=top+h*0.28f;
                float pelvisY=top+h*0.55f;
                float neckY=top+h*0.18f;
                float hipY=top+h*0.65f;
                float kneeY=top+h*0.82f;
                float halfShoulder=bw*0.35f;
                float halfHip=bw*0.25f;
                ImVec2 head{e.head_x,e.head_y};
                ImVec2 neck{center,neckY};
                ImVec2 pelvis{center,pelvisY};
                ImVec2 lShoulder{center-halfShoulder,shoulderY};
                ImVec2 rShoulder{center+halfShoulder,shoulderY};
                ImVec2 lElbow{center-halfShoulder*1.3f,shoulderY+h*0.12f};
                ImVec2 rElbow{center+halfShoulder*1.3f,shoulderY+h*0.12f};
                ImVec2 lHand{center-halfShoulder*1.4f,shoulderY+h*0.24f};
                ImVec2 rHand{center+halfShoulder*1.4f,shoulderY+h*0.24f};
                ImVec2 lHip{center-halfHip,hipY};
                ImVec2 rHip{center+halfHip,hipY};
                ImVec2 lKnee{center-halfHip*0.8f,kneeY};
                ImVec2 rKnee{center+halfHip*0.8f,kneeY};
                ImVec2 lFoot{center-halfHip*0.7f,bottom};
                ImVec2 rFoot{center+halfHip*0.7f,bottom};
                dl->AddLine(head, neck, scol, 1.0f);
                dl->AddLine(neck, pelvis, scol, 1.0f);
                dl->AddLine(neck, lShoulder, scol, 1.0f);
                dl->AddLine(neck, rShoulder, scol, 1.0f);
                dl->AddLine(lShoulder, lElbow, scol, 1.0f);
                dl->AddLine(rShoulder, rElbow, scol, 1.0f);
                dl->AddLine(lElbow, lHand, scol, 1.0f);
                dl->AddLine(rElbow, rHand, scol, 1.0f);
                dl->AddLine(pelvis, lHip, scol, 1.0f);
                dl->AddLine(pelvis, rHip, scol, 1.0f);
                dl->AddLine(lHip, lKnee, scol, 1.0f);
                dl->AddLine(rHip, rKnee, scol, 1.0f);
                dl->AddLine(lKnee, lFoot, scol, 1.0f);
                dl->AddLine(rKnee, rFoot, scol, 1.0f);
            }
        }
        if(g_espName&&e.name[0]){
            ImFont* font = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();  // Cyrillic support
            ImVec2 ts=font->CalcTextSizeA(g_espNameSize,FLT_MAX,0.f,e.name);
            float tx=cx-ts.x*0.5f,ty=bt2-ts.y-4.f;
            for(int glow=3;glow>=1;glow--){
                float o=(float)glow; int glowA=(int)(60*alpha/(float)glow);
                dl->AddText(font,g_espNameSize,{tx+o,ty},IM_COL32(0,0,0,glowA),e.name);
                dl->AddText(font,g_espNameSize,{tx-o,ty},IM_COL32(0,0,0,glowA),e.name);
            }
            dl->AddText(font,g_espNameSize,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(150*alpha)),e.name);
            dl->AddText(font,g_espNameSize,{tx,ty},IM_COL32(220,220,230,(int)(alpha*255)),e.name);
        }
        if(e.planting||e.flashed||e.scoped){
            ImFont* sf = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
            float tagY = bt2 - (g_espName&&e.name[0] ? g_espNameSize+8.f : 0.f) - 10.f;
            ImU32 tagCol = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(200*alpha));
            float tagX = cx;
            if(e.planting){ const char*t="[Planting]"; ImVec2 tts=sf->CalcTextSizeA(10.f,FLT_MAX,0.f,t); dl->AddText(sf,10.f,{tagX-tts.x*0.5f,tagY},tagCol,t); tagY-=12.f; }
            if(e.flashed){ const char*t="[Flashed]"; ImVec2 tts=sf->CalcTextSizeA(10.f,FLT_MAX,0.f,t); dl->AddText(sf,10.f,{tagX-tts.x*0.5f,tagY},tagCol,t); tagY-=12.f; }
            if(e.scoped){ const char*t="[Scoped]"; ImVec2 tts=sf->CalcTextSizeA(10.f,FLT_MAX,0.f,t); dl->AddText(sf,10.f,{tagX-tts.x*0.5f,tagY},tagCol,t); }
        }
        uintptr_t weapon = 0;
        WeaponInfo winfo = WeaponInfoForId(0);
        int clip = 0;
        if((g_espWeapon||g_espWeaponIcon||g_espAmmo) && entityList){
            weapon = GetActiveWeapon(e.pawn, entityList);
            if(weapon){
                int wId = GetWeaponId(weapon);
                winfo = WeaponInfoForId(wId);
                clip = GetWeaponClip(weapon);
            }
        }
        if(g_espAmmo && weapon && winfo.maxClip > 0){
            int maxClip = winfo.maxClip;
            float frac = Clampf((float)clip / (float)maxClip, 0.f, 1.f);
            float barH = 8.f;
            float barRound = 4.f;
            ImU32 ammoBg = IM_COL32((int)(g_espAmmoCol1[0]*255),(int)(g_espAmmoCol1[1]*255),(int)(g_espAmmoCol1[2]*255),(int)(230*alpha));
            ImU32 ammoBorder = IM_COL32(45,45,65,(int)(200*alpha));
            ImU32 ammoOuter = IM_COL32(0,0,0,(int)(180*alpha));
            ImU32 ammoBarCol;
            if(g_espAmmoStyle==0){
                ImU32 c1=IM_COL32((int)(g_espAmmoCol1[0]*255),(int)(g_espAmmoCol1[1]*255),(int)(g_espAmmoCol1[2]*255),(int)(245*alpha));
                ImU32 c2=IM_COL32((int)(g_espAmmoCol2[0]*255),(int)(g_espAmmoCol2[1]*255),(int)(g_espAmmoCol2[2]*255),(int)(245*alpha));
                dl->AddRectFilled({bl-2.f,belowY-2.f},{br+2.f,belowY+barH+2.f},ammoOuter,barRound+2.f);
                dl->AddRectFilled({bl-1.f,belowY-1.f},{br+1.f,belowY+barH+1.f},ammoBorder,barRound+1.f);
                dl->AddRectFilled({bl,belowY},{br,belowY+barH},ammoBg,barRound);
                dl->AddRectFilledMultiColor({bl,belowY},{bl+bw*frac,belowY+barH}, c1,c1, c2,c2);
            }else{
                ammoBarCol = IM_COL32((int)(g_espAmmoCol2[0]*255),(int)(g_espAmmoCol2[1]*255),(int)(g_espAmmoCol2[2]*255),(int)(245*alpha));
                dl->AddRectFilled({bl-2.f,belowY-2.f},{br+2.f,belowY+barH+2.f},ammoOuter,barRound+2.f);
                dl->AddRectFilled({bl-1.f,belowY-1.f},{br+1.f,belowY+barH+1.f},ammoBorder,barRound+1.f);
                dl->AddRectFilled({bl,belowY},{br,belowY+barH},ammoBg,barRound);
                dl->AddRectFilled({bl,belowY},{bl+bw*frac,belowY+barH},ammoBarCol,barRound);
            }
            if(frac > 0.01f && frac < 1.f) dl->AddRect({bl+bw*frac,belowY},{bl+bw*frac+0.5f,belowY+barH},IM_COL32(255,255,255,(int)(80*alpha)),0.f,0,1.f);
            belowY += barH + 5.f;
        }
        if((g_espWeapon||g_espWeaponIcon) && weapon){
            float iconW = 0.f;
            ImVec2 its = {0.f, 0.f};
            ImVec2 ts = {0.f, 0.f};
            // gun_icons font disabled - causes crash, use text fallback (AK, M4, etc.)
            if(g_espWeaponIcon && *winfo.icon){
                ImFont* wf = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
                its = wf->CalcTextSizeA(12.f, FLT_MAX, 0.f, winfo.icon);
                iconW = its.x + (g_espWeapon ? 4.f : 0.f);
            }
            if(g_espWeapon){
                std::string wtext = winfo.name;
                if(!wtext.empty()){
                    ImFont* wf = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
                    ts = wf->CalcTextSizeA(12.f, FLT_MAX, 0.f, wtext.c_str());
                }
            }
            float blockLeft = cx - (iconW + ts.x)*0.5f;
            if(g_espWeaponIcon && *winfo.icon){
                ImFont* wf = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
                float itx = blockLeft;
                dl->AddText(wf, 12.f, {itx+1.f,belowY+1.f}, IM_COL32(0,0,0,(int)(140*alpha)), winfo.icon);
                dl->AddText(wf, 12.f, {itx,belowY}, dimCol, winfo.icon);
            }
            if(g_espWeapon){
                std::string wtext = winfo.name;
                if(!wtext.empty()){
                    ImFont* wf = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
                    float tx = blockLeft + iconW;
                    dl->AddText(wf, 12.f, {tx+1.f,belowY+1.f}, IM_COL32(0,0,0,(int)(140*alpha)), wtext.c_str());
                    dl->AddText(wf, 12.f, {tx,belowY}, dimCol, wtext.c_str());
                }
            }
            if(g_espWeaponIcon||g_espWeapon){
                float maxH = 12.f;
                if(g_espWeapon) maxH = 14.f;
                belowY += maxH + 2.f;
            }
        }
        if(g_espMoney){
            int money = GetPlayerMoney(e.controller);
            if(money > 0){
                char mbuf[32];
                std::snprintf(mbuf,sizeof(mbuf),"$%d", money);
                ImVec2 ts=ImGui::CalcTextSize(mbuf);
                float sw=(float)g_esp_screen_w;
                float tx = (g_espMoneyPos==1) ? (sw - ts.x - 12.f) : (cx-ts.x*0.5f);
                float ty = (g_espMoneyPos==1) ? (bt2 + bh*0.5f - ts.y*0.5f) : belowY;
                dl->AddText({tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(140*alpha)),mbuf);
                dl->AddText({tx,ty},IM_COL32(120,220,120,(int)(220*alpha)),mbuf);
                if(g_espMoneyPos==0) belowY += ts.y + 2.f;
            }
        }
        if(g_espDist){
            char dbuf[32];snprintf(dbuf,sizeof(dbuf),"%.0fm",e.distance);
            ImVec2 ts=ImGui::CalcTextSize(dbuf);float tx=cx-ts.x*0.5f;
            dl->AddText({tx+1.f,belowY+1.f},IM_COL32(0,0,0,(int)(130*alpha)),dbuf);
            dl->AddText({tx,belowY},dimCol,dbuf);
            belowY += ts.y + 2.f;
        }
        if(g_espSpotted&&e.spotted){
            dl->AddCircleFilled({br+6.f,bt2+6.f},3.f,IM_COL32(90,220,130,(int)(200*alpha)));
        }
    }
}

static void DrawOofArrows(){
    if(!g_espOof||g_esp_oof_count<=0)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float sz=g_espOofSize*0.5f;
    float c=0.7f;
    for(int i=0;i<g_esp_oof_count;i++){
        float x=g_esp_oof[i].x, y=g_esp_oof[i].y, a=g_esp_oof[i].angle;
        ImU32 col=g_esp_oof[i].col;
        float co=cosf(a), si=sinf(a);
        ImVec2 tip{x + si*sz, y - co*sz};
        ImVec2 left{x - co*sz*c - si*sz*c, y - si*sz*c + co*sz*c};
        ImVec2 right{x + co*sz*c - si*sz*c, y + si*sz*c + co*sz*c};
        dl->AddTriangleFilled(tip, left, right, col);
        dl->AddTriangle(tip, left, right, IM_COL32(0,0,0,180),1.5f);
    }
}

static void DrawRadar(){
    if(!g_radarEnabled) return;
    ImGui::SetNextWindowSize({g_radarSize,g_radarSize}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({20.f,80.f}, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGuiWindowFlags wf=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    if(!g_menuOpen) wf |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##radar", nullptr, wf);
    ImDrawList*dl=ImGui::GetWindowDrawList();if(!dl){ImGui::End();return;}
    if(ImGui::IsWindowHovered()){
        float wheel = ImGui::GetIO().MouseWheel;
        if(wheel != 0.f) g_radarRange = Clampf(g_radarRange - wheel*200.f, 300.f, 8000.f);
    }
    ImVec2 pos=ImGui::GetWindowPos();
    ImVec2 size=ImGui::GetWindowSize();
    ImVec2 center{pos.x+size.x*0.5f,pos.y+size.y*0.5f};
    dl->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},IM_COL32(10,10,14,200),6.f);
    dl->AddRect(pos,{pos.x+size.x,pos.y+size.y},IM_COL32(60,60,80,200),6.f);
    float radarRadius = size.x*0.5f-8.f;
    ImU32 ringCol = IM_COL32(80,80,110,90);
    dl->AddCircle(center, radarRadius*0.33f, ringCol, 64, 1.f);
    dl->AddCircle(center, radarRadius*0.67f, ringCol, 64, 1.f);
    dl->AddLine({center.x,pos.y+6.f},{center.x,pos.y+size.y-6.f},IM_COL32(80,80,110,120),1.f);
    dl->AddLine({pos.x+6.f,center.y},{pos.x+size.x-6.f,center.y},IM_COL32(80,80,110,120),1.f);
    uintptr_t vaAddr=ViewAnglesAddr();
    float yaw=vaAddr?Rd<float>(vaAddr+4):0.f;
    float yawRad=yaw*(3.14159265f/180.f);
    float cosY=cosf(yawRad), sinY=sinf(yawRad);
    float scale=radarRadius/Clampf(g_radarRange,300.f,8000.f);
    ImVec2 base{center.x-cosY*4.f, center.y-sinY*4.f};
    ImVec2 tip{center.x+cosY*10.f, center.y+sinY*10.f};
    ImVec2 left{base.x-sinY*5.f, base.y+cosY*5.f};
    ImVec2 right{base.x+sinY*5.f, base.y-cosY*5.f};
    dl->AddTriangleFilled(tip, left, right, IM_COL32(240,240,240,255));
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry& e=g_esp_players[i];
        if(!e.valid) continue;
        Vec3 delta{e.origin_x-g_localOrigin.x,e.origin_y-g_localOrigin.y,0.f};
        float rx = delta.x*cosY + delta.y*sinY;
        float ry = -delta.x*sinY + delta.y*cosY;
        rx*=scale; ry*=scale;
        rx=Clampf(rx, -(size.x*0.5f-10.f), size.x*0.5f-10.f);
        ry=Clampf(ry, -(size.y*0.5f-10.f), size.y*0.5f-10.f);
        ImU32 col = (e.team==g_esp_local_team)?IM_COL32(80,140,255,230):IM_COL32(255,90,90,230);
        float rad = Clampf((float)e.health/100.f*5.f, 2.f, 5.f);
        ImVec2 p{center.x+rx,center.y+ry};
        dl->AddCircleFilled(p,rad,col,12);
        if(e.name[0]){
            ImFont* f = ImGui::GetFont();
            dl->AddText(f,9.f,{p.x+4.f,p.y-4.f},IM_COL32(200,200,220,200),e.name);
        }
    }
    if(g_bombActive){
        Vec3 delta{g_bombPos.x-g_localOrigin.x,g_bombPos.y-g_localOrigin.y,0.f};
        float rx = delta.x*cosY + delta.y*sinY;
        float ry = -delta.x*sinY + delta.y*cosY;
        rx*=scale; ry*=scale;
        rx=Clampf(rx, -(size.x*0.5f-10.f), size.x*0.5f-10.f);
        ry=Clampf(ry, -(size.y*0.5f-10.f), size.y*0.5f-10.f);
        dl->AddText({center.x+rx-3.f,center.y+ry-5.f},IM_COL32(255,220,120,230),"B");
    }
    dl->AddCircleFilled(center,4.f,IM_COL32(240,240,240,255),12);
    ImGui::End();
}

static void InitImGui(IDXGISwapChain*sc){
    if(!sc)return;
    DebugLog("[LitWare] InitImGui");
    DXGI_SWAP_CHAIN_DESC desc{};sc->GetDesc(&desc);g_gameHwnd=desc.OutputWindow;
    g_bbWidth = desc.BufferDesc.Width;
    g_bbHeight = desc.BufferDesc.Height;
    g_bbFormat = desc.BufferDesc.Format;
    ID3D11Texture2D*bb=nullptr;
    if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||!bb)return;
    sc->GetDevice(__uuidof(ID3D11Device),(void**)&g_device);
    if(g_device){
        g_device->GetImmediateContext(&g_context);
        HRESULT hr = g_device->CreateRenderTargetView(bb,nullptr,&g_rtv);
        if(FAILED(hr)){
            DWORD now = GetTickCount();
            if(now - g_lastRtvFail > 2000){
                PushNotification("RTV create failed", IM_COL32(255,80,80,255));
                g_lastRtvFail = now;
            }
        }
    }
    bb->Release();
    if(!g_device||!g_rtv||!g_gameHwnd){
        if(g_rtv){g_rtv->Release();g_rtv=nullptr;}
        if(g_context){g_context->Release();g_context=nullptr;}
        if(g_device){g_device->Release();g_device=nullptr;}
        return;
    }
    g_origWndProc=(WNDPROC)SetWindowLongPtrA(g_gameHwnd,GWLP_WNDPROC,(LONG_PTR)HookWndProc);
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGuiIO&io=ImGui::GetIO();
    io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    ImFontConfig fc{};fc.SizePixels=17.f;fc.FontDataOwnedByAtlas=false;
    font::lexend_bold = io.Fonts->AddFontFromMemoryTTF((void*)lexend_bold, (int)sizeof(lexend_bold), 17.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    fc.SizePixels=14.f;
    font::lexend_regular = io.Fonts->AddFontFromMemoryTTF((void*)lexend_regular, (int)sizeof(lexend_regular), 14.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    fc.SizePixels=20.f;
    font::icomoon = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 20.f, &fc, io.Fonts->GetGlyphRangesDefault());
    fc.SizePixels=15.f;
    font::icomoon_widget = io.Fonts->AddFontFromMemoryTTF((void*)icomoon_widget, (int)sizeof(icomoon_widget), 15.f, &fc, io.Fonts->GetGlyphRangesDefault());
    // CS2GunIcons disabled - causes crash. Weapon icon uses text fallback (AK, M4, etc.)
    // font::gun_icons stays nullptr
    ImFont* font = font::lexend_bold;
    if(!font) font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    if(!font) font = io.Fonts->AddFontDefault(&fc);
    if(font) io.FontDefault = font;
    bool winOk = ImGui_ImplWin32_Init(g_gameHwnd);
    bool dxOk = ImGui_ImplDX11_Init(g_device,g_context);
    if(!winOk || !dxOk){
        if(dxOk) ImGui_ImplDX11_Shutdown();
        if(winOk) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if(g_gameHwnd && g_origWndProc){
            SetWindowLongPtrA(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
            g_origWndProc = nullptr;
        }
        if(g_rtv){g_rtv->Release();g_rtv=nullptr;}
        if(g_context){g_context->Release();g_context=nullptr;}
        if(g_device){g_device->Release();g_device=nullptr;}
        return;
    }
    g_imguiInitialized=true;DebugLog("[LitWare] ImGui OK");
}

static bool g_firstFrame=false;

static void CleanupRender(){
    if(g_cleanupDone.exchange(true))return;
    ClipCursor(nullptr);  // Release cursor clip from menu (avoids hang if menu was open)
    // Unbind RTV before releasing - game/ImGui may have it bound
    if(g_context){g_context->OMSetRenderTargets(0,nullptr,nullptr);}
    if(g_gameHwnd&&g_origWndProc){
        SetWindowLongPtrA(g_gameHwnd,GWLP_WNDPROC,(LONG_PTR)g_origWndProc);
        g_origWndProc=nullptr;
    }
    if(g_imguiInitialized){
        ImGui_ImplDX11_Shutdown();   // also releases device/context refs it holds
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiInitialized=false;
    }
    if(g_rtv){g_rtv->Release();g_rtv=nullptr;}
    if(g_context){g_context->Release();g_context=nullptr;}
    if(g_device){g_device->Release();g_device=nullptr;}
    g_gameHwnd=nullptr;
    g_bbWidth=0;g_bbHeight=0;g_bbFormat=DXGI_FORMAT_UNKNOWN;
}

static void DoDeferredUnload(){
    if(!g_pendingUnload.exchange(false)) return;
    CleanupRender();
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    if(g_thisModule){
        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID mod)->DWORD{
            Sleep(800);  // Longer delay so game finishes any in-flight calls before unload
            FreeLibraryAndExitThread((HMODULE)mod, 0);  // never returns
            __assume(0);  // suppress C4702 unreachable code
        }, g_thisModule, 0, nullptr);
        if(hThread) CloseHandle(hThread);
    }
}

static void RequestUnload(){
    if(g_unloading) return;
    g_unloading = true;
    g_pendingUnload = true;
}
static void RenderFrame(IDXGISwapChain*sc){
    if(g_unloading)return;
    if(!g_firstFrame){DebugLog("[LitWare] first Present");g_firstFrame=true;g_telegramNoteStart=GetTickCount();}
    if(!g_imguiInitialized){InitImGui(sc);if(!g_imguiInitialized)return;}
    EnsureClientHooks();
    EnsureSceneHooks();
    DXGI_SWAP_CHAIN_DESC desc{};
    if(SUCCEEDED(sc->GetDesc(&desc))){
        if(desc.BufferDesc.Width!=g_bbWidth||desc.BufferDesc.Height!=g_bbHeight||desc.BufferDesc.Format!=g_bbFormat){
            if(g_rtv){g_rtv->Release();g_rtv=nullptr;}
            g_bbWidth=desc.BufferDesc.Width;
            g_bbHeight=desc.BufferDesc.Height;
            g_bbFormat=desc.BufferDesc.Format;
        }
    }
    if(!g_rtv){ID3D11Texture2D*bb=nullptr;
        if(g_device&&SUCCEEDED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))){
            HRESULT hr = g_device->CreateRenderTargetView(bb,nullptr,&g_rtv);
            bb->Release();
            if(FAILED(hr)){
                DWORD now = GetTickCount();
                if(now - g_lastRtvFail > 2000){
                    PushNotification("RTV create failed", IM_COL32(255,80,80,255));
                    g_lastRtvFail = now;
                }
            }
        }if(!g_rtv)return;
    }
    if(GetAsyncKeyState(VK_INSERT)&1)g_menuOpen=!g_menuOpen;
    // F1 keybinds popup disabled
    if(GetAsyncKeyState(VK_END)&1){RequestUnload();return;}
    if(!g_safeMode){
        BuildESPData();BuildSpectatorList();ProcessHitEvents();UpdateBombInfo();UpdateSoundPings();
        RunNoFlash();RunNoSmoke();RunGlow();RunRadarHack();RunBHop();RunFOVChanger();
        RunAutostop();RunRCS();RunStrafeHelper();RunTriggerBot();ReleaseTriggerAttack();RunAimbot();RunDoubleTap();
    }else{g_esp_count=0;g_esp_oof_count=0;}
    ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
    ImGuiIO&io=ImGui::GetIO();
    io.MouseDrawCursor = g_menuOpen;
    if(g_menuOpen&&g_gameHwnd){
        RECT r{}; GetWindowRect(g_gameHwnd,&r);
        ClipCursor(&r);  // Block mouse input to game when menu open
        POINT pt{};GetCursorPos(&pt);ScreenToClient(g_gameHwnd,&pt);
        io.MousePos={(float)pt.x,(float)pt.y};
        io.MouseDown[0]=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        io.MouseDown[1]=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;SetCursor(LoadCursor(nullptr,IDC_ARROW));
    }else{
        ClipCursor(nullptr);  // Release cursor when menu closed
        io.MouseDown[0]=false;io.MouseDown[1]=false;
    }
    float sw=(float)g_esp_screen_w, sh=(float)g_esp_screen_h;
    UpdateAndDrawParticles(io.DeltaTime, sw, sh);
    DrawMenu();
    DrawKeybindsWindow();
    if(!g_safeMode){ DrawESP(); DrawOofArrows(); DrawBombTimer(sw);
        DrawSoundPings(io.DeltaTime);
        DrawSpectatorList(sw); DrawNoCrosshair(sw, sh); DrawFovCircle(sw, sh); }
    DrawLogs(io.DeltaTime, sw, sh);
    DrawHitmarker(sw, sh);
    DrawNotifications(io.DeltaTime, sw, sh);
    DrawWatermark(sw);
    ImGui::Render();
    g_context->OMSetRenderTargets(1,&g_rtv,nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

HRESULT __stdcall HookPresent(IDXGISwapChain*sc,UINT sync,UINT flags){
    static DWORD s_lastCrashTime = 0;
    static int s_crashCount = 0;
    __try {
        if(!g_pendingUnload) RenderFrame(sc);
        s_crashCount = 0;  // reset on success
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_crashCount++;
        g_safeMode = true;  // skip game reads next frames
        g_esp_count = 0; g_esp_oof_count = 0; g_espEnabled = false;
        DWORD now = GetTickCount();
        if(now - s_lastCrashTime > 5000){
            s_lastCrashTime = now;
            PushNotification("Crash caught - safe mode. Enable ESP to retry.", IM_COL32(255,100,80,255));
        }
    }
    if(g_pendingUnload) DoDeferredUnload();
    return g_originalPresent(sc,sync,flags);
}

static bool GetPresentVtable(void*&out){
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;
    wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="LW_D11_DUMMY";
        RegisterClassA(&wc);HWND hw=CreateWindowA("LW_D11_DUMMY","",WS_OVERLAPPED,0,0,8,8,
        nullptr,nullptr,wc.hInstance,nullptr);
    if(!hw){UnregisterClassA("LW_D11_DUMMY",wc.hInstance);return false;}
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=1;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=hw;
    sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain*dsc=nullptr;ID3D11Device*ddev=nullptr;D3D_FEATURE_LEVEL dfl{};
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
        nullptr,0,D3D11_SDK_VERSION,&sd,&dsc,&ddev,&dfl,nullptr);
    bool ok=false;
    if(SUCCEEDED(hr)){out=(*reinterpret_cast<void***>(dsc))[8];
        dsc->Release();ddev->Release();ok=(out!=nullptr);
        DebugLog("[LitWare] vtable Present @ %p",out);
    }
    DestroyWindow(hw);UnregisterClassA("LW_D11_DUMMY",wc.hInstance);return ok;
}

static void*PatternScan(HMODULE mod,const char*pat,const char*mask){
    if(!mod||!pat||!mask)return nullptr;
    MODULEINFO mi{};if(!GetModuleInformation(GetCurrentProcess(),mod,&mi,sizeof(mi)))return nullptr;
    auto b=(const uint8_t*)mi.lpBaseOfDll;size_t sz=mi.SizeOfImage,pl=strlen(mask);
    for(size_t i=0;i+pl<=sz;++i){bool ok=true;
        for(size_t j=0;j<pl&&ok;++j)if(mask[j]!='?'&&b[i+j]!=(uint8_t)pat[j])ok=false;
        if(ok)return(void*)(b+i);
    }return nullptr;
}

static void* ResolveRelative(void* instr, int offset, int size){
    if(!instr) return nullptr;
    int32_t rel = *reinterpret_cast<int32_t*>((uint8_t*)instr + offset);
    return (uint8_t*)instr + size + rel;
}

static void __fastcall HookDrawSceneObject(void* a1, void* a2, void* a3, int a4, int a5, void* a6, void* a7, void* a8){
    const bool doSceneChams = g_chamsEnabled && g_chamsScene;
    if(doSceneChams && a3 && a4 > 0 && a4 < 20000){
        __try{
            uint8_t* base = (uint8_t*)a3;
            int localTeam = g_esp_local_team;

            // Try multiple known strides/offsets for different CS2 builds
            static const size_t strides[] = { 0x68, 0x78, 0x58 };
            static const size_t colorOffsets[] = { 0x40, 0x48, 0x38 };
            static const size_t infoOffsets[] = { 0x48, 0x50, 0x40 };
            static const size_t materialOffsets[] = { 0x18, 0x20, 0x10 };
            static const int infoIdOffsets[] = { 0xB0, 0xC0, 0xA0, 0x90 };
            // Known player model class IDs across different builds
            static const int ctIds[] = { 113, 107, 115, 120 };
            static const int tIds[] = { 104, 98, 106, 110 };
            static const int armsIds[] = { 38, 35, 40 };

            for(int i=0;i<a4;++i){
                __try{
                    for(size_t strideTry = 0; strideTry < 3; ++strideTry){
                        size_t stride = strides[strideTry];
                        size_t colOff = colorOffsets[strideTry];
                        size_t infoOff = infoOffsets[strideTry];
                        uintptr_t objBase = (uintptr_t)(base + i * stride);
                        auto* c = reinterpret_cast<MaterialColor*>(objBase + colOff);
                        if(!IsLikelyPtr((uintptr_t)c)) continue;
                        // Sanity check: color values should be in valid range (non-zero alpha)
                        if(c->a == 0) continue;

                        bool applied = false;
                        if(doSceneChams){
                            uintptr_t info = Rd<uintptr_t>(objBase + infoOff);
                            // Try multiple ID offsets
                            int id = 0;
                            for(int idOff : infoIdOffsets){
                                int candidate = info ? Rd<int>(info + idOff) : 0;
                                if(candidate > 0 && candidate < 300){ id = candidate; break; }
                            }

                            bool isCT = false, isT = false, isArms = false;
                            for(int cid : ctIds) if(id == cid) isCT = true;
                            for(int tid : tIds) if(id == tid) isT = true;
                            for(int aid : armsIds) if(id == aid) isArms = true;

                            bool isWeapon = false;
                            if(g_weaponChamsEnabled){
                                uintptr_t mat = Rd<uintptr_t>(objBase + materialOffsets[strideTry]);
                                const char* name = SafeMaterialName(mat);
                                if(name && (strstr(name,"weapon") || strstr(name,"knife") || strstr(name,"rifle") || strstr(name,"pistol")))
                                    isWeapon = true;
                            }

                            if(isArms && g_handsColorEnabled){
                                MaterialColor out = MakeMatColor(g_handsColor);
                                out.a = c->a;
                                *c = out;
                                applied = true;
                            }else if(isWeapon && g_weaponChamsEnabled){
                                MaterialColor out = MakeMatColor(g_weaponChamsCol);
                                out.a = c->a;
                                *c = out;
                                applied = true;
                            }else if(isCT || isT){
                                int team = isCT ? 3 : 2;
                                bool isTeam = (localTeam != 0 && team == localTeam);
                                if(!g_chamsEnemyOnly || !isTeam){
                                    float tmp[4];
                                    if(g_chamsIgnoreZ && !isTeam){
                                        tmp[0]=g_chamsIgnoreZCol[0]; tmp[1]=g_chamsIgnoreZCol[1];
                                        tmp[2]=g_chamsIgnoreZCol[2]; tmp[3]=g_chamsIgnoreZCol[3];
                                    }else{
                                        float* baseCol = isTeam ? g_chamsTeamCol : g_chamsEnemyCol;
                                        tmp[0]=baseCol[0]; tmp[1]=baseCol[1]; tmp[2]=baseCol[2]; tmp[3]=baseCol[3];
                                    }
                                    ApplyChamsMaterial(tmp);
                                    MaterialColor out = MakeMatColor(tmp);
                                    out.a = c->a;
                                    *c = out;
                                    applied = true;
                                }
                            }
                        }
                        break;
                    }
                }__except(EXCEPTION_EXECUTE_HANDLER){}
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){}
    }
    if(g_origDrawSceneObject) g_origDrawSceneObject(a1,a2,a3,a4,a5,a6,a7,a8);
}

static void __fastcall HookDrawSkyboxArray(void* a1, void* a2, void* draw_primitive, int count, void* a5, void* a6, void* a7){
    if(g_skyColorEnabled && draw_primitive && count > 0 && count < 100){
        __try{
            size_t offset = (size_t)(count * 0x68) - 0x50;
            void** skybox_obj_ptr = (void**)((char*)draw_primitive + offset);
            if(skybox_obj_ptr && *skybox_obj_ptr){
                float* color_ptr = (float*)((char*)(*skybox_obj_ptr) + 0x100);
                color_ptr[0] = g_skyColor[0];
                color_ptr[1] = g_skyColor[1];
                color_ptr[2] = g_skyColor[2];
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){}
    }
    if(g_origDrawSkyboxArray) g_origDrawSkyboxArray(a1,a2,draw_primitive,count,a5,a6,a7);
}

static float __fastcall HookGetWorldFov(void* rcx){
    float orig = g_origGetWorldFov ? g_origGetWorldFov(rcx) : 90.f;
    if(!g_fovEnabled) return orig;
    if(!g_client) EnsureModules();
    uintptr_t lp = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!lp) return orig;
    bool scoped = Rd<bool>(lp + offsets::cs_pawn::m_bIsScoped);
    if(scoped) return orig;
    return g_fovValue;
}

static void* __fastcall HookFirstPersonLegs(void* a1, void* a2, void* a3, void* a4, void* a5){
    if(g_noLegs) return nullptr;
    return g_origFirstPersonLegs ? g_origFirstPersonLegs(a1,a2,a3,a4,a5) : nullptr;
}

static void EnsureSceneHooks(){
    bool needSky = g_skyColorEnabled;
    if(g_drawSceneHooked && (!needSky || g_drawSkyHooked)){
        g_sceneHooksReady = true;
        return;
    }
    DWORD now = GetTickCount();
    if(now - g_lastSceneHookAttempt < 2000) return;
    g_lastSceneHookAttempt = now;
    HMODULE scene = GetModuleHandleA("scenesystem.dll");
    if(!scene) return;
    static const char PAT_DRAW_SCENE[] = "\x48\x8B\xC4\x4C\x89\x40\x00\x48\x89\x50\x00\x55\x53\x41\x56";
    static const char MSK_DRAW_SCENE[] = "xxxxxx?xxx?xxxx";
    // Fallback pattern from weave/pidoraise
    static const char PAT_DRAW_SCENE2[] = "\x48\x8B\xC4\x53\x41\x54\x41\x55\x48\x81\xEC";
    static const char MSK_DRAW_SCENE2[] = "xxxxxxxxxxx";
    // Another fallback
    static const char PAT_DRAW_SCENE3[] = "\x48\x89\x54\x24\x00\x55\x57\x41\x55\x48\x8D\xAC\x24";
    static const char MSK_DRAW_SCENE3[] = "xxxx?xxxxxxxx";
    static const char PAT_SKY[] = "\x45\x85\xC9\x0F\x8E\x00\x00\x00\x00\x4C\x8B\xDC\x55";
    static const char MSK_SKY[] = "xxxxx????xxxx";
    void* drawScene = nullptr;
    if(!g_drawSceneHooked){
        drawScene = PatternScan(scene, PAT_DRAW_SCENE, MSK_DRAW_SCENE);
        if(!drawScene) drawScene = PatternScan(scene, PAT_DRAW_SCENE2, MSK_DRAW_SCENE2);
        if(!drawScene) drawScene = PatternScan(scene, PAT_DRAW_SCENE3, MSK_DRAW_SCENE3);
    }
    void* drawSky = (!g_drawSkyHooked && needSky) ? PatternScan(scene, PAT_SKY, MSK_SKY) : nullptr;
    if(!g_drawSceneHooked){
        if(drawScene && MH_CreateHook(drawScene, &HookDrawSceneObject, reinterpret_cast<void**>(&g_origDrawSceneObject))==MH_OK){
            MH_EnableHook(drawScene);
            g_drawSceneHooked = true;
            DebugLog("[LitWare] draw_scene_object hook ok");
        } else {
            DebugLog("[LitWare] draw_scene_object hook failed");
            if(now - g_lastSceneHookNotify > 5000){
                PushNotification("Scene hook failed", IM_COL32(255,80,80,255));
                g_lastSceneHookNotify = now;
            }
        }
    }
    if(needSky && !g_drawSkyHooked){
        if(drawSky && MH_CreateHook(drawSky, &HookDrawSkyboxArray, reinterpret_cast<void**>(&g_origDrawSkyboxArray))==MH_OK){
            MH_EnableHook(drawSky);
            g_drawSkyHooked = true;
            DebugLog("[LitWare] draw_skybox_array hook ok");
        } else {
            DebugLog("[LitWare] draw_skybox_array hook failed");
        }
    }
    g_sceneHooksReady = g_drawSceneHooked && (!needSky || g_drawSkyHooked);
}

static void EnsureClientHooks(){
    if(g_clientHooksReady && g_fpLegsHooked) return;
    DWORD now = GetTickCount();
    if(now - g_lastClientHookAttempt < 2000) return;
    g_lastClientHookAttempt = now;
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    static const char PAT_FOV[] = "\xE8\x00\x00\x00\x00\xF3\x0F\x11\x45\x00\x48\x8B\x5C\x24";
    static const char MSK_FOV[] = "x????xxxx?xxx";
    static const char PAT_FP_LEGS[] = "\x40\x55\x53\x56\x41\x56\x41\x57\x48\x8D\xAC\x24\x00\x00\x00\x00\x48\x81\xEC\x00\x00\x00\x00\xF2\x0F\x10\x42";
    static const char MSK_FP_LEGS[] = "xxxxxxxxxxxx????xxx????xxxx";
    if(!g_worldFovHooked){
        void* callSite = PatternScan(client, PAT_FOV, MSK_FOV);
        void* fn = ResolveRelative(callSite, 1, 5);
        if(fn && MH_CreateHook(fn, &HookGetWorldFov, reinterpret_cast<void**>(&g_origGetWorldFov))==MH_OK){
            MH_EnableHook(fn);
            g_worldFovHooked = true;
            DebugLog("[LitWare] get_world_fov hook ok");
        } else {
            DebugLog("[LitWare] get_world_fov hook failed");
            if(now - g_lastClientHookNotify > 5000){
                PushNotification("FOV hook failed", IM_COL32(255,80,80,255));
                g_lastClientHookNotify = now;
            }
        }
    }
    if(!g_fpLegsHooked){
        void* fn = PatternScan(client, PAT_FP_LEGS, MSK_FP_LEGS);
        if(fn && MH_CreateHook(fn, &HookFirstPersonLegs, reinterpret_cast<void**>(&g_origFirstPersonLegs))==MH_OK){
            MH_EnableHook(fn);
            g_fpLegsHooked = true;
            DebugLog("[LitWare] fp_legs hook ok");
        } else {
            DebugLog("[LitWare] fp_legs hook failed");
        }
    }
    g_clientHooksReady = g_worldFovHooked;
}

}

namespace render_hook{
bool Initialize(){
    void*presentAddr=nullptr;
    if(!GetPresentVtable(presentAddr)){
        DebugLog("[LitWare] vtable failed, pattern scan fallback");
        const int kMs=15000;HMODULE ov=nullptr;
        for(int w=0;w<kMs&&!ov;w+=100){
            ov=GetModuleHandleA("gameoverlayrenderer64.dll");if(!ov)Sleep(100);
        }
        if(ov){
            static const char PPAT[]="\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x56\x57\x41\x54\x41\x56\x41\x57\x48\x83\xEC\x00\x41\x8B\xE8";
            static const char PMASK[]="xxxx?xxxx?xxxxxxxxx?xxxxx";
            presentAddr=PatternScan(ov,PPAT,PMASK);
        }
        if(!presentAddr){DebugLog("[LitWare] could not find Present address");return false;}
    }
    MH_STATUS mh = MH_Initialize();
    if(mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED){
        DebugLog("[LitWare] MH_Initialize failed: %d", (int)mh);
        return false;
    }
    if(MH_CreateHook(presentAddr,&HookPresent,reinterpret_cast<void**>(&g_originalPresent))!=MH_OK){
        DebugLog("[LitWare] MH_CreateHook failed");MH_Uninitialize();return false;
    }
    if(MH_EnableHook(presentAddr)!=MH_OK){
        DebugLog("[LitWare] MH_EnableHook failed");MH_Uninitialize();return false;
    }
    DebugLog("[LitWare] Present hook installed");return true;
}
void Shutdown(){RequestUnload();}
}
 