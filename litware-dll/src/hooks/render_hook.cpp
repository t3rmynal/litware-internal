#include "render_hook.h"
#include "Fonts.h"
#include "res/font.h"
#include "res/jetbrains_mono.h"
#include "res/cs2_gun_icons.h"
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

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "winmm.lib")
#endif

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

static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_bbWidth = 0;
static UINT g_bbHeight = 0;
static DXGI_FORMAT g_bbFormat = DXGI_FORMAT_UNKNOWN;
static DWORD g_lastRtvFail = 0;
static bool g_imguiInitialized = false;
static bool g_menuOpen = false;
static bool g_showDebugConsole = false;
static std::atomic_bool g_unloading{false};
static std::atomic_bool g_cleanupDone{false};
static std::atomic_bool g_pendingUnload{false};
static HWND g_gameHwnd = nullptr;
static uintptr_t g_client = 0;
static uintptr_t g_engine2 = 0;
static Vec3 g_localOrigin{};

static bool g_espEnabled = true;
static bool g_espDrawBox = true;
static bool g_espOnlyVis = false;
static int g_espBoxStyle = 4;
static float g_espBoxThick = 1.5f;
static float g_espEnemyCol[4]{0.209677f,0.502861f,1.f,1.f};
static float g_espTeamCol[4]{1.f,0.25f,0.921371f,1.f};
static bool g_espShowTeam = false;
static bool g_espName = true;
static float g_espNameSize = 14.8f;
static bool g_espHealth = true;
static bool g_espHealthText = true;
static int g_espHealthPos = 0;
static int g_espHealthStyle = 0;
static float g_espHealthGradientCol1[4]{0.56f,0.92f,0.2f,1.f};
static float g_espHealthGradientCol2[4]{1.f,0.27f,0.27f,1.f};
static bool g_espDist = true;
static float g_espMaxDist = 100.f;
static bool g_espSkeleton = false;
static bool g_espLines = true;
static int g_espLineAnchor = 1;  // 0 верх 1 центр 2 низ
static bool g_espOof = true;
static float g_espOofSize = 33.f;
static float g_skeletonThick = 1.1f;
struct OofEntry { float x, y; float angle; ImU32 col; };
static OofEntry g_esp_oof[32];
static int g_esp_oof_count = 0;
static bool g_espHeadDot = false;
static bool g_espSpotted = true;
static bool g_visCheckEnabled = true;
static bool g_espWeapon = true;
static bool g_espWeaponIcon = true;
static bool g_espAmmo = true;
static int g_espAmmoStyle = 0;
static float g_espAmmoCol1[4]{0.145098f,0.337255f,0.768627f,1.f};
static float g_espAmmoCol2[4]{0.329412f,0.803922f,1.f,1.f};
static bool g_espMoney = true;
static int g_espMoneyPos = 0;  // 0 снизу 1 справа
static float g_espHeadForward = 6.f;
static float g_espScale = 1.0f;
static int g_espPreviewPos = 0;  // 0 справа 1 слева 2 сверху 3 снизу
static bool g_noFlash = false;
static bool g_noSmoke = false;
static bool g_noCrosshair = false;
static bool g_noLegs = false;
static DWORD g_lastNoSmokeTick = 0;
static bool g_glowEnabled = true;
static float g_glowEnemyCol[4]{0.f,0.225806f,1.f,1.f};
static float g_glowTeamCol[4]{0.f,0.f,0.f,1.f};
static float g_glowAlpha = 1.0f;
static bool g_chamsEnabled = false;
static bool g_chamsEnemyOnly = true;
static bool g_chamsIgnoreZ = false;
static int g_chamsMaterial = 0;      // 0 обычный 1 яркий 2 мягкий 3 свечение 4 пульс 5 металл
static float g_chamsEnemyCol[4]{1.f,0.2f,0.2f,1.f};
static float g_chamsTeamCol[4]{0.2f,0.5f,1.f,1.f};
static float g_chamsIgnoreZCol[4]{0.4f,0.9f,0.5f,0.8f};
static bool g_chamsScene = true;
static bool g_weaponChamsEnabled = false;
static float g_weaponChamsCol[4]{1.f,0.88f,0.35f,1.f};
static bool g_aimbotEnabled = false;
static int g_aimbotKey = VK_LBUTTON;
static float g_aimbotFov = 9.f;
static float g_aimbotSmooth = 15.1f;
static bool g_fovCircleEnabled = false;
static float g_fovCircleCol[4]{0.4f,0.7f,1.f,0.5f};
static bool g_aimbotTeamChk = true;
static bool g_aimbotVisCheck = true;  // цель только под прицелом
static int g_aimbotWeaponFilter = 0;  // 0 все 1 винтовки 2 снайперки 3 пистолеты
static bool g_rcsEnabled = false;
static float g_rcsX = 1.0f;
static float g_rcsY = 1.0f;
static float g_rcsSmooth = 1.0f;
static float g_rcsPrevPunchX = 0.f, g_rcsPrevPunchY = 0.f;
static bool g_tbEnabled = false;
static int g_tbKey = 0;
static int g_tbDelay = 50;
static bool g_tbTeamChk = true;
static UINT64 g_tbFireTime = 0;
static bool g_tbShouldFire = false;
static bool g_bombDefusing = false;
static uintptr_t g_bombDefuserPawn = 0;  // кто дефузит
static bool g_tbJustFired = false;
static int g_tbHoldFramesLeft = 0;
static bool g_dtEnabled = false;
static int g_dtKey = 0;
static bool g_bhopEnabled = false;
static bool g_strafeEnabled = false;
static int g_strafeKey = 0;
static bool g_nightModeOverlay = false;
static bool g_fovEnabled = false;
static float g_fovValue = 121.f;
static bool g_autostopEnabled = true;
static bool g_waitAimThenFire = true;
static float g_waitAimFovDeg = 2.5f;
static float g_aimbotLastBestFov = 1e9f;
static bool g_aimbotLastFound = false;
static bool g_snowEnabled = false;
static int g_snowDensity = 1;
static bool g_sakuraEnabled = false;
static float g_sakuraCol[4]{1.f,0.55f,0.7f,0.85f};
static bool g_starsEnabled = false;
static bool g_particlesWorld = false;
static float g_particlesWorldRadius = 600.f;
static float g_particlesWorldHeight = 320.f;
static float g_particlesWorldFloor = 40.f;
static float g_particlesWind = 18.f;
static float g_particlesDepthFade = 0.0022f;
static bool g_handsColorEnabled = false;
static float g_handsColor[4]{0.9f,0.9f,0.95f,1.f};
static bool g_skyColorEnabled = false;
static float g_skyColor[4]{0.225259f,0.192963f,0.693548f,1.f};
static bool g_worldColorEnabled = false;
static float g_worldColor[4]{0.92f, 0.94f, 1.f, 1.f};
static bool g_watermarkEnabled = true;
static bool g_showFpsWatermark = true;
static bool g_spectatorListEnabled = false;
static bool g_hitNotifEnabled = true;
static bool g_killNotifEnabled = true;
static bool g_hitSoundEnabled = false;
static bool g_damageFloatersEnabled = true;
static float g_damageFloaterDuration = 0.85f;
static float g_damageFloaterScale = 1.f;
static int g_damageFloaterAnchor = 0;  // 0 голова 1 грудь
static bool g_killEffectEnabled = false;
static float g_killEffectDuration = 0.6f;
static UINT64 g_lastKillEffectTime = 0;
static Vec3 g_lastKillEffectPos{};
static bool g_pendingKillParticles = false;
static int g_hitEffectType = 0;   // 0 нет 1 крест 2 вспышка 3 круг
static int g_killEffectType = 1;  // 0 нет 1 всплеск 2 текст
static float g_hitEffectCol[4]{1.f,0.9f,0.2f,0.9f};
static float g_killEffectCol[4]{1.f,0.3f,0.3f,0.95f};
static int g_hitSoundType = 1;
static bool g_radarEnabled = true;
static bool g_radarIngame = false;  // радар в игре
static float g_radarRange = 2000.f;
static float g_radarSize = 180.f;
static bool g_bombTimerEnabled = true;
static bool g_bulletTraceEnabled = true;
static float g_impactCol[4]{0.35f,0.94f,0.47f,0.78f};
static bool g_soundEnabled = true;
static float g_soundPuddleScale = 0.8f;
static float g_soundPuddleAlpha = 0.2f;
static bool g_soundBlipEnemy = true;
static bool g_soundBlipTeam = true;
static float g_soundBlipCol[4]{0.f, 0.419355f, 1.f, 1.f};
static float g_accentColor[4]{0.1f,0.55f,1.0f,1.0f};
static float g_menuOpacity = 0.96f;
static float g_uiScale = 1.00f;
static int g_activeTab = 0;  // 0 аим 1 визуал 2 мир 3 разное
static float g_tabAnim[8] = {};
static float g_tabIndicatorY = 0.f;
static bool g_keybindsEnabled = false;
static bool g_keybindsWindowOpen = false;
static float g_menuAnim = 0.f;
static float g_menuAnimSpeed = 12.f;

struct SkinOverride{
    int weaponId = 0;
    int paintKit = 0;
    float wear = 0.01f;
    int seed = 0;
    int statTrak = 0;
};
static bool g_skinEnabled = false;
static bool g_skinActiveOnly = false;
static bool g_skinForceUpdate = false;
static int g_skinSelectedWeapon = 0;
static int g_skinPaintKit = 0;
static float g_skinWear = 0.01f;
static int g_skinSeed = 0;
static int g_skinStatTrak = 0;
static std::vector<SkinOverride> g_skinOverrides;
using RegenerateWeaponSkinsFn = void(*)();
static RegenerateWeaponSkinsFn g_regenSkins = nullptr;
static bool g_regenSkinsReady = false;

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
struct SkeetUiMetrics {
    float childRounding = 6.0f;
    float rowHeight = 28.0f;
    float sectionHeight = 22.0f;
    float tabHeight = 56.0f;
    float sliderTrackH = 2.5f;
};
static SkeetUiMetrics g_skeetUi;

static inline float Clampf(float v, float lo, float hi);
template<typename T> static inline T Rd(uintptr_t addr);
static void DrawFilledEllipse(ImDrawList* dl, const ImVec2& center, float rx, float ry, ImU32 col, int segments);
static void DrawRotatedQuad(ImDrawList* dl, ImVec2 center, float w, float h, float angle, ImU32 col);

static void UpdatePidoraisePalette(float fade = 1.f) {
    float opacity = Clampf(g_menuOpacity, 0.f, 1.f) * Clampf(fade, 0.f, 1.f);
    int bgA  = (int)(opacity * 252.f);
    int elemA= (int)(opacity * 245.f);
    int textA= (int)(Clampf(fade, 0.f, 1.f) * 255.f);
    int accA = textA;
    g_pido.accent     = IM_COL32(215, 215, 225, accA);
    g_pido.bgFill     = IM_COL32(8, 9, 12, bgA);
    g_pido.bgStroke   = IM_COL32(215, 215, 225, (int)(120.f*fade));
    g_pido.tabBg      = IM_COL32(6, 7, 10, bgA);
    g_pido.tabActive  = IM_COL32(16, 16, 22, (int)(250.f*fade));
    g_pido.elemBg     = IM_COL32(11, 12, 16, elemA);
    g_pido.elemStroke = IM_COL32(45, 46, 60, (int)(180.f*fade));
    g_pido.text       = IM_COL32(200, 200, 210, textA);
    g_pido.textDim    = IM_COL32(90, 92, 108, textA);
    g_pido.textActive = IM_COL32(230, 230, 240, textA);
}

static char g_configName[64] = "default";
static int g_configSelected = -1;
static std::vector<std::string> g_configList;

static int g_lastHealth[ESP_MAX_PLAYERS + 1] = {};
static bool g_seenThisFrame[ESP_MAX_PLAYERS + 1] = {};

struct LogEntry{char text[256];ImU32 color;float lifetime,maxlife;int type;};  // 0 попадание 1 убийство
static std::deque<LogEntry>g_logs;
struct DamageFloater{ int damage; UINT64 spawnMs; float ax, ay; float duration;
    float wx, wy, wz;   // точка в мире
    float randOffX;
};
static std::deque<DamageFloater> g_damageFloaters;
static DWORD g_lastSoundPingTick[ESP_MAX_PLAYERS + 1] = {};
static bool g_visMap[ESP_MAX_PLAYERS + 1] = {};
static ESPEntry g_esp_stale[ESP_MAX_PLAYERS + 1] = {};
static UINT64 g_esp_stale_tick[ESP_MAX_PLAYERS + 1] = {};
static UINT64 g_visLastTrueTick[ESP_MAX_PLAYERS + 1] = {};
static constexpr DWORD ESP_STALE_MS = 150;

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
static inline ImU32 LerpColor(ImU32 a, ImU32 b, float t){
    t = Clampf(t, 0.f, 1.f);
    int ar = (a >> IM_COL32_R_SHIFT) & 0xFF;
    int ag = (a >> IM_COL32_G_SHIFT) & 0xFF;
    int ab = (a >> IM_COL32_B_SHIFT) & 0xFF;
    int aa = (a >> IM_COL32_A_SHIFT) & 0xFF;
    int br = (b >> IM_COL32_R_SHIFT) & 0xFF;
    int bg = (b >> IM_COL32_G_SHIFT) & 0xFF;
    int bb = (b >> IM_COL32_B_SHIFT) & 0xFF;
    int ba = (b >> IM_COL32_A_SHIFT) & 0xFF;
    int rr = (int)(ar + (br - ar) * t);
    int rg = (int)(ag + (bg - ag) * t);
    int rb = (int)(ab + (bb - ab) * t);
    int ra = (int)(aa + (ba - aa) * t);
    return IM_COL32(rr, rg, rb, ra);
}

static inline uintptr_t ViewAnglesAddr(){
    if(!g_client) return 0;
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
static void DrawCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick);
static void DrawOutlineBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick);
static void DrawOutlineCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick);
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
        case 1: {
            col[0] = Clampf(col[0]*1.25f + 0.08f, 0.f, 1.f);
            col[1] = Clampf(col[1]*1.25f + 0.08f, 0.f, 1.f);
            col[2] = Clampf(col[2]*1.25f + 0.08f, 0.f, 1.f);
            col[3] = Clampf(col[3]*1.1f, 0.f, 1.f);
        } break;
        case 2: {
            col[0] = LerpF(col[0], 0.85f, 0.2f);
            col[1] = LerpF(col[1], 0.85f, 0.2f);
            col[2] = LerpF(col[2], 0.85f, 0.2f);
            col[3] = Clampf(col[3]*0.9f, 0.f, 1.f);
        } break;
        case 3: {
            col[0] = Clampf(col[0]*1.1f, 0.f, 1.f);
            col[1] = Clampf(col[1]*1.1f, 0.f, 1.f);
            col[2] = Clampf(col[2]*1.1f, 0.f, 1.f);
            col[3] = Clampf(col[3]*1.25f + 0.15f, 0.f, 1.f);
        } break;
        case 4: {
            float pulse = 0.75f + 0.25f * sinf((float)ImGui::GetTime() * 3.0f);
            col[0] = Clampf(col[0]*pulse + 0.08f, 0.f, 1.f);
            col[1] = Clampf(col[1]*pulse + 0.08f, 0.f, 1.f);
            col[2] = Clampf(col[2]*pulse + 0.08f, 0.f, 1.f);
            col[3] = Clampf(col[3]*1.15f, 0.f, 1.f);
        } break;
        case 5: {
            float lum = (col[0] + col[1] + col[2]) / 3.f;
            col[0] = LerpF(col[0], lum, 0.45f);
            col[1] = LerpF(col[1], lum, 0.45f);
            col[2] = LerpF(col[2], lum, 0.45f);
            col[3] = Clampf(col[3]*0.95f, 0.f, 1.f);
        } break;
        default: break;
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

static inline bool CharEqI(char a, char b){
    unsigned char ua = (unsigned char)a, ub = (unsigned char)b;
    if(ua >= 'A' && ua <= 'Z') ua = (unsigned char)(ua - 'A' + 'a');
    if(ub >= 'A' && ub <= 'Z') ub = (unsigned char)(ub - 'A' + 'a');
    return ua == ub;
}
static bool StrContainsI(const char* hay, const char* needle){
    if(!hay || !needle) return false;
    for(; *hay; ++hay){
        const char* h = hay;
        const char* n = needle;
        while(*h && *n && CharEqI(*h, *n)){ ++h; ++n; }
        if(!*n) return true;
    }
    return false;
}

static const char* SafeMaterialName(uintptr_t mat){
    if(!IsLikelyPtr(mat)) return nullptr;
    __try{
        void** vtbl = *reinterpret_cast<void***>(mat);
        if(!IsLikelyPtr((uintptr_t)vtbl)) return nullptr;
        using Fn = const char*(__fastcall*)(void*);
        Fn fn = reinterpret_cast<Fn>(vtbl[0]);
        return fn ? fn(reinterpret_cast<void*>(mat)) : nullptr;
    }__except(EXCEPTION_EXECUTE_HANDLER){
        return nullptr;
    }
}

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

struct BoneData{
    Vec3 pos;
    float scale;
    char _pad[16];
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
    if(!bones) return false;
    uintptr_t boneAddr = bones + (uintptr_t)bone * BONE_STRIDE;
    BoneData bd = Rd<BoneData>(boneAddr);
    out = bd.pos;
    return true;
}

static void EnsureCalcWorldSpaceBones(){
    if(g_calcWorldSpaceBones) return;
    UINT64 now = GetTickCount64();
    if(now - g_lastBoneResolve < 2000) return;
    g_lastBoneResolve = now;
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    static const char PAT_BONES[] = "\x40\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x81\xEC\xD0";
    static const char MSK_BONES[] = "xxxxxxxxxxxxxxxx";
    void* fn = PatternScan(client, PAT_BONES, MSK_BONES);
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

static void WCharToUtf8(ImWchar c, char out[5]){
    if(!out){return;}
    if(c < 0x80){
        out[0] = (char)c; out[1] = 0;
    }else if(c < 0x800){
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        out[2] = 0;
    }else{
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        out[3] = 0;
    }
}

static ImFont* GetEspFont(){
    if(font::esp_mono) return font::esp_mono;
    if(font::regular) return font::regular;
    return ImGui::GetFont();
}

static void EnsureModules(){
    if(!g_client)g_client=(uintptr_t)GetModuleHandleA("client.dll");
    if(!g_engine2)g_engine2=(uintptr_t)GetModuleHandleA("engine2.dll");
}

static float GetCurTime(){
    if(!g_client) return 0.f;
    uintptr_t gv = Rd<uintptr_t>(g_client + offsets::client::dwGlobalVars);
    if(!gv) return 0.f;
    return Rd<float>(gv + 0x30);
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
    ImWchar iconChar;  // 0 если нет иконки
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

static bool PlayerHasWeaponId(uintptr_t pawn, uintptr_t entityList, int weaponId){
    if(!pawn||!entityList) return false;
    uintptr_t ws = Rd<uintptr_t>(pawn + offsets::base_pawn::m_pWeaponServices);
    if(!ws) return false;
    uintptr_t vecBase = ws + offsets::weapon_services::m_hMyWeapons;
    int count = Rd<int>(vecBase); if(count<=0||count>16) return false;
    uintptr_t data = Rd<uintptr_t>(vecBase + 8);
    if(!data) return false;
    for(int i=0;i<count;i++){
        uint32_t h = Rd<uint32_t>(data + (size_t)i*4);
        uintptr_t w = ResolveHandle(entityList, h);
        if(w && GetWeaponId(w)==weaponId) return true;
    }
    return false;
}

static SkinOverride* FindSkinOverride(int weaponId){
    for(auto& o : g_skinOverrides){
        if(o.weaponId == weaponId) return &o;
    }
    return nullptr;
}

static void SetSkinOverride(int weaponId, int paintKit, float wear, int seed, int statTrak){
    if(weaponId <= 0) return;
    SkinOverride* o = FindSkinOverride(weaponId);
    if(!o){
        g_skinOverrides.push_back({});
        o = &g_skinOverrides.back();
    }
    o->weaponId = weaponId;
    o->paintKit = paintKit;
    o->wear = Clampf(wear, 0.0001f, 1.f);
    o->seed = seed;
    o->statTrak = statTrak;
}

static void RemoveSkinOverride(int weaponId){
    g_skinOverrides.erase(
        std::remove_if(g_skinOverrides.begin(), g_skinOverrides.end(),
            [&](const SkinOverride& o){ return o.weaponId == weaponId; }),
        g_skinOverrides.end());
}

static void CollectWeapons(uintptr_t pawn, uintptr_t entityList, std::vector<uintptr_t>& out){
    out.clear();
    if(!pawn || !entityList) return;
    uintptr_t ws = Rd<uintptr_t>(pawn + offsets::base_pawn::m_pWeaponServices);
    if(!ws) return;
    uintptr_t vecBase = ws + offsets::weapon_services::m_hMyWeapons;
    int count = Rd<int>(vecBase);
    if(count <= 0 || count > 64) return;
    uintptr_t data = Rd<uintptr_t>(vecBase + 8);
    if(!data) return;
    out.reserve(count);
    for(int i = 0; i < count; i++){
        uint32_t h = Rd<uint32_t>(data + (size_t)i * 4);
        uintptr_t w = ResolveHandle(entityList, h);
        if(w) out.push_back(w);
    }
}

static void ApplySkinToWeapon(uintptr_t weapon, const SkinOverride& o){
    if(!weapon) return;
    uintptr_t attr = weapon + offsets::econ_entity::m_AttributeManager;
    uintptr_t item = attr + offsets::attribute_container::m_Item;
    if(o.paintKit <= 0){
        Wr<int>(weapon + offsets::econ_entity::m_nFallbackPaintKit, -1);
        Wr<int>(weapon + offsets::econ_entity::m_nFallbackSeed, 0);
        Wr<float>(weapon + offsets::econ_entity::m_flFallbackWear, 0.01f);
        Wr<int>(weapon + offsets::econ_entity::m_nFallbackStatTrak, 0);
        return;
    }
    Wr<uint32_t>(item + offsets::econ_item_view::m_iItemIDHigh, 0xFFFFFFFF);
    Wr<uint32_t>(item + offsets::econ_item_view::m_iItemIDLow, 0xFFFFFFFF);
    Wr<int>(weapon + offsets::econ_entity::m_nFallbackPaintKit, o.paintKit);
    Wr<int>(weapon + offsets::econ_entity::m_nFallbackSeed, o.seed);
    Wr<float>(weapon + offsets::econ_entity::m_flFallbackWear, Clampf(o.wear, 0.0001f, 1.f));
    Wr<int>(weapon + offsets::econ_entity::m_nFallbackStatTrak, o.statTrak);
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

static bool ParseSkinOverride(const std::string& v, SkinOverride& out){
    std::stringstream ss(v);
    char c=0;
    if(!(ss>>out.weaponId)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.paintKit)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.wear)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.seed)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.statTrak)) return false;
    return true;
}

