#include "render_hook.h"
#include "Fonts.h"
#include "../platform/minhook_utils.h"
#include "../platform/winapi.h"
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
static bool g_menuAnglesLocked = false;
static float g_menuLockPitch = 0.f;
static float g_menuLockYaw = 0.f;
static float g_menuLockRoll = 0.f;
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
static bool g_espBoxShadow = false;
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
static int g_aimbotBone = 0;
static bool g_fovCircleEnabled = false;
static float g_fovCircleCol[4]{0.4f,0.7f,1.f,0.5f};
static bool g_aimbotTeamChk = true;
static bool g_aimbotVisCheck = false;
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
static bool g_watermarkEnabled = true;
static bool g_showFpsWatermark = true;
static float g_watermarkOverlayHeight = 0.f;
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
template<typename T> static inline void Wr(uintptr_t addr,T val);
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
static bool g_autostopOwned = false;
static bool g_strafeOwned = false;
static bool g_dtOwned = false;

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

static void UpdateMenuCameraLock(){
    if(!g_menuOpen){
        g_menuAnglesLocked = false;
        return;
    }
    uintptr_t vaAddr = ViewAnglesAddr();
    if(!vaAddr){
        g_menuAnglesLocked = false;
        return;
    }
    if(!g_menuAnglesLocked){
        g_menuLockPitch = Rd<float>(vaAddr);
        g_menuLockYaw = Rd<float>(vaAddr + 4);
        g_menuLockRoll = Rd<float>(vaAddr + 8);
        g_menuAnglesLocked = true;
        return;
    }
    Wr<float>(vaAddr, g_menuLockPitch);
    Wr<float>(vaAddr + 4, g_menuLockYaw);
    Wr<float>(vaAddr + 8, g_menuLockRoll);
}

static void ReleaseMoveButtons(){
    if(!g_client) return;
    Wr<int>(g_client + offsets::buttons::forward, 0);
    Wr<int>(g_client + offsets::buttons::back, 0);
    Wr<int>(g_client + offsets::buttons::left, 0);
    Wr<int>(g_client + offsets::buttons::right, 0);
}

static void ReleaseStrafeButtons(){
    if(!g_client) return;
    Wr<int>(g_client + offsets::buttons::left, 0);
    Wr<int>(g_client + offsets::buttons::right, 0);
}

static void ReleaseAttackButton(){
    if(!g_client) return;
    Wr<int>(g_client + offsets::buttons::attack, 0);
}

static void ClearAutostopInput(){
    if(!g_autostopOwned) return;
    ReleaseMoveButtons();
    g_autostopOwned = false;
}

static void ClearStrafeInput(){
    if(!g_strafeOwned) return;
    ReleaseStrafeButtons();
    g_strafeOwned = false;
}

static void ClearDtInput(){
    if(!g_dtOwned) return;
    ReleaseAttackButton();
    g_dtOwned = false;
}

static void ClearTriggerInput(){
    if(g_tbShouldFire || g_tbJustFired) ReleaseAttackButton();
    g_tbShouldFire = false;
    g_tbFireTime = 0;
    g_tbJustFired = false;
    g_tbHoldFramesLeft = 0;
}

static void ReleaseRuntimeInputs(){
    ClearAutostopInput();
    ClearStrafeInput();
    ClearDtInput();
    ClearTriggerInput();
    if(g_client) Wr<int>(g_client + offsets::buttons::jump, 0);
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
static void PushLog(const char* text, ImU32 color, int type);
static void PlayHitSound(int type);
static void EnsureSceneHooks();
static void EnsureClientHooks();
static void* PatternScan(HMODULE mod,const char*pat,const char*mask);
static void EnsureCalcWorldSpaceBones();
static void UpdatePawnBones(uintptr_t pawn);
static void EnsureModules();
static inline bool IsLikelyPtr(uintptr_t p);

static bool HasUsableLocalWorld(){
    EnsureModules();
    if(!g_client) return false;
    uintptr_t lp = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!lp || !IsLikelyPtr(lp)) return false;
    uintptr_t scn = Rd<uintptr_t>(lp + offsets::base_entity::m_pGameSceneNode);
    if(!scn || !IsLikelyPtr(scn)) return false;
    if(g_engine2){
        uintptr_t netClient = Rd<uintptr_t>(g_engine2 + offsets::engine2::dwNetworkGameClient);
        if(netClient){
            int signOnState = Rd<int>(netClient + offsets::engine2::dwNetworkGameClient_signOnState);
            if(signOnState > 0 && signOnState < 6) return false;
        }
    }
    return true;
}

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

