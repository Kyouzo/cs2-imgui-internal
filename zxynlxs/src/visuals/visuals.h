#pragma once

#include <cstddef>
#include <cstdint>
#include "../math/math.h"

namespace offsets
{
	// Generated offsets verified against a2x/cs2-dumper main (2026-11-05).
	// https://github.com/a2x/cs2-dumper/tree/main/output

	constexpr std::ptrdiff_t dwEntityList = 0x271FBB8;
	constexpr std::ptrdiff_t dwLocalPlayerController = 0x2542548;
	constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x256A948;
	constexpr std::ptrdiff_t dwViewAngles = 0x2581098;
	constexpr std::ptrdiff_t dwViewMatrix = 0x2570250;

	constexpr std::ptrdiff_t m_hPlayerPawn = 0x92C; // CHandle<C_CSPlayerPawn>
	constexpr std::ptrdiff_t m_hObserverPawn = 0x930; // CHandle<C_CSObserverPawn>
	constexpr std::ptrdiff_t m_iszPlayerName = 0x6FC; // char[128], CBasePlayerController
	constexpr std::ptrdiff_t m_iTeamNum = 0x3E7; // uint8
	constexpr std::ptrdiff_t m_bDormant = 0x103; // bool, CEntityInstance
	constexpr std::ptrdiff_t m_vOldOrigin = 0x14A4; // Vector
	constexpr std::ptrdiff_t m_vecAbsOrigin = 0xC8; // CGameSceneNode::m_vecAbsOrigin
	constexpr std::ptrdiff_t m_vecViewOffset = 0xF60; // C_BaseModelEntity::m_vecViewOffset
	constexpr std::ptrdiff_t m_iHealth = 0x34C; // int32
	constexpr std::ptrdiff_t m_lifeState = 0x354; // uint8
	constexpr std::ptrdiff_t m_pGameSceneNode = 0x330; // CGameSceneNode*
	constexpr std::ptrdiff_t m_pObserverServices = 0x1308; // CPlayer_ObserverServices*
	constexpr std::ptrdiff_t m_hObserverTarget = 0x4C; // CHandle<C_BaseEntity>, relative to observer services
	constexpr std::ptrdiff_t m_modelState = 0x140; // CSkeletonInstance::m_modelState
	constexpr std::ptrdiff_t m_boneCache = 0x80; // CModelState bone cache pointer

	// EntitySpottedState_t (C_CSPlayerPawn, 2026-11-05 dump)
	constexpr std::ptrdiff_t m_entitySpottedState = 0x1E88; // EntitySpottedState_t
	constexpr std::ptrdiff_t m_bSpotted            = 0x08;  // bool, within EntitySpottedState_t

	// C_CSPlayerPawn fields (2026-11-05 dump)
	constexpr std::ptrdiff_t m_flFlashDuration  = 0x1510; // float32
	constexpr std::ptrdiff_t m_bIsScoped        = 0x1EA0; // bool
	constexpr std::ptrdiff_t m_iShotsFired      = 0x1EB8; // int32
	constexpr std::ptrdiff_t m_bGunGameImmunity  = 0x3508; // bool
	constexpr std::ptrdiff_t m_angEyeAngles      = 0x3600; // QAngle (full eye orientation, NOT for RCS)

	// RCS: pawn → CCSPlayer_AimPunchServices* → m_predictableBaseAngle (dedicated recoil punch)
	constexpr std::ptrdiff_t m_pAimPunchServices = 0x1598; // CCSPlayer_AimPunchServices*
	constexpr std::ptrdiff_t m_aimPunchAngle     = 0x50;   // QAngle within CCSPlayer_AimPunchServices

	constexpr std::ptrdiff_t dwCSGOInput    = 0x2580A10; // CCSGOInput* pointer

	constexpr std::ptrdiff_t dwWindowHeight = 0x9BCFEC;
	constexpr std::ptrdiff_t dwWindowWidth = 0x9BCFE8;
	constexpr std::ptrdiff_t dwNetworkGameClient = 0x9B88C0;
	constexpr std::ptrdiff_t dwNetworkGameClient_signOnState = 0x230;

}

namespace visuals
{
	extern uintptr_t clientModule;
	extern uintptr_t engineModule;

	void PlayerESP(bool renderPlayers = true);
	bool FindBestAimPoint(const Vec3& currentView, Vec3& targetAngles,
		bool enemiesOnly, int targetBone, float maxFov);
}