static bool LoadConfigKeyEsp(const std::string& key, const std::string& val, bool& ok){
    if(key=="esp_enabled"){ g_espEnabled=ParseBool(val); return true; }
    if(key=="esp_draw_box"){ g_espDrawBox=ParseBool(val); return true; }
    if(key=="esp_only_vis"){ g_espOnlyVis=ParseBool(val); return true; }
    if(key=="esp_box_style"){ int v; if(ParseInt(val,v)) g_espBoxStyle=v; else ok=false; return true; }
    if(key=="esp_box_thick"){ float v; if(ParseFloat(val,v)) g_espBoxThick=v; else ok=false; return true; }
    if(key=="esp_enemy_col"){ if(!ParseColor4(val,g_espEnemyCol)) ok=false; return true; }
    if(key=="esp_team_col"){ if(!ParseColor4(val,g_espTeamCol)) ok=false; return true; }
    if(key=="esp_team"){ g_espShowTeam=ParseBool(val); return true; }
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
    if(key=="fov_circle_col"){ if(!ParseColor4(val,g_fovCircleCol)) ok=false; return true; }
    if(key=="aimbot_team"){ g_aimbotTeamChk=ParseBool(val); return true; }
    if(key=="aimbot_vis"){ g_aimbotVisCheck=ParseBool(val); return true; }
    if(key=="wait_aim_fire"){ g_waitAimThenFire=ParseBool(val); return true; }
    if(key=="wait_aim_deg"){ float v; if(ParseFloat(val,v)) g_waitAimFovDeg=v; else ok=false; return true; }
    if(key=="autostop"){ g_autostopEnabled=ParseBool(val); return true; }
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
    if(key=="night_mode_overlay"){ g_nightModeOverlay=ParseBool(val); return true; }
    if(key=="fov_enabled"){ g_fovEnabled=ParseBool(val); return true; }
    if(key=="fov_value"){ float v; if(ParseFloat(val,v)) g_fovValue=v; else ok=false; return true; }
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
    if(key=="kill_effect"){ g_killEffectEnabled=ParseBool(val); return true; }
    if(key=="particles_world"){ g_particlesWorld=ParseBool(val); return true; }
    if(key=="particles_world_radius"){ float v; if(ParseFloat(val,v)) g_particlesWorldRadius=v; else ok=false; return true; }
    if(key=="particles_world_height"){ float v; if(ParseFloat(val,v)) g_particlesWorldHeight=v; else ok=false; return true; }
    if(key=="particles_world_floor"){ float v; if(ParseFloat(val,v)) g_particlesWorldFloor=v; else ok=false; return true; }
    if(key=="particles_wind"){ float v; if(ParseFloat(val,v)) g_particlesWind=v; else ok=false; return true; }
    if(key=="particles_depth_fade"){ float v; if(ParseFloat(val,v)) g_particlesDepthFade=v; else ok=false; return true; }
    if(key=="sky_color_enabled"){ g_skyColorEnabled=ParseBool(val); return true; }
    if(key=="sky_color"){ if(!ParseColor4(val,g_skyColor)) ok=false; return true; }
    if(key=="world_color_enabled"){ g_worldColorEnabled=ParseBool(val); return true; }
    if(key=="world_color"){ if(!ParseColor4(val,g_worldColor)) ok=false; return true; }
    if(key=="damage_floaters"){ g_damageFloatersEnabled=ParseBool(val); return true; }
    if(key=="damage_floater_duration"){ float v; if(ParseFloat(val,v)) g_damageFloaterDuration=v; else ok=false; return true; }
    if(key=="damage_floater_scale"){ float v; if(ParseFloat(val,v)) g_damageFloaterScale=v; else ok=false; return true; }
    if(key=="damage_floater_anchor"){ int v; if(ParseInt(val,v)) g_damageFloaterAnchor=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeySkins(const std::string& key, const std::string& val, bool& ok){
    if(key=="skin_enabled"){ g_skinEnabled=ParseBool(val); return true; }
    if(key=="skin_active_only"){ g_skinActiveOnly=ParseBool(val); return true; }
    if(key=="skin_clear"){ g_skinOverrides.clear(); return true; }
    if(key.rfind("skin_override_", 0) == 0){
        SkinOverride o{};
        if(ParseSkinOverride(val, o)){
            SetSkinOverride(o.weaponId, o.paintKit, o.wear, o.seed, o.statTrak);
        }else{
            ok = false;
        }
        return true;
    }
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
    if(key=="accent"){ if(!ParseColor4(val,g_accentColor)) ok=false; return true; }
    if(key=="menu_opacity"){ float v; if(ParseFloat(val,v)) g_menuOpacity=v; else ok=false; return true; }
    if(key=="ui_scale"){ float v; if(ParseFloat(val,v)) g_uiScale=v; else ok=false; return true; }
    if(key=="menu_anim_speed"){ float v; if(ParseFloat(val,v)) g_menuAnimSpeed=v; else ok=false; return true; }
    return false;
}

static void ApplyDefaults(){
    g_espEnabled = true;
    g_espDrawBox = true;
    g_espOnlyVis = false;
    g_espBoxStyle = 0;
    g_espBoxThick = 1.5f;
    g_espEnemyCol[0]=1.f; g_espEnemyCol[1]=0.25f; g_espEnemyCol[2]=0.25f; g_espEnemyCol[3]=1.f;
    g_espTeamCol[0]=0.25f; g_espTeamCol[1]=0.55f; g_espTeamCol[2]=1.f; g_espTeamCol[3]=1.f;
    g_espShowTeam = true;
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
    g_espMoneyPos = 0;
    g_espHeadForward = 6.f;
    g_noFlash = false;
    g_noSmoke = false;
    g_noCrosshair = false;
    g_noLegs = false;
    g_glowEnabled = false;
    g_glowEnemyCol[0]=1.f; g_glowEnemyCol[1]=0.18f; g_glowEnemyCol[2]=0.18f; g_glowEnemyCol[3]=1.f;
    g_glowTeamCol[0]=0.f; g_glowTeamCol[1]=0.f; g_glowTeamCol[2]=0.f; g_glowTeamCol[3]=1.f;
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
    g_fovCircleEnabled = false;
    g_fovCircleCol[0]=0.4f; g_fovCircleCol[1]=0.7f; g_fovCircleCol[2]=1.f; g_fovCircleCol[3]=0.5f;
    g_aimbotTeamChk = true;
    g_aimbotVisCheck = true;
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
    g_nightModeOverlay = false;
    g_fovEnabled = false;
    g_fovValue = 90.f;
    g_snowEnabled = false;
    g_snowDensity = 1;
    g_sakuraEnabled = false;
    g_sakuraCol[0]=1.f; g_sakuraCol[1]=0.55f; g_sakuraCol[2]=0.7f; g_sakuraCol[3]=0.85f;
    g_starsEnabled = false;
    g_killEffectEnabled = false;
    g_particlesWorld = true;
    g_particlesWorldRadius = 600.f;
    g_particlesWorldHeight = 320.f;
    g_particlesWorldFloor = 40.f;
    g_particlesWind = 18.f;
    g_particlesDepthFade = 0.0022f;
    g_skyColorEnabled = false;
    g_skyColor[0]=0.4f; g_skyColor[1]=0.5f; g_skyColor[2]=0.8f; g_skyColor[3]=1.f;
    g_worldColorEnabled = false;
    g_worldColor[0]=0.92f; g_worldColor[1]=0.94f; g_worldColor[2]=1.f; g_worldColor[3]=1.f;
    g_damageFloatersEnabled = true;
    g_damageFloaterDuration = 0.85f;
    g_damageFloaterScale = 1.f;
    g_damageFloaterAnchor = 0;
    g_handsColorEnabled = false;
    g_handsColor[0]=0.9f; g_handsColor[1]=0.9f; g_handsColor[2]=0.95f; g_handsColor[3]=1.f;
    g_watermarkEnabled = true;
    g_showFpsWatermark = true;
    g_spectatorListEnabled = true;
    g_keybindsEnabled = true;
    g_hitNotifEnabled = true;
    g_killNotifEnabled = true;
    g_waitAimThenFire = true;
    g_waitAimFovDeg = 2.5f;
    g_autostopEnabled = true;
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
    g_accentColor[0]=0.1f; g_accentColor[1]=0.55f; g_accentColor[2]=1.0f; g_accentColor[3]=1.0f;
    g_menuOpacity = 1.0f;
    g_uiScale = 1.10f;
    g_menuAnimSpeed = 12.f;
    g_skinEnabled = false;
    g_skinActiveOnly = false;
    g_skinForceUpdate = false;
    g_skinSelectedWeapon = 0;
    g_skinPaintKit = 0;
    g_skinWear = 0.01f;
    g_skinSeed = 0;
    g_skinStatTrak = 0;
    g_skinOverrides.clear();
}

static bool SaveConfig(const char* name){
    EnsureConfigDir();
    std::ofstream out(ConfigPath(name), std::ios::trunc);
    if(!out.is_open()) return false;
    WriteBool(out, "esp_enabled", g_espEnabled);
    WriteBool(out, "esp_draw_box", g_espDrawBox);
    WriteBool(out, "esp_only_vis", g_espOnlyVis);
    WriteInt(out, "esp_box_style", g_espBoxStyle);
    WriteFloat(out, "esp_box_thick", g_espBoxThick);
    WriteColor(out, "esp_enemy_col", g_espEnemyCol);
    WriteColor(out, "esp_team_col", g_espTeamCol);
    WriteBool(out, "esp_team", g_espShowTeam);
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
    WriteBool(out, "aimbot_vis", g_aimbotVisCheck);

    WriteBool(out, "wait_aim_fire", g_waitAimThenFire);
    WriteFloat(out, "wait_aim_deg", g_waitAimFovDeg);
    WriteBool(out, "autostop", g_autostopEnabled);
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
    WriteBool(out, "night_mode_overlay", g_nightModeOverlay);
    WriteBool(out, "fov_enabled", g_fovEnabled);
    WriteFloat(out, "fov_value", g_fovValue);
    WriteBool(out, "fov_circle", g_fovCircleEnabled);
    WriteColor(out, "fov_circle_col", g_fovCircleCol);
    WriteBool(out, "hands_color_enabled", g_handsColorEnabled);
    WriteColor(out, "hands_color", g_handsColor);
    WriteBool(out, "snow", g_snowEnabled);
    WriteInt(out, "snow_density", g_snowDensity);
    WriteBool(out, "sakura", g_sakuraEnabled);
    WriteColor(out, "sakura_col", g_sakuraCol);
    WriteBool(out, "stars", g_starsEnabled);
    WriteBool(out, "kill_effect", g_killEffectEnabled);
    WriteBool(out, "particles_world", g_particlesWorld);
    WriteFloat(out, "particles_world_radius", g_particlesWorldRadius);
    WriteFloat(out, "particles_world_height", g_particlesWorldHeight);
    WriteFloat(out, "particles_world_floor", g_particlesWorldFloor);
    WriteFloat(out, "particles_wind", g_particlesWind);
    WriteFloat(out, "particles_depth_fade", g_particlesDepthFade);
    WriteBool(out, "sky_color_enabled", g_skyColorEnabled);
    WriteColor(out, "sky_color", g_skyColor);
    WriteBool(out, "world_color_enabled", g_worldColorEnabled);
    WriteColor(out, "world_color", g_worldColor);
    WriteBool(out, "damage_floaters", g_damageFloatersEnabled);
    WriteFloat(out, "damage_floater_duration", g_damageFloaterDuration);
    WriteFloat(out, "damage_floater_scale", g_damageFloaterScale);
    WriteInt(out, "damage_floater_anchor", g_damageFloaterAnchor);
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
    WriteColor(out, "accent", g_accentColor);
    WriteFloat(out, "menu_opacity", g_menuOpacity);
    WriteFloat(out, "ui_scale", g_uiScale);
    WriteFloat(out, "menu_anim_speed", g_menuAnimSpeed);
    WriteBool(out, "skin_enabled", g_skinEnabled);
    WriteBool(out, "skin_active_only", g_skinActiveOnly);
    out << "skin_clear=1\n";
    for(size_t i = 0; i < g_skinOverrides.size(); ++i){
        const SkinOverride& o = g_skinOverrides[i];
        out << "skin_override_" << i << "="
            << o.weaponId << "," << o.paintKit << "," << o.wear << "," << o.seed << "," << o.statTrak << "\n";
    }
    return true;
}

static bool LoadConfig(const char* name){
    std::ifstream in(ConfigPath(name));
    if(!in.is_open()) return false;
    std::string line;
    bool ok = true;
    bool rcsXSet = false;
    bool rcsYSet = false;
    g_skinOverrides.clear();
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
        if(LoadConfigKeySkins(key, val, ok)) continue;
        LoadConfigKeyMisc(key, val, ok);
    }
    g_menuOpacity = Clampf(g_menuOpacity, 0.3f, 1.0f);
    g_uiScale = Clampf(g_uiScale, 0.85f, 1.6f);
    g_menuAnimSpeed = Clampf(g_menuAnimSpeed, 2.f, 30.f);
    g_glowAlpha = Clampf(g_glowAlpha, 0.f, 1.f);
    g_skeletonThick = Clampf(g_skeletonThick, 0.5f, 3.5f);
    g_espOofSize = Clampf(g_espOofSize, 8.f, 64.f);
    g_particlesWorldRadius = Clampf(g_particlesWorldRadius, 200.f, 2000.f);
    g_particlesWorldHeight = Clampf(g_particlesWorldHeight, 100.f, 1200.f);
    g_particlesWorldFloor = Clampf(g_particlesWorldFloor, -200.f, 400.f);
    g_particlesWind = Clampf(g_particlesWind, 0.f, 60.f);
    g_particlesDepthFade = Clampf(g_particlesDepthFade, 0.0005f, 0.01f);
    g_waitAimFovDeg = Clampf(g_waitAimFovDeg, 0.4f, 15.f);
    g_damageFloaterDuration = Clampf(g_damageFloaterDuration, 0.25f, 2.5f);
    g_damageFloaterScale = Clampf(g_damageFloaterScale, 0.4f, 2.5f);
    g_damageFloaterAnchor &= 1;
    g_soundPuddleScale = Clampf(g_soundPuddleScale, 0.3f, 3.0f);
    g_soundPuddleAlpha = Clampf(g_soundPuddleAlpha, 0.f, 2.0f);
    return ok;
}

static bool GetOofArrowPos(const float* vm, const Vec3& head, int sw, int sh, float& ox, float& oy);

static void BuildESPData(){
    g_esp_count=0;g_esp_oof_count=0;g_esp_local_team=0;g_esp_local_pawn=0;
    g_localOrigin = {};
    static int s_visFrame = 0;
    if((s_visFrame % 3) == 0) std::fill(std::begin(g_visMap), std::end(g_visMap), false);
    s_visFrame++;

    EnsureModules();if(!g_client)return;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
    if(!entityList)return;
    const float*vm=reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix);
    int sw=1920, sh=1080;
    if(g_bbWidth >= 100 && g_bbHeight >= 100){
        sw = (int)g_bbWidth;
        sh = (int)g_bbHeight;
    }else if(g_engine2){
        int w = Rd<int>(g_engine2+offsets::engine2::dwWindowWidth);
        int h = Rd<int>(g_engine2+offsets::engine2::dwWindowHeight);
        if(w > 100 && h > 100){ sw = w; sh = h; }
    }
    uintptr_t localPawn=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
    (void)Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController);
    int localTeam=0;Vec3 localOrigin{};
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
        if(g_visCheckEnabled && i > 0 && i <= ESP_MAX_PLAYERS){
            if((s_visFrame % 3) == 0){
                vis=Rd<bool>(pawn+offsets::spotted::m_entitySpottedState+offsets::spotted::m_bSpotted);
                g_visMap[i] = vis;
            }else{
                vis = g_visMap[i];
            }
        }
        if(i > 0 && i <= ESP_MAX_PLAYERS && vis) g_visLastTrueTick[i] = GetTickCount64();
        UINT64 now = GetTickCount64();
        bool effVis = vis || (i > 0 && i <= ESP_MAX_PLAYERS && (now - g_visLastTrueTick[i]) < ESP_STALE_MS);
        if(g_espOnlyVis && !effVis) continue;
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
                    float angle=atan2f(dx, -dy);
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
        float boxH = fy - hy;
        float top = hy - boxH * 0.18f;
        boxH = fy - top;
        float boxW = boxH * 0.52f;
        float cx = (hx + fx) * 0.5f;
        float dist = (origin - localOrigin).length() / 100.f;
        uintptr_t namePtr=Rd<uintptr_t>(ctrl+offsets::controller::m_sSanitizedPlayerName);
        float flashDur=Rd<float>(pawn+offsets::cs_pawn_base::m_flFlashDuration);
        ESPEntry&e=g_esp_players[g_esp_count++];
        e.valid=true;e.visible=effVis;e.flashed=(flashDur>0.1f);
        e.planting=Rd<bool>(pawn+offsets::cs_pawn::m_bIsPlantingViaUse);
        e.scoped=Rd<bool>(pawn+offsets::cs_pawn::m_bIsScoped);
        e.box_l = cx - boxW * 0.5f;
        e.box_t = top;
        e.box_r = cx + boxW * 0.5f;
        e.box_b = fy;
        e.defusing=(g_bombDefusing&&g_bombDefuserPawn==pawn);
        e.hasBomb=entityList?PlayerHasWeaponId(pawn,entityList,88):false;
        e.hasKits=Rd<bool>(ctrl+offsets::controller::m_bPawnHasDefuser);
        e.spotted=effVis;
        e.ent_index=i;
        e.pawn=pawn;
        e.controller=ctrl;
        e.head_x=hx;e.head_y=hy;
        e.chest_x=(hx+fx)*0.5f;e.chest_y=(hy+fy)*0.5f;
        e.head_fx=hfx;e.head_fy=hfy;
        e.head_ox=head.x;e.head_oy=head.y;e.head_oz=head.z;
        e.origin_x=origin.x;e.origin_y=origin.y;e.origin_z=origin.z;
        e.feet_x=fx;e.feet_y=fy;
        e.health=health;e.team=team;e.distance=dist;e.yaw=0.f;
        RdName(namePtr,e.name,sizeof(e.name));
        g_esp_stale[i] = e;
        g_esp_stale_tick[i] = GetTickCount64();
    }
}

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

    if(localLife == 0){
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
    }else{
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

static void DrawOverlayWatermarkChrome(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float round){
    const ImU32 colBg = IM_COL32(16,16,16,245);
    const ImU32 colBorder = IM_COL32(64,64,64,220);
    const ImU32 colInner = IM_COL32(8,8,8,210);
    dl->AddRectFilled(p0, p1, colBg, round);
    dl->AddRect(p0, p1, colBorder, round, 0, 1.f);
    dl->AddRect({p0.x+1.f,p0.y+1.f},{p1.x-1.f,p1.y-1.f}, colInner, round, 0, 1.f);
    float stripH = (std::min)(3.f, (p1.y - p0.y) * 0.22f);
    if(stripH < 1.2f) return;
    float gx0 = p0.x + 2.f;
    float gy0 = p0.y + 1.f;
    float gy1 = p0.y + stripH;
    float gq1 = p0.x + (p1.x - p0.x) * 0.33f;
    float gq2 = p0.x + (p1.x - p0.x) * 0.66f;
    float gx1 = p1.x - 2.f;
    if(gy1 > p0.y + round * 0.45f) gy1 = p0.y + round * 0.45f;
    if(gy1 <= gy0) return;
    dl->AddRectFilledMultiColor({gx0,gy0},{gq1,gy1},
        IM_COL32(108,132,188,220), IM_COL32(174,122,190,220), IM_COL32(174,122,190,220), IM_COL32(108,132,188,220));
    dl->AddRectFilledMultiColor({gq1,gy0},{gq2,gy1},
        IM_COL32(174,122,190,220), IM_COL32(194,166,118,220), IM_COL32(194,166,118,220), IM_COL32(174,122,190,220));
    dl->AddRectFilledMultiColor({gq2,gy0},{gx1,gy1},
        IM_COL32(194,166,118,220), IM_COL32(116,168,148,220), IM_COL32(116,168,148,220), IM_COL32(194,166,118,220));
}

static void DrawSpectatorList(float sw){
    if(!g_spectatorListEnabled) return;
    if(g_spectatorCount == 0 && !g_weAreSpectating) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList(); if(!dl) return;
    ImFont* fBold = font::bold    ? font::bold    : ImGui::GetFont();
    ImFont* fReg  = font::regular ? font::regular : ImGui::GetFont();

    const float margin  = 15.f;
    const float padX    = 12.f;
    const float padY    = 10.f;
    const float rnd     = 8.f;
    const float lineH   = 22.f;
    const float headerH = 30.f;

    float yBase = margin;
    if(g_watermarkEnabled){
        yBase += 34.f + 8.f;
    }

    const ImU32 colAccent  = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    const ImU32 colAccentD = IM_COL32((int)(g_accentColor[0]*160),(int)(g_accentColor[1]*160),(int)(g_accentColor[2]*160),255);
    const ImU32 colText    = IM_COL32(215, 220, 230, 255);
    const ImU32 colDim     = IM_COL32(100, 105, 118, 255);
    const ImU32 colSep     = IM_COL32(32, 34, 42, 255);
    const ImU32 colRowHov  = IM_COL32(255, 255, 255, 8);

    if(g_weAreSpectating && g_spectatingTarget[0]){
        ImVec2 szTarget = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, g_spectatingTarget);
        const char* prefix = "WATCHING";
        ImVec2 szPrefix  = fBold->CalcTextSizeA(fBold->LegacySize, FLT_MAX, 0.f, prefix);
        float pillW = padX + szPrefix.x + 8.f + szTarget.x + padX;
        pillW = (std::max)(pillW, 140.f);
        float pillH = headerH;
        float px = sw - pillW - margin;
        float py = yBase;
        DrawOverlayWatermarkChrome(dl, {px, py}, {px+pillW, py+pillH}, rnd);
        dl->AddRectFilled({px, py+4.f}, {px+2.f, py+pillH-4.f}, colAccent, 2.f);
        float midY = py + pillH * 0.5f;
        dl->AddText(fBold, fBold->LegacySize, {px+padX, midY - szPrefix.y*0.5f}, colAccent, prefix);
        dl->AddText(fReg,  fReg->LegacySize,  {px+padX+szPrefix.x+8.f, midY - szTarget.y*0.5f}, colText, g_spectatingTarget);
        return;
    }

    if(g_spectatorCount <= 0) return;

    float maxNameW = 60.f;
    for(int i = 0; i < g_spectatorCount; i++){
        ImVec2 ts = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, g_spectatorNames[i]);
        if(ts.x > maxNameW) maxNameW = ts.x;
    }

    const char* hdrLabel = "SPECTATORS";
    ImVec2 szHdr = fBold->CalcTextSizeA(fBold->LegacySize, FLT_MAX, 0.f, hdrLabel);
    char cntBuf[8]; std::snprintf(cntBuf, sizeof(cntBuf), "%d", g_spectatorCount);
    ImVec2 szCnt = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, cntBuf);
    float badgeW = szCnt.x + 10.f;
    float badgeH = 16.f;

    float boxW = (std::max)(maxNameW + padX*2.f, szHdr.x + padX*2.f + badgeW + 6.f + padX);
    boxW = (std::max)(boxW, 130.f);
    float boxH = headerH + 1.f + (float)g_spectatorCount * lineH + padY;

    float x = sw - boxW - margin;
    float y = yBase;

    dl->AddRectFilled({x-3.f, y-3.f}, {x+boxW+3.f, y+boxH+3.f}, IM_COL32(0,0,0,55), rnd+2.f);
    DrawOverlayWatermarkChrome(dl, {x, y}, {x+boxW, y+boxH}, rnd);
    dl->AddRectFilled({x+1.f, y+1.f}, {x+boxW-1.f, y+headerH}, IM_COL32(10,10,14,140), rnd, ImDrawFlags_RoundCornersTop);
    dl->AddLine({x+2.f, y+headerH}, {x+boxW-2.f, y+headerH}, colSep, 1.f);
    dl->AddRectFilled({x, y+4.f}, {x+2.f, y+boxH-4.f}, colAccent, 2.f);

    float hMid = y + headerH * 0.5f;
    dl->AddText(fBold, fBold->LegacySize, {x+padX, hMid - szHdr.y*0.5f}, colAccent, hdrLabel);

    float badgeX = x + boxW - padX - badgeW;
    float badgeY = hMid - badgeH * 0.5f;
    dl->AddRectFilled({badgeX, badgeY}, {badgeX+badgeW, badgeY+badgeH}, colAccentD, badgeH*0.5f);
    dl->AddText(fReg, fReg->LegacySize, {badgeX + (badgeW-szCnt.x)*0.5f, badgeY + (badgeH-szCnt.y)*0.5f}, IM_COL32(255,255,255,230), cntBuf);

    for(int i = 0; i < g_spectatorCount; i++){
        float ey = y + headerH + 1.f + (float)i * lineH;
        float eMid = ey + lineH * 0.5f;

        if(i % 2 == 0)
            dl->AddRectFilled({x+1.f, ey}, {x+boxW-1.f, ey+lineH}, colRowHov);

        char idxBuf[4]; std::snprintf(idxBuf, sizeof(idxBuf), "%d", i+1);
        ImVec2 szIdx = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, idxBuf);
        dl->AddText(fReg, fReg->LegacySize, {x+padX, eMid - szIdx.y*0.5f}, colDim, idxBuf);

        dl->AddText(fReg, fReg->LegacySize, {x+padX+18.f, eMid - szIdx.y*0.5f}, colText, g_spectatorNames[i]);
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
            int dmg = prev - e.health;
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Hit %s for %d", e.name[0]?e.name:"Enemy", dmg);
            if(g_hitNotifEnabled) PushNotification(buf, IM_COL32(240,180,60,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(240,180,60,255); le.maxlife=4.f; le.lifetime=4.f; le.type=0;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            PlayHitSound(g_hitSoundType);
            if(g_damageFloatersEnabled){
                DamageFloater df{};
                df.damage = dmg;
                df.spawnMs = GetTickCount64();
                df.ax = e.head_x;
                df.ay = e.head_y;
                df.duration = g_damageFloaterDuration;
                df.wx = e.head_ox;
                df.wy = e.head_oy;
                df.wz = e.head_oz + 8.f;
                UINT64 j = GetTickCount64() ^ (UINT64)(uintptr_t)e.pawn ^ ((UINT64)e.ent_index << 17);
                df.randOffX = ((j & 0xFFF) / 4095.f) * 16.f - 8.f;
                g_damageFloaters.push_back(df);
                while(g_damageFloaters.size() > 32) g_damageFloaters.pop_front();
            }
        }
        if(prev > 0 && e.health <= 0){
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Killed %s", e.name[0]?e.name:"Enemy");
            if(g_killNotifEnabled) PushNotification(buf, IM_COL32(140,100,255,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(140,100,255,255); le.maxlife=4.f; le.lifetime=4.f; le.type=1;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            if(g_killEffectEnabled){
                g_lastKillEffectTime = GetTickCount64();
                g_lastKillEffectPos = {e.head_ox, e.head_oy, e.head_oz};
                g_pendingKillParticles = true;
            }
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
        uintptr_t flashAddr=lp+offsets::cs_pawn_base::m_flFlashDuration;
        if(!IsLikelyPtr((uintptr_t)flashAddr))return;
        float dur=Rd<float>(flashAddr);
        if(dur>0.01f){
            Wr<float>(flashAddr,0.f);
            Wr<float>(lp+offsets::cs_pawn_base::m_flFlashMaxAlpha,0.f);
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunNoSmoke(){
    if(!g_noSmoke||!g_client) return;
    __try{
        uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
        if(lp&&IsLikelyPtr(lp)) Wr<float>(lp+offsets::cs_pawn_base::m_flLastSmokeOverlayAlpha,0.f);
        UINT64 now = GetTickCount64();
        if(now - g_lastNoSmokeTick < 100) return;
        g_lastNoSmokeTick = now;
        uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
        for(int i=0;i<2048;i++){
            uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
            uintptr_t ent=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ent||!IsLikelyPtr(ent))continue;
            __try{
                uint8_t spawned=Rd<uint8_t>(ent+offsets::smoke_projectile::m_bSmokeEffectSpawned);
                if(!spawned)continue;
                uintptr_t alphaProp=Rd<uintptr_t>(ent+offsets::model_entity::m_pClientAlphaProperty);
                if(alphaProp&&IsLikelyPtr(alphaProp)) Wr<uint8_t>(alphaProp+offsets::client_alpha_prop::m_nAlpha,0);
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
            }else if(isTeam){
                float sumRgb = tmp[0] + tmp[1] + tmp[2];
                tmp[3] = (sumRgb < 1e-4f) ? 0.f : Clampf(g_glowAlpha, 0.f, 1.f);
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
        if(team==localTeam)continue;
        uintptr_t spotBase=pawn+offsets::spotted::m_entitySpottedState;
        Wr<uint8_t>(spotBase+offsets::spotted::m_bSpotted, 1);
        Wr<uint32_t>(spotBase+offsets::spotted::m_bSpottedByMask, 0xFFFFFFFF);
        Wr<uint32_t>(spotBase+offsets::spotted::m_bSpottedByMask+4, 0xFFFFFFFF);
    }
}

static void EnsureSkinRegen(){
    if(g_regenSkinsReady) return;
    g_regenSkinsReady = true;
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    static const char PAT_REGEN[] = "\x48\x83\xEC\x00\xE8\x00\x00\x00\x00\x48\x85\xC0\x0F\x84\x00\x00\x00\x00\x48\x8B\x10";
    static const char MSK_REGEN[] = "xxx?x????xxxxx????xxx";
    void* fn = PatternScan(client, PAT_REGEN, MSK_REGEN);
    if(fn) g_regenSkins = reinterpret_cast<RegenerateWeaponSkinsFn>(fn);
}

static void RunSkinChanger(){
    return;
    if(!g_client) return;
    if(!g_skinEnabled){
        if(g_skinForceUpdate){
            g_skinForceUpdate = false;
            EnsureSkinRegen();
            if(g_regenSkins){
                __try{ g_regenSkins(); }__except(EXCEPTION_EXECUTE_HANDLER){}
            }
        }
        return;
    }
    uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
    if(!entityList) return;
    uintptr_t lp = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!lp) return;
    if(g_skinActiveOnly){
        uintptr_t weapon = GetActiveWeapon(lp, entityList);
        if(weapon){
            int wId = GetWeaponId(weapon);
            if(SkinOverride* o = FindSkinOverride(wId)) ApplySkinToWeapon(weapon, *o);
        }
    }else{
        static std::vector<uintptr_t> weapons;
        CollectWeapons(lp, entityList, weapons);
        for(uintptr_t w : weapons){
            int wId = GetWeaponId(w);
            if(SkinOverride* o = FindSkinOverride(wId)) ApplySkinToWeapon(w, *o);
        }
    }
    if(g_skinForceUpdate){
        g_skinForceUpdate = false;
        EnsureSkinRegen();
        if(g_regenSkins){
            __try{ g_regenSkins(); }__except(EXCEPTION_EXECUTE_HANDLER){}
        }
    }
}

static void RunAutostop(){
    if(!g_autostopEnabled||!g_client)return;
    if(g_menuOpen) return;
    bool aimHeld = (GetAsyncKeyState(g_aimbotKey) & 0x8000) != 0;
    bool fireHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if(!aimHeld || !fireHeld) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int shots = Rd<int>(lp + offsets::cs_pawn::m_iShotsFired);
    if(shots > 0) return;
    uintptr_t vaAddr = ViewAnglesAddr();
    if(!vaAddr) return;
    __try{
        Vec3 vel = Rd<Vec3>(lp + offsets::base_entity::m_vecVelocity);
        float yawDeg = Rd<float>(vaAddr + 4);
        float yaw = yawDeg * (3.14159265f / 180.f);
        float cosY = cosf(yaw), sinY = sinf(yaw);
        float forward = vel.x * cosY + vel.y * sinY;
        float side = -vel.x * sinY + vel.y * cosY;
        const float dz = 12.f;
        if(fabsf(forward) < dz && fabsf(side) < dz) return;
        Wr<int>(g_client + offsets::buttons::forward, forward < -dz ? 65537 : 0);
        Wr<int>(g_client + offsets::buttons::back,    forward > dz ? 65537 : 0);
        Wr<int>(g_client + offsets::buttons::left,    side > dz ? 65537 : 0);
        Wr<int>(g_client + offsets::buttons::right,   side < -dz ? 65537 : 0);
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void DrawDebugConsole() {
    if (!g_showDebugConsole) return;
    ImGui::SetNextWindowSize({ 500, 400 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug Console", &g_showDebugConsole)) {
        if (ImGui::Button("Clear")) ClearDebugLogs();
        ImGui::Separator();
        ImGui::BeginChild("LogScroll");
        auto logs = GetDebugLogs();
        for (const auto& log : logs) {
            ImGui::TextUnformatted(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

static void RunBHop(){
    if(!g_bhopEnabled||!g_client)return;
    if(g_menuOpen){ Wr<int>(g_client+offsets::buttons::jump, 0); return; }

    bool spaceHeld = (GetAsyncKeyState(VK_SPACE)&0x8000) != 0;
    if(!spaceHeld){ Wr<int>(g_client+offsets::buttons::jump, 0); return; }

    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn); if(!lp)return;
    bool onGround = (Rd<uint32_t>(lp+offsets::base_entity::m_fFlags) & 1) != 0;

    Wr<int>(g_client+offsets::buttons::jump, onGround ? 65537 : 0);
}




static void RunFOVChanger(){
    if(!g_fovEnabled||!g_client)return;
    if(g_origGetWorldFov) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    bool scoped=Rd<bool>(lp+offsets::cs_pawn::m_bIsScoped);if(scoped)return;
    uintptr_t camSvc=Rd<uintptr_t>(lp+offsets::base_pawn::m_pCameraServices);if(!camSvc)return;
    Wr<float>(camSvc+offsets::camera::m_iFOV,g_fovValue);
}

static void RunRCS(){
    if(!g_rcsEnabled||!g_client)return;
    if(g_menuOpen)return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;

    float punchX=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle);
    float punchY=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle+4);

    bool shooting=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    int shots=Rd<int>(lp+offsets::cs_pawn::m_iShotsFired);

    if(!shooting || shots<1){
        g_rcsPrevPunchX=punchX;
        g_rcsPrevPunchY=punchY;
        return;
    }

    float dx=(punchX-g_rcsPrevPunchX)*g_rcsX;
    float dy=(punchY-g_rcsPrevPunchY)*g_rcsY;
    g_rcsPrevPunchX=punchX;
    g_rcsPrevPunchY=punchY;

    if(dx==0.f&&dy==0.f)return;

    uintptr_t vaAddr=ViewAnglesAddr();if(!vaAddr)return;
    float pitch=Rd<float>(vaAddr);float yaw=Rd<float>(vaAddr+4);
    pitch-=dx*2.f;
    yaw -=dy*2.f;
    pitch=Clampf(pitch,-89.f,89.f);
    if(yaw>180.f)yaw-=360.f;else if(yaw<-180.f)yaw+=360.f;
    Wr<float>(vaAddr,pitch);
    Wr<float>(vaAddr+4,yaw);
}

static void RunStrafeHelper(){
    if(!g_strafeEnabled||!g_client) return;
    if(g_menuOpen) return;
    if(g_strafeKey!=0&&!(GetAsyncKeyState(g_strafeKey)&0x8000)) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn); if(!lp) return;
    if(Rd<uint32_t>(lp+offsets::base_entity::m_fFlags)&1) return;
    
    uintptr_t vaAddr=ViewAnglesAddr(); if(!vaAddr) return;
    float curYaw=Rd<float>(vaAddr+4);
    static float s_lastYaw = 0.f;
    float delta = curYaw - s_lastYaw;
    if(delta > 180.f) delta -= 360.f; else if(delta < -180.f) delta += 360.f;
    s_lastYaw = curYaw;

    if(fabsf(delta) < 0.1f) return;

    if(delta > 0.f){
        Wr<int>(g_client+offsets::buttons::left,  65537);
        Wr<int>(g_client+offsets::buttons::right, 0);
    } else {
        Wr<int>(g_client+offsets::buttons::right, 65537);
        Wr<int>(g_client+offsets::buttons::left,  0);
    }
}

static constexpr int kEntityListStride = 112;
static void RunTriggerBot(){
    if(!g_tbEnabled||!g_client)return;
    if(g_tbKey!=0&&!(GetAsyncKeyState(g_tbKey)&0x8000)){g_tbShouldFire=false;return;}
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int entIdx=Rd<int>(lp+offsets::cs_pawn::m_iIDEntIndex);
    if(entIdx<=0||entIdx>8192){g_tbShouldFire=false;return;}
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((entIdx&0x7FFF)>>9)+16);if(!pchunk)return;
    uintptr_t targPawn=Rd<uintptr_t>(pchunk+kEntityListStride*(entIdx&0x1FF));
    if(!targPawn||!IsLikelyPtr(targPawn)){g_tbShouldFire=false;return;}
    int lifeState=Rd<uint8_t>(targPawn+offsets::base_entity::m_lifeState);
    if(lifeState!=0){g_tbShouldFire=false;return;}
    int targTeam=(int)Rd<uint8_t>(targPawn+offsets::base_entity::m_iTeamNum);
    int targHealth=Rd<int>(targPawn+offsets::base_entity::m_iHealth);
    if(targHealth<=0){g_tbShouldFire=false;return;}
    if(g_tbTeamChk&&targTeam==g_esp_local_team){g_tbShouldFire=false;return;}
    if(!g_tbShouldFire){g_tbShouldFire=true;g_tbFireTime=GetTickCount64()+(UINT64)g_tbDelay;}
    if(GetTickCount64()>=g_tbFireTime){
        Wr<int>(g_client+offsets::buttons::attack,65537);
        g_tbShouldFire=false;
        g_tbJustFired=true;
        g_tbHoldFramesLeft=4;
    }
}

static void ReleaseTriggerAttack(){
    if(!g_client||!g_tbEnabled)return;
    if(g_tbJustFired && g_tbHoldFramesLeft>0){
        Wr<int>(g_client+offsets::buttons::attack,65537);
        g_tbHoldFramesLeft--;
        return;
    }
    if(g_tbJustFired){
        Wr<int>(g_client+offsets::buttons::attack,256);
        g_tbJustFired=false;
        return;
    }
}

static void RunAimbot(){
    g_aimbotLastFound = false;
    g_aimbotLastBestFov = 1e9f;
    if(!g_aimbotEnabled||!g_client)return;
    if(g_menuOpen) return;
    if(!(GetAsyncKeyState(g_aimbotKey)&0x8000))return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    bool lmbHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    int shotsFired = Rd<int>(lp + offsets::cs_pawn::m_iShotsFired);
    if(g_rcsEnabled && lmbHeld && shotsFired >= 1) return;
    uintptr_t vaAddr=ViewAnglesAddr();if(!vaAddr)return;
    uintptr_t sc0=Rd<uintptr_t>(lp+offsets::base_entity::m_pGameSceneNode);Vec3 localOrigin{};
    if(sc0)localOrigin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);
    Vec3 eyePos=localOrigin+Rd<Vec3>(lp+offsets::base_pawn::m_vecViewOffset);
    float curPitch=Rd<float>(vaAddr);float curYaw=Rd<float>(vaAddr+4);
    float bestDist=g_aimbotFov;Vec3 bestPoint{};bool found=false;
    auto evalPoint = [&](const Vec3& p){
        Vec2 aimAngle=CalcAngle(eyePos,p);
        float dPitch=fabsf(AngleDiff(aimAngle.x,curPitch));
        float dYaw=fabsf(AngleDiff(aimAngle.y,curYaw));
        float fovDist=sqrtf(dPitch*dPitch+dYaw*dYaw);
        if(fovDist<bestDist){bestDist=fovDist;bestPoint=p;found=true;}
    };
    uintptr_t crossPawn = 0;
    if(g_aimbotVisCheck){
        int crossIdx = Rd<int>(lp + offsets::cs_pawn::m_iIDEntIndex);
        if(crossIdx > 0 && crossIdx <= 8192){
            uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
            if(entityList){
                uintptr_t pchunk = Rd<uintptr_t>(entityList + 8*((crossIdx&0x7FFF)>>9) + 16);
                if(pchunk)
                    crossPawn = Rd<uintptr_t>(pchunk + kEntityListStride * (crossIdx & 0x1FF));
            }
        }
        if(!crossPawn || !IsLikelyPtr(crossPawn)) return;
        int ls = Rd<uint8_t>(crossPawn + offsets::base_entity::m_lifeState);
        if(ls != 0) return;
        if(Rd<int>(crossPawn + offsets::base_entity::m_iHealth) <= 0) return;
        if(g_aimbotTeamChk && (int)Rd<uint8_t>(crossPawn + offsets::base_entity::m_iTeamNum) == g_esp_local_team) return;
    }
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry&e=g_esp_players[i];
        if(!e.valid||!e.pawn||!IsLikelyPtr(e.pawn))continue;
        if(g_aimbotTeamChk&&e.team==g_esp_local_team)continue;
        if(e.distance>g_espMaxDist)continue;
        if(g_aimbotVisCheck && e.pawn != crossPawn)continue;
        UpdatePawnBones(e.pawn);
        auto getBone = [&](int id, Vec3& out) -> bool {
            return GetBonePos(e.pawn, id, out);
        };
        Vec3 aimPoint{e.head_ox, e.head_oy, e.head_oz};
        { Vec3 bp{}; if(getBone(BONE_HEAD,bp)) aimPoint=bp; }
        evalPoint(aimPoint);
    }
    if(!found){
        uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
        int localTeam=(int)Rd<uint8_t>(lp+offsets::base_entity::m_iTeamNum);
        uintptr_t localCtrl=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController);
        if(entityList){
            for(int i=1;i<64;i++){
                uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16); if(!chunk)continue;
                uintptr_t ctrl=Rd<uintptr_t>(chunk+kEntityListStride*(i&0x1FF)); if(!ctrl||!IsLikelyPtr(ctrl))continue;
                if(ctrl==localCtrl)continue;
                if(!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
                uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn); if(!ph)continue;
                uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16); if(!pchunk)continue;
                uintptr_t pawn=Rd<uintptr_t>(pchunk+kEntityListStride*(ph&0x1FF)); if(!pawn||!IsLikelyPtr(pawn)||pawn==lp)continue;
                if(Rd<uint8_t>(pawn+offsets::base_entity::m_lifeState)!=0)continue;
                if(Rd<int>(pawn+offsets::base_entity::m_iHealth)<=0)continue;
                if(g_aimbotTeamChk && (int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum)==localTeam)continue;
                if(g_aimbotVisCheck && pawn != crossPawn)continue;
                Vec3 origin=Rd<Vec3>(pawn+offsets::base_pawn::m_vOldOrigin);
                uintptr_t scn=Rd<uintptr_t>(pawn+offsets::base_entity::m_pGameSceneNode); if(scn) origin=Rd<Vec3>(scn+offsets::scene_node::m_vecAbsOrigin);
                Vec3 viewOff=Rd<Vec3>(pawn+offsets::base_pawn::m_vecViewOffset);
                Vec3 head={origin.x+viewOff.x, origin.y+viewOff.y, origin.z+viewOff.z};
                UpdatePawnBones(pawn);
                Vec3 aimPoint = head;
                { Vec3 bp{}; if(GetBonePos(pawn, BONE_HEAD, bp)) aimPoint = bp; }
                float dist=(aimPoint-localOrigin).length()/100.f; if(dist>g_espMaxDist)continue;
                evalPoint(aimPoint);
            }
        }
    }
    if(!found)return;
    g_aimbotLastFound = true;
    g_aimbotLastBestFov = bestDist;
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

static void RunAimFireGate(){
    if(!g_waitAimThenFire || !g_aimbotEnabled || !g_client || g_menuOpen) return;
    if(!(GetAsyncKeyState(g_aimbotKey) & 0x8000)) return;
    if(!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    uintptr_t lp = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!lp) return;
    int shots = Rd<int>(lp + offsets::cs_pawn::m_iShotsFired);
    if(g_rcsEnabled && shots >= 1) return;
    if(g_aimbotLastFound && g_aimbotLastBestFov <= g_waitAimFovDeg) return;
    Wr<int>(g_client + offsets::buttons::attack, 0);
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

struct BulletTrace{Vec3 start,end;float lifetime,maxlife;ImU32 color;};
static std::deque<BulletTrace>g_traces;
static int g_lastShotsFired = 0;

struct SoundPing{Vec3 pos;float lifetime,maxlife;float radius;float height;};
static std::deque<SoundPing>g_soundPings;

static bool g_bombActive=false;
static int g_bombSite=-1;
static Vec3 g_bombPos{};
static float g_bombExplodeTime=0.f;
static float g_bombDefuseEnd=0.f;
static float g_lastBombScan=0.f;

static void PushNotification(const char*text,ImU32 color){
    (void)color;
    if(!text||!text[0])return;
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
            uint8_t r=(t.color>>IM_COL32_R_SHIFT)&0xFF, gc=(t.color>>IM_COL32_G_SHIFT)&0xFF, b=(t.color>>IM_COL32_B_SHIFT)&0xFF;
            dl->AddLine({sx,sy},{ex,ey}, IM_COL32(r,gc,b,(int)(40*a)),  7.f);
            dl->AddLine({sx,sy},{ex,ey}, IM_COL32(r,gc,b,(int)(80*a)),  3.5f);
            dl->AddLine({sx,sy},{ex,ey}, IM_COL32(r,gc,b,(int)(220*a)), 1.2f);
            dl->AddCircleFilled({ex,ey}, 3.5f*a, IM_COL32(r,gc,b,(int)(180*a)), 8);
            dl->AddCircleFilled({ex,ey}, 1.5f*a, IM_COL32(255,255,255,(int)(180*a)), 8);
        }
    }
    g_traces.erase(std::remove_if(g_traces.begin(),g_traces.end(),[](const BulletTrace& t){return t.lifetime<=0.f;}),g_traces.end());
}

static void UpdateSoundPings(){
    if(!g_soundEnabled||!g_client) return;
    if(!g_soundBlipEnemy && !g_soundBlipTeam) return;
    UINT64 nowTick = GetTickCount64();
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
        for(int ring = 9; ring >= 0; ring--){
            float rMul = 0.15f + (float)ring * 0.09f;
            int a = (int)((160 - ring*10) * alpha);
            if(a < 12) continue;
            ImU32 ringCol = IM_COL32(colR, colG, colB, (int)Clampf((float)a, 0.f, 255.f));
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
                float thick = 1.8f + (1.f - rMul) * 1.2f;
                for(int j=1;j<rCnt;j++) dl->AddLine(rPts[j-1], rPts[j], ringCol, thick);
                if(rCnt>1) dl->AddLine(rPts[rCnt-1], rPts[0], ringCol, thick);
            }
        }
        ImU32 fillCol = IM_COL32(colR, colG, colB, (int)(90*alpha));
        dl->AddConvexPolyFilled(pts, validCount, fillCol);
        ImU32 strokeCol = IM_COL32(colR, colG, colB, (int)(255*alpha));
        for(int j=1;j<validCount;j++) dl->AddLine(pts[j-1], pts[j], strokeCol, 2.5f);
        if(validCount>1) dl->AddLine(pts[validCount-1], pts[0], strokeCol, 2.5f);
    }
    g_soundPings.erase(std::remove_if(g_soundPings.begin(),g_soundPings.end(),[](const SoundPing& p){return p.lifetime<=0.f;}),g_soundPings.end());
}

