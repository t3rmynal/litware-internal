#include "render_hook.h"
#include "Fonts.h"
#include "res/font.h"
#include "res/jetbrains_mono.h"
#include "../core/offsets.h"
#include "../core/electron_bridge.h"
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
static bool g_menuLaunched = false;
static bool g_showDebugConsole = false;
static std::atomic_bool g_unloading{false};
static std::atomic_bool g_cleanupDone{false};
static std::atomic_bool g_pendingUnload{false};
static HWND g_gameHwnd = nullptr;
static uintptr_t g_client = 0;
static uintptr_t g_engine2 = 0;
static Vec3 g_localOrigin{};

static bool g_espEnabled = true;
static bool g_espOnlyVis = false;
static int g_espBoxStyle = 4;
static float g_espBoxThick = 1.5f;
static float g_espEnemyCol[4]{0.209677f,0.502861f,1.f,1.f};
static float g_espTeamCol[4]{1.f,0.25f,0.921371f,1.f};
static bool g_espShowTeam = false;
static bool g_espName = true;
static float g_espNameSize = 14.8f;
static bool g_espHealth = true;
static bool g_espHealthText = true;   // HP number (Weave style)
static int g_espHealthPos = 0;
static int g_espHealthStyle = 0;
static float g_espHealthGradientCol1[4]{0.56f,0.92f,0.2f,1.f};  // green (full)
static float g_espHealthGradientCol2[4]{1.f,0.27f,0.27f,1.f};   // red (empty)
static bool g_espDist = true;
static float g_espMaxDist = 100.f;
static bool g_espSkeleton = false;
static bool g_espLines = true;
static int g_espLineAnchor = 1;  // 0=Top 1=Middle 2=Bottom
static bool g_espOof = true;       // Offscreen arrows (from Pidoraise/Weave)
static float g_espOofSize = 33.f;
static float g_skeletonThick = 1.1f;
struct OofEntry { float x, y; float angle; ImU32 col; };  // angle: arrow points toward player
static OofEntry g_esp_oof[32];
static int g_esp_oof_count = 0;
static bool g_espHeadDot = false;
static bool g_espSpotted = true;
static bool g_visCheckEnabled = true;
static bool g_espWeapon = true;
static bool g_espWeaponIcon = true;
static bool g_espAmmo = true;
static int g_espAmmoStyle = 0;
static float g_espAmmoCol1[4]{0.145098f,0.337255f,0.768627f,1.f};  // dark blue
static float g_espAmmoCol2[4]{0.329412f,0.803922f,1.f,1.f};   // cyan
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
static bool g_glowEnabled = true;
static float g_glowEnemyCol[4]{0.f,0.225806f,1.f,1.f};
static float g_glowTeamCol[4]{0.74871f,0.18f,1.f,1.f};
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
static int g_aimbotKey = VK_LBUTTON;  // LMB
static float g_aimbotFov = 9.f;
static float g_aimbotSmooth = 15.1f;
static bool g_fovCircleEnabled = false;
static float g_fovCircleCol[4]{0.4f,0.7f,1.f,0.5f};  // R,G,B,A for FOV circle
static bool g_aimbotTeamChk = true;
static int g_aimbotBone = 0;
static int g_aimbotWeaponFilter = 0;  // 0=All 1=Rifles 2=Snipers 3=Pistols
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
static uintptr_t g_bombDefuserPawn = 0;  // pawn who is defusing (for ESP status)
static bool g_tbJustFired = false;  // Release attack after hold frames
static int g_tbHoldFramesLeft = 0;  // Hold IN_ATTACK for N frames so game picks it up (internal reads input once per frame)
static bool g_dtEnabled = false;
static int g_dtKey = 0;
static bool g_bhopEnabled = false;
static bool g_strafeEnabled = false;
static int g_strafeKey = 0;
static bool g_antiAimEnabled = false;
static int g_antiAimType = 0;  // 0=spin, 1=desync, 2=jitter
static float g_antiAimSpeed = 180.f;
static bool g_fovEnabled = false;
static float g_fovValue = 121.f;
static bool g_thirdPerson = false;
static bool g_autostopEnabled = false;  // stop when shooting for accuracy
static float g_tpDist = 120.f;
static float g_tpHeightOffset = 30.f;
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
static bool g_spectatorListEnabled = false;
static bool g_hitNotifEnabled = true;
static bool g_killNotifEnabled = true;
static bool g_hitSoundEnabled = false;
static bool g_hitmarkerEnabled = false;
static float g_hitmarkerDuration = 0.4f;
static int g_hitmarkerStyle = 0;  // 0=cross 1=X
static UINT64 g_lastHitmarkerTime = 0;
static bool g_killEffectEnabled = false;
static float g_killEffectDuration = 0.6f;
static UINT64 g_lastKillEffectTime = 0;
static Vec3 g_lastKillEffectPos{};  // victim head position for kill particles
static bool g_pendingKillParticles = false;  // spawn 67+LitWare burst on next particle update
static int g_hitEffectType = 0;   // 0=none 1=cross 2=screen flash 3=circle
static int g_killEffectType = 1;  // 0=none 1=burst 2=KILL text
static float g_hitEffectCol[4]{1.f,0.9f,0.2f,0.9f};
static float g_killEffectCol[4]{1.f,0.3f,0.3f,0.95f};
static int g_hitSoundType = 1;
static bool g_radarEnabled = true;  // Overlay radar window (separate from in-game minimap)
static bool g_radarIngame = false;   // Force spot all for in-game minimap (overlay radar removed)
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
static bool g_keybindsEnabled = false;
static int g_menuTheme = 0; // потом
static float g_themeTransition = 0.f;
static float g_menuAnimSpeed = 12.f;
static UINT64 g_telegramNoteStart = 0;  

// Skins
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

static inline float Clampf(float v, float lo, float hi);
template<typename T> static inline T Rd(uintptr_t addr);
static void DrawFilledEllipse(ImDrawList* dl, const ImVec2& center, float rx, float ry, ImU32 col, int segments);
static void DrawRotatedQuad(ImDrawList* dl, ImVec2 center, float w, float h, float angle, ImU32 col);

static char g_configName[64] = "default";
static int g_configSelected = -1;
static std::vector<std::string> g_configList;

