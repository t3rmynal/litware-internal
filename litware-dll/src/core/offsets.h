#pragma once
#include <cstdint>

// All offsets from cs2-dumper (2026-03-12)
// Source: offsets/output/offsets.hpp, client_dll.hpp, buttons.hpp
// Copied from aporia-external/src/core/offsets.h

namespace offsets {

    // -- Module offsets (client.dll) --
    namespace client {
        constexpr uintptr_t dwCSGOInput              = 0x2318FC0;
        constexpr uintptr_t dwEntityList             = 0x24AE268;
        constexpr uintptr_t dwGlobalVars             = 0x205D5C0;
        constexpr uintptr_t dwLocalPlayerController  = 0x22F3178;
        constexpr uintptr_t dwLocalPlayerPawn        = 0x2068B60;
        constexpr uintptr_t dwViewAngles             = 0x2319648;
        constexpr uintptr_t dwViewMatrix             = 0x230EF20;
        constexpr uintptr_t dwGlowManager            = 0x2309CE8;
    }

    // -- Module offsets (engine2.dll) --
    namespace engine2 {
        constexpr uintptr_t dwWindowWidth  = 0x90C8D8;
        constexpr uintptr_t dwWindowHeight = 0x90C8DC;
    }

    // -- Button commands (client.dll) --
    namespace buttons {
        constexpr uintptr_t attack  = 0x20618F0;
        constexpr uintptr_t attack2 = 0x2061980;
        constexpr uintptr_t jump    = 0x2061E00;
        constexpr uintptr_t duck    = 0x2061E90;
    }

    // -- C_BaseEntity --
    namespace base_entity {
        constexpr uintptr_t m_CBodyComponent   = 0x38;
        constexpr uintptr_t m_iHealth         = 0x354;
        constexpr uintptr_t m_lifeState       = 0x35C;
        constexpr uintptr_t m_iTeamNum        = 0x3F3;
        constexpr uintptr_t m_fFlags          = 0x400;
        constexpr uintptr_t m_vecVelocity     = 0x438;
        constexpr uintptr_t m_pGameSceneNode  = 0x338;
    }

    // -- CGameSceneNode --
    namespace scene_node {
        constexpr uintptr_t m_vecAbsOrigin = 0xD0;
        constexpr uintptr_t m_angRotation  = 0xC0;
    }

    // -- CBodyComponent / CBodyComponentSkeletonInstance --
    namespace body_component {
        constexpr uintptr_t m_pSceneNode       = 0x8;
        constexpr uintptr_t m_skeletonInstance = 0x80; // CBodyComponentSkeletonInstance::m_skeletonInstance
    }

    // -- CSkeletonInstance --
    namespace skeleton_instance {
        constexpr uintptr_t m_modelState = 0x160; // CSkeletonInstance::m_modelState
    }

    // -- CModelState (internal) --
    namespace model_state {
        constexpr uintptr_t m_pBones = 0x80; // CModelState::m_pBones (non-networked)
    }

    // -- C_BasePlayerPawn --
    namespace base_pawn {
        constexpr uintptr_t m_pCameraServices   = 0x1410;
        constexpr uintptr_t m_pObserverServices = 0x13F0;
        constexpr uintptr_t m_pWeaponServices   = 0x13D8;
        constexpr uintptr_t m_vOldOrigin        = 0x1588;
        constexpr uintptr_t m_vecViewOffset     = 0xD58;
    }

    // -- CPlayer_ObserverServices --
    namespace observer {
        constexpr uintptr_t m_iObserverMode           = 0x48;
        constexpr uintptr_t m_hObserverTarget         = 0x4C;  // CHandle<C_BaseEntity> - who we spectate
        constexpr uintptr_t m_flObserverChaseDistance = 0x58;
        constexpr uintptr_t m_bForcedObserverMode     = 0x54;
    }

    // -- C_CSPlayerPawnBase --
    namespace cs_pawn_base {
        constexpr uintptr_t m_flFlashDuration = 0x15F8;
        constexpr uintptr_t m_flFlashMaxAlpha = 0x15FC;
        constexpr uintptr_t m_flLastSmokeOverlayAlpha = 0x1618;  // smoke overlay alpha (0=invisible)
        constexpr uintptr_t m_pGlowServices   = 0x1678;
    }

    // -- C_CSPlayerPawn --
    namespace cs_pawn {
        constexpr uintptr_t m_aimPunchAngle = 0x16CC;
        constexpr uintptr_t m_bIsPlantingViaUse = 0x1F51;
        constexpr uintptr_t m_bIsScoped     = 0x26F8;
        constexpr uintptr_t m_iShotsFired   = 0x270C;
        constexpr uintptr_t m_iIDEntIndex   = 0x3EAC;
    }