static void DrawLogs(float dt,float sw,float sh){
    if(g_logs.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImFont* mainFont=font::bold?font::bold:ImGui::GetFont();
    ImFont* iconFont=font::icomoon;
    float cx=sw*0.5f,cy=sh*0.5f,y=cy+28.f;
    static const char hitIcon[] = "\xee\x80\x81";
    static const char killIcon[] = "\xee\x80\x82";
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

static const float g_bombTimerMaxDist = 1500.f;

static void UpdateBombInfo(){
    if(!g_bombTimerEnabled||!g_client) return;
    float now = GetCurTime();
    if(now - g_lastBombScan < 0.2f) return;
    g_lastBombScan = now;
    g_bombActive=false;g_bombSite=-1;g_bombExplodeTime=0.f;g_bombDefusing=false;g_bombDefuseEnd=0.f;g_bombDefuserPawn=0;g_bombPos={};
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
        float dx = g_bombPos.x - g_localOrigin.x, dy = g_bombPos.y - g_localOrigin.y;
        if(dx*dx + dy*dy > g_bombTimerMaxDist*g_bombTimerMaxDist) continue;
        g_bombActive=true;
        g_bombSite=Rd<int>(ent+offsets::planted_c4::m_nBombSite);
        g_bombExplodeTime=blow;
        g_bombDefusing=Rd<bool>(ent+offsets::planted_c4::m_bBeingDefused);
        if(g_bombDefusing){
            g_bombDefuseEnd=Rd<float>(ent+offsets::planted_c4::m_flDefuseCountDown);
            uint32_t hDefuser=Rd<uint32_t>(ent+offsets::planted_c4::m_hBombDefuser);
            g_bombDefuserPawn=hDefuser?ResolveHandle(entityList,hDefuser):0;
        }
        break;
    }
}

static void DrawBombTimer(float sw){
    if(!g_bombTimerEnabled||!g_bombActive) return;
    float now = GetCurTime();
    float tLeft = g_bombExplodeTime - now;
    if(tLeft <= 0.f) return;
    if(tLeft > 42.f) return;
    char tail[96];
    if(g_bombDefusing){
        float dLeft = g_bombDefuseEnd - now;
        if(dLeft < 0.f) dLeft = 0.f;
        std::snprintf(tail,sizeof(tail)," | %.1fs | Defuse %.1fs", tLeft, dLeft);
    }else{
        std::snprintf(tail,sizeof(tail)," | %.1fs", tLeft);
    }
    char siteStr[2] = { (char)(g_bombSite==1?'B':'A'), '\0' };
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const float fs = reg->LegacySize;
    ImVec2 wBomb = reg->CalcTextSizeA(fs, FLT_MAX, 0.f, "Bomb ");
    ImVec2 wSite = reg->CalcTextSizeA(fs, FLT_MAX, 0.f, siteStr);
    ImVec2 wTail = reg->CalcTextSizeA(fs, FLT_MAX, 0.f, tail);
    float tw = wBomb.x + wSite.x + wTail.x;
    float th = (std::max)({ wBomb.y, wSite.y, wTail.y });
    float pad=8.f;
    ImVec2 pos{sw*0.5f-tw*0.5f-pad,30.f};
    ImVec2 boxMax{pos.x+tw+pad*2.f,pos.y+th+pad*2.f};
    DrawOverlayWatermarkChrome(dl, pos, boxMax, 6.f);
    ImU32 acc = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    ImU32 dim = IM_COL32(180,182,190,255);
    float tx = pos.x + pad;
    float ty = pos.y + pad;
    dl->AddText(reg, fs, {tx, ty}, dim, "Bomb ");
    tx += wBomb.x;
    dl->AddText(reg, fs, {tx, ty}, acc, siteStr);
    tx += wSite.x;
    dl->AddText(reg, fs, {tx, ty}, dim, tail);
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
    if(msg == WM_KEYDOWN && wp == VK_F5) {
        g_showDebugConsole = !g_showDebugConsole;
        return true;
    }
    if(g_imguiInitialized&&(g_menuOpen || g_showDebugConsole)){
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

    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* btn = active ? "..." : KeyName(*key);

    ImU32 btnBg = active
        ? IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),210)
        : IM_COL32(23,23,23,220);
    if(hovered && !active)
        btnBg = IM_COL32(28,30,36,235);
    dl->AddRectFilled(btnPos, {btnPos.x+90.f, btnPos.y+26.f}, btnBg, 6.f);
    dl->AddRect(btnPos, {btnPos.x+90.f, btnPos.y+26.f}, IM_COL32(50,52,60,200), 6.f, 0, 1.f);

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
    ImU32 tc = hovered ? IM_COL32(220,220,220,255) : IM_COL32(100,100,110,255);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tc));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopID();
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