static int g_lastHealth[ESP_MAX_PLAYERS + 1] = {};
static bool g_seenThisFrame[ESP_MAX_PLAYERS + 1] = {};

struct LogEntry{char text[256];ImU32 color;float lifetime,maxlife;int type;};  // type: 0=hit 1=kill
static std::deque<LogEntry>g_logs;
static DWORD g_lastSoundPingTick[ESP_MAX_PLAYERS + 1] = {};
static bool g_visMap[ESP_MAX_PLAYERS + 1] = {};
// кеш сущностей 80мс чтобы не мигали
static ESPEntry g_esp_stale[ESP_MAX_PLAYERS + 1] = {};
static UINT64 g_esp_stale_tick[ESP_MAX_PLAYERS + 1] = {};
static UINT64 g_visLastTrueTick[ESP_MAX_PLAYERS + 1] = {};
static constexpr DWORD ESP_STALE_MS = 80;

// математика
static inline float Clampf(float v, float lo, float hi){return v<lo?lo:(v>hi?hi:v);}
static inline ImVec4 LerpV4(ImVec4 a, ImVec4 b, float t){
    return ImVec4(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t);
}
static inline float LerpF(float a,float b,float t){return a+(b-a)*t;}
static inline float AngleDiff(float a,float b){float d=fmodf(a-b+540.f,360.f)-180.f;return d;}
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
    UINT64 now = GetTickCount64();
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