static bool IsFiniteVec3(const Vec3& v){
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static bool IsUsableWorldVec3(const Vec3& v){
    if(!IsFiniteVec3(v)) return false;
    if(fabsf(v.x) > 100000.f || fabsf(v.y) > 100000.f || fabsf(v.z) > 100000.f) return false;
    if(fabsf(v.x) < 0.001f && fabsf(v.y) < 0.001f && fabsf(v.z) < 0.001f) return false;
    return true;
}

static Vec3 BuildPawnAimFallback(uintptr_t pawn, const Vec3& fallback){
    if(IsUsableWorldVec3(fallback)) return fallback;

    Vec3 origin{};
    uintptr_t scn = Rd<uintptr_t>(pawn + offsets::base_entity::m_pGameSceneNode);
    if(scn) origin = Rd<Vec3>(scn + offsets::scene_node::m_vecAbsOrigin);
    if(!IsUsableWorldVec3(origin)) origin = Rd<Vec3>(pawn + offsets::base_pawn::m_vOldOrigin);
    if(!IsUsableWorldVec3(origin)) return fallback;

    Vec3 viewOff = Rd<Vec3>(pawn + offsets::base_pawn::m_vecViewOffset);
    if(!IsFiniteVec3(viewOff) || fabsf(viewOff.z) < 1.f || fabsf(viewOff.z) > 128.f){
        viewOff = {0.f, 0.f, 64.f};
    }

    Vec3 point = origin + viewOff;
    if(IsUsableWorldVec3(point)) return point;
    return {origin.x, origin.y, origin.z + 64.f};
}

static bool ResolveAimbotPoint(uintptr_t pawn, const Vec3& fallback, Vec3& out){
    static const int kHeadBones[]   = { BONE_HEAD,   BONE_NECK,  BONE_SPINE3, BONE_SPINE2, BONE_PELVIS };
    static const int kNeckBones[]   = { BONE_NECK,   BONE_HEAD,  BONE_SPINE3, BONE_SPINE2, BONE_PELVIS };
    static const int kChestBones[]  = { BONE_SPINE3, BONE_SPINE2, BONE_NECK,  BONE_HEAD,   BONE_PELVIS };
    static const int kStomachBones[]= { BONE_SPINE2, BONE_SPINE3, BONE_PELVIS, BONE_NECK,  BONE_HEAD };
    static const int kPelvisBones[] = { BONE_PELVIS, BONE_SPINE2, BONE_SPINE3, BONE_NECK,  BONE_HEAD };

    const int* bones = kHeadBones;
    int boneCount = (int)IM_ARRAYSIZE(kHeadBones);

    switch(g_aimbotBone){
        case 1: bones = kNeckBones; break;
        case 2: bones = kChestBones; break;
        case 3: bones = kStomachBones; break;
        case 4: bones = kPelvisBones; break;
        default: break;
    }

    Vec3 fallbackPoint = BuildPawnAimFallback(pawn, fallback);

    UpdatePawnBones(pawn);
    for(int i = 0; i < boneCount; ++i){
        Vec3 bonePos{};
        if(GetBonePos(pawn, bones[i], bonePos) && IsUsableWorldVec3(bonePos)){
            if(IsUsableWorldVec3(fallbackPoint)){
                Vec3 delta = bonePos - fallbackPoint;
                if(delta.length() > 96.f) continue;
            }
            out = bonePos;
            return true;
        }
    }

    out = fallbackPoint;
    return IsUsableWorldVec3(out);
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

#include "render_hook_config.cpp"

static bool GetOofArrowPos(const float* vm, const Vec3& head, int sw, int sh, float& ox, float& oy);

#include "render_hook_game.cpp"

#include "render_hook_menu.cpp"

#include "render_hook_visuals.cpp"

static void InitImGui(IDXGISwapChain*sc){
    if(!sc)return;
    DebugLog("[LitWare] InitImGui");
    DXGI_SWAP_CHAIN_DESC desc{};sc->GetDesc(&desc);g_gameHwnd=desc.OutputWindow;
    g_bbWidth = desc.BufferDesc.Width;
    g_bbHeight = desc.BufferDesc.Height;
    g_bbFormat = desc.BufferDesc.Format;
    ID3D11Texture2D*bb=nullptr;
    if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||!bb)return;
    if(FAILED(sc->GetDevice(__uuidof(ID3D11Device),(void**)&g_device))||!g_device){bb->Release();return;}
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
    ReleaseRuntimeInputs();
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
    if(GetAsyncKeyState(VK_INSERT)&1){
        g_menuOpen=!g_menuOpen;
        if(!g_menuOpen) g_menuAnglesLocked = false;
    }
    if(GetAsyncKeyState(VK_END)&1){ReleaseRuntimeInputs();RequestUnload();return;}
    UpdateMenuCameraLock();
    if(!g_safeMode){
        BuildESPData();BuildSpectatorList();ProcessHitEvents();UpdateBombInfo();UpdateSoundPings();
        RunNoFlash();RunNoSmoke();RunGlow();RunRadarHack();
        RunSkinChanger();
        RunFOVChanger();
        if(g_menuOpen){
            ReleaseRuntimeInputs();
        }else{
            RunBHop();
            RunAutostop();RunAimbot();RunRCS();RunStrafeHelper();RunTriggerBot();ReleaseTriggerAttack();RunDoubleTap();RunAimFireGate();
        }
    }else{
        ReleaseRuntimeInputs();
        g_esp_count=0;g_esp_oof_count=0;
        if(g_bbWidth >= 100 && g_bbHeight >= 100){
            g_esp_screen_w = (int)g_bbWidth;
            g_esp_screen_h = (int)g_bbHeight;
        }
    }
    UpdateMenuCameraLock();
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
    bool shouldUnload = g_pendingUnload.load();
    __try {
        if(!shouldUnload) RenderFrame(sc);
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
    HRESULT hr = g_originalPresent ? g_originalPresent(sc,sync,flags) : S_OK;
    if(g_pendingUnload.load()) DoDeferredUnload();
    return hr;
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
    if((doSceneChams || g_handsColorEnabled || g_weaponChamsEnabled) && a3 && a4 > 0 && a4 < 20000){
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
    bool needScene = (g_chamsEnabled && g_chamsScene) || g_handsColorEnabled || g_weaponChamsEnabled;
    bool needSky = g_skyColorEnabled;
    if(!needScene && !needSky){
        g_sceneHooksReady = true;
        return;
    }
    if((!needScene || g_drawSceneHooked) && (!needSky || g_drawSkyHooked)){
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
    if(needScene && !g_drawSceneHooked){
        drawScene = PatternScan(scene, PAT_DRAW_SCENE, MSK_DRAW_SCENE);
        if(!drawScene) drawScene = PatternScan(scene, PAT_DRAW_SCENE2, MSK_DRAW_SCENE2);
        if(!drawScene) drawScene = PatternScan(scene, PAT_DRAW_SCENE3, MSK_DRAW_SCENE3);
    }
    void* drawSky = (!g_drawSkyHooked && needSky) ? PatternScan(scene, PAT_SKY, MSK_SKY) : nullptr;
    if(needScene && !g_drawSceneHooked){
        if(drawScene && minhook_utils::CreateHook(drawScene, &HookDrawSceneObject, &g_origDrawSceneObject)==MH_OK){
            minhook_utils::EnableHook(drawScene);
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
        if(drawSky && minhook_utils::CreateHook(drawSky, &HookDrawSkyboxArray, &g_origDrawSkyboxArray)==MH_OK){
            minhook_utils::EnableHook(drawSky);
            g_drawSkyHooked = true;
            DebugLog("[LitWare] draw_skybox_array hook ok");
        } else {
            DebugLog("[LitWare] draw_skybox_array hook failed");
        }
    }
    g_sceneHooksReady = (!needScene || g_drawSceneHooked) && (!needSky || g_drawSkyHooked);
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
        if(fn && minhook_utils::CreateHook(fn, &HookGetWorldFov, &g_origGetWorldFov)==MH_OK){
            minhook_utils::EnableHook(fn);
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
        if(fn && minhook_utils::CreateHook(fn, &HookFirstPersonLegs, &g_origFirstPersonLegs)==MH_OK){
            minhook_utils::EnableHook(fn);
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
    if(minhook_utils::CreateHook(presentAddr, &HookPresent, &g_originalPresent)!=MH_OK){
        DebugLog("[LitWare] MH_CreateHook failed");MH_Uninitialize();return false;
    }
    if(minhook_utils::EnableHook(presentAddr)!=MH_OK){
        DebugLog("[LitWare] MH_EnableHook failed");MH_Uninitialize();return false;
    }
    DebugLog("[LitWare] Present hook installed");return true;
}
void Shutdown(){RequestUnload();}
}
 