static const char* SectionIconForTitle(const char* title){
    if(!title) return "";
    if(strcmp(title, "Aimbot") == 0) return "A";
    if(strcmp(title, "Triggerbot") == 0) return "T";
    if(strcmp(title, "ESP") == 0) return "E";
    if(strcmp(title, "Preview") == 0) return "P";
    if(strcmp(title, "Effects") == 0) return "F";
    if(strcmp(title, "Skins") == 0) return "S";
    if(strcmp(title, "World") == 0) return "W";
    if(strcmp(title, "Misc") == 0) return "M";
    if(strcmp(title, "Config") == 0) return "C";
    return "";
}

static void PidoSection(const char* title){
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    if(width < 4.f) return;

    const float h    = 20.f * s;
    const float fpx  = 9.f * s;
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImFont* reg      = font::regular ? font::regular : ImGui::GetFont();
    float midY       = pos.y + h * 0.5f;

    dl->AddLine({pos.x+2.f*s, midY}, {pos.x+width-2.f*s, midY}, IM_COL32(48,51,68,50));

    ImVec2 tsz = reg->CalcTextSizeA(fpx, FLT_MAX, 0.f, title);
    float lx   = pos.x + 10.f * s;
    dl->AddCircleFilled({lx, midY}, 1.5f*s, WithAlpha(g_pido.accent, 0.7f), 8);
    dl->AddText(reg, fpx, {lx + 4.f*s, midY - tsz.y*0.5f}, IM_COL32(95,100,125,220), title);

    ImGui::Dummy(ImVec2(width, h));
}

static bool BeginPidoChild(const char* id, const ImVec2& size){
    float scale = MenuScale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * scale, 8.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 3.f * scale));
    bool open = ImGui::BeginChild(id, size, false);
    ImGui::SetWindowFontScale(scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sw = ImGui::GetWindowSize();
    const float rnd = g_skeetUi.childRounding * scale;
    dl->AddRectFilled(p, {p.x+sw.x, p.y+sw.y}, g_pido.elemBg, rnd);
    dl->AddRect(p, {p.x+sw.x, p.y+sw.y}, g_pido.elemStroke, rnd, 0, 1.f);
    dl->AddRect({p.x+1.f, p.y+1.f}, {p.x+sw.x-1.f, p.y+sw.y-1.f}, IM_COL32(8, 8, 12, 180), rnd, 0, 1.f);
    dl->AddLine({p.x+3.f, p.y+1.f}, {p.x+sw.x-3.f, p.y+1.f}, IM_COL32(255, 255, 255, 14));
    return open;
}

static void EndPidoChild(){
    SmoothScrollCurrentWindow(45.f * MenuScale(), 14.f);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

static int g_grpGeneration = 0;

static bool BeginPidoGroup(const char* id, const char* title, const ImVec2& size){
    float s = MenuScale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f*s, 2.f*s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.f,   2.f*s));
    bool open = ImGui::BeginChild(id, size, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sw = ImGui::GetWindowSize();
    const float rnd = 4.f * s;

    static std::unordered_map<ImGuiID, float> s_grpAnims;
    ImGuiID gid = ImGui::GetID(id) ^ (ImGuiID)(g_grpGeneration * 0x9e3779b9u);
    float& ga = s_grpAnims[gid];
    ga = LerpF(ga, 1.f, 1.f - expf(-10.f * ImGui::GetIO().DeltaTime));

    dl->AddRectFilled(p, {p.x+sw.x, p.y+sw.y}, WithAlpha(g_pido.elemBg, ga), rnd);
    dl->AddRect(p, {p.x+sw.x, p.y+sw.y}, WithAlpha(g_pido.elemStroke, ga), rnd, 0, 1.f);

    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const float fpx = 9.f * s;
    const float titleH = 18.f * s;
    ImVec2 tsz = reg->CalcTextSizeA(fpx, FLT_MAX, 0.f, title);
    dl->AddText(reg, fpx, {p.x + (sw.x - tsz.x)*0.5f, p.y + (titleH - tsz.y)*0.5f},
        WithAlpha(g_pido.textDim, ga), title);
    dl->AddLine({p.x + 6.f*s, p.y + titleH}, {p.x + sw.x - 6.f*s, p.y + titleH},
        WithAlpha(g_pido.elemStroke, ga * 0.6f));

    ImGui::SetCursorPosY(titleH + 3.f * s);
    return open;
}

static void EndPidoGroup(){
    SmoothScrollCurrentWindow(45.f * MenuScale(), 14.f);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

static bool PidoTab(const char* icon, const char* label, const char*, bool selected, float forceW = 0.f){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = (forceW > 0.f) ? forceW : ImGui::GetContentRegionAvail().x;
    float height = 40.f * s;
    ImGui::InvisibleButton("##tab", ImVec2(width, height));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    static std::unordered_map<ImGuiID, float> s_tabDotAnims;
    ImGuiID dotId = ImGui::GetID("##dot");
    float& dotAnim = s_tabDotAnims[dotId];
    dotAnim = LerpF(dotAnim, selected ? 1.f : 0.f, 1.f - expf(-14.f * ImGui::GetIO().DeltaTime));

    ImU32 rowBg = hovered && !selected ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, rowBg, 0.f);
    if(selected || dotAnim > 0.01f)
        dl->AddRectFilled(pos, {pos.x+width, pos.y+height},
            IM_COL32(28, 30, 42, (int)(255.f*dotAnim)), 0.f);

    if(selected || dotAnim > 0.01f)
        dl->AddRectFilled({pos.x, pos.y+8.f*s}, {pos.x+3.5f*s*dotAnim, pos.y+height-8.f*s},
            WithAlpha(g_pido.accent, dotAnim), 2.f);

    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f},
        IM_COL32(255, 255, 255, 8));

    ImU32 txtCol = selected ? g_pido.textActive : (hovered ? g_pido.text : g_pido.textDim);
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;
    ImU32 dotCol = LerpColor(IM_COL32(50, 52, 64, 200), g_pido.accent, dotAnim);

    float tx = pos.x + 32.f*s;
    if(icon && icon[0] >= '0' && icon[0] <= '4'){
        int idx = icon[0] - '0';
        float cx = pos.x + 17.f*s;
        float r  = 7.f*s;
        ImU32 ic  = LerpColor(IM_COL32(70,73,95,220), g_pido.accent, dotAnim);
        ImU32 ic2 = LerpColor(IM_COL32(50,52,68,180), g_pido.accent, dotAnim * 0.6f);
        switch(idx){
        case 0: {
            const float tickLen = 2.8f*s, gap2 = 1.6f*s;
            dl->AddCircle({cx,midY}, r, ic, 24, 1.f*s);
            dl->AddLine({cx-r-tickLen, midY}, {cx-r-gap2, midY}, ic, 1.f*s);
            dl->AddLine({cx+r+gap2, midY}, {cx+r+tickLen, midY}, ic, 1.f*s);
            dl->AddLine({cx, midY-r-tickLen}, {cx, midY-r-gap2}, ic, 1.f*s);
            dl->AddLine({cx, midY+r+gap2}, {cx, midY+r+tickLen}, ic, 1.f*s);
            dl->AddCircleFilled({cx,midY}, 1.6f*s, ic, 8);
        }   break;
        case 1: {
            const int NS = 20;
            float eyeRx = r + 1.f*s, eyeRy = r * 0.55f;
            for(int i=0;i<NS;i++){
                float a0 = 3.14159265f + (float)i/(float)NS * 3.14159265f;
                float a1 = 3.14159265f + (float)(i+1)/(float)NS * 3.14159265f;
                dl->AddLine(
                    {cx + cosf(a0)*eyeRx, midY + sinf(a0)*eyeRy},
                    {cx + cosf(a1)*eyeRx, midY + sinf(a1)*eyeRy}, ic, 1.f*s);
            }
            for(int i=0;i<NS;i++){
                float a0 = (float)i/(float)NS * 3.14159265f;
                float a1 = (float)(i+1)/(float)NS * 3.14159265f;
                dl->AddLine(
                    {cx + cosf(a0)*eyeRx, midY + sinf(a0)*eyeRy},
                    {cx + cosf(a1)*eyeRx, midY + sinf(a1)*eyeRy}, ic, 1.f*s);
            }
            dl->AddCircleFilled({cx, midY}, r*0.32f, ic, 12);
        }   break;
        case 2: {
            dl->AddCircle({cx,midY}, r, ic, 24, 1.f*s);
            float latY1 = midY - r*0.45f, latY2 = midY + r*0.45f;
            float hw1 = sqrtf(r*r - (r*0.45f)*(r*0.45f));
            dl->AddLine({cx-hw1, latY1},{cx+hw1, latY1}, ic2, 0.8f*s);
            dl->AddLine({cx-hw1, latY2},{cx+hw1, latY2}, ic2, 0.8f*s);
            dl->AddLine({cx, midY-r},{cx, midY+r}, ic2, 0.8f*s);
        }   break;
        case 3: {
            float ri = r*0.42f, ro = r*0.72f;
            dl->AddCircle({cx,midY}, ri, ic, 16, 1.f*s);
            const int TEETH = 6;
            for(int i=0;i<TEETH;i++){
                float ang = (float)i / (float)TEETH * 3.14159265f * 2.f;
                float halfA = 3.14159265f / (float)TEETH * 0.5f;
                ImVec2 p0 = {cx + cosf(ang-halfA)*ro, midY + sinf(ang-halfA)*ro};
                ImVec2 p1 = {cx + cosf(ang+halfA)*ro, midY + sinf(ang+halfA)*ro};
                ImVec2 p2 = {cx + cosf(ang+halfA)*(ro+2.8f*s), midY + sinf(ang+halfA)*(ro+2.8f*s)};
                ImVec2 p3 = {cx + cosf(ang-halfA)*(ro+2.8f*s), midY + sinf(ang-halfA)*(ro+2.8f*s)};
                dl->AddQuad(p0, p1, p2, p3, ic, 1.f*s);
            }
        }   break;
        }
    } else {
        dl->AddCircleFilled({pos.x + 11.f*s, midY}, 2.5f*s, dotCol, 12);
        tx = pos.x + 20.f*s;
    }
    dl->AddText(reg, 12.f*s, {tx, midY - reg->LegacySize*s*0.5f}, txtCol, label, labelEnd);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return pressed;
}