[[maybe_unused]] static void WCharToUtf8(ImWchar c, char out[5]){
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

static int ParseKeyName(const std::string& v);

// Config load helpers ? split to avoid MSVC "invalid nesting of blocks"
static bool LoadConfigKeyEsp(const std::string& key, const std::string& val, bool& ok){
    if(key=="esp_enabled"){ g_espEnabled=ParseBool(val); return true; }
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
    if(key=="aimbot_key"){ int vk=ParseKeyName(val); if(vk>=0){ g_aimbotKey=vk; return true; } int v; if(ParseInt(val,v)){ g_aimbotKey=v; return true; } ok=false; return true; }
    if(key=="aimbot_fov"){ float v; if(ParseFloat(val,v)) g_aimbotFov=v; else ok=false; return true; }
    if(key=="aimbot_smooth"){ float v; if(ParseFloat(val,v)) g_aimbotSmooth=v; else ok=false; return true; }
    if(key=="fov_circle"){ g_fovCircleEnabled=ParseBool(val); return true; }
    if(key=="fov_circle_col"){ if(!ParseColor4(val,g_fovCircleCol)) ok=false; return true; }
    if(key=="aimbot_team"){ g_aimbotTeamChk=ParseBool(val); return true; }
    if(key=="aimbot_bone"){ int v; if(ParseInt(val,v)) g_aimbotBone=v; else ok=false; return true; }
    if(key=="rcs_enabled"){ g_rcsEnabled=ParseBool(val); return true; }
    if(key=="rcs_x"){ float v; if(ParseFloat(val,v)){ g_rcsX=v; rcsXSet=true; } else ok=false; return true; }
    if(key=="rcs_y"){ float v; if(ParseFloat(val,v)){ g_rcsY=v; rcsYSet=true; } else ok=false; return true; }
    if(key=="rcs_smooth"){ float v; if(ParseFloat(val,v)) g_rcsSmooth=v; else ok=false; return true; }
    if(key=="rcs_strength"){ float v; if(ParseFloat(val,v)){ if(!rcsXSet&&!rcsYSet){ g_rcsX=v; g_rcsY=v; } } else ok=false; return true; }
    if(key=="tb_enabled"){ g_tbEnabled=ParseBool(val); return true; }
    if(key=="tb_key"){ int vk=ParseKeyName(val); if(vk>=0){ g_tbKey=vk; return true; } int v; if(ParseInt(val,v)){ g_tbKey=v; return true; } ok=false; return true; }
    if(key=="tb_delay"){ int v; if(ParseInt(val,v)) g_tbDelay=v; else ok=false; return true; }
    if(key=="tb_team"){ g_tbTeamChk=ParseBool(val); return true; }
    if(key=="dt_enabled"){ g_dtEnabled=ParseBool(val); return true; }
    if(key=="dt_key"){ int vk=ParseKeyName(val); if(vk>=0){ g_dtKey=vk; return true; } int v; if(ParseInt(val,v)){ g_dtKey=v; return true; } ok=false; return true; }
    return false;
}
static bool LoadConfigKeyMovement(const std::string& key, const std::string& val, bool& ok){
    if(key=="bhop"){ g_bhopEnabled=ParseBool(val); return true; }
    if(key=="strafe_enabled"){ g_strafeEnabled=ParseBool(val); return true; }
    if(key=="strafe_key"){ int vk=ParseKeyName(val); if(vk>=0){ g_strafeKey=vk; return true; } int v; if(ParseInt(val,v)){ g_strafeKey=v; return true; } ok=false; return true; }
    if(key=="anti_aim_enabled"){ g_antiAimEnabled=ParseBool(val); return true; }
    if(key=="anti_aim_type"){ int v; if(ParseInt(val,v)) g_antiAimType=v; else ok=false; return true; }
    if(key=="anti_aim_speed"){ float v; if(ParseFloat(val,v)) g_antiAimSpeed=v; else ok=false; return true; }
    if(key=="fov_enabled"){ g_fovEnabled=ParseBool(val); return true; }
    if(key=="fov_value"){ float v; if(ParseFloat(val,v)) g_fovValue=v; else ok=false; return true; }
    if(key=="third_person"){ g_thirdPerson=ParseBool(val); return true; }
    if(key=="tp_dist"){ float v; if(ParseFloat(val,v)) g_tpDist=v; else ok=false; return true; }
    if(key=="tp_height"){ float v; if(ParseFloat(val,v)) g_tpHeightOffset=v; else ok=false; return true; }
    if(key=="autostop"){ g_autostopEnabled=ParseBool(val); return true; }
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
    g_fovCircleEnabled = false;
    g_fovCircleCol[0]=0.4f; g_fovCircleCol[1]=0.7f; g_fovCircleCol[2]=1.f; g_fovCircleCol[3]=0.5f;
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
    g_killEffectEnabled = false;
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
    g_accentColor[0]=0.1f; g_accentColor[1]=0.55f; g_accentColor[2]=1.0f; g_accentColor[3]=1.0f;
    g_menuOpacity = 1.0f;
    g_uiScale = 1.10f;
    g_menuTheme = 0;
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
    WriteInt(out, "menu_theme", g_menuTheme);
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
    int sw=g_bbWidth>0?g_bbWidth:1920, sh=g_bbHeight>0?g_bbHeight:1080;
    if(g_engine2){int w=Rd<int>(g_engine2+offsets::engine2::dwWindowWidth);
    int h=Rd<int>(g_engine2+offsets::engine2::dwWindowHeight);
    if(w>100&&h>100){sw=w;sh=h;}}
    if(sw<=100||sh<=100){ sw=g_bbWidth; sh=g_bbHeight; if(sw<=0)sw=1920; if(sh<=0)sh=1080; }
    uintptr_t localPawn=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
    (void)Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController); // localCtrl reserved
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
        // Visibility hysteresis: once visible, stay visible for 80ms to reduce flicker
        if(i > 0 && i <= ESP_MAX_PLAYERS && vis) g_visLastTrueTick[i] = GetTickCount64();
        UINT64 now = GetTickCount64();
        bool effVis = vis || (i > 0 && i <= ESP_MAX_PLAYERS && (now - g_visLastTrueTick[i]) < 80);
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
        float boxH = fy - hy;
        float top = hy - boxH * 0.18f; // Move slightly above head head center
        boxH = fy - top;
        float boxW = boxH * 0.52f;
        float cx = (hx + fx) * 0.5f;
        float dist = (origin - localOrigin).length() / 100.f;  // meters
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
        e.head_x=hx;e.head_y=hy;e.head_fx=hfx;e.head_fy=hfy;
        e.head_ox=head.x;e.head_oy=head.y;e.head_oz=head.z;
        e.origin_x=origin.x;e.origin_y=origin.y;e.origin_z=origin.z;
        e.feet_x=fx;e.feet_y=fy;
        e.health=health;e.team=team;e.distance=dist;e.yaw=0.f;
        RdName(namePtr,e.name,sizeof(e.name));
        // Stale cache: keep last-known data for anti-flicker
        g_esp_stale[i] = e;
        g_esp_stale_tick[i] = GetTickCount64();
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
    ImDrawList* dl = ImGui::GetForegroundDrawList(); if(!dl) return;
    ImFont* fBold = font::lexend_bold    ? font::lexend_bold    : ImGui::GetFont();
    ImFont* fReg  = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();

    const float margin  = 15.f;
    const float padX    = 12.f;
    const float padY    = 10.f;
    const float rnd     = 8.f;
    const float lineH   = 22.f;
    const float headerH = 30.f;

    float yBase = margin;
    if(g_watermarkEnabled){
        // watermark height: approx 34px + margin
        yBase += 34.f + 8.f;
    }

    const ImU32 colBg      = IM_COL32(10, 10, 12, 245);
    const ImU32 colBorder  = IM_COL32(38, 38, 48, 255);
    const ImU32 colHeader  = IM_COL32(16, 16, 20, 255);
    const ImU32 colAccent  = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    const ImU32 colAccentD = IM_COL32((int)(g_accentColor[0]*160),(int)(g_accentColor[1]*160),(int)(g_accentColor[2]*160),255);
    const ImU32 colText    = IM_COL32(215, 220, 230, 255);
    const ImU32 colDim     = IM_COL32(100, 105, 118, 255);
    const ImU32 colSep     = IM_COL32(32, 34, 42, 255);
    const ImU32 colRowHov  = IM_COL32(255, 255, 255, 8);

    // === "Spectating: target" pill — shown when we spectate someone ===
    if(g_weAreSpectating && g_spectatingTarget[0]){
        ImVec2 szTarget = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, g_spectatingTarget);
        const char* prefix = "WATCHING";
        ImVec2 szPrefix  = fBold->CalcTextSizeA(fBold->LegacySize, FLT_MAX, 0.f, prefix);
        float pillW = padX + szPrefix.x + 8.f + szTarget.x + padX;
        pillW = (std::max)(pillW, 140.f);
        float pillH = headerH;
        float px = sw - pillW - margin;
        float py = yBase;
        dl->AddRectFilled({px, py}, {px+pillW, py+pillH}, colBg, rnd);
        dl->AddRect({px, py}, {px+pillW, py+pillH}, colBorder, rnd, 0, 1.f);
        // accent left bar
        dl->AddRectFilled({px, py+4.f}, {px+2.f, py+pillH-4.f}, colAccent, 2.f);
        float midY = py + pillH * 0.5f;
        dl->AddText(fBold, fBold->LegacySize, {px+padX, midY - szPrefix.y*0.5f}, colAccent, prefix);
        dl->AddText(fReg,  fReg->LegacySize,  {px+padX+szPrefix.x+8.f, midY - szTarget.y*0.5f}, colText, g_spectatingTarget);
        return;
    }

    if(g_spectatorCount <= 0) return;

    // Measure max name width
    float maxNameW = 60.f;
    for(int i = 0; i < g_spectatorCount; i++){
        ImVec2 ts = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, g_spectatorNames[i]);
        if(ts.x > maxNameW) maxNameW = ts.x;
    }

    // Header label + count badge
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

    // Shadow
    dl->AddRectFilled({x-3.f, y-3.f}, {x+boxW+3.f, y+boxH+3.f}, IM_COL32(0,0,0,55), rnd+2.f);

    // Main background
    dl->AddRectFilled({x, y}, {x+boxW, y+boxH}, colBg, rnd);

    // Header background
    dl->AddRectFilled({x, y}, {x+boxW, y+headerH}, colHeader,
        rnd, ImDrawFlags_RoundCornersTop);

    // Separator under header
    dl->AddLine({x+1.f, y+headerH}, {x+boxW-1.f, y+headerH}, colSep, 1.f);

    // Border
    dl->AddRect({x, y}, {x+boxW, y+boxH}, colBorder, rnd, 0, 1.f);

    // Accent left bar
    dl->AddRectFilled({x, y+4.f}, {x+2.f, y+boxH-4.f}, colAccent, 2.f);

    // Header text
    float hMid = y + headerH * 0.5f;
    dl->AddText(fBold, fBold->LegacySize, {x+padX, hMid - szHdr.y*0.5f}, colAccent, hdrLabel);

    // Count badge (pill)
    float badgeX = x + boxW - padX - badgeW;
    float badgeY = hMid - badgeH * 0.5f;
    dl->AddRectFilled({badgeX, badgeY}, {badgeX+badgeW, badgeY+badgeH}, colAccentD, badgeH*0.5f);
    dl->AddText(fReg, fReg->LegacySize, {badgeX + (badgeW-szCnt.x)*0.5f, badgeY + (badgeH-szCnt.y)*0.5f}, IM_COL32(255,255,255,230), cntBuf);

    // Entries
    for(int i = 0; i < g_spectatorCount; i++){
        float ey = y + headerH + 1.f + (float)i * lineH;
        float eMid = ey + lineH * 0.5f;

        // Subtle row hover highlight for even rows
        if(i % 2 == 0)
            dl->AddRectFilled({x+1.f, ey}, {x+boxW-1.f, ey+lineH}, colRowHov);

        // Index dot
        char idxBuf[4]; std::snprintf(idxBuf, sizeof(idxBuf), "%d", i+1);
        ImVec2 szIdx = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, idxBuf);
        dl->AddText(fReg, fReg->LegacySize, {x+padX, eMid - szIdx.y*0.5f}, colDim, idxBuf);

        // Name
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
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Hit %s for %d", e.name[0]?e.name:"Enemy", (prev - e.health));
            if(g_hitNotifEnabled) PushNotification(buf, IM_COL32(240,180,60,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(240,180,60,255); le.maxlife=4.f; le.lifetime=4.f; le.type=0;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            PlayHitSound(g_hitSoundType);
            if(g_hitmarkerEnabled) g_lastHitmarkerTime = GetTickCount64();
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
        if(!IsLikelyPtr((uintptr_t)flashAddr))return;  // avoid writing to obviously bad addr
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
        // Approach 1: Zero smoke overlay alpha on local pawn (per-player smoke visibility)
        uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
        if(lp&&IsLikelyPtr(lp)) Wr<float>(lp+offsets::cs_pawn_base::m_flLastSmokeOverlayAlpha,0.f);
        // Approach 2: Set entity alpha via ClientAlphaProperty on smoke projectiles
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
    if(!(GetAsyncKeyState(g_aimbotKey)&0x8000))return;  // Only when holding aim key
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    __try{
        Vec3 vel=Rd<Vec3>(lp+offsets::base_entity::m_vecVelocity);
        float spd=sqrtf(vel.x*vel.x+vel.y*vel.y+vel.z*vel.z);
        if(spd>5.f) Wr<Vec3>(lp+offsets::base_entity::m_vecVelocity, Vec3{0.f,0.f,0.f});
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

// Bunnyhop: auto-jump when holding space. On ground=65537, in air=256 (per blast.hk/internal cheat convention)
static void DrawDebugConsole() {
    if (!g_showDebugConsole) return;
    ImGui::SetNextWindowSize({ 500, 400 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug Console", &g_showDebugConsole)) {
        if (ImGui::Button("Clear")) ClearDebugLogs();
        ImGui::Separator();
        ImGui::BeginChild("LogScroll");
        auto& logs = GetDebugLogs();
        for (const auto& log : logs) {
            ImGui::TextUnformatted(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

// Bunnyhop: hold jump pressed for several frames after landing so game tick catches it
// CS2 bhop: trigger a fresh jump ONLY on the landing frame (state transition),
// hold the press for a few frames so the server tick always catches it.
// Writing 65537 continuously = game sees "held", not "new press" → no re-jump.
static void RunBHop(){
    if(!g_bhopEnabled||!g_client)return;
    if(g_menuOpen) return;

    if(!(GetAsyncKeyState(VK_SPACE)&0x8000)){
        Wr<int>(g_client+offsets::buttons::jump, 0);
        return;
    }

    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;

    bool  onGround = (Rd<uint32_t>(lp+offsets::base_entity::m_fFlags) & 1) != 0;
    float velZ     = Rd<float>(lp+offsets::base_entity::m_vecVelocity+8); // Z component

    static bool  s_prevOnGround = false;
    static float s_prevVelZ     = 0.f;
    static int   s_holdFrames   = 0;

    // Detect landing via flag transition OR velocity transition (falling → stopped)
    bool justLanded = (onGround && !s_prevOnGround) ||
                      (s_prevVelZ < -60.f && velZ > -10.f);

    s_prevOnGround = onGround;
    s_prevVelZ     = velZ;

    if(justLanded) s_holdFrames = 3; // hold 3 frames so game tick picks up press

    if(s_holdFrames > 0){
        Wr<int>(g_client+offsets::buttons::jump, 65537);
        --s_holdFrames;
    } else {
        Wr<int>(g_client+offsets::buttons::jump, 0);
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
        UINT64 now = GetTickCount64();
        if(now - desyncTime > 100){
            antiAimAngle = (antiAimAngle > 0.f) ? -45.f : 45.f;
            desyncTime = now;
        }
        Wr<float>(vaAddr+4, yaw + antiAimAngle);
    }
    else if(g_antiAimType == 2){ // Jitter
        antiAimAngle = (sinf((float)GetTickCount64()*0.01f)*30.f);
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
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;

    float punchX=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle);
    float punchY=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle+4);

    bool shooting=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    int shots=Rd<int>(lp+offsets::cs_pawn::m_iShotsFired);

    if(!shooting || shots<1){
        // Track current punch so delta=0 on the first active frame (no spike)
        g_rcsPrevPunchX=punchX;
        g_rcsPrevPunchY=punchY;
        return;
    }

    float dx=(punchX-g_rcsPrevPunchX)*g_rcsX;
    float dy=(punchY-g_rcsPrevPunchY)*g_rcsY;
    g_rcsPrevPunchX=punchX;
    g_rcsPrevPunchY=punchY;

    if(dx==0.f&&dy==0.f)return; // punch didn't change this frame

    uintptr_t vaAddr=ViewAnglesAddr();if(!vaAddr)return;
    float pitch=Rd<float>(vaAddr);float yaw=Rd<float>(vaAddr+4);
    float smooth=Clampf(g_rcsSmooth,1.f,50.f);
    pitch-=(dx*2.f)/smooth;
    yaw -=(dy*2.f)/smooth;
    pitch=Clampf(pitch,-89.f,89.f);
    if(yaw>180.f)yaw-=360.f;else if(yaw<-180.f)yaw+=360.f;
    Wr<float>(vaAddr,pitch);
    Wr<float>(vaAddr+4,yaw);
}

// Auto Strafe: optimal yaw turn for max air speed gain (sv_airaccelerate/speed formula)
static void RunStrafeHelper(){
    if(!g_strafeEnabled||!g_client) return;
    if(g_menuOpen) return;
    if(g_strafeKey!=0&&!(GetAsyncKeyState(g_strafeKey)&0x8000)) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn); if(!lp) return;
    if(Rd<uint32_t>(lp+offsets::base_entity::m_fFlags)&1) return;  // On ground - no strafe
    
    uintptr_t vaAddr=ViewAnglesAddr(); if(!vaAddr) return;
    float curYaw=Rd<float>(vaAddr+4);
    static float s_lastYaw = 0.f;
    float delta = curYaw - s_lastYaw;
    if(delta > 180.f) delta -= 360.f; else if(delta < -180.f) delta += 360.f;
    s_lastYaw = curYaw;

    if(fabsf(delta) < 0.1f) return;

    if(delta > 0.f){ // Moving mouse Left (positive delta in degrees usually)
        // Note: ViewAngles in CS2: Left is increasing degrees (0 -> 90 -> 180)
        // If delta > 0, we are turning Left. Press 'A'.
        Wr<int>(g_client+offsets::buttons::left, 65537);
        Wr<int>(g_client+offsets::buttons::right, 256);
    } else { // Moving mouse Right
        Wr<int>(g_client+offsets::buttons::right, 65537);
        Wr<int>(g_client+offsets::buttons::left, 256);
    }
}

// Entity list stride (Source2: often 112/0x70; if trigger stops working after game update try 120/0x78)
static constexpr int kEntityListStride = 112;
static void RunTriggerBot(){
    if(!g_tbEnabled||!g_client)return;
    if(g_tbKey!=0&&!(GetAsyncKeyState(g_tbKey)&0x8000)){g_tbShouldFire=false;return;}
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int entIdx=Rd<int>(lp+offsets::cs_pawn::m_iIDEntIndex);
    if(entIdx<=0||entIdx>8192){g_tbShouldFire=false;return;}  // CEntityIndex: 0 or -1 = none, sanity cap
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((entIdx&0x7FFF)>>9)+16);if(!pchunk)return;
    uintptr_t targPawn=Rd<uintptr_t>(pchunk+kEntityListStride*(entIdx&0x1FF));
    if(!targPawn||!IsLikelyPtr(targPawn)){g_tbShouldFire=false;return;}
    int lifeState=Rd<uint8_t>(targPawn+offsets::base_entity::m_lifeState);
    if(lifeState!=0){g_tbShouldFire=false;return;}  // 0 = alive
    int targTeam=(int)Rd<uint8_t>(targPawn+offsets::base_entity::m_iTeamNum);
    int targHealth=Rd<int>(targPawn+offsets::base_entity::m_iHealth);
    if(targHealth<=0){g_tbShouldFire=false;return;}
    if(g_tbTeamChk&&targTeam==g_esp_local_team){g_tbShouldFire=false;return;}
    if(!g_tbShouldFire){g_tbShouldFire=true;g_tbFireTime=GetTickCount64()+(UINT64)g_tbDelay;}
    if(GetTickCount64()>=g_tbFireTime){
        Wr<int>(g_client+offsets::buttons::attack,65537);  // Press
        g_tbShouldFire=false;
        g_tbJustFired=true;
        g_tbHoldFramesLeft=4;  // Hold 4 frames so game input sees it (internal processes input once per frame)
    }
}

static void ReleaseTriggerAttack(){
    if(!g_client||!g_tbEnabled)return;
    if(g_tbJustFired && g_tbHoldFramesLeft>0){
        Wr<int>(g_client+offsets::buttons::attack,65537);  // Keep pressed
        g_tbHoldFramesLeft--;
        return;
    }
    if(g_tbJustFired){
        Wr<int>(g_client+offsets::buttons::attack,256);  // Release
        g_tbJustFired=false;
        return;
    }
    // Do not override user input when triggerbot is idle.
}

static void RunAimbot(){
    if(!g_aimbotEnabled||!g_client)return;
    if(g_menuOpen) return;
    if(!(GetAsyncKeyState(g_aimbotKey)&0x8000))return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
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
    // Path 1: use ESP cache (filled by BuildESPData same frame)
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry&e=g_esp_players[i];
        if(!e.valid||!e.pawn||!IsLikelyPtr(e.pawn))continue;
        if(g_aimbotTeamChk&&e.team==g_esp_local_team)continue;
        if(e.distance>g_espMaxDist)continue;
        UpdatePawnBones(e.pawn);
        Vec3 origin{e.origin_x,e.origin_y,e.origin_z};
        Vec3 headWorld{e.head_ox,e.head_oy,e.head_oz};
        Vec3 viewOff=headWorld-origin;
        Vec3 aimPoint=origin+viewOff;
        if(g_aimbotBone==1 || g_aimbotBone==2){
            Vec3 bonePos{};
            int boneId = (g_aimbotBone==1) ? BONE_NECK : BONE_SPINE3;
            if(GetBonePos(e.pawn, boneId, bonePos)) aimPoint = bonePos;
            else { float boneFactor=(g_aimbotBone==1)?0.75f:0.5f; aimPoint=origin+viewOff*boneFactor; }
            evalPoint(aimPoint);
        }else if(g_aimbotBone==3){
            static const int bones[] = {BONE_HEAD,BONE_NECK,BONE_SPINE3,BONE_SPINE2,BONE_PELVIS};
            for(int b: bones){ Vec3 bp{}; if(GetBonePos(e.pawn,b,bp)) evalPoint(bp); }
            if(!found) evalPoint(aimPoint);
        }else evalPoint(aimPoint);
    }
    // Path 2: if no ESP entries, iterate entity list directly (like TempleWare)
    if(!found){
        uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
        int localTeam=(int)Rd<uint8_t>(lp+offsets::base_entity::m_iTeamNum);
        uintptr_t localCtrl=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController);
        if(entityList){
            for(int i=1;i<64;i++){
                uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16); if(!chunk)continue;
                uintptr_t ctrl=Rd<uintptr_t>(chunk+kEntityListStride*(i&0x1FF)); if(!ctrl||!IsLikelyPtr(ctrl))continue;
                if(ctrl==localCtrl)continue;  // skip self
                if(!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
                uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn); if(!ph)continue;
                uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16); if(!pchunk)continue;
                uintptr_t pawn=Rd<uintptr_t>(pchunk+kEntityListStride*(ph&0x1FF)); if(!pawn||!IsLikelyPtr(pawn)||pawn==lp)continue;
                if(Rd<uint8_t>(pawn+offsets::base_entity::m_lifeState)!=0)continue;
                if(Rd<int>(pawn+offsets::base_entity::m_iHealth)<=0)continue;
                if(g_aimbotTeamChk && (int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum)==localTeam)continue;
                Vec3 origin=Rd<Vec3>(pawn+offsets::base_pawn::m_vOldOrigin);
                uintptr_t scn=Rd<uintptr_t>(pawn+offsets::base_entity::m_pGameSceneNode); if(scn) origin=Rd<Vec3>(scn+offsets::scene_node::m_vecAbsOrigin);
                Vec3 viewOff=Rd<Vec3>(pawn+offsets::base_pawn::m_vecViewOffset);
                Vec3 head={origin.x+viewOff.x, origin.y+viewOff.y, origin.z+viewOff.z};
                float dist=(head-localOrigin).length()/100.f; if(dist>g_espMaxDist)continue;
                evalPoint(head);
            }
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
        // Intense glow: more rings, brighter alpha, thicker lines
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
        // Only consider active when player is near bomb (far entities are dormant, timer data is stale)
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
    if(msg == WM_KEYDOWN && wp == VK_F5) {
        g_showDebugConsole = !g_showDebugConsole;
        return 0;
    }
    if(g_menuOpen) return 0;
    if(g_imguiInitialized && g_showDebugConsole){
        if(ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp)) return 0;
        if(IsInputMessage(msg)) return 0;
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

// Parse key name from Electron (e.g. "MOUSE1", "LMB", "SHIFT") -> VK code; -1 if unknown.
static int ParseKeyName(const std::string& v){
    if(v.empty()) return -1;
    if(v=="LMB"||v=="MOUSE1") return VK_LBUTTON;
    if(v=="RMB"||v=="MOUSE2") return VK_RBUTTON;
    if(v=="MMB"||v=="MOUSE3") return VK_MBUTTON;
    if(v=="X1"||v=="MOUSE4") return VK_XBUTTON1;
    if(v=="X2"||v=="MOUSE5") return VK_XBUTTON2;
    if(v=="SHIFT") return VK_SHIFT;
    if(v=="CTRL"||v=="CONTROL") return VK_CONTROL;
    if(v=="ALT") return VK_MENU;
    if(v=="SPACE") return VK_SPACE;
    if(v=="CAPSLOCK") return VK_CAPITAL;
    if(v=="ESCAPE") return VK_ESCAPE;
    if(v=="INSERT") return VK_INSERT;
    if(v=="END") return VK_END;
    if(v=="HOME") return VK_HOME;
    if(v=="PAGEUP") return VK_PRIOR;
    if(v=="PAGEDOWN") return VK_NEXT;
    if(v.size()==2 && v[0]=='F' && v[1]>='1' && v[1]<='9') return VK_F1 + (v[1]-'1');
    if(v.size()==3 && v[0]=='F' && v[1]=='1' && v[2]>='0' && v[2]<='2') return VK_F10 + (v[2]-'0');
    int num; if(ParseInt(v, num)) return num;
    return -1;
}

static void ApplyConfigKeyFromElectron(const char* key, const char* value){
    if(!key||!value) return;
    std::string k(key), val(value);
    bool ok = true;
    bool rcsXSet = false, rcsYSet = false;
    if(LoadConfigKeyEsp(k, val, ok)) return;
    if(LoadConfigKeyChams(k, val, ok)) return;
    if(LoadConfigKeyAimbot(k, val, ok, rcsXSet, rcsYSet)) return;
    if(LoadConfigKeyMovement(k, val, ok)) return;
    if(LoadConfigKeyVisual(k, val, ok)) return;
    if(LoadConfigKeySkins(k, val, ok)) return;
    LoadConfigKeyMisc(k, val, ok);
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
            if(p.type==2 || p.type==3){/* twinkle only */}
            if(p.type==4){p.worldVel.z -= 12.f * dt;}

            float baseZ = g_localOrigin.z + worldFloor;
            float topZ = baseZ + worldHeight;
            if(p.type==4){
                if(p.worldPos.z < baseZ) p.lifetime = 0.f;
            }else if(p.worldPos.z < baseZ){
                p.worldPos.z = topZ + Randf(0.f, worldHeight*0.15f);
                p.worldPos.x = g_localOrigin.x;
                p.worldPos.y = g_localOrigin.y;
                spawnWorld(p);
            }
            if(p.type!=4){
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
    UINT64 elapsed = GetTickCount64() - g_lastHitmarkerTime;
    float durMs = g_hitmarkerDuration * 1000.f;
    if(elapsed >= (UINT64)durMs) { g_lastHitmarkerTime = 0; return; }
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
    UINT64 elapsed = GetTickCount64() - g_lastKillEffectTime;
    float durMs = g_killEffectDuration * 1000.f;
    if(elapsed >= (UINT64)durMs) { g_lastKillEffectTime = 0; return; }
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
    UINT64 now = GetTickCount64();
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

static void DrawWatermark(float sw){
    if(!g_watermarkEnabled)return;
    ImDrawList* dl = ImGui::GetForegroundDrawList(); if(!dl) return;
    ImFont* fBold = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
    ImFont* fReg  = font::lexend_regular ? font::lexend_regular : ImGui::GetFont();
    ImGuiIO& io   = ImGui::GetIO();

    SYSTEMTIME st{}; GetLocalTime(&st);
    char timeBuf[16]; std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", st.wHour, st.wMinute);

    const float fps    = io.Framerate;
    const float padX   = 14.f;
    const float padY   = 8.f;
    const float rnd    = 8.f;
    const float accentBarH = 2.f;
    const float dotR   = 2.f;

    const ImU32 colBg     = IM_COL32(10, 10, 12, 245);
    const ImU32 colBorder = IM_COL32(38, 38, 48, 255);
    const ImU32 colAccent = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    const ImU32 colText   = IM_COL32(220, 225, 235, 255);
    const ImU32 colDim    = IM_COL32(110, 115, 130, 255);
    const ImU32 colDot    = IM_COL32(55, 58, 70, 255);

    // FPS color: green >120, yellow >60, red <=60
    ImU32 colFps;
    if     (fps > 120.f) colFps = IM_COL32(80, 210, 120, 255);
    else if(fps >  60.f) colFps = IM_COL32(230, 185, 60, 255);
    else                 colFps = IM_COL32(220, 70, 70, 255);

    // Measure text
    const char* label = "LITWARE";
    ImVec2 szLabel = fBold->CalcTextSizeA(fBold->LegacySize, FLT_MAX, 0.f, label);
    char fpsBuf[16]; std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", fps);
    char fpsUnit[] = "fps";
    ImVec2 szFpsNum  = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, fpsBuf);
    ImVec2 szFpsUnit = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, fpsUnit);
    ImVec2 szTime    = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, timeBuf);

    const float innerGap = 10.f;
    const float dotDiam  = dotR * 2.f;

    float totalW = szLabel.x;
    if(g_showFpsWatermark){
        totalW += innerGap + dotDiam + innerGap;   // dot
        totalW += szFpsNum.x + 2.f + szFpsUnit.x; // "120fps"
        totalW += innerGap + dotDiam + innerGap;   // dot
        totalW += szTime.x;
    }

    float barH = (std::max)({szLabel.y, szFpsNum.y, szTime.y}) + padY * 2.f;
    float barW = totalW + padX * 2.f;
    barW = (std::max)(barW, 120.f);

    float x = sw - barW - 15.f;
    float y = 15.f;

    // Shadow
    dl->AddRectFilled({x-3.f, y-3.f}, {x+barW+3.f, y+barH+3.f}, IM_COL32(0,0,0,60), rnd+2.f);

    // Background + border
    dl->AddRectFilled({x, y}, {x+barW, y+barH}, colBg, rnd);
    dl->AddRect({x, y}, {x+barW, y+barH}, colBorder, rnd, 0, 1.f);

    // Accent line bottom
    dl->AddRectFilled({x+rnd, y+barH-accentBarH}, {x+barW-rnd, y+barH}, colAccent, accentBarH*0.5f);

    // Draw contents
    float cx   = x + padX;
    float midY = y + barH * 0.5f;

    // "LITWARE" in accent, bold
    dl->AddText(fBold, fBold->LegacySize, {cx, midY - szLabel.y * 0.5f}, colAccent, label);
    cx += szLabel.x;

    if(g_showFpsWatermark){
        // dot separator
        cx += innerGap;
        dl->AddCircleFilled({cx + dotR, midY}, dotR, colDot);
        cx += dotDiam + innerGap;

        // fps number (colored) + "fps" unit (dim)
        dl->AddText(fReg, fReg->LegacySize, {cx, midY - szFpsNum.y * 0.5f}, colFps, fpsBuf);
        cx += szFpsNum.x + 2.f;
        dl->AddText(fReg, fReg->LegacySize, {cx, midY - szFpsUnit.y * 0.5f}, colDim, fpsUnit);
        cx += szFpsUnit.x;

        // dot separator
        cx += innerGap;
        dl->AddCircleFilled({cx + dotR, midY}, dotR, colDot);
        cx += dotDiam + innerGap;

        // time
        dl->AddText(fReg, fReg->LegacySize, {cx, midY - szTime.y * 0.5f}, colText, timeBuf);
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
    (void)vm;
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
        ImU32 accent=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(200*alpha));
        float belowY = bb + 3.f * s;
        // Shadow + glow layers (ESP)
        dl->AddRectFilled({bl+3.f,bt2+3.f},{br+3.f,bb+3.f},IM_COL32(0,0,0,(int)(45*alpha)));
        dl->AddRectFilled({bl+2.f,bt2+2.f},{br+2.f,bb+2.f},IM_COL32(0,0,0,(int)(50*alpha)));
        for(int g=4;g>=1;g--){
            int r=(boxCol>>IM_COL32_R_SHIFT)&0xFF,g_=(boxCol>>IM_COL32_G_SHIFT)&0xFF,b=(boxCol>>IM_COL32_B_SHIFT)&0xFF;
            int ga=(g==4)?50:(g==3)?35:(g==2)?20:10;
            dl->AddRect({bl-(float)g,bt2-(float)g},{br+(float)g,bb+(float)g},IM_COL32(r,g_,b,(int)(ga*alpha)),0.f,0,1.5f);
        }
        if(g_espBoxStyle==0){
            DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
            DrawCornerBox(dl,bl,bt2,br,bb,boxCol,boxThick);
        }
        else if(g_espBoxStyle==1){
            dl->AddRect({bl,bt2},{br,bb},IM_COL32(0,0,0,(int)(200*alpha)),0.f,0,boxThick+1.0f);
            dl->AddRect({bl,bt2},{br,bb},boxCol,0.f,0,boxThick);
        }
        else if(g_espBoxStyle==2){
            dl->AddRectFilled({bl,bt2},{br,bb},IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(35*alpha)));
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
            for(int gl=3;gl>=1;gl--) dl->AddLine({sx+(float)gl,sy+(float)gl},{e.feet_x+(float)gl,e.feet_y+(float)gl},IM_COL32(0,0,0,(int)(40*alpha/(float)gl)),(1.2f+(float)gl*0.3f)*s);
            dl->AddLine({sx,sy},{e.feet_x,e.feet_y},IM_COL32(r,g_,b,(int)(100*alpha)),0.8f*s);
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
            float padX = 6.f * s, padY = 3.f * s;
            float tx=cx-ts.x*0.5f,ty=bt2-ts.y-6.f*s;
            ImVec2 bgMin{tx - padX, ty - padY};
            ImVec2 bgMax{tx + ts.x + padX, ty + ts.y + padY};
            dl->AddRectFilled(bgMin, bgMax, IM_COL32(12,12,16,(int)(140*alpha)), 4.f * s);
            dl->AddRect(bgMin, bgMax, IM_COL32(0,0,0,(int)(200*alpha)), 4.f * s);
            dl->AddRectFilled({bgMin.x, bgMax.y - 1.f*s}, {bgMax.x, bgMax.y}, accent, 3.f*s);
            for(int glow=3;glow>=1;glow--){
                float o=(float)glow; int glowA=(int)(60*alpha/(float)glow);
                dl->AddText(font,nameSize,{tx+o,ty},IM_COL32(0,0,0,glowA),e.name);
                dl->AddText(font,nameSize,{tx-o,ty},IM_COL32(0,0,0,glowA),e.name);
            }
            dl->AddText(font,nameSize,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(150*alpha)),e.name);
            dl->AddText(font,nameSize,{tx,ty},IM_COL32(220,220,230,(int)(alpha*255)),e.name);
        }
        if(e.planting||e.flashed||e.scoped||e.defusing||e.hasBomb||e.hasKits){
            ImFont* sf = espFont;
            float tagX = br + 8.f * s;  // draw status tags to the right of the box
            float tagY = bt2;
            float tagSize = 10.f * s;
            ImU32 tagCol = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(200*alpha));
            auto drawTag=[&](const char* t){
                for(int sh=2;sh>=1;sh--) dl->AddText(sf,tagSize,{tagX+(float)sh,tagY+(float)sh},IM_COL32(0,0,0,(int)(90*alpha/(float)sh)),t);
                dl->AddText(sf,tagSize,{tagX+1.f,tagY+1.f},IM_COL32(0,0,0,(int)(140*alpha)),t);
                dl->AddText(sf,tagSize,{tagX,tagY},tagCol,t);
                tagY+=12.f * s;
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
            if(g_espWeaponIcon && winfo.icon && winfo.icon[0]){
                iconText = winfo.icon;
                iconFont = textFont;
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
        if(g_espSpotted&&e.spotted){
            dl->AddCircleFilled({br+6.f,bt2+6.f},3.f,IM_COL32(90,220,130,(int)(200*alpha)));
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
    io.ConfigErrorRecoveryEnableAssert=false;  // avoid crash on SetCursorPos/SetCursorScreenPos boundary (Pido)
    ImFontConfig fc{};fc.SizePixels=17.f;fc.FontDataOwnedByAtlas=false;
    font::lexend_bold = io.Fonts->AddFontFromMemoryTTF((void*)lexend_bold, (int)sizeof(lexend_bold), 17.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    fc.SizePixels=14.f;
    font::lexend_regular = io.Fonts->AddFontFromMemoryTTF((void*)lexend_regular, (int)sizeof(lexend_regular), 14.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    font::esp_mono = io.Fonts->AddFontFromMemoryTTF((void*)jetbrains_mono_regular, (int)sizeof(jetbrains_mono_regular), 14.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
    fc.SizePixels=20.f;
    font::icomoon = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 20.f, &fc, io.Fonts->GetGlyphRangesDefault());
    fc.SizePixels=15.f;
    font::icomoon_widget = io.Fonts->AddFontFromMemoryTTF((void*)icomoon_widget, (int)sizeof(icomoon_widget), 15.f, &fc, io.Fonts->GetGlyphRangesDefault());
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
    g_menuLaunched=true; // electron запущен из entry.cpp заранее
    ElectronBridge_SetApply(ApplyConfigKeyFromElectron);
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
    if(!g_firstFrame){DebugLog("[LitWare] first Present");g_firstFrame=true;g_telegramNoteStart=GetTickCount64();}
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
                UINT64 now = GetTickCount64();
                if(now - g_lastRtvFail > 2000){
                    PushNotification("RTV create failed", IM_COL32(255,80,80,255));
                    g_lastRtvFail = now;
                }
            }
        }if(!g_rtv)return;
    }
    bool gameFocused = (g_gameHwnd && GetForegroundWindow() == g_gameHwnd);
    bool menuFocused = ElectronBridge_IsMenuFocused();
    bool overlayVisible = gameFocused || menuFocused;
    static bool s_wasVisible = true;
    static UINT64 s_pendingHideTick = 0;
    if (overlayVisible) {
        s_pendingHideTick = 0;
        if (!s_wasVisible) { s_wasVisible = true; ElectronBridge_SendVisibility(true); }
    } else {
        if (s_wasVisible) {
            if (!s_pendingHideTick) s_pendingHideTick = GetTickCount64();
            else if ((GetTickCount64() - s_pendingHideTick) >= 150) {
                s_pendingHideTick = 0;
                s_wasVisible = false;
                ElectronBridge_SendVisibility(false);
            }
        }
    }
    if (!overlayVisible) return;
    if(GetAsyncKeyState(VK_INSERT)&1){
        g_menuOpen=!g_menuOpen;
        if(g_menuOpen && !g_menuLaunched){ g_menuLaunched=true; ElectronBridge_LaunchMenu(); }
        ElectronBridge_SendMenuOpen(g_menuOpen);
    }
    // F1 keybinds popup disabled
    if(GetAsyncKeyState(VK_END)&1){RequestUnload();return;}
    if(!g_safeMode){
        BuildESPData();BuildSpectatorList();ProcessHitEvents();UpdateBombInfo();UpdateSoundPings();
        RunNoFlash();RunNoSmoke();RunGlow();RunRadarHack();RunSkinChanger();
        RunBHop();
        RunFOVChanger();
        RunAutostop();RunRCS();RunAimbot();RunStrafeHelper();RunTriggerBot();ReleaseTriggerAttack();RunDoubleTap();
    }else{g_esp_count=0;g_esp_oof_count=0;}
    ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
    ImGuiIO&io=ImGui::GetIO();
    io.MouseDrawCursor = g_showDebugConsole;
    if(!g_menuOpen) ClipCursor(nullptr);
    float sw=(float)g_esp_screen_w, sh=(float)g_esp_screen_h;
    UpdateAndDrawParticles(io.DeltaTime, sw, sh);
    DrawDebugConsole();
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
    UINT64 now = GetTickCount64();
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
    UINT64 now = GetTickCount64();
    if(now - g_lastClientHookAttempt < 2000) return;
    g_lastClientHookAttempt = now;
    EnsureModules();
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    uintptr_t clientBase = (uintptr_t)client;
    (void)clientBase;
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
    ElectronBridge_SetApply(ApplyConfigKeyFromElectron);
    DebugLog("[LitWare] Present hook installed");return true;
}
void Shutdown(){RequestUnload();}
}
 
