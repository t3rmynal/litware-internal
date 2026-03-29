#pragma once
#include <cstdint>

// обновлено 2026 03 29 (Linux CS2)
namespace offsets {

    namespace client {
        constexpr uintptr_t dwCSGOInput             = 0x231B2E0;
        constexpr uintptr_t dwEntityList            = 0x24B0258;
        constexpr uintptr_t dwGlobalVars            = 0x205F540;
        constexpr uintptr_t dwLocalPlayerController = 0x22F5028;
        constexpr uintptr_t dwLocalPlayerPawn       = 0x206A9E0;
        constexpr uintptr_t dwViewAngles            = 0x231B968;
        constexpr uintptr_t dwViewMatrix            = 0x2310F10;
        constexpr uintptr_t dwGlowManager           = 0x230BCD8;
        constexpr uintptr_t dwPlantedC4             = 0x2318A60;
        constexpr uintptr_t dwGameRules             = 0x230EED0;
        constexpr uintptr_t dwPrediction            = 0x206A8F0;
        constexpr uintptr_t dwViewRender            = 0x2311328;
        constexpr uintptr_t dwWeaponC4              = 0x229A2B0;
        constexpr uintptr_t dwSensitivity           = 0x230C7E8;
    }

    namespace engine2 {
        constexpr uintptr_t dwWindowWidth  = 0x90D998;
        constexpr uintptr_t dwWindowHeight = 0x90D99C;
        constexpr uintptr_t dwBuildNumber  = 0x60E514;
        constexpr uintptr_t dwNetworkGameClient = 0x9095D0;
    }

    namespace buttons {
        constexpr uintptr_t attack   = 0x2063760;
        constexpr uintptr_t attack2  = 0x20637F0;
        constexpr uintptr_t forward  = 0x20639A0;
        constexpr uintptr_t back     = 0x2063A30;
        constexpr uintptr_t left     = 0x2063AC0;
        constexpr uintptr_t right    = 0x2063B50;
        constexpr uintptr_t jump     = 0x2063C70;
        constexpr uintptr_t duck     = 0x2063D00;
        constexpr uintptr_t reload   = 0x20636D0;
        constexpr uintptr_t sprint   = 0x2063640;
        constexpr uintptr_t use      = 0x2063BE0;
    }

    namespace base_entity {
        constexpr uintptr_t m_CBodyComponent  = 0x38;
        constexpr uintptr_t m_iHealth         = 0x354;
        constexpr uintptr_t m_lifeState       = 0x35C;
        constexpr uintptr_t m_iTeamNum        = 0x3F3;
        constexpr uintptr_t m_fFlags          = 0x400;
        constexpr uintptr_t m_vecVelocity     = 0x438;
        constexpr uintptr_t m_pGameSceneNode  = 0x338;
    }

    namespace scene_node {
        constexpr uintptr_t m_vecAbsOrigin = 0xD0;
        constexpr uintptr_t m_angRotation  = 0xC0;
    }

    namespace body_component {
        constexpr uintptr_t m_pSceneNode       = 0x8;
        constexpr uintptr_t m_skeletonInstance = 0x80;
    }

    namespace skeleton_instance {
        constexpr uintptr_t m_modelState = 0x160;
    }

    namespace model_state {
        constexpr uintptr_t m_pBones = 0x80;  // если сломаются кости, это место надо проверить
    }

    namespace base_pawn {
        constexpr uintptr_t m_pCameraServices   = 0x1410;
        constexpr uintptr_t m_pObserverServices = 0x13F0;
        constexpr uintptr_t m_pWeaponServices   = 0x13D8;
        constexpr uintptr_t m_vOldOrigin        = 0x1588;
        constexpr uintptr_t m_vecViewOffset     = 0xD58;
    }

    namespace observer {
        constexpr uintptr_t m_iObserverMode           = 0x48;
        constexpr uintptr_t m_hObserverTarget         = 0x4C;
        constexpr uintptr_t m_flObserverChaseDistance = 0x58;
        constexpr uintptr_t m_bForcedObserverMode     = 0x54;
    }

    namespace cs_pawn_base {
        constexpr uintptr_t m_flFlashDuration         = 0x15F8;
        constexpr uintptr_t m_flFlashMaxAlpha         = 0x15F4;
        constexpr uintptr_t m_flLastSmokeOverlayAlpha = 0x1618;
    }