static bool PidoToggle(const char* label, const char* desc, bool* v){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x+width, pos.y+height});
    if(hovered && ImGui::IsMouseClicked(0)) *v = !*v;
    bool pressed = hovered && ImGui::IsMouseClicked(0);

    static std::unordered_map<ImGuiID, float> s_toggleAnims;
    ImGuiID tid = ImGui::GetID(label);
    float& anim = s_toggleAnims[tid];
    anim = LerpF(anim, *v ? 1.f : 0.f, 1.f - expf(-18.f * ImGui::GetIO().DeltaTime));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(hovered) dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, IM_COL32(255,255,255,5), 2.f*s);

    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;

    const float sqSz = 8.f * s;
    const float sqX  = pos.x + 8.f*s;
    const float sqY  = midY - sqSz * 0.5f;
    ImU32 sqBorder = anim > 0.01f
        ? WithAlpha(g_pido.accent, 0.7f + anim * 0.3f)
        : IM_COL32(50, 52, 72, 200);
    dl->AddRect({sqX, sqY}, {sqX+sqSz, sqY+sqSz}, sqBorder, 1.5f*s, 0, 1.f*s);
    if(anim > 0.01f)
        dl->AddRectFilled({sqX+1.f, sqY+1.f}, {sqX+sqSz-1.f, sqY+sqSz-1.f},
            WithAlpha(g_pido.accent, anim * 0.9f), 1.f*s);

    ImU32 labelCol = *v ? g_pido.textActive : g_pido.text;
    dl->AddText(reg, 11.f*s, {sqX+sqSz+6.f*s, midY-reg->LegacySize*s*0.5f}, labelCol, label, labelEnd);
    if(desc && desc[0])
        dl->AddText(reg, 9.f*s, {sqX+sqSz+6.f*s, midY+2.f*s}, g_pido.textDim, desc);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return pressed;
}

static bool PidoSliderFloat(const char* label, const char* desc, float* v, float v_min, float v_max, const char* format = "%.1f"){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    float midY = pos.y + height * 0.5f;
    bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x+width, pos.y+height});

    const float btnSz = 12.f*s, trackW = 80.f*s, trackH = 3.f*s, gp = 4.f*s, rp = 8.f*s;
    float plusL  = pos.x + width - rp - btnSz;
    float trackR = plusL - gp;
    float trackL = trackR - trackW;
    float minusL = trackL - gp - btnSz;

    ImGui::SetCursorScreenPos({plusL, pos.y});
    ImGui::InvisibleButton("##plus", ImVec2(btnSz, height));
    bool plusHov = ImGui::IsItemHovered();
    bool plusClicked = ImGui::IsItemClicked();
    if(plusClicked) *v = Clampf(*v + (v_max-v_min)/100.f, v_min, v_max);

    ImGui::SetCursorScreenPos({minusL, pos.y});
    ImGui::InvisibleButton("##minus", ImVec2(btnSz, height));
    bool minusHov = ImGui::IsItemHovered();
    bool minusClicked = ImGui::IsItemClicked();
    if(minusClicked) *v = Clampf(*v - (v_max-v_min)/100.f, v_min, v_max);

    ImGui::SetCursorScreenPos({trackL, pos.y});
    ImGui::InvisibleButton("##track", ImVec2(trackW, height));
    bool trackActive = ImGui::IsItemActive();
    if(trackActive){
        float t = Clampf((ImGui::GetIO().MousePos.x - trackL) / trackW, 0.f, 1.f);
        *v = v_min + t * (v_max - v_min);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(hovered) dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, IM_COL32(255,255,255,4), 2.f*s);

    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    dl->AddText(reg, 11.f*s, {pos.x+8.f*s, midY-reg->LegacySize*s*0.5f}, g_pido.text, label, labelEnd);

    dl->AddRectFilled({trackL, midY-trackH*0.5f}, {trackR, midY+trackH*0.5f}, IM_COL32(28,28,42,255), trackH*0.5f);
    float t = Clampf((*v - v_min) / (v_max - v_min), 0.f, 1.f);
    float fillX = trackL + t * trackW;
    if(fillX > trackL)
        dl->AddRectFilled({trackL, midY-trackH*0.5f}, {fillX, midY+trackH*0.5f}, g_pido.accent, trackH*0.5f);

    char valBuf[32]; std::snprintf(valBuf, sizeof(valBuf), format, *v);
    ImVec2 vSz = reg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, valBuf);
    dl->AddText(reg, 9.f*s, {trackL+(trackW-vSz.x)*0.5f, midY-trackH*0.5f-vSz.y-1.f*s}, g_pido.textDim, valBuf);

    ImVec2 mSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "-");
    ImVec2 pSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "+");
    dl->AddText(reg, 11.f*s, {minusL+(btnSz-mSz.x)*0.5f, midY-mSz.y*0.5f},
        minusHov ? g_pido.accent : g_pido.textDim, "-");
    dl->AddText(reg, 11.f*s, {plusL+(btnSz-pSz.x)*0.5f, midY-pSz.y*0.5f},
        plusHov ? g_pido.accent : g_pido.textDim, "+");

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return trackActive || plusClicked || minusClicked;
}

static bool PidoSliderInt(const char* label, const char* desc, int* v, int v_min, int v_max, const char* format = "%d"){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    float midY = pos.y + height * 0.5f;
    bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x+width, pos.y+height});

    const float btnSz = 12.f*s, trackW = 80.f*s, trackH = 3.f*s, gp = 4.f*s, rp = 8.f*s;
    float plusL  = pos.x + width - rp - btnSz;
    float trackR = plusL - gp;
    float trackL = trackR - trackW;
    float minusL = trackL - gp - btnSz;

    ImGui::SetCursorScreenPos({plusL, pos.y});
    ImGui::InvisibleButton("##plus", ImVec2(btnSz, height));
    bool plusHov = ImGui::IsItemHovered();
    bool plusClicked = ImGui::IsItemClicked();
    if(plusClicked) *v = (std::min)(*v + 1, v_max);

    ImGui::SetCursorScreenPos({minusL, pos.y});
    ImGui::InvisibleButton("##minus", ImVec2(btnSz, height));
    bool minusHov = ImGui::IsItemHovered();
    bool minusClicked = ImGui::IsItemClicked();
    if(minusClicked) *v = (std::max)(*v - 1, v_min);

    ImGui::SetCursorScreenPos({trackL, pos.y});
    ImGui::InvisibleButton("##track", ImVec2(trackW, height));
    bool trackActive = ImGui::IsItemActive();
    if(trackActive){
        float t = Clampf((ImGui::GetIO().MousePos.x - trackL) / trackW, 0.f, 1.f);
        *v = v_min + (int)(t * (float)(v_max - v_min) + 0.5f);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(hovered) dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, IM_COL32(255,255,255,4), 2.f*s);

    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    dl->AddText(reg, 11.f*s, {pos.x+8.f*s, midY-reg->LegacySize*s*0.5f}, g_pido.text, label, labelEnd);

    dl->AddRectFilled({trackL, midY-trackH*0.5f}, {trackR, midY+trackH*0.5f}, IM_COL32(28,28,42,255), trackH*0.5f);
    float t = Clampf((float)(*v - v_min)/(float)(v_max - v_min), 0.f, 1.f);
    float fillX = trackL + t * trackW;
    if(fillX > trackL)
        dl->AddRectFilled({trackL, midY-trackH*0.5f}, {fillX, midY+trackH*0.5f}, g_pido.accent, trackH*0.5f);

    char valBuf[32]; std::snprintf(valBuf, sizeof(valBuf), format, *v);
    ImVec2 vSz = reg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, valBuf);
    dl->AddText(reg, 9.f*s, {trackL+(trackW-vSz.x)*0.5f, midY-trackH*0.5f-vSz.y-1.f*s}, g_pido.textDim, valBuf);

    ImVec2 mSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "-");
    ImVec2 pSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "+");
    dl->AddText(reg, 11.f*s, {minusL+(btnSz-mSz.x)*0.5f, midY-mSz.y*0.5f},
        minusHov ? g_pido.accent : g_pido.textDim, "-");
    dl->AddText(reg, 11.f*s, {plusL+(btnSz-pSz.x)*0.5f, midY-pSz.y*0.5f},
        plusHov ? g_pido.accent : g_pido.textDim, "+");

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return trackActive || plusClicked || minusClicked;
}

