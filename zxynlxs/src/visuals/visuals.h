#pragma once

#include <cstddef>
#include <cstdint>
#include "../math/math.h"
#include "../signatures/signatures.h"

namespace offsets
{
	// Global offsets resolved from signatures at runtime (see signatures/).
	// These reference the signatures:: namespace so they stay up-to-date
	// across game patches without manual offset updates.

	inline std::ptrdiff_t dwEntityList()             { return signatures::dwEntityList; }
	inline std::ptrdiff_t dwLocalPlayerController()  { return signatures::dwLocalPlayerController; }
	inline std::ptrdiff_t dwLocalPlayerPawn()         { return signatures::dwLocalPlayerPawn; }
	inline std::ptrdiff_t dwViewAngles()              { return signatures::dwViewAngles; }
	inline std::ptrdiff_t dwViewMatrix()              { return signatures::dwViewMatrix; }
	inline std::ptrdiff_t dwCSGOInput()               { return signatures::dwCSGOInput; }
	inline std::ptrdiff_t dwGameEntitySystem()        { return signatures::dwGameEntitySystem; }
	inline std::ptrdiff_t dwWindowHeight()            { return signatures::dwWindowHeight; }
	inline std::ptrdiff_t dwWindowWidth()             { return signatures::dwWindowWidth; }
	inline std::ptrdiff_t dwNetworkGameClient()       { return signatures::dwNetworkGameClient; }
	inline std::ptrdiff_t dwNetworkGameClient_signOnState() { return signatures::dwNetworkGameClient_signOnState; }

	// Schema field offsets — these come from the CS2 schema dump and are
	// relatively stable between patches. Update after a schema-breaking update.
	constexpr std::ptrdiff_t m_hPlayerPawn = 0x92C;
	constexpr std::ptrdiff_t m_hObserverPawn = 0x930;
	constexpr std::ptrdiff_t m_iszPlayerName = 0x6FC;
	constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
	constexpr std::ptrdiff_t m_bDormant = 0x103;
	constexpr std::ptrdiff_t m_vOldOrigin = 0x14A4;
	constexpr std::ptrdiff_t m_vecAbsOrigin = 0xC8;
	constexpr std::ptrdiff_t m_vecViewOffset = 0xF60;
	constexpr std::ptrdiff_t m_iHealth = 0x34C;
	constexpr std::ptrdiff_t m_lifeState = 0x354;
	constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
	constexpr std::ptrdiff_t m_pObserverServices = 0x1308;
	constexpr std::ptrdiff_t m_hObserverTarget = 0x4C;
	constexpr std::ptrdiff_t m_modelState = 0x140;
	constexpr std::ptrdiff_t m_boneCache = 0x80;

	constexpr std::ptrdiff_t m_entitySpottedState = 0x1EB8;
	constexpr std::ptrdiff_t m_bSpotted            = 0x08;

	constexpr std::ptrdiff_t m_flFlashDuration  = 0x1510;
	constexpr std::ptrdiff_t m_bIsScoped        = 0x1EA0;
	constexpr std::ptrdiff_t m_iShotsFired      = 0x1EB8;
	constexpr std::ptrdiff_t m_bGunGameImmunity  = 0x3508;
	constexpr std::ptrdiff_t m_angEyeAngles      = 0x3630;

	constexpr std::ptrdiff_t m_pAimPunchServices = 0x1598;
	constexpr std::ptrdiff_t m_aimPunchAngle     = 0x50;

}

namespace visuals
{
	extern uintptr_t clientModule;
	extern uintptr_t engineModule;

	void PlayerESP(bool renderPlayers = true);
	bool FindBestAimPoint(const Vec3& currentView, Vec3& targetAngles,
		bool enemiesOnly, int targetBone, float maxFov);
}