    namespace cs_pawn {
        constexpr uintptr_t m_aimPunchAngle       = 0x16CC;
        constexpr uintptr_t m_bIsPlantingViaUse   = 0x1F51;  // поле пока не подтверждено
        constexpr uintptr_t m_bIsScoped           = 0x26F8;
        constexpr uintptr_t m_iShotsFired         = 0x270C;
        constexpr uintptr_t m_iIDEntIndex         = 0x3EAC;
        constexpr uintptr_t m_pGlowServices       = 0x1678;
        constexpr uintptr_t m_thirdPersonHeading  = 0x24D0;
    }

    namespace spotted {
        constexpr uintptr_t m_entitySpottedState = 0x26E0;
        constexpr uintptr_t m_bSpotted           = 0x8;
        constexpr uintptr_t m_bSpottedByMask     = 0xC;
    }

    namespace controller {
        constexpr uintptr_t m_sSanitizedPlayerName = 0x860;
        constexpr uintptr_t m_hPlayerPawn          = 0x90C;
        constexpr uintptr_t m_hObserverPawn        = 0x910;
        constexpr uintptr_t m_bPawnIsAlive         = 0x914;
        constexpr uintptr_t m_iPawnHealth          = 0x918;
        constexpr uintptr_t m_pInGameMoneyServices = 0x808;
        constexpr uintptr_t m_bPawnHasDefuser      = 0x920;
    }

    namespace money_services {
        constexpr uintptr_t m_iAccount = 0x40;
    }

    namespace model_entity {
        constexpr uintptr_t m_Glow                = 0xCC0;
        constexpr uintptr_t m_pClientAlphaProperty = 0xE38;
    }

    namespace client_alpha_prop {
        constexpr uintptr_t m_nAlpha = 0x17;
    }

    namespace glow_prop {
        constexpr uintptr_t m_glowColorOverride = 0x40;
        constexpr uintptr_t m_bGlowing          = 0x51;
    }

    namespace camera {
        constexpr uintptr_t m_iFOV = 0x290;
    }

    namespace csgo_input {
        constexpr uintptr_t m_in_thirdperson      = 0x441;
        constexpr uintptr_t m_third_person_angles = 0x258;
    }

    namespace weapon_services {
        constexpr uintptr_t m_hActiveWeapon = 0x60;
        constexpr uintptr_t m_hMyWeapons    = 0x48;
    }

    namespace base_weapon {
        constexpr uintptr_t m_iClip1       = 0x18D0;
        constexpr uintptr_t m_pReserveAmmo = 0x18D8;
    }

    namespace econ_entity {
        constexpr uintptr_t m_AttributeManager      = 0x1378;
        constexpr uintptr_t m_OriginalOwnerXuidLow  = 0x1848;
        constexpr uintptr_t m_nFallbackPaintKit     = 0x1850;
        constexpr uintptr_t m_nFallbackSeed         = 0x1854;
        constexpr uintptr_t m_flFallbackWear        = 0x1858;
        constexpr uintptr_t m_nFallbackStatTrak     = 0x185C;
    }

    namespace attribute_container {
        constexpr uintptr_t m_Item = 0x50;
    }

    namespace econ_item_view {
        constexpr uintptr_t m_iItemDefinitionIndex = 0x1BA;
        constexpr uintptr_t m_iItemIDHigh          = 0x1D0;
        constexpr uintptr_t m_iItemIDLow           = 0x1D4;
        constexpr uintptr_t m_iAccountID           = 0x1D8;
        constexpr uintptr_t m_iEntityQuality       = 0x1BC;
        constexpr uintptr_t m_bInitialized         = 0x1E8;
        constexpr uintptr_t m_AttributeList        = 0x208;
    }

    namespace planted_c4 {
        constexpr uintptr_t m_bBombTicking     = 0x1170;
        constexpr uintptr_t m_nBombSite        = 0x1174;
        constexpr uintptr_t m_flC4Blow         = 0x11A0;
        constexpr uintptr_t m_bBeingDefused    = 0x11AC;
        constexpr uintptr_t m_flDefuseCountDown = 0x11C0;
        constexpr uintptr_t m_hBombDefuser     = 0x11C8;
    }

    namespace smoke_projectile {
        constexpr uintptr_t m_bSmokeEffectSpawned = 0x1499;
    }
}