static bool PidoCombo(const char* label, const char* desc, int* current_item, const char* const items[], int items_count){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;

    ImGui::InvisibleButton("##row", ImVec2(width, height));
    bool rowHovered  = ImGui::IsItemHovered();
    bool rowClicked  = ImGui::IsItemClicked();
    ImVec2 nextPos   = ImGui::GetCursorScreenPos();

    ImVec2 bbMin = pos, bbMax{pos.x+width, pos.y+height};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 rowBg = rowHovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, rowBg, 2.f*s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;
    dl->AddText(bold, 12.f*s, {pos.x+8.f*s, midY - 6.f*s}, g_pido.textActive, label, labelEnd);

    const char* curItem = (*current_item >= 0 && *current_item < items_count) ? items[*current_item] : "";
    const float chipPadX = 7.f*s, chipPadY = 2.f*s, chipRnd = 4.f*s, arrowW = 10.f*s;
    ImVec2 chipSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, curItem);
    float chipW = chipSz.x + chipPadX*2.f + arrowW + 4.f*s;
    float chipH = chipSz.y + chipPadY*2.f;
    float chipX = bbMax.x - chipW - 10.f*s;
    float chipY = midY - chipH*0.5f;
    dl->AddRectFilled({chipX, chipY}, {chipX+chipW, chipY+chipH}, IM_COL32(22,24,32,255), chipRnd);
    dl->AddRect({chipX, chipY}, {chipX+chipW, chipY+chipH}, WithAlpha(g_pido.accent, 0.35f), chipRnd, 0, 1.f);
    dl->AddText(reg, 11.f*s, {chipX+chipPadX, chipY+chipPadY}, g_pido.text, curItem);

    const char* popupId = "##pido_combo_popup";
    if(rowClicked) ImGui::OpenPopup(popupId);
    bool popup_open = ImGui::IsPopupOpen(popupId);

    float ax = chipX + chipW - arrowW*0.5f - 2.f*s;
    if(popup_open)
        dl->AddTriangleFilled({ax-3.f*s,midY+2.f*s},{ax+3.f*s,midY+2.f*s},{ax,midY-2.f*s}, g_pido.textDim);
    else
        dl->AddTriangleFilled({ax-3.f*s,midY-2.f*s},{ax+3.f*s,midY-2.f*s},{ax,midY+2.f*s}, g_pido.textDim);

    ImGui::SetNextWindowPos({pos.x, bbMax.y + 2.f*s});
    ImGui::SetNextWindowSize({width, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f*s, 5.f*s));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f*s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(17, 18, 24, 252));
    ImGui::PushStyleColor(ImGuiCol_Border,  WithAlpha(g_pido.accent, 0.3f));

    bool changed = false;
    if(ImGui::BeginPopup(popupId)){
        const float lineH = reg->LegacySize + 8.f*s;
        for(int i = 0; i < items_count; i++){
            ImGui::PushID(i);
            bool selected = (*current_item == i);
            ImVec2 ipos = ImGui::GetCursorScreenPos();
            float iw = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##item", ImVec2(iw, lineH));
            bool iHov = ImGui::IsItemHovered();
            if(ImGui::IsItemClicked()){
                *current_item = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            if(selected){
                pdl->AddRectFilled(ipos, {ipos.x+iw, ipos.y+lineH}, WithAlpha(g_pido.accent, 0.22f), 3.f*s);
            } else if(iHov){
                pdl->AddRectFilled(ipos, {ipos.x+iw, ipos.y+lineH}, IM_COL32(255,255,255,8), 3.f*s);
            }
            ImU32 itemCol = selected ? g_pido.accent : (iHov ? g_pido.textActive : g_pido.text);
            pdl->AddText(reg, reg->LegacySize, {ipos.x + 5.f*s, ipos.y + (lineH - reg->LegacySize)*0.5f}, itemCol, items[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoColorEdit4(const char* label, const char* desc, float col[4], ImGuiColorEditFlags flags = 0){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 44.f * s;
    ImVec2 bbMin = pos, bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 rowBg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, rowBg, 2.f*s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;
    dl->AddText(bold, 13.f*s, {pos.x+12.f*s, midY - 6.5f*s}, g_pido.textActive, label, labelEnd);

    const float swW = 22.f*s, swH = 22.f*s, swRnd = 4.f*s;
    float swX = bbMax.x - swW - 10.f*s;
    float swY = midY - swH*0.5f;
    ImU32 chk1 = IM_COL32(60,60,60,255), chk2 = IM_COL32(40,40,40,255);
    float cw = swW / 4.f, ch = swH / 2.f;
    for(int ci=0; ci<4; ci++){
        float cx2 = swX + ci*cw;
        dl->AddRectFilled({cx2, swY},      {cx2+cw, swY+ch}, (ci%2==0)?chk1:chk2, 0);
        dl->AddRectFilled({cx2, swY+ch},   {cx2+cw, swY+swH}, (ci%2==0)?chk2:chk1, 0);
    }
    dl->AddRectFilled({swX,swY},{swX+swW,swY+swH}, IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(col[3]*255)), swRnd);
    dl->AddRect({swX,swY},{swX+swW,swY+swH}, g_pido.elemStroke, swRnd, 0, 1.f);

    ImGui::SetCursorScreenPos({swX, swY});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, swRnd);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    bool changed = ImGui::ColorEdit4("##col", col, flags | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::SetCursorScreenPos({pos.x, bbMax.y});
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoInputText(const char* label, const char* desc, char* buf, size_t bufSize){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 34.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, bg, 2.f * s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+2.f*s : pos.y + (height - bold->LegacySize*s)*0.5f;
    dl->AddText(bold, 12.f * s, {pos.x+8.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 10.f * s, {pos.x+8.f*s, pos.y+14.f*s}, g_pido.textDim, desc);

    float inputW = (std::min)(200.f * s, width * 0.55f);
    ImGui::SetCursorScreenPos({bbMax.x - inputW - 8.f*s, bbMin.y + (height - 22.f*s)*0.5f});
    ImGui::PushItemWidth(inputW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    bool changed = ImGui::InputText("##input", buf, bufSize, ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoInputInt(const char* label, const char* desc, int* v){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 34.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, bg, 2.f * s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+2.f*s : pos.y + (height - bold->LegacySize*s)*0.5f;
    dl->AddText(bold, 12.f * s, {pos.x+8.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 10.f * s, {pos.x+8.f*s, pos.y+14.f*s}, g_pido.textDim, desc);

    float inputW = (std::min)(200.f * s, width * 0.55f);
    ImGui::SetCursorScreenPos({bbMax.x - inputW - 8.f*s, bbMin.y + (height - 22.f*s)*0.5f});
    ImGui::PushItemWidth(inputW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    bool changed = ImGui::InputInt("##input", v, 1, 10);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoKeybind(const char* label, const char* desc, int* key){
    static int* capture = nullptr;
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 34.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, bg, 2.f * s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+2.f*s : pos.y + (height - bold->LegacySize*s)*0.5f;
    dl->AddText(bold, 12.f * s, {pos.x+8.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 10.f * s, {pos.x+8.f*s, pos.y+14.f*s}, g_pido.textDim, desc);

    float btnW = 76.f * s, btnH = 22.f * s;
    ImGui::SetCursorScreenPos({bbMax.x - btnW - 8.f*s, bbMin.y + (height - btnH)*0.5f});
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
    ImGui::Dummy(ImVec2(0,0));
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
    const float prvRnd = 3.5f;
    auto drawBox=[&](ImVec2 center,ImU32 col){
        float l=center.x-boxW*0.5f,r=center.x+boxW*0.5f,t=center.y-boxH*0.5f,b=center.y+boxH*0.5f;
        if(g_espBoxStyle==2){
            dl->AddRectFilled({l,t},{r,b},IM_COL32(20,20,28,140),prvRnd);
            DrawCornerBox(dl,l,t,r,b,IM_COL32(0,0,0,200),g_espBoxThick+1.f);
            DrawCornerBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==1){
            dl->AddRect({l,t},{r,b},IM_COL32(0,0,0,200),prvRnd,0,g_espBoxThick+1.f);
            dl->AddRect({l,t},{r,b},col,prvRnd,0,g_espBoxThick);
        }else if(g_espBoxStyle==3){
            DrawOutlineBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==4){
            DrawCoalBox(dl,l,t,r,b,IM_COL32(0,0,0,200),g_espBoxThick+1.f);
            DrawCoalBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==5){
            DrawOutlineCoalBox(dl,l,t,r,b,col,g_espBoxThick);
        }else{
            DrawCornerBox(dl,l,t,r,b,IM_COL32(0,0,0,200),g_espBoxThick+1.f);
            DrawCornerBox(dl,l,t,r,b,col,g_espBoxThick);
        }
        if(g_espHeadDot){
            float dotR = 7.f;
            dl->AddCircle({center.x, t+6.f},dotR,IM_COL32(0,0,0,180),16,1.5f);
            dl->AddCircleFilled({center.x, t+6.f},dotR*0.55f,col,12);
            dl->AddCircle({center.x, t+6.f},dotR,col,16,1.0f);
        }
        if(g_espHealth){
            float barW=5.f;float bx=l-8.f-barW;
            dl->AddRectFilled({bx-1.f,t-1.f},{bx+barW+1.f,b+1.f},IM_COL32(40,42,52,200),3.f);
            dl->AddRectFilled({bx,t},{bx+barW,b},IM_COL32(10,10,14,220),2.f);
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

    float menuScale = MenuScale();
    float animEased = sqrtf(1.f - powf(g_menuAnim - 1.f, 2.f));
    float animScale = LerpF(0.92f, 1.f, animEased);
    float menuW = 820.f * menuScale * animScale;
    float menuH = 540.f * menuScale * animScale;
    float sw=(float)g_esp_screen_w, sh=(float)g_esp_screen_h;
    if(sw < 100.f || sh < 100.f){
        if(g_bbWidth >= 100){ sw = (float)g_bbWidth; } else if(sw < 100.f) sw = 1920.f;
        if(g_bbHeight >= 100){ sh = (float)g_bbHeight; } else if(sh < 100.f) sh = 1080.f;
    }

    ImGui::SetNextWindowSize(ImVec2(menuW,menuH), ImGuiCond_Always);
    float slideY = (1.f - animEased) * 10.f * menuScale;
    float menuX = (sw - menuW) * 0.5f;
    float menuY = (sh - menuH) * 0.5f + slideY;
    menuX = Clampf(menuX, 0.f, sw - menuW);
    menuY = Clampf(menuY, 0.f, sh - menuH);
    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animEased);
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

    if(font::bold) ImGui::PushFont(font::bold);

    ImDrawList*dl=ImGui::GetWindowDrawList();
    ImVec2 pos=ImGui::GetWindowPos();
    ImVec2 size=ImGui::GetWindowSize();

    const float s = menuScale;
    ImGui::SetWindowFontScale(s);
    const float menuRounding  = 0.f;
    const float pad           = 10.f * s;
    const float headerH       = 32.f * s;
    const float tabBarH       = 26.f * s;

    dl->AddRectFilled(pos, {pos.x+size.x, pos.y+size.y}, g_pido.bgFill, menuRounding);
    dl->AddRect(pos, {pos.x+size.x, pos.y+size.y},
        WithAlpha(g_pido.accent, 0.55f * animEased), menuRounding, 0, 1.f);
    dl->AddRect({pos.x+1.f, pos.y+1.f}, {pos.x+size.x-1.f, pos.y+size.y-1.f},
        IM_COL32(4, 5, 8, (int)(200.f*animEased)), menuRounding);

    {
        const float gy0 = pos.y + 1.f, gy1 = pos.y + 3.f;
        float gx0 = pos.x + 2.f, gx1 = pos.x + size.x - 2.f;
        float w = gx1 - gx0;
        float gq1 = gx0 + w * 0.33f, gq2 = gx0 + w * 0.66f;
        auto fa = [&](ImU32 c) -> ImU32 { return WithAlpha(c, animEased); };
        dl->AddRectFilledMultiColor({gx0,gy0},{gq1,gy1},
            fa(IM_COL32(108,132,188,220)), fa(IM_COL32(174,122,190,220)),
            fa(IM_COL32(174,122,190,220)), fa(IM_COL32(108,132,188,220)));
        dl->AddRectFilledMultiColor({gq1,gy0},{gq2,gy1},
            fa(IM_COL32(174,122,190,220)), fa(IM_COL32(194,166,118,220)),
            fa(IM_COL32(194,166,118,220)), fa(IM_COL32(174,122,190,220)));
        dl->AddRectFilledMultiColor({gq2,gy0},{gx1,gy1},
            fa(IM_COL32(194,166,118,220)), fa(IM_COL32(116,168,148,220)),
            fa(IM_COL32(116,168,148,220)), fa(IM_COL32(194,166,118,220)));
    }

    dl->AddLine({pos.x, pos.y+headerH}, {pos.x+size.x, pos.y+headerH},
        WithAlpha(g_pido.elemStroke, animEased), 1.f);
    float tabBarTop2 = pos.y + size.y - tabBarH;
    dl->AddRectFilled({pos.x, tabBarTop2}, {pos.x+size.x, pos.y+size.y}, g_pido.tabBg, 0.f);
    dl->AddLine({pos.x, tabBarTop2}, {pos.x+size.x, tabBarTop2},
        WithAlpha(g_pido.elemStroke, animEased), 1.f);

    {
        ImFont* fBold = font::bold ? font::bold : ImGui::GetFont();
        ImFont* fReg  = font::regular ? font::regular : fBold;
        float midH = pos.y + headerH * 0.5f;
        ImU32 dimFaded = WithAlpha(g_pido.textDim, animEased);

        const char* title = "litware";
        const float tSize = 15.f * s;
        ImVec2 szT = fBold->CalcTextSizeA(tSize, FLT_MAX, 0.f, title);
        dl->AddText(fBold, tSize, {pos.x + (size.x - szT.x)*0.5f, midH - szT.y*0.5f},
            WithAlpha(g_pido.textActive, animEased), title);

        const char* verBuf  = "v0.1.3";
        const char* dateBuf = "release: 03.04.2026";
        ImVec2 szVer  = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, verBuf);
        ImVec2 szDate = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, dateBuf);
        float lx = pos.x + 12.f*s;
        dl->AddText(fReg, 9.f*s, {lx, midH - szVer.y*0.5f},
            WithAlpha(g_pido.textActive, animEased), verBuf);
        lx += szVer.x + 6.f*s;
        dl->AddCircleFilled({lx, midH}, 1.3f*s, dimFaded, 8);
        lx += 6.f*s;
        dl->AddText(fReg, 9.f*s, {lx, midH - szDate.y*0.5f}, dimFaded, dateBuf);

        SYSTEMTIME st{}; GetLocalTime(&st);
        char timeBuf[12], fpsBuf2[16];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", st.wHour, st.wMinute);
        std::snprintf(fpsBuf2, sizeof(fpsBuf2), "%.0f fps", io.Framerate);
        ImVec2 szTime2 = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, timeBuf);
        ImVec2 szFps2  = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, fpsBuf2);
        float rx = pos.x + size.x - 12.f*s;
        rx -= szFps2.x;
        dl->AddText(fReg, 9.f*s, {rx, midH - szFps2.y*0.5f}, dimFaded, fpsBuf2);
        rx -= 8.f*s;
        dl->AddCircleFilled({rx, midH}, 1.3f*s, dimFaded, 8);
        rx -= szTime2.x + 8.f*s;
        dl->AddText(fReg, 9.f*s, {rx, midH - szTime2.y*0.5f}, dimFaded, timeBuf);
    }
    {
        const char* tabLabels2[] = {"Aimbot","Visuals","World","Misc"};
        ImFont* fTabReg = font::regular ? font::regular : ImGui::GetFont();
        float tabBarTop = pos.y + size.y - tabBarH;
        float eachW = size.x / 4.f;
        for(int i=0;i<4;i++){
            float tx = pos.x + i*eachW;
            ImGui::SetCursorScreenPos({tx, tabBarTop});
            ImGui::InvisibleButton(tabLabels2[i], ImVec2(eachW, tabBarH));
            if(ImGui::IsItemClicked()) page=i;
            bool tabSel=(page==i);
            float fpx=tabSel?11.f*s:10.f*s;
            ImU32 tCol=tabSel?WithAlpha(g_pido.accent,animEased):WithAlpha(g_pido.textDim,animEased);
            ImVec2 tSz=fTabReg->CalcTextSizeA(fpx,FLT_MAX,0.f,tabLabels2[i]);
            float tlx=tx+(eachW-tSz.x)*0.5f;
            float tly=tabBarTop+(tabBarH-tSz.y)*0.5f;
            dl->AddText(fTabReg,fpx,{tlx,tly},tCol,tabLabels2[i]);
            if(tabSel)
                dl->AddLine({tlx, tabBarTop+tabBarH-2.f*s},
                            {tlx+tSz.x, tabBarTop+tabBarH-2.f*s},
                            WithAlpha(g_pido.accent,animEased), 1.5f*s);
            if(i>0)
                dl->AddLine({tx, tabBarTop+5.f*s}, {tx, tabBarTop+tabBarH-5.f*s},
                    WithAlpha(g_pido.elemStroke, animEased * 0.6f));
        }
    }

    if(page != g_activeTab){
        g_grpGeneration++;
        g_activeTab = page;
    }

    static bool s_configTabWasVisible = false;
    const bool configTabVisible = g_activeTab == 3;
    if(configTabVisible && !s_configTabWasVisible) RefreshConfigList();
    s_configTabWasVisible = configTabVisible;

    const float tabAlphaEased = 1.f;

    float contentX = pad;
    float contentY = headerH + pad;
    float contentW = size.x - pad*2.f;
    float contentH = (std::max)(80.f*s, size.y - headerH - tabBarH - pad*2.f);
    const float gap  = 6.f * s;
    float childW = (contentW - gap) * 0.5f;
    float rightX = contentX + childW + gap;
    auto grpH = [&](int rows) -> float {
        return rows * g_skeetUi.rowHeight * s + 30.f * s;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animEased * tabAlphaEased);

    if(g_activeTab==0){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_aim", "Aimbot", {childW, contentH});
        PidoToggle("Enable","", &g_aimbotEnabled);
        if(g_aimbotEnabled){
            PidoSliderFloat("FOV","", &g_aimbotFov, 1.f, 90.f, "%.1f");
            PidoSliderFloat("Smooth","", &g_aimbotSmooth, 1.f, 30.f, "%.1f");
            PidoKeybind("Aimbot key","", &g_aimbotKey);
            PidoToggle("FOV circle","", &g_fovCircleEnabled);
            if(g_fovCircleEnabled) PidoColorEdit4("FOV color","", g_fovCircleCol);
            PidoToggle("Autostop","", &g_autostopEnabled);
            PidoToggle("Wait aim then fire","", &g_waitAimThenFire);
            if(g_waitAimThenFire) PidoSliderFloat("Aim lock (deg)","", &g_waitAimFovDeg, 0.5f, 8.f, "%.2f");
            PidoToggle("Crosshair target only","", &g_aimbotVisCheck);
        }
        EndPidoGroup();

        float tbH  = grpH(3);
        float rcsH = contentH - tbH - gap;
        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_tb", "Triggerbot", {childW, tbH});
        PidoToggle("Enable##tb","", &g_tbEnabled);
        PidoSliderInt("Delay (ms)","", &g_tbDelay, 0, 300);
        PidoKeybind("Trigger key","", &g_tbKey);
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY + tbH + gap});
        BeginPidoGroup("##g_rcs", "Recoil Control", {childW, rcsH});
        PidoToggle("Enable##rcs","", &g_rcsEnabled);
        PidoSliderFloat("X axis","", &g_rcsX, 0.f, 2.f, "%.2f");
        PidoSliderFloat("Y axis","", &g_rcsY, 0.f, 2.f, "%.2f");
        EndPidoGroup();
    }else if(g_activeTab==1){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_esp", "ESP", {childW, contentH});
        PidoToggle("Enable##esp","", &g_espEnabled);
        if(g_safeMode){ ImGui::SameLine(); if(ImGui::Button("Retry##esp2")) g_safeMode=false; }
        PidoSection("Box");
        {
            PidoToggle("Draw box","", &g_espDrawBox);
            const char* boxItems[]={"Corner","Full","Corner Fill","Outline","Coal","Outline Coal"};
            PidoCombo("Style","", &g_espBoxStyle, boxItems, IM_ARRAYSIZE(boxItems));
            PidoSliderFloat("Thickness","", &g_espBoxThick, 0.5f, 4.f, "%.1f");
            PidoColorEdit4("Enemy color","", g_espEnemyCol);
            PidoColorEdit4("Team color","", g_espTeamCol);
        }
        PidoSection("Labels");
        {
            PidoToggle("Name","", &g_espName);
            if(g_espName) PidoSliderFloat("Name size","", &g_espNameSize, 10.f, 18.f, "%.1f");
            PidoToggle("Health","", &g_espHealth);
            if(g_espHealth){
                const char* hStyles[]={"Gradient","Solid"};
                PidoCombo("Health style","", &g_espHealthStyle, hStyles, 2);
                if(g_espHealthStyle==0){
                    PidoColorEdit4("HP full","", g_espHealthGradientCol1);
                    PidoColorEdit4("HP empty","", g_espHealthGradientCol2);
                }
            }
            PidoToggle("Distance","", &g_espDist);
            PidoToggle("Money","", &g_espMoney);
            PidoToggle("Weapon","", &g_espWeapon);
            PidoToggle("Weapon icon","", &g_espWeaponIcon);
        }
        PidoSection("Extra");
        {
            PidoToggle("Only visible","", &g_espOnlyVis);
            PidoToggle("Teammates","", &g_espShowTeam);
            PidoToggle("Head dot","", &g_espHeadDot);
            PidoToggle("OOF arrows","", &g_espOof);
            if(g_espOof) PidoSliderFloat("OOF size","", &g_espOofSize, 12.f, 48.f, "%.0f");
            PidoToggle("Lines","", &g_espLines);
            if(g_espLines){ const char* lineAnchors[]={"Top","Middle","Bottom"}; PidoCombo("Line anchor","", &g_espLineAnchor, lineAnchors, 3); }
            PidoToggle("Ammo bar","", &g_espAmmo);
            if(g_espAmmo){
                const char* aStyles[]={"Gradient","Solid"};
                PidoCombo("Ammo style","", &g_espAmmoStyle, aStyles, 2);
                if(g_espAmmoStyle==0){
                    PidoColorEdit4("Ammo empty","", g_espAmmoCol1);
                    PidoColorEdit4("Ammo full","", g_espAmmoCol2);
                }
            }
            PidoToggle("Damage numbers","", &g_damageFloatersEnabled);
            if(g_damageFloatersEnabled){
                PidoSliderFloat("Dmg duration","", &g_damageFloaterDuration, 0.35f, 2.f, "%.2f");
                PidoSliderFloat("Dmg scale","", &g_damageFloaterScale, 0.5f, 2.f, "%.2f");
                const char* anchItems[]={"Head","Chest"};
                PidoCombo("Dmg anchor","", &g_damageFloaterAnchor, anchItems, 2);
            }
        }
        EndPidoGroup();

        float effectsH = contentH * 0.58f - gap * 0.5f;
        float soundH   = contentH - effectsH - gap;

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_effects", "Effects", {childW, effectsH});
        PidoToggle("No flash","", &g_noFlash);
        PidoToggle("No smoke","", &g_noSmoke);
        PidoToggle("Glow","", &g_glowEnabled);
        if(g_glowEnabled){
            PidoColorEdit4("Glow enemy","", g_glowEnemyCol);
            PidoColorEdit4("Glow team","", g_glowTeamCol, ImGuiColorEditFlags_NoAlpha);
            PidoSliderFloat("Glow alpha","", &g_glowAlpha, 0.2f, 1.0f, "%.2f");
        }
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY + effectsH + gap});
        BeginPidoGroup("##g_sound", "Sound", {childW, soundH});
        PidoToggle("Sound pings","", &g_soundEnabled);
        if(g_soundEnabled){
            PidoSliderFloat("Scale","", &g_soundPuddleScale, 0.5f, 2.5f, "%.2f");
            PidoSliderFloat("Alpha","", &g_soundPuddleAlpha, 0.2f, 1.5f, "%.2f");
            PidoToggle("Enemy","", &g_soundBlipEnemy);
            PidoToggle("Teammates","", &g_soundBlipTeam);
            PidoColorEdit4("Color","", g_soundBlipCol);
        }
        EndPidoGroup();
    }else if(g_activeTab==2){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_world", "World", {childW, contentH});
        PidoToggle("Sky color","", &g_skyColorEnabled);
        if(g_skyColorEnabled) PidoColorEdit4("Sky##col","", g_skyColor);
        PidoToggle("World color","Tint map/props (scene draw)", &g_worldColorEnabled);
        if(g_worldColorEnabled) PidoColorEdit4("World tint (multiply)","", g_worldColor);
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_part", "Particles", {childW, contentH});
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
        EndPidoGroup();
    }else if(g_activeTab==3){
        float movH   = grpH(2);
        float radH   = grpH(1);
        float hudH   = contentH - movH - radH - gap * 2.f;
        if (hudH < 40.f * s) hudH = 40.f * s;

        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_move", "Movement", {childW, movH});
        PidoToggle("Bunny hop","", &g_bhopEnabled);
        PidoToggle("Auto strafe","", &g_strafeEnabled);
        EndPidoGroup();

        ImGui::SetCursorPos({contentX, contentY + movH + gap});
        BeginPidoGroup("##g_radar", "Radar", {childW, radH});
        PidoToggle("In-game radar","", &g_radarIngame);
        EndPidoGroup();

        ImGui::SetCursorPos({contentX, contentY + movH + radH + gap * 2.f});
        BeginPidoGroup("##g_hud", "HUD", {childW, hudH});
        PidoToggle("Bomb timer","", &g_bombTimerEnabled);
        PidoToggle("Watermark","", &g_watermarkEnabled);
        PidoToggle("Spectator list","", &g_spectatorListEnabled);
        PidoToggle("Keybinds","", &g_keybindsEnabled);
        EndPidoGroup();

        float viewH   = grpH(2);
        float configH = contentH - viewH - gap;
        if (configH < 40.f * s) configH = 40.f * s;

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_view", "View", {childW, viewH});
        PidoToggle("FOV changer","", &g_fovEnabled);
        PidoSliderFloat("FOV","", &g_fovValue, 70.f, 130.f, "%.0f");
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY + viewH + gap});
        BeginPidoGroup("##g_config", "Config", {childW, configH});
        PidoSliderFloat("Menu opacity","", &g_menuOpacity, 0.6f, 1.0f, "%.2f");
        PidoSliderFloat("Menu anim","", &g_menuAnimSpeed, 4.f, 20.f, "%.1f");
        PidoSliderFloat("ESP scale","", &g_espScale, 0.7f, 1.5f, "%.2f");
        PidoSliderFloat("Menu scale","", &g_uiScale, 0.85f, 1.6f, "%.2f");
        PidoInputText("Config name","", g_configName, sizeof(g_configName));
        static int g_configListSel = -1;
        if(!g_configList.empty()){
            std::vector<const char*> cfgPtrs;
            cfgPtrs.reserve(g_configList.size());
            for(auto& c : g_configList) cfgPtrs.push_back(c.c_str());
            if(g_configListSel < 0 || g_configListSel >= (int)cfgPtrs.size()) g_configListSel = 0;
            if(PidoCombo("Config list##cfg", "", &g_configListSel, (const char* const*)cfgPtrs.data(), (int)cfgPtrs.size())){
                strncpy_s(g_configName, sizeof(g_configName), g_configList[g_configListSel].c_str(), _TRUNCATE);
            }
        }
        float btnW = (ImGui::GetContentRegionAvail().x - 8.f*s) / 4.f;
        if(PidoButton("Save",    ImVec2(btnW, 0))){ if(SaveConfig(g_configName)) RefreshConfigList(); }
        ImGui::SameLine(0, 4.f*s);
        if(PidoButton("Load",    ImVec2(btnW, 0))) LoadConfig(g_configName);
        ImGui::SameLine(0, 4.f*s);
        if(PidoButton("Refresh", ImVec2(btnW, 0))) RefreshConfigList();
        ImGui::SameLine(0, 4.f*s);
        if(PidoButton("Reset",   ImVec2(btnW, 0))) ApplyDefaults();
        EndPidoGroup();
    }

    ImGui::PopStyleVar();

    if(font::bold) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

static void DrawKeybindsWindow(){
    if(!g_keybindsEnabled || !g_keybindsWindowOpen) return;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(16/255.f,16/255.f,16/255.f,245/255.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(64/255.f,64/255.f,64/255.f,220/255.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
    ImGui::SetNextWindowSize({320.f,300.f},ImGuiCond_Always);
    ImGui::Begin("Keybinds",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    wdl->AddRect({wp.x+1.f,wp.y+1.f},{wp.x+ws.x-1.f,wp.y+ws.y-1.f}, IM_COL32(8,8,8,210), 6.f, 0, 1.f);
    ImVec2 cr0 = ImGui::GetWindowContentRegionMin();
    float cw = ImGui::GetWindowContentRegionMax().x - cr0.x;
    float yTop = wp.y + cr0.y + 2.f;
    float gx0 = wp.x + cr0.x + 2.f;
    float gq1 = gx0 + cw * 0.33f;
    float gq2 = gx0 + cw * 0.66f;
    float gx1 = wp.x + cr0.x + cw - 2.f;
    wdl->AddRectFilledMultiColor({gx0,yTop},{gq1,yTop+3.f},
        IM_COL32(108,132,188,220), IM_COL32(174,122,190,220), IM_COL32(174,122,190,220), IM_COL32(108,132,188,220));
    wdl->AddRectFilledMultiColor({gq1,yTop},{gq2,yTop+3.f},
        IM_COL32(174,122,190,220), IM_COL32(194,166,118,220), IM_COL32(194,166,118,220), IM_COL32(174,122,190,220));
    wdl->AddRectFilledMultiColor({gq2,yTop},{gx1,yTop+3.f},
        IM_COL32(194,166,118,220), IM_COL32(116,168,148,220), IM_COL32(116,168,148,220), IM_COL32(194,166,118,220));
    KeyBindWidget("Aimbot key",&g_aimbotKey);
    KeyBindWidget("Triggerbot key",&g_tbKey);
    KeyBindWidget("DT key",&g_dtKey);
    KeyBindWidget("Strafe key",&g_strafeKey);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

static constexpr int MAX_PARTICLES = 1500;

static void UpdateAndDrawParticles(float dt,float sw,float sh){
    ImDrawList*dl=ImGui::GetBackgroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    if(g_pendingKillParticles && vm && g_killEffectEnabled){
        ImU32 accentCol=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),230);
        for(int i=0;i<67 && (int)g_particles.size()<MAX_PARTICLES;i++){
            Particle p{}; p.type=4; p.color=accentCol;
            p.worldPos=g_lastKillEffectPos; p.is3D=true;
            float theta=Randf(0.f,6.2831853f); float phi=Randf(0.2f,1.57f);
            float spd=Randf(80.f,180.f); float vx=spd*sinf(phi)*cosf(theta);
            float vy=spd*sinf(phi)*sinf(theta); float vz=Randf(40.f,120.f);
            p.worldVel={vx,vy,vz}; p.size=Randf(1.5f,4.f); p.maxlife=Randf(1.2f,2.5f);
            p.lifetime=p.maxlife; p.phase=Randf(0.f,6.28f); p.rot=Randf(0.f,6.28f); p.spin=Randf(-2.f,2.f);
            g_particles.push_back(p);
        }
        g_pendingKillParticles=false;
    }
    static float snowAcc=0.f,sakuraAcc=0.f,starAcc=0.f;
    int snowRate=(g_snowDensity==0?25:(g_snowDensity==1?60:120));
    if(g_snowEnabled){snowAcc+=dt*(float)snowRate;}
    if(g_sakuraEnabled){sakuraAcc+=dt*40.f;}
    if(g_starsEnabled){starAcc+=dt*12.f;}
    bool use3D = (vm != nullptr) && g_particlesWorld && (g_esp_count > 0 || (g_localOrigin.x*g_localOrigin.x + g_localOrigin.y*g_localOrigin.y + g_localOrigin.z*g_localOrigin.z) > 100.f);
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
        }else if(p.type==3){
            p.worldVel = {0.f,0.f,0.f};
        }else{
            p.worldVel = {Randf(-5.f,5.f), Randf(-5.f,5.f), Randf(-70.f,-30.f)};
        }
    };

    auto spawn=[&](int count,int type){
        for(int i=0;i<count;i++){
            if((int)g_particles.size() >= MAX_PARTICLES) break;
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
            else if(type==3)p.color=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),200);
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
            if(p.type==2 || p.type==3){}
            if(p.type==4){p.worldVel.z -= 12.f * dt;}
            if(p.type==5){
                p.worldVel.z -= 540.f * dt;
                p.worldVel.x *= (1.f - 1.4f * dt);
                p.worldVel.y *= (1.f - 1.4f * dt);
            }

            float baseZ = g_localOrigin.z + worldFloor;
            float topZ = baseZ + worldHeight;
            if(p.type==4){
                if(p.worldPos.z < baseZ) p.lifetime = 0.f;
            }else if(p.type==5){
                if(p.worldPos.z < baseZ - 10.f) p.lifetime = 0.f;
            }else if(p.worldPos.z < baseZ){
                p.worldPos.z = topZ + Randf(0.f, worldHeight*0.15f);
                p.worldPos.x = g_localOrigin.x;
                p.worldPos.y = g_localOrigin.y;
                spawnWorld(p);
            }
            if(p.type!=4 && p.type!=5){
            float dx = p.worldPos.x - g_localOrigin.x;
            float dy = p.worldPos.y - g_localOrigin.y;
            float dist2 = dx*dx + dy*dy;
            float maxR = worldRadius * 1.35f;
            if(dist2 > maxR*maxR){
                spawnWorld(p);
            }
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
        }else if(p.type==4){
            ImU32 soft = WithAlpha(p.color, alpha*0.4f);
            ImU32 core = WithAlpha(p.color, alpha);
            dl->AddCircleFilled({x,y},size*1.4f,soft,12);
            dl->AddCircleFilled({x,y},size,core,12);
        }else if(p.type==5){
            ImU32 soft = WithAlpha(p.color, alpha*0.55f);
            ImU32 core = WithAlpha(p.color, alpha);
            dl->AddCircleFilled({x,y}, size * 2.0f, soft, 10);
            dl->AddCircleFilled({x,y}, size * 0.85f, core, 10);
        }else{
            float tw = 0.4f + 0.6f * (sinf(p.phase*2.f + p.rot)*0.5f + 0.5f);
            ImU32 col = WithAlpha(p.color, alpha * tw);
            dl->AddCircleFilled({x,y},size*0.7f,col,10);
            dl->AddLine({x-size,y},{x+size,y},WithAlpha(p.color, alpha*0.35f),1.f);
            dl->AddLine({x,y-size},{x,y+size},WithAlpha(p.color, alpha*0.35f),1.f);
        }
    }
}

static void DrawDamageFloaters(float sw, float sh){
    if(g_damageFloaters.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    ImFont* fb = font::bold ? font::bold : ImGui::GetFont();
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    UINT64 now = GetTickCount64();

    for(auto it = g_damageFloaters.begin(); it != g_damageFloaters.end(); ){
        float elapsed = (float)(now - it->spawnMs) / 1000.f;
        if(elapsed >= it->duration){
            it = g_damageFloaters.erase(it);
            continue;
        }
        float u = elapsed / it->duration;

        float worldZOff = u * 55.f * g_damageFloaterScale;
        Vec3 worldPos{it->wx + it->randOffX, it->wy, it->wz + worldZOff};

        float sx, sy;
        bool onScreen = vm && WorldToScreen(vm, worldPos, sw, sh, sx, sy);
        if(!onScreen){ ++it; continue; }

        float entryT = Clampf(elapsed / 0.12f, 0.f, 1.f);
        float entryScale = 0.55f + 0.45f * (entryT * entryT * (3.f - 2.f * entryT));
        float fadeT = Clampf((u - 0.65f) / 0.35f, 0.f, 1.f);
        float alpha = 1.f - fadeT * fadeT;

        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", it->damage);

        float baseSize = Clampf(14.f + (float)it->damage * 0.08f, 14.f, 24.f) * g_damageFloaterScale;
        float fs = baseSize * entryScale;
        ImVec2 ts = fb->CalcTextSizeA(fs, FLT_MAX, 0.f, buf);

        float tx = sx - ts.x * 0.5f;
        float ty = sy - ts.y * 0.5f;

        ImU32 textCol;
        if(it->damage >= 100)      textCol = IM_COL32(255,80,80,(int)(255*alpha));
        else if(it->damage >= 90)  textCol = IM_COL32(255,210,50,(int)(255*alpha));
        else                       textCol = IM_COL32(255,255,255,(int)(255*alpha));

        for(int sh2=2;sh2>=1;sh2--){
            float o=(float)sh2;
            dl->AddText(fb, fs, {tx+o,ty+o}, IM_COL32(0,0,0,(int)(190*alpha)), buf);
        }
        dl->AddText(fb, fs, {tx, ty}, textCol, buf);
        ++it;
    }
}

static void DrawKillEffect(float sw, float sh){
    if(!g_killEffectEnabled || !g_lastKillEffectTime) return;
    UINT64 elapsed = GetTickCount64() - g_lastKillEffectTime;
    float durMs = g_killEffectDuration * 1000.f;
    if(elapsed >= (UINT64)durMs) { g_lastKillEffectTime = 0; return; }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    float t = 1.f - (float)elapsed / durMs;
    int alpha = (int)(80 * t);
    dl->AddRectFilled({0,0},{sw,sh}, IM_COL32(180,60,80,alpha));
}

static void DrawWatermark(float sw){
    if(!g_watermarkEnabled)return;
    ImDrawList* dl = ImGui::GetForegroundDrawList(); if(!dl) return;
    ImFont* fBold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* fReg  = font::regular ? font::regular : ImGui::GetFont();
    ImGuiIO& io   = ImGui::GetIO();

    SYSTEMTIME st{}; GetLocalTime(&st);
    char timeBuf[16]; std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", st.wHour, st.wMinute);
    const float fps    = io.Framerate;
    const float padX   = 8.f;
    const float padY   = 4.f;
    const float dotR   = 1.5f;
    const char* label = "litware";
    char fpsBuf[16]; std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", fps);
    const char* pingBuf = "ping 0";

    ImVec2 szLabel = fBold->CalcTextSizeA(12.f, FLT_MAX, 0.f, label);
    ImVec2 szFps   = fReg->CalcTextSizeA(11.f, FLT_MAX, 0.f, fpsBuf);
    ImVec2 szTime  = fReg->CalcTextSizeA(11.f, FLT_MAX, 0.f, timeBuf);
    ImVec2 szPing  = fReg->CalcTextSizeA(11.f, FLT_MAX, 0.f, pingBuf);

    float contentW = szLabel.x + 8.f + szFps.x + 8.f + szTime.x + 8.f + szPing.x + 20.f;
    float barW = (std::max)(170.f, contentW + padX * 2.f);
    float barH = (std::max)({szLabel.y, szFps.y, szTime.y, szPing.y}) + padY * 2.f;
    float x = sw - barW - 12.f;
    float y = 12.f;

    const ImU32 colBg = IM_COL32(16,16,16,245);
    const ImU32 colBorder = IM_COL32(64,64,64,220);
    const ImU32 colInner = IM_COL32(8,8,8,210);
    const ImU32 colAccent = IM_COL32(220,220,220,255);
    const ImU32 colText = IM_COL32(220,220,220,255);
    const ImU32 colDim = IM_COL32(140,140,140,255);

    dl->AddRectFilled({x,y},{x+barW,y+barH}, colBg, 2.f);
    dl->AddRect({x,y},{x+barW,y+barH}, colBorder, 2.f, 0, 1.f);
    dl->AddRect({x+1.f,y+1.f},{x+barW-1.f,y+barH-1.f}, colInner, 2.f, 0, 1.f);
    float gx0 = x + 2.f;
    float gy0 = y + 1.f;
    float gy1 = y + 3.f;
    float gq1 = x + barW * 0.33f;
    float gq2 = x + barW * 0.66f;
    float gx1 = x + barW - 2.f;
    dl->AddRectFilledMultiColor({gx0,gy0},{gq1,gy1},
        IM_COL32(108,132,188,220), IM_COL32(174,122,190,220), IM_COL32(174,122,190,220), IM_COL32(108,132,188,220));
    dl->AddRectFilledMultiColor({gq1,gy0},{gq2,gy1},
        IM_COL32(174,122,190,220), IM_COL32(194,166,118,220), IM_COL32(194,166,118,220), IM_COL32(174,122,190,220));
    dl->AddRectFilledMultiColor({gq2,gy0},{gx1,gy1},
        IM_COL32(194,166,118,220), IM_COL32(116,168,148,220), IM_COL32(116,168,148,220), IM_COL32(194,166,118,220));

    float cx = x + padX;
    float midY = y + barH*0.5f;
    dl->AddText(fBold, 12.f, {cx, midY - szLabel.y*0.5f}, colAccent, label);
    cx += szLabel.x + 8.f;
    dl->AddCircleFilled({cx, midY}, dotR, colDim); cx += 6.f;
    dl->AddText(fReg, 11.f, {cx, midY - szFps.y*0.5f}, colText, fpsBuf);
    cx += szFps.x + 8.f;
    dl->AddCircleFilled({cx, midY}, dotR, colDim); cx += 6.f;
    dl->AddText(fReg, 11.f, {cx, midY - szTime.y*0.5f}, colDim, timeBuf);
    cx += szTime.x + 8.f;
    dl->AddCircleFilled({cx, midY}, dotR, colDim); cx += 6.f;
    dl->AddText(fReg, 11.f, {cx, midY - szPing.y*0.5f}, colDim, pingBuf);
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
    ImU32 col = IM_COL32((int)(g_fovCircleCol[0]*255),(int)(g_fovCircleCol[1]*255),(int)(g_fovCircleCol[2]*255),(int)(g_fovCircleCol[3]*255));
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

static void DrawCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    float lx=(r-l)*0.25f,ly=(b-t)*0.25f;
    dl->AddLine({l,t},{l+lx,t},col,thick);dl->AddLine({l,t},{l,t+ly},col,thick);
    dl->AddLine({r,t},{r-lx,t},col,thick);dl->AddLine({r,t},{r,t+ly},col,thick);
    dl->AddLine({l,b},{l+lx,b},col,thick);dl->AddLine({l,b},{l,b-ly},col,thick);
    dl->AddLine({r,b},{r-lx,b},col,thick);dl->AddLine({r,b},{r,b-ly},col,thick);
}
static void DrawOutlineBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    dl->AddRect({l-1.f,t-1.f},{r+1.f,b+1.f},IM_COL32(0,0,0,(int)(235)),0.f,0,thick+1.f);
    dl->AddRect({l+1.f,t+1.f},{r-1.f,b-1.f},IM_COL32(0,0,0,(int)(130)),0.f,0,thick);
    dl->AddRect({l,t},{r,b},col,0.f,0,thick);
}
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

static bool DrawSkeletonBones(ImDrawList*dl,const ESPEntry& e,ImU32 col,ImU32 shadowCol=0){
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
    float thick = Clampf(g_skeletonThick, 0.5f, 3.5f) * Clampf(g_espScale, 0.7f, 1.5f);
    const float shadowOff = (shadowCol != 0) ? 1.5f : 0.f;
    auto line=[&](bool ha,const Vec3& a,bool hb,const Vec3& b){
        if(!ha||!hb) return;
        float ax,ay,bx,by;
        if(WorldToScreen(vm,a,g_esp_screen_w,g_esp_screen_h,ax,ay)&&WorldToScreen(vm,b,g_esp_screen_w,g_esp_screen_h,bx,by)){
            if(!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by)) return;
            if(shadowCol!=0){ dl->AddLine({ax+shadowOff,ay+shadowOff},{bx+shadowOff,by+shadowOff},shadowCol,thick+0.5f); }
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
    if(!g_espEnabled)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    uintptr_t entityList = g_client ? Rd<uintptr_t>(g_client+offsets::client::dwEntityList) : 0;
    ImFont* espFont = GetEspFont();
    auto drawOne=[&](const ESPEntry& e, float alphaMul){
        if(!e.valid||e.distance>g_espMaxDist)return;
        bool enemy=(e.team!=g_esp_local_team);float*ecol=enemy?g_espEnemyCol:g_espTeamCol;
        if(!enemy && !g_espShowTeam) return;
        float s = Clampf(g_espScale, 0.7f, 1.5f);
        float boxThick = g_espBoxThick * s;
        float alpha=(e.visible?1.f:0.5f)*alphaMul;float bl=e.box_l,bt2=e.box_t,br=e.box_r,bb=e.box_b;
        float bw=br-bl,bh=bb-bt2,cx=(bl+br)*0.5f;
        ImU32 boxCol=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(alpha*255));
        ImU32 dimCol=IM_COL32(160,160,170,(int)(180*alpha));
        float belowY = bb + 3.f * s;
        const float boxRnd = 3.5f * s;
        if(g_espDrawBox){
            dl->AddRectFilled({bl+4.f,bt2+4.f},{br+4.f,bb+4.f},IM_COL32(0,0,0,(int)(34*alpha)),boxRnd+1.5f);
            dl->AddRectFilled({bl+2.f,bt2+2.f},{br+2.f,bb+2.f},IM_COL32(0,0,0,(int)(48*alpha)),boxRnd+0.5f);
            for(int g=3;g>=1;g--){
                int r=(boxCol>>IM_COL32_R_SHIFT)&0xFF,g_=(boxCol>>IM_COL32_G_SHIFT)&0xFF,b=(boxCol>>IM_COL32_B_SHIFT)&0xFF;
                int ga=(g==3)?30:(g==2)?16:7;
                dl->AddRect({bl-(float)g,bt2-(float)g},{br+(float)g,bb+(float)g},IM_COL32(r,g_,b,(int)(ga*alpha)),0.f,0,1.15f);
            }
            if(g_espBoxStyle==0){
                DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
                DrawCornerBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==1){
                dl->AddRect({bl,bt2},{br,bb},IM_COL32(0,0,0,(int)(200*alpha)),boxRnd,0,boxThick+1.0f);
                dl->AddRect({bl,bt2},{br,bb},boxCol,boxRnd,0,boxThick);
            }
            else if(g_espBoxStyle==2){
                dl->AddRectFilled({bl,bt2},{br,bb},IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(42*alpha)),boxRnd);
                DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
                DrawCornerBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==3){
                DrawOutlineBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==4){
                DrawCoalBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
                DrawCoalBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==5){
                DrawOutlineCoalBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
        }
        if(g_espHealth&&e.health>0){
            float fill=Clampf((float)e.health/100.f,0.f,1.f),barW=5.f*s,barOff=8.f*s,barRound=4.f*s;
            ImU32 hbCol=HealthCol(e.health);
            if(g_espHealthStyle==1)hbCol=IM_COL32(60,200,120,(int)(220*alpha));
            if(g_espHealthStyle==2)hbCol=boxCol;
            ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
            ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
            ImU32 cFill = LerpColor(c2, c1, fill);
            ImU32 bgDark=IM_COL32(8,8,12,(int)(220*alpha));
            ImU32 borderCol=IM_COL32(40,40,50,(int)(180*alpha));
            float bx=0.f, byBar=0.f;
            bool useGradientGlow=(g_espHealthStyle==0);
            if(g_espHealthPos==0){
                bx=bl-barOff-barW;
                float fillTop = bt2+bh*(1.f-fill);
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    if(useGradientGlow){
                        ImU32 c1g=IM_COL32((c1>>IM_COL32_R_SHIFT)&0xFF,(c1>>IM_COL32_G_SHIFT)&0xFF,(c1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        ImU32 c2g=IM_COL32((c2>>IM_COL32_R_SHIFT)&0xFF,(c2>>IM_COL32_G_SHIFT)&0xFF,(c2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        dl->AddRectFilledMultiColor({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f}, c1g,c1g, c2g,c2g);
                    }else
                        dl->AddRectFilled({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bx-1.f,bt2-1.f},{bx+barW+1.f,bb+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bx,bt2},{bx+barW,bb},bgDark,barRound);
                if(g_espHealthStyle==0){
                    dl->AddRectFilledMultiColor({bx,fillTop},{bx+barW,bb}, cFill,cFill, c2,c2);
                }else{
                    dl->AddRectFilled({bx,fillTop},{bx+barW,bb},hbCol,barRound);
                }
                byBar=fillTop;
            }else if(g_espHealthPos==2){
                bx=br+barOff;
                float fillTop = bt2+bh*(1.f-fill);
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    if(useGradientGlow){
                        ImU32 c1g=IM_COL32((c1>>IM_COL32_R_SHIFT)&0xFF,(c1>>IM_COL32_G_SHIFT)&0xFF,(c1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        ImU32 c2g=IM_COL32((c2>>IM_COL32_R_SHIFT)&0xFF,(c2>>IM_COL32_G_SHIFT)&0xFF,(c2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        dl->AddRectFilledMultiColor({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f}, c1g,c1g, c2g,c2g);
                    }
                    else
                        dl->AddRectFilled({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bx-1.f,bt2-1.f},{bx+barW+1.f,bb+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bx,bt2},{bx+barW,bb},bgDark,barRound);
                if(g_espHealthStyle==0){
                    dl->AddRectFilledMultiColor({bx,fillTop},{bx+barW,bb}, cFill,cFill, c2,c2);
                }else{
                    dl->AddRectFilled({bx,fillTop},{bx+barW,bb},hbCol,barRound);
                }
                byBar=fillTop;
            }
else if(g_espHealthPos==1){
                float by=bt2-barOff-barW;
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    if(useGradientGlow){
                        ImU32 c1g=IM_COL32((c1>>IM_COL32_R_SHIFT)&0xFF,(c1>>IM_COL32_G_SHIFT)&0xFF,(c1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        ImU32 c2g=IM_COL32((c2>>IM_COL32_R_SHIFT)&0xFF,(c2>>IM_COL32_G_SHIFT)&0xFF,(c2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        dl->AddRectFilledMultiColor({bl-o-1.f,by-o-1.f},{br+o+1.f,by+barW+o+1.f}, c2g,c1g, c1g,c2g);
                    }else
                        dl->AddRectFilled({bl-o-1.f,by-o-1.f},{br+o+1.f,by+barW+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bl-1.f,by-1.f},{br+1.f,by+barW+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bl,by},{br,by+barW},bgDark,barRound);
                if(g_espHealthStyle==0){
                    ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
                    ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
                    dl->AddRectFilledMultiColor({bl,by},{bl+bw*fill,by+barW}, c2,cFill, cFill,c2);
                }else{
                    dl->AddRectFilled({bl,by},{bl+bw*fill,by+barW},hbCol,barRound);
                }
            }
            if(e.health<100){
                char hpBuf[16]; std::snprintf(hpBuf,sizeof(hpBuf),"%d",e.health);
                ImFont* font=espFont;
                float fsz=(g_espHealthPos==1)?(10.f*s):(g_espNameSize*0.85f*s);
                ImVec2 ts=font->CalcTextSizeA(fsz,FLT_MAX,0.f,hpBuf);
                float tx=bx+(g_espHealthPos==0?barW+2.f:(g_espHealthPos==2?-ts.x-2.f:bl+bw*0.5f-ts.x*0.5f));
                float ty=(g_espHealthPos==1)?bt2-barOff-barW-ts.y-1.f:byBar-ts.y*0.5f;
                dl->AddText(font,fsz,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(180*alpha)),hpBuf);
                dl->AddText(font,fsz,{tx,ty},IM_COL32(255,255,255,(int)(220*alpha)),hpBuf);
            }
        }
        if(g_espHeadDot){
            float dotR = bw*0.16f;
            if(dotR < 7.f * s) dotR = 7.f * s;
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
            int r=(int)(ecol[0]*255),g_=(int)(ecol[1]*255),b=(int)(ecol[2]*255);
            for(int gl=3;gl>=1;gl--) dl->AddLine({sx+(float)gl,sy+(float)gl},{e.feet_x+(float)gl,e.feet_y+(float)gl},IM_COL32(0,0,0,(int)(48*alpha/(float)gl)),(1.35f+(float)gl*0.32f)*s);
            dl->AddLine({sx,sy},{e.feet_x,e.feet_y},IM_COL32(r,g_,b,(int)(150*alpha)),1.05f*s);
            dl->AddLine({sx,sy},{e.feet_x,e.feet_y},IM_COL32(255,255,255,(int)(36*alpha)),0.5f*s);
        }
        if(g_espSkeleton){
            ImU32 scol=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(180*alpha));
            ImU32 sShadow=IM_COL32(0,0,0,(int)(120*alpha));
            bool drew = DrawSkeletonBones(dl, e, scol, sShadow);
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
                ImU32 scolShadow=IM_COL32(0,0,0,(int)(90*alpha));
                auto skLine=[&](ImVec2 a, ImVec2 b){ for(int s=2;s>=1;s--) dl->AddLine({a.x+(float)s,a.y+(float)s},{b.x+(float)s,b.y+(float)s},scolShadow,1.8f+(float)s*0.4f); dl->AddLine(a,b,scol,1.0f); };
                skLine(head, neck); skLine(neck, pelvis); skLine(neck, lShoulder); skLine(neck, rShoulder);
                skLine(lShoulder, lElbow); skLine(rShoulder, rElbow); skLine(lElbow, lHand); skLine(rElbow, rHand);
                skLine(pelvis, lHip); skLine(pelvis, rHip); skLine(lHip, lKnee); skLine(rHip, rKnee); skLine(lKnee, lFoot); skLine(rKnee, rFoot);
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
            ImFont* font = espFont;
            float nameSize = g_espNameSize * s;
            ImVec2 ts=font->CalcTextSizeA(nameSize,FLT_MAX,0.f,e.name);
            float tx=cx-ts.x*0.5f,ty=bt2-ts.y-5.f*s;
            dl->AddText(font,nameSize,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(180*alpha)),e.name);
            dl->AddText(font,nameSize,{tx,ty},IM_COL32(240,240,245,(int)(alpha*255)),e.name);
        }
        if(e.planting||e.flashed||e.scoped||e.defusing||e.hasBomb||e.hasKits){
            ImFont* sf = espFont;
            float tagX = br + 8.f * s;
            float tagY = bt2;
            float tagSize = 10.f * s;
            ImU32 tagCol = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(220*alpha));
            auto drawTag=[&](const char* t){
                ImVec2 tsz = sf->CalcTextSizeA(tagSize, FLT_MAX, 0.f, t);
                float padX = 5.f * s, padY = 2.f * s, chipR = 3.f * s;
                float px = tagX, py = tagY;
                float chipW = tsz.x + padX * 2.f, chipH = tsz.y + padY * 2.f;
                dl->AddRectFilled({px,py},{px+chipW,py+chipH},IM_COL32(14,16,22,(int)(210*alpha)),chipR);
                dl->AddRect({px,py},{px+chipW,py+chipH},IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(72*alpha)),chipR,0,1.f);
                float ttx = px + padX, tty = py + padY;
                for(int sh=2;sh>=1;sh--) dl->AddText(sf,tagSize,{ttx+(float)sh,tty+(float)sh},IM_COL32(0,0,0,(int)(85*alpha/(float)sh)),t);
                dl->AddText(sf,tagSize,{ttx+1.f,tty+1.f},IM_COL32(0,0,0,(int)(130*alpha)),t);
                dl->AddText(sf,tagSize,{ttx,tty},tagCol,t);
                tagY += chipH + 3.f * s;
            };
            if(e.planting){ drawTag("Planting"); }
            if(e.scoped){ drawTag("Scoped"); }
            if(e.flashed){ drawTag("Flashed"); }
            if(e.defusing){ drawTag("Defusing"); }
            if(e.hasBomb){ drawTag("Bomb"); }
            if(e.hasKits){ drawTag("Kits"); }
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
            float barH = 5.f * s;
            float barRound = 4.f * s;
            ImU32 ammoBg = IM_COL32((int)(g_espAmmoCol1[0]*255),(int)(g_espAmmoCol1[1]*255),(int)(g_espAmmoCol1[2]*255),(int)(230*alpha));
            ImU32 ammoBorder = IM_COL32(45,45,65,(int)(200*alpha));
            ImU32 ammoOuter = IM_COL32(0,0,0,(int)(180*alpha));
            ImU32 ammoC1=IM_COL32((int)(g_espAmmoCol1[0]*255),(int)(g_espAmmoCol1[1]*255),(int)(g_espAmmoCol1[2]*255),(int)(245*alpha));
            ImU32 ammoC2=IM_COL32((int)(g_espAmmoCol2[0]*255),(int)(g_espAmmoCol2[1]*255),(int)(g_espAmmoCol2[2]*255),(int)(245*alpha));
            for(int g=4;g>=1;g--){
                float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                if(g_espAmmoStyle==0){
                    ImU32 ag1=IM_COL32((ammoC1>>IM_COL32_R_SHIFT)&0xFF,(ammoC1>>IM_COL32_G_SHIFT)&0xFF,(ammoC1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                    ImU32 ag2=IM_COL32((ammoC2>>IM_COL32_R_SHIFT)&0xFF,(ammoC2>>IM_COL32_G_SHIFT)&0xFF,(ammoC2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                    dl->AddRectFilledMultiColor({bl-o-1.f,belowY-o-1.f},{br+o+1.f,belowY+barH+o+1.f}, ag1,ag2, ag2,ag1);
                }else{
                    ImU32 ag=IM_COL32((ammoC2>>IM_COL32_R_SHIFT)&0xFF,(ammoC2>>IM_COL32_G_SHIFT)&0xFF,(ammoC2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                    dl->AddRectFilled({bl-o-1.f,belowY-o-1.f},{br+o+1.f,belowY+barH+o+1.f},ag,barRound+o);
                }
            }
            dl->AddRectFilled({bl-2.f,belowY-2.f},{br+2.f,belowY+barH+2.f},ammoOuter,barRound+2.f);
            dl->AddRectFilled({bl-1.f,belowY-1.f},{br+1.f,belowY+barH+1.f},ammoBorder,barRound+1.f);
            dl->AddRectFilled({bl,belowY},{br,belowY+barH},ammoBg,barRound);
            if(g_espAmmoStyle==0){
                dl->AddRectFilledMultiColor({bl,belowY},{bl+bw*frac,belowY+barH}, ammoC1,ammoC2, ammoC2,ammoC1);
            }else{
                ImU32 ammoBarCol = IM_COL32((int)(g_espAmmoCol2[0]*255),(int)(g_espAmmoCol2[1]*255),(int)(g_espAmmoCol2[2]*255),(int)(245*alpha));
                dl->AddRectFilled({bl,belowY},{bl+bw*frac,belowY+barH},ammoBarCol,barRound);
            }
            if(frac > 0.01f && frac < 1.f) dl->AddRect({bl+bw*frac,belowY},{bl+bw*frac+0.5f,belowY+barH},IM_COL32(255,255,255,(int)(80*alpha)),0.f,0,1.f);
            belowY += barH + 5.f * s;
        }
        if((g_espWeapon||g_espWeaponIcon) && weapon){
            float iconW = 0.f;
            ImVec2 its = {0.f, 0.f};
            ImVec2 ts = {0.f, 0.f};
            float wepSize = 12.f * s;
            ImFont* textFont = espFont;
            ImFont* iconFont = nullptr;
            const char* iconText = nullptr;
            char iconBuf[5] = {};
            if(g_espWeaponIcon){
                ImFontBaked* gunBaked = font::gun_icons ? font::gun_icons->GetFontBaked(wepSize) : nullptr;
                if(gunBaked && winfo.iconChar && gunBaked->FindGlyphNoFallback(winfo.iconChar)){
                    WCharToUtf8(winfo.iconChar, iconBuf);
                    iconText = iconBuf;
                    iconFont = font::gun_icons;
                }else if(winfo.icon && winfo.icon[0]){
                    iconText = winfo.icon;
                    iconFont = textFont;
                }
            }
            if(iconText && iconFont){
                its = iconFont->CalcTextSizeA(wepSize, FLT_MAX, 0.f, iconText);
                iconW = its.x + (g_espWeapon ? 4.f * s : 0.f);
            }
            std::string wtext;
            if(g_espWeapon){
                wtext = winfo.name;
                if(!wtext.empty()){
                    ts = textFont->CalcTextSizeA(wepSize, FLT_MAX, 0.f, wtext.c_str());
                }
            }
            float blockLeft = cx - (iconW + ts.x)*0.5f;
            float iconYOffset = 0.f;
            if(iconText && iconFont && textFont && iconFont != textFont && (g_espWeapon && !wtext.empty())){
                ImFontBaked* iconBaked = iconFont->GetFontBaked(wepSize);
                ImFontBaked* textBaked = textFont->GetFontBaked(wepSize);
                if(iconBaked && textBaked){
                    float iconBaseline = iconBaked->Ascent;
                    float textBaseline = textBaked->Ascent;
                    iconYOffset = textBaseline - iconBaseline;
                }
            }
            if(iconText && iconFont){
                float itx = blockLeft;
                float ity = belowY + iconYOffset;
                ImU32 shadowCol = IM_COL32(0,0,0,(int)(140*alpha));
                dl->AddText(iconFont, wepSize, {itx+1.f,ity+1.f}, shadowCol, iconText);
                dl->AddText(iconFont, wepSize, {itx,ity}, dimCol, iconText);
            }
            if(g_espWeapon && !wtext.empty()){
                float tx = blockLeft + iconW;
                ImU32 shadowCol = IM_COL32(0,0,0,(int)(140*alpha));
                dl->AddText(textFont, wepSize, {tx+1.f,belowY+1.f}, shadowCol, wtext.c_str());
                dl->AddText(textFont, wepSize, {tx,belowY}, dimCol, wtext.c_str());
            }
            if(g_espWeaponIcon||g_espWeapon){
                float maxH = wepSize;
                if(iconText && iconFont) maxH = std::max(maxH, its.y);
                if(g_espWeapon && !wtext.empty()) maxH = std::max(maxH, ts.y);
                belowY += maxH + 2.f * s;
            }
        }
        if(g_espMoney){
            int money = GetPlayerMoney(e.controller);
            if(money > 0){
                char mbuf[32];
                std::snprintf(mbuf,sizeof(mbuf),"$%d", money);
                ImFont* infoFont = espFont;
                float infoSize = 12.f * s;
                ImVec2 ts=infoFont->CalcTextSizeA(infoSize, FLT_MAX, 0.f, mbuf);
                float sw=(float)g_esp_screen_w;
                float tx = (g_espMoneyPos==1) ? (sw - ts.x - 12.f * s) : (cx-ts.x*0.5f);
                float ty = (g_espMoneyPos==1) ? (bt2 + bh*0.5f - ts.y*0.5f) : belowY;
                dl->AddText(infoFont, infoSize, {tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(140*alpha)),mbuf);
                dl->AddText(infoFont, infoSize, {tx,ty},IM_COL32(120,220,120,(int)(220*alpha)),mbuf);
                if(g_espMoneyPos==0) belowY += ts.y + 2.f * s;
            }
        }
        if(g_espDist){
            char dbuf[32];snprintf(dbuf,sizeof(dbuf),"%.0fm",e.distance);
            ImFont* infoFont = espFont;
            float infoSize = 12.f * s;
            ImVec2 ts=infoFont->CalcTextSizeA(infoSize, FLT_MAX, 0.f, dbuf);float tx=cx-ts.x*0.5f;
            dl->AddText(infoFont, infoSize, {tx+1.f,belowY+1.f},IM_COL32(0,0,0,(int)(130*alpha)),dbuf);
            dl->AddText(infoFont, infoSize, {tx,belowY},dimCol,dbuf);
            belowY += ts.y + 2.f * s;
        }
    };

    for(int i=0;i<g_esp_count;i++) drawOne(g_esp_players[i], 1.f);
    bool inCur[65]={false};
    for(int i=0;i<g_esp_count;i++) if(g_esp_players[i].ent_index>=0&&g_esp_players[i].ent_index<=64) inCur[g_esp_players[i].ent_index]=true;
    UINT64 now=GetTickCount64();
    for(int j=1;j<=64;j++){
        if(inCur[j])continue;
        if(!g_esp_stale[j].valid)continue;
        if((now-g_esp_stale_tick[j])>=ESP_STALE_MS)continue;
        float t=(float)(now-g_esp_stale_tick[j])/(float)ESP_STALE_MS;
        drawOne(g_esp_stale[j], 1.f - t*0.35f);
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
        for(int sh=2;sh>=1;sh--){
            ImVec2 dt{(float)sh,(float)sh};
            dl->AddTriangleFilled({tip.x+dt.x,tip.y+dt.y},{left.x+dt.x,left.y+dt.y},{right.x+dt.x,right.y+dt.y},IM_COL32(0,0,0,(int)(80/(float)sh)));
        }
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
            ImFont* f = GetEspFont();
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
    if(FAILED(sc->GetDevice(__uuidof(ID3D11Device),(void**)&g_device))||!g_device)return;
    g_device->GetImmediateContext(&g_context);
    if(!g_context){g_device->Release();g_device=nullptr;bb->Release();return;}
    HRESULT hr = g_device->CreateRenderTargetView(bb,nullptr,&g_rtv);
    if(FAILED(hr)){
        UINT64 now = GetTickCount64();
        if(now - g_lastRtvFail > 2000){
            PushNotification("RTV create failed", IM_COL32(255,80,80,255));
            g_lastRtvFail = now;
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
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO&io=ImGui::GetIO();
    if(!io.Fonts){
        ImGui::DestroyContext();
        SetWindowLongPtrA(g_gameHwnd,GWLP_WNDPROC,(LONG_PTR)g_origWndProc);
        g_origWndProc=nullptr;
        if(g_rtv){g_rtv->Release();g_rtv=nullptr;}
        if(g_context){g_context->Release();g_context=nullptr;}
        if(g_device){g_device->Release();g_device=nullptr;}
        return;
    }
    io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigErrorRecoveryEnableAssert=false;  // чтобы не падало
    ImFontConfig fc{};fc.SizePixels=13.f;fc.FontDataOwnedByAtlas=false;
    font::bold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 13.f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    if(!font::bold)
        font::bold = io.Fonts->AddFontFromMemoryTTF((void*)lexend_bold, (int)sizeof(lexend_bold), 13.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    fc.SizePixels=12.f;
    font::regular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 12.f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    if(!font::regular)
        font::regular = io.Fonts->AddFontFromMemoryTTF((void*)lexend_regular, (int)sizeof(lexend_regular), 12.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    font::esp_mono = io.Fonts->AddFontFromMemoryTTF((void*)jetbrains_mono_regular, (int)sizeof(jetbrains_mono_regular), 14.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    fc.SizePixels=20.f;
    static const ImWchar icomoonRanges[] = { 0x0020, 0x00FF, 0xE000, 0xE0FF, 0 };
    font::icomoon = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 20.f, &fc, icomoonRanges);
    fc.SizePixels=15.f;
    font::icomoon_widget = io.Fonts->AddFontFromMemoryTTF((void*)icomoon_widget, (int)sizeof(icomoon_widget), 15.f, &fc, icomoonRanges);
    static const ImWchar gunRanges[] = { 0xE000, 0xE0FF, 0 };
    ImFontConfig gunCfg = fc;
    gunCfg.SizePixels = 16.f;
    gunCfg.PixelSnapH = true;
    font::gun_icons = io.Fonts->AddFontFromMemoryTTF((void*)cs2_gun_icons_ttf, (int)sizeof(cs2_gun_icons_ttf), 16.f, &gunCfg, gunRanges);
    ImFont* font = font::bold;
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
    ClipCursor(nullptr);
    if(g_context){g_context->OMSetRenderTargets(0,nullptr,nullptr);}
    if(g_gameHwnd&&g_origWndProc){
        SetWindowLongPtrA(g_gameHwnd,GWLP_WNDPROC,(LONG_PTR)g_origWndProc);
        g_origWndProc=nullptr;
    }
    if(g_imguiInitialized){
        ImGui_ImplDX11_Shutdown();
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
            Sleep(800);
            FreeLibraryAndExitThread((HMODULE)mod, 0);
            __assume(0);
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
    g_swapChain = sc;
    if(!g_firstFrame){DebugLog("[LitWare] first Present");g_firstFrame=true;}
    if(!g_imguiInitialized){InitImGui(sc);if(!g_imguiInitialized)return;}
    EnsureClientHooks();
    EnsureSceneHooks();
    DXGI_SWAP_CHAIN_DESC desc{};
    if(SUCCEEDED(sc->GetDesc(&desc))){
        UINT nw = desc.BufferDesc.Width, nh = desc.BufferDesc.Height;
        DXGI_FORMAT nf = desc.BufferDesc.Format;
        if(nw != g_bbWidth || nh != g_bbHeight || nf != g_bbFormat){
            if(g_rtv){ g_rtv->Release(); g_rtv = nullptr; }
        }
        g_bbWidth = nw;
        g_bbHeight = nh;
        g_bbFormat = nf;
    }
    if(!g_rtv){ID3D11Texture2D*bb=nullptr;
        if(g_device&&SUCCEEDED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))){
            HRESULT hr = g_device->CreateRenderTargetView(bb,nullptr,&g_rtv);
            bb->Release();
            if(FAILED(hr)){
                UINT64 now = GetTickCount64();
                if(now - g_lastRtvFail > 2000){
                    PushNotification("RTV create failed", IM_COL32(255,80,80,255));
                    g_lastRtvFail = now;
                }
            }
        }if(!g_rtv)return;
    }
    if(GetAsyncKeyState(VK_INSERT)&1)g_menuOpen=!g_menuOpen;
    if(GetAsyncKeyState(VK_END)&1){RequestUnload();return;}
    if(!g_safeMode){
        BuildESPData();BuildSpectatorList();ProcessHitEvents();UpdateBombInfo();UpdateSoundPings();
        RunNoFlash();RunNoSmoke();RunGlow();RunRadarHack();
        RunBHop();
        RunFOVChanger();
        RunAutostop();RunAimbot();RunRCS();RunStrafeHelper();RunTriggerBot();ReleaseTriggerAttack();RunDoubleTap();RunAimFireGate();
    }else{
        g_esp_count=0;g_esp_oof_count=0;
        if(g_bbWidth >= 100 && g_bbHeight >= 100){
            g_esp_screen_w = (int)g_bbWidth;
            g_esp_screen_h = (int)g_bbHeight;
        }
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    if(g_bbWidth > 0 && g_bbHeight > 0)
        io.DisplaySize = ImVec2((float)g_bbWidth, (float)g_bbHeight);
    ImGui::NewFrame();
    io.MouseDrawCursor = g_menuOpen;
    if(g_menuOpen&&g_gameHwnd){
        RECT r{}; GetWindowRect(g_gameHwnd,&r);
        ClipCursor(&r);
        POINT pt{};GetCursorPos(&pt);ScreenToClient(g_gameHwnd,&pt);
        io.MousePos={(float)pt.x,(float)pt.y};
        io.MouseDown[0]=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        io.MouseDown[1]=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;SetCursor(LoadCursor(nullptr,IDC_ARROW));
    }else{
        ClipCursor(nullptr);
        io.MouseDown[0]=false;io.MouseDown[1]=false;
    }
    float sw=(float)g_esp_screen_w, sh=(float)g_esp_screen_h;
    UpdateAndDrawParticles(io.DeltaTime, sw, sh);
    DrawMenu();
    DrawDebugConsole();
    DrawKeybindsWindow();
    if(!g_safeMode){ DrawESP(); DrawOofArrows(); DrawBombTimer(sw);
        DrawSoundPings(io.DeltaTime);
        DrawSpectatorList(sw); DrawNoCrosshair(sw, sh); DrawFovCircle(sw, sh); }
    DrawLogs(io.DeltaTime, sw, sh);
    DrawDamageFloaters(sw, sh);
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
        s_crashCount = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_crashCount++;
        g_safeMode = true;
        g_esp_count = 0; g_esp_oof_count = 0; g_espEnabled = false;
        if(g_imguiInitialized){
            __try { ImGui::EndFrame(); } __except(EXCEPTION_EXECUTE_HANDLER){}
        }
        UINT64 now = GetTickCount64();
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
    const bool doWorldColor = g_worldColorEnabled;
    if((doSceneChams || doWorldColor || g_handsColorEnabled || g_weaponChamsEnabled) && a3 && a4 > 0 && a4 < 20000){
        __try{
            uint8_t* base = (uint8_t*)a3;
            int localTeam = g_esp_local_team;

            static const size_t strides[] = { 0x68, 0x78, 0x58 };
            static const size_t colorOffsets[] = { 0x40, 0x48, 0x38 };
            static const size_t infoOffsets[] = { 0x48, 0x50, 0x40 };
            static const size_t materialOffsets[] = { 0x18, 0x20, 0x10 };
            static const int infoIdOffsets[] = { 0xB0, 0xC0, 0xA0, 0x90 };
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
                        if(c->a == 0) continue;

                        uintptr_t info = Rd<uintptr_t>(objBase + infoOff);
                        int id = 0;
                        for(int idOff : infoIdOffsets){
                            int candidate = info ? Rd<int>(info + idOff) : 0;
                            if(candidate > 0 && candidate < 300){ id = candidate; break; }
                        }

                        bool isCT = false, isT = false, isArms = false;
                        for(int cid : ctIds) if(id == cid) isCT = true;
                        for(int tid : tIds) if(id == tid) isT = true;
                        for(int aid : armsIds) if(id == aid) isArms = true;

                        uintptr_t mat = Rd<uintptr_t>(objBase + materialOffsets[strideTry]);
                        const char* matName = SafeMaterialName(mat);
                        bool isWeapon = matName && (strstr(matName,"weapon") || strstr(matName,"knife") || strstr(matName,"rifle") || strstr(matName,"pistol"));

                        bool applied = false;
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
                        }else if(doSceneChams && (isCT || isT)){
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

                        if(!applied && doWorldColor && matName){
                            bool ex = StrContainsI(matName,"player") || StrContainsI(matName,"character")
                                || StrContainsI(matName,"viewmodel") || StrContainsI(matName,"vm_") || StrContainsI(matName,"arms");
                            bool inc = StrContainsI(matName,"lightmapped") || StrContainsI(matName,"worldvertex")
                                || StrContainsI(matName,"decal") || StrContainsI(matName,"concrete") || StrContainsI(matName,"metal")
                                || StrContainsI(matName,"brick") || StrContainsI(matName,"plaster") || StrContainsI(matName,"models/props")
                                || StrContainsI(matName,"prop_") || StrContainsI(matName,"tools/") || StrContainsI(matName,"sky")
                                || StrContainsI(matName,"blend");
                            if(inc && !ex){
                                MaterialColor out{};
                                out.r = (uint8_t)Clampf((float)c->r * g_worldColor[0], 0.f, 255.f);
                                out.g = (uint8_t)Clampf((float)c->g * g_worldColor[1], 0.f, 255.f);
                                out.b = (uint8_t)Clampf((float)c->b * g_worldColor[2], 0.f, 255.f);
                                out.a = c->a;
                                *c = out;
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
    UINT64 now = GetTickCount64();
    if(now - g_lastSceneHookAttempt < 2000) return;
    g_lastSceneHookAttempt = now;
    HMODULE scene = GetModuleHandleA("scenesystem.dll");
    if(!scene) return;
    static const char PAT_DRAW_SCENE[] = "\x48\x8B\xC4\x4C\x89\x40\x00\x48\x89\x50\x00\x55\x53\x41\x56";
    static const char MSK_DRAW_SCENE[] = "xxxxxx?xxx?xxxx";
    static const char PAT_DRAW_SCENE2[] = "\x48\x8B\xC4\x53\x41\x54\x41\x55\x48\x81\xEC";
    static const char MSK_DRAW_SCENE2[] = "xxxxxxxxxxx";
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
        if(drawScene && MH_CreateHook(drawScene, reinterpret_cast<LPVOID>(&HookDrawSceneObject), reinterpret_cast<LPVOID*>(&g_origDrawSceneObject))==MH_OK){
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
        if(drawSky && MH_CreateHook(drawSky, reinterpret_cast<LPVOID>(&HookDrawSkyboxArray), reinterpret_cast<LPVOID*>(&g_origDrawSkyboxArray))==MH_OK){
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
    UINT64 now = GetTickCount64();
    if(now - g_lastClientHookAttempt < 2000) return;
    g_lastClientHookAttempt = now;
    EnsureModules();
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    uintptr_t clientBase = (uintptr_t)client;
    static const char PAT_FOV[] = "\xE8\x00\x00\x00\x00\xF3\x0F\x11\x45\x00\x48\x8B\x5C\x24";
    static const char MSK_FOV[] = "x????xxxx?xxx";
    static const char PAT_FP_LEGS[] = "\x40\x55\x53\x56\x41\x56\x41\x57\x48\x8D\xAC\x24\x00\x00\x00\x00\x48\x81\xEC\x00\x00\x00\x00\xF2\x0F\x10\x42";
    static const char MSK_FP_LEGS[] = "xxxxxxxxxxxx????xxx????xxxx";
    if(!g_worldFovHooked){
        void* callSite = PatternScan(client, PAT_FOV, MSK_FOV);
        void* fn = ResolveRelative(callSite, 1, 5);
        if(fn && MH_CreateHook(fn, reinterpret_cast<LPVOID>(&HookGetWorldFov), reinterpret_cast<LPVOID*>(&g_origGetWorldFov))==MH_OK){
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
        if(fn && MH_CreateHook(fn, reinterpret_cast<LPVOID>(&HookFirstPersonLegs), reinterpret_cast<LPVOID*>(&g_origFirstPersonLegs))==MH_OK){
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
    if(MH_CreateHook(presentAddr, reinterpret_cast<LPVOID>(&HookPresent), reinterpret_cast<LPVOID*>(&g_originalPresent))!=MH_OK){
        DebugLog("[LitWare] MH_CreateHook failed");MH_Uninitialize();return false;
    }
    if(MH_EnableHook(presentAddr)!=MH_OK){
        DebugLog("[LitWare] MH_EnableHook failed");MH_Uninitialize();return false;
    }
    DebugLog("[LitWare] Present hook installed");return true;
}
void Shutdown(){RequestUnload();}
}
 
