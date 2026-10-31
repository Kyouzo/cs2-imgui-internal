#pragma once

#include <cstddef>
#include <cstdint>

namespace offsets
{
	// Generated offsets verified against a2x/cs2-dumper main (2026-10-30).
	// https://github.com/a2x/cs2-dumper/tree/main/output

	constexpr std::ptrdiff_t dwEntityList = 0x271EB08;
	constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x25698C8;
	constexpr std::ptrdiff_t dwViewMatrix = 0x256F1D0;

	constexpr std::ptrdiff_t m_hPlayerPawn = 0x92C; // CHandle<C_CSPlayerPawn>
	constexpr std::ptrdiff_t m_iTeamNum = 0x3E7; // uint8
	constexpr std::ptrdiff_t m_vOldOrigin = 0x14A4; // Vector
	constexpr std::ptrdiff_t m_iHealth = 0x34C; // int32
	constexpr std::ptrdiff_t m_lifeState = 0x354; // uint8
	constexpr std::ptrdiff_t m_pGameSceneNode = 0x330; // CGameSceneNode*
	constexpr std::ptrdiff_t m_modelState = 0x140; // CSkeletonInstance::m_modelState
	constexpr std::ptrdiff_t m_boneCache = 0x80; // CModelState bone cache pointer


	constexpr std::ptrdiff_t dwWindowHeight = 0x9BCFEC;
	constexpr std::ptrdiff_t dwWindowWidth = 0x9BCFE8;

}

namespace visuals
{
	extern uintptr_t clientModule;
	extern uintptr_t engineModule;

	void PlayerESP();
}