    // -- EntitySpottedState_t (inside C_CSPlayerPawn) --
    namespace spotted {
        constexpr uintptr_t m_entitySpottedState = 0x26E0;  // C_CSPlayerPawn (verified 2026-03-12)
        constexpr uintptr_t m_bSpotted           = 0x8;     // offset within EntitySpottedState_t
        constexpr uintptr_t m_bSpottedByMask     = 0xC;     // uint32[2]
    }

    // -- CCSPlayerController --
    namespace controller {
        constexpr uintptr_t m_sSanitizedPlayerName = 0x860;
        constexpr uintptr_t m_hPlayerPawn          = 0x90C;
        constexpr uintptr_t m_hObserverPawn        = 0x910;
        constexpr uintptr_t m_bPawnIsAlive         = 0x914;
        constexpr uintptr_t m_iPawnHealth          = 0x918;
        constexpr uintptr_t m_pInGameMoneyServices = 0x808;
        constexpr uintptr_t m_bPawnHasDefuser      = 0x920;  // defuse kit
    }

    // -- CCSPlayerController_InGameMoneyServices --
    namespace money_services {
        constexpr uintptr_t m_iAccount = 0x40;
    }

    // -- C_BaseModelEntity --
    namespace model_entity {
        constexpr uintptr_t m_Glow = 0xCC0;
        constexpr uintptr_t m_pClientAlphaProperty = 0xE38;
    }
    // -- CClientAlphaProperty --
    namespace client_alpha_prop {
        constexpr uintptr_t m_nAlpha = 0x17;
    }

    // -- CGlowProperty --
    namespace glow_prop {
        constexpr uintptr_t m_glowColorOverride = 0x40;
        constexpr uintptr_t m_bGlowing          = 0x51;
    }

    // -- CPlayer_CameraServices / CCSPlayerBase_CameraServices --
    namespace camera {
        constexpr uintptr_t m_iFOV = 0x290;
        constexpr uintptr_t m_thirdPersonHeading = 0x24D0;  // QAngle - for third person camera
    }

    // -- CCSGOInput --
    namespace csgo_input {
        constexpr uintptr_t m_in_thirdperson = 0x441;
        constexpr uintptr_t m_third_person_angles = 0x258;
    }

    // -- CPlayer_WeaponServices --
    namespace weapon_services {
        constexpr uintptr_t m_hActiveWeapon = 0x60;
        constexpr uintptr_t m_hMyWeapons   = 0x48;   // C_NetworkUtlVectorBase<CHandle>
    }

    // -- C_BasePlayerWeapon --
    namespace base_weapon {
        constexpr uintptr_t m_iClip1      = 0x18D0;
        constexpr uintptr_t m_pReserveAmmo = 0x18D8;
    }

    // -- C_EconEntity / C_AttributeContainer / C_EconItemView --
    namespace econ_entity {
        constexpr uintptr_t m_AttributeManager = 0x1378;
        constexpr uintptr_t m_OriginalOwnerXuidLow = 0x1848;
        constexpr uintptr_t m_nFallbackPaintKit = 0x1850;
        constexpr uintptr_t m_nFallbackSeed = 0x1854;
        constexpr uintptr_t m_flFallbackWear = 0x1858;
        constexpr uintptr_t m_nFallbackStatTrak = 0x185C;
    }
    namespace attribute_container {
        constexpr uintptr_t m_Item = 0x50;
    }
    namespace econ_item_view {
        constexpr uintptr_t m_iItemDefinitionIndex = 0x1BA;
        constexpr uintptr_t m_iItemIDHigh = 0x1D0;
        constexpr uintptr_t m_iItemIDLow = 0x1D4;
        constexpr uintptr_t m_iAccountID = 0x1D8;
        constexpr uintptr_t m_iEntityQuality = 0x1BC;
        constexpr uintptr_t m_bInitialized = 0x1E8;
        constexpr uintptr_t m_AttributeList = 0x208;
    }

    // -- C_PlantedC4 --
    namespace planted_c4 {
        constexpr uintptr_t m_bBombTicking   = 0x1170;
        constexpr uintptr_t m_nBombSite      = 0x1174;
        constexpr uintptr_t m_flC4Blow       = 0x11A0;
        constexpr uintptr_t m_bBeingDefused  = 0x11AC;
        constexpr uintptr_t m_flDefuseCountDown = 0x11C0;
        constexpr uintptr_t m_hBombDefuser   = 0x11C8;  // CHandle to defusing pawn
    }

    // -- C_SmokeGrenadeProjectile --
    namespace smoke_projectile {
        constexpr uintptr_t m_bSmokeEffectSpawned = 0x1499;
    }
}
