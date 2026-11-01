#define NOMINMAX
#include "visuals.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <array>
#include <utility>
#include <Windows.h>
#include <libloaderapi.h>
#include <algorithm>
#include "../math/math.h"
#include "../imgui/imgui.h"
#include "../debug/debug.h"
#include "../globals/globals.h"
#include "../sdk/entity_system.h"

uintptr_t visuals::clientModule = 0;
uintptr_t visuals::engineModule = 0;

namespace
{
    struct BoneTransform
    {
        Vec3 translation;
        float scale;
        float rotation[4];
    };

    static_assert(sizeof(BoneTransform) == 0x20);

    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 18> kSkeletonConnections{{
        {1, 2}, {2, 3}, {3, 4}, {4, 23}, {23, 6}, {6, 7},
        {6, 9}, {9, 10}, {10, 11}, {6, 13}, {13, 14}, {14, 15},
        {1, 17}, {17, 18}, {18, 19}, {1, 20}, {20, 21}, {21, 22}
    }};

    constexpr std::array<std::uint32_t, 21> kBodyBones{
        1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        17, 18, 19, 20, 21, 22, 23
    };

    uintptr_t FindWorldToProjectionMatrix(uintptr_t clientModule)
    {
        static bool initialized = false;
        static uintptr_t cachedMatrix = 0;
        if (initialized)
            return cachedMatrix;

        initialized = true;
        const auto* base = reinterpret_cast<const std::uint8_t*>(clientModule);
        const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;

        const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const auto* sections = IMAGE_FIRST_SECTION(ntHeaders);
        constexpr std::uint8_t pattern[] = {
            0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00,
            0x48, 0xC1, 0xE0, 0x06
        };

        for (unsigned sectionIndex = 0; sectionIndex < ntHeaders->FileHeader.NumberOfSections; ++sectionIndex)
        {
            const auto& section = sections[sectionIndex];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const auto* sectionBegin = base + section.VirtualAddress;
            const auto sectionSize = static_cast<size_t>(section.Misc.VirtualSize);
            if (sectionSize < sizeof(pattern))
                continue;

            for (size_t offset = 0; offset <= sectionSize - sizeof(pattern); ++offset)
            {
                const auto* candidate = sectionBegin + offset;
                if (candidate[0] != pattern[0] || candidate[1] != pattern[1] || candidate[2] != pattern[2] ||
                    candidate[7] != pattern[7] || candidate[8] != pattern[8] ||
                    candidate[9] != pattern[9] || candidate[10] != pattern[10])
                    continue;

                std::int32_t relative = 0;
                std::memcpy(&relative, candidate + 3, sizeof(relative));
                cachedMatrix = reinterpret_cast<uintptr_t>(candidate + 7 + relative);
                debug::Log("[esp] resolved world-to-projection matrix: 0x%p",
                    reinterpret_cast<void*>(cachedMatrix));
                return cachedMatrix;
            }
        }

        debug::Log("[esp] failed to resolve world-to-projection matrix");
        return 0;
    }

    uintptr_t FindEntitySystemGlobal(uintptr_t clientModule)
    {
        static bool initialized = false;
        static uintptr_t cachedGlobal = 0;
        if (initialized)
            return cachedGlobal;

        initialized = true;
        const auto* base = reinterpret_cast<const std::uint8_t*>(clientModule);
        const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;

        const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const auto* sections = IMAGE_FIRST_SECTION(ntHeaders);
        constexpr std::uint8_t pattern[] = {
            0x48, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x4C, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x44, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F
        };
        constexpr bool wildcard[] = {
            false, false, true, true, true, true, true,
            false, false, true, true, true, true, true,
            false, false, true, true, true, true, true, false
        };

        for (unsigned sectionIndex = 0; sectionIndex < ntHeaders->FileHeader.NumberOfSections; ++sectionIndex)
        {
            const auto& section = sections[sectionIndex];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const auto* sectionBegin = base + section.VirtualAddress;
            const auto sectionSize = static_cast<size_t>(section.Misc.VirtualSize);
            if (sectionSize < sizeof(pattern))
                continue;

            for (size_t offset = 0; offset <= sectionSize - sizeof(pattern); ++offset)
            {
                bool matched = true;
                for (size_t byteIndex = 0; byteIndex < sizeof(pattern); ++byteIndex)
                {
                    if (!wildcard[byteIndex] && sectionBegin[offset + byteIndex] != pattern[byteIndex])
                    {
                        matched = false;
                        break;
                    }
                }

                if (matched)
                {
                    std::int32_t relative = 0;
                    std::memcpy(&relative, sectionBegin + offset + 3, sizeof(relative));
                    cachedGlobal = reinterpret_cast<uintptr_t>(sectionBegin + offset + 7 + relative);
                    debug::Log("[esp] resolved EntitySystem global: 0x%p",
                        reinterpret_cast<void*>(cachedGlobal));
                    return cachedGlobal;
                }
            }
        }

        debug::Log("[esp] failed to resolve EntitySystem global");
        return 0;
    }

    int8_t FindEntityListOffset(uintptr_t clientModule)
    {
        static bool initialized = false;
        static int8_t cachedOffset = 0;
        if (initialized)
            return cachedOffset;

        initialized = true;

        const auto* base = reinterpret_cast<const std::uint8_t*>(clientModule);
        const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;

        const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const auto* sections = IMAGE_FIRST_SECTION(ntHeaders);
        constexpr std::uint8_t pattern[] = {
            0x48, 0x8D, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8D, 0x85
        };
        constexpr bool wildcard[] = {
            false, false, true, true, false, true, true, true, true, false, false
        };

        for (unsigned sectionIndex = 0; sectionIndex < ntHeaders->FileHeader.NumberOfSections; ++sectionIndex)
        {
            const auto& section = sections[sectionIndex];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const auto* sectionBegin = base + section.VirtualAddress;
            const auto sectionSize = static_cast<size_t>(section.Misc.VirtualSize);
            if (sectionSize < sizeof(pattern))
                continue;

            for (size_t offset = 0; offset <= sectionSize - sizeof(pattern); ++offset)
            {
                bool matched = true;
                for (size_t byteIndex = 0; byteIndex < sizeof(pattern); ++byteIndex)
                {
                    if (!wildcard[byteIndex] && sectionBegin[offset + byteIndex] != pattern[byteIndex])
                    {
                        matched = false;
                        break;
                    }
                }

                if (matched)
                {
                    cachedOffset = *reinterpret_cast<const int8_t*>(sectionBegin + offset + 3);
                    debug::Log("[esp] resolved EntityListOffset: 0x%02X", static_cast<unsigned char>(cachedOffset));
                    return cachedOffset;
                }
            }
        }

        debug::Log("[esp] failed to resolve EntityListOffset");
        return 0;
    }

    template <typename T>
    bool ReadValue(uintptr_t address, T& value, const char* stage)
    {
        (void)stage;
        value = *reinterpret_cast<const T*>(address);
        return true;
    }

    uintptr_t GetEntityByIndex(uintptr_t entityList, std::uint32_t index, const char* stage)
    {
        if (index > 0x7FFFu)
            return 0;

        const auto chunkIndex = index / 512u;
        const auto indexInChunk = index % 512u;

        uintptr_t chunk = 0;
        if (!ReadValue(entityList + chunkIndex * sizeof(uintptr_t),
            chunk, stage) || !chunk)
            return 0;

        sdk::EntityIdentity identity{};
        if (!ReadValue(chunk + indexInChunk * sizeof(sdk::EntityIdentity),
            identity, stage))
            return 0;

        if (!identity.entity || identity.handle.value == 0xFFFFFFFFu ||
            identity.handle.index() != index)
        {
            static int invalidIdentityLogs = 0;
            if (invalidIdentityLogs++ < 8)
            {
                debug::Log("[esp] invalid identity at index %u: entity=0x%p handle=0x%08X",
                    index, reinterpret_cast<void*>(identity.entity), identity.handle.value);
            }
            return 0;
        }

        return identity.entity;
    }

    bool ReadBonePositions(uintptr_t pawn, std::array<Vec3, 128>& bones)
    {
        uintptr_t sceneNode = 0;
        if (!ReadValue(pawn + offsets::m_pGameSceneNode, sceneNode, "pawn.m_pGameSceneNode") || !sceneNode)
            return false;

        uintptr_t boneCache = 0;
        if (!ReadValue(sceneNode + offsets::m_modelState + offsets::m_boneCache,
            boneCache, "scene.m_modelState.boneCache") || !boneCache)
            return false;

        bool valid = false;
        for (std::size_t index = 0; index < bones.size(); ++index)
        {
            BoneTransform transform{};
            if (!ReadValue(boneCache + index * sizeof(BoneTransform), transform, "bone"))
                return false;
            bones[index] = transform.translation;
            valid = valid || (transform.translation.x != 0.0f || transform.translation.y != 0.0f || transform.translation.z != 0.0f);
        }
        return valid;
    }

    ImU32 HealthColor(int health)
    {
        const float fraction = std::clamp(static_cast<float>(health) / 100.0f, 0.0f, 1.0f);
        return ImColor(1.0f - fraction, fraction, 0.12f, 1.0f);
    }

    ImU32 TeamColor(std::uint8_t team, bool enemy)
    {
        if (!globals::espConfig.teamColors)
            return ImColor(235, 235, 235, 255);
        if (enemy)
            return ImColor(255, 92, 92, 255);
        return team == 3 ? ImColor(86, 165, 255, 255) : ImColor(255, 190, 70, 255);
    }
}

void visuals::PlayerESP()
{
    static bool loggedFirstPass = false;

    clientModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    engineModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine2.dll"));

    if (!clientModule || !engineModule)
    {
        debug::Log("[esp] modules unavailable: client=0x%p engine=0x%p",
            reinterpret_cast<void*>(clientModule), reinterpret_cast<void*>(engineModule));
        return;
    }

    auto viewMatrixAddress = FindWorldToProjectionMatrix(clientModule);
    if (!viewMatrixAddress)
        viewMatrixAddress = clientModule + offsets::dwViewMatrix;
    auto ViewMatrix = reinterpret_cast<float(*)[4][4]>(viewMatrixAddress);

    uintptr_t localPlayerPawn = 0;
    if (!ReadValue(clientModule + offsets::dwLocalPlayerPawn,
        localPlayerPawn, "dwLocalPlayerPawn"))
        return;

    if (!loggedFirstPass)
    {
        debug::Log("[esp] first pass: client=0x%p engine=0x%p localPawn=0x%p",
            reinterpret_cast<void*>(clientModule),
            reinterpret_cast<void*>(engineModule),
            reinterpret_cast<void*>(localPlayerPawn));
        loggedFirstPass = true;
    }

    if (!localPlayerPawn)
        return;

    uint8_t localTeam = 0;
    if (!ReadValue(localPlayerPawn + offsets::m_iTeamNum,
        localTeam, "localPawn.m_iTeamNum"))
        return;

    uintptr_t entitySystemGlobal = FindEntitySystemGlobal(clientModule);
    uintptr_t entitySystem = 0;
    if (entitySystemGlobal && ReadValue(entitySystemGlobal, entitySystem, "entitySystem"))
    {
        static uintptr_t lastLoggedEntitySystem = 0;
        if (lastLoggedEntitySystem != entitySystem)
        {
            debug::Log("[esp] EntitySystem=0x%p", reinterpret_cast<void*>(entitySystem));
            lastLoggedEntitySystem = entitySystem;
        }
    }
    else if (!ReadValue(clientModule + offsets::dwEntityList,
        entitySystem, "dwGameEntitySystem"))
    {
        return;
    }

    if (!entitySystem)
        return;

    const auto entityListOffset = FindEntityListOffset(clientModule);
    if (!entityListOffset)
        return;

    const auto entityList = entitySystem + entityListOffset;

    for (int i = 1; i < 64; i++) {
        const auto playerController = GetEntityByIndex(
            entityList, static_cast<std::uint32_t>(i), "controller.identity");
        if (!playerController)
            continue;

        uint32_t playerPawn = 0;
        if (!ReadValue(playerController + offsets::m_hPlayerPawn,
            playerPawn, "controller.m_hPlayerPawn"))
            continue;
        if (!playerPawn || playerPawn == 0xFFFFFFFFu)
            continue;

        const auto pCSPlayerPawnPointer = GetEntityByIndex(
            entityList, playerPawn & 0x7FFFu, "pawn.identity");
        if (!pCSPlayerPawnPointer)
            continue;

        uint8_t lifeState = 0;
        if (!ReadValue(pCSPlayerPawnPointer + offsets::m_lifeState,
            lifeState, "pawn.m_lifeState"))
            continue;
        if (lifeState != 0)
            continue;

        int health = 0;
        if (!ReadValue(pCSPlayerPawnPointer + offsets::m_iHealth,
            health, "pawn.m_iHealth"))
            continue;
        if (!health || health > 100)
            continue;

        uint8_t team = 0;
        if (!ReadValue(pCSPlayerPawnPointer + offsets::m_iTeamNum,
            team, "pawn.m_iTeamNum"))
            continue;
        const bool enemy = team != localTeam;
        if (globals::espConfig.onlyEnemies && !enemy)
            continue;

        Vec3 feetPos;
        if (!ReadValue(pCSPlayerPawnPointer + offsets::m_vOldOrigin,
            feetPos, "pawn.m_vOldOrigin"))
            continue;

        std::array<Vec3, 128> bones{};
        const bool hasBones = ReadBonePositions(pCSPlayerPawnPointer, bones);
        Vec2 feet, head;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        bool projectedBounds = false;

        if (hasBones)
        {
            for (const auto boneIndex : kBodyBones)
            {
                Vec2 point;
                if (!bones[boneIndex].WorldToScreen(point, ViewMatrix))
                    continue;
                if (!projectedBounds)
                {
                    minX = maxX = point.x;
                    minY = maxY = point.y;
                    projectedBounds = true;
                }
                else
                {
                    minX = std::min(minX, point.x);
                    maxX = std::max(maxX, point.x);
                    minY = std::min(minY, point.y);
                    maxY = std::max(maxY, point.y);
                }
            }
        }

        Vec3 headWorld = hasBones ? bones[7] : Vec3{feetPos.x, feetPos.y, feetPos.z + 65.0f};
        if (!feetPos.WorldToScreen(feet, ViewMatrix) || !headWorld.WorldToScreen(head, ViewMatrix))
            continue;

        if (!projectedBounds)
        {
            minX = feet.x;
            maxX = feet.x;
            minY = head.y;
            maxY = feet.y;
        }

        // Use the projected skeleton extent when available. This keeps the box
        // anchored to the actual top of the head and bottom of the player.
        minX = std::min(minX, std::min(head.x, feet.x));
        maxX = std::max(maxX, std::max(head.x, feet.x));
        minY = std::min(minY, head.y);
        maxY = std::max(maxY, feet.y);

        {
            auto draw = ImGui::GetBackgroundDrawList();
            const float x = minX - 4.0f;
            const float y = minY - 3.0f;
            const float width = std::max(8.0f, (maxX - minX) + 8.0f);
            const float height = std::max(12.0f, (maxY - minY) + 6.0f);
            const ImU32 color = TeamColor(team, enemy);

            // Osiris-style in-world presentation: dark shadow, team-colored outline,
            // health information below the player and an optional position marker.
            if (globals::espConfig.cornerBox)
            {
                const float corner = std::min(10.0f, std::min(width, height) * 0.35f);
                draw->AddLine({x, y}, {x + corner, y}, color, 1.5f);
                draw->AddLine({x, y}, {x, y + corner}, color, 1.5f);
                draw->AddLine({x + width - corner, y}, {x + width, y}, color, 1.5f);
                draw->AddLine({x + width, y}, {x + width, y + corner}, color, 1.5f);
                draw->AddLine({x, y + height - corner}, {x, y + height}, color, 1.5f);
                draw->AddLine({x, y + height}, {x + corner, y + height}, color, 1.5f);
                draw->AddLine({x + width - corner, y + height}, {x + width, y + height}, color, 1.5f);
                draw->AddLine({x + width, y + height - corner}, {x + width, y + height}, color, 1.5f);
            }

            if (globals::espConfig.headMarker)
            {
                draw->AddCircleFilled({head.x, head.y}, 3.0f, color);
                draw->AddCircle({head.x, head.y}, 4.5f, ImColor(0, 0, 0, 190), 12, 1.0f);
            }

            if (globals::espConfig.skeleton && hasBones)
            {
                for (const auto& [from, to] : kSkeletonConnections)
                {
                    Vec2 fromScreen, toScreen;
                    if (bones[from].WorldToScreen(fromScreen, ViewMatrix) && bones[to].WorldToScreen(toScreen, ViewMatrix))
                    {
                        draw->AddLine({fromScreen.x, fromScreen.y}, {toScreen.x, toScreen.y}, ImColor(255, 255, 255, 230), 1.0f);
                    }
                }
            }

            if (globals::espConfig.snapline)
            {
                const float screenBottom = ImGui::GetIO().DisplaySize.y;
                draw->AddLine({feet.x, feet.y}, {feet.x, screenBottom}, ImColor(255, 255, 255, 160), 1.0f);
            }

            if (globals::espConfig.healthBar)
            {
                const float barX = x - 7.0f;
                const float barWidth = 4.0f;
                const float barHeight = height * std::clamp(static_cast<float>(health) / 100.0f, 0.0f, 1.0f);
                draw->AddRectFilled({barX - 1.0f, y - 1.0f}, {barX + barWidth + 1.0f, y + height + 1.0f}, ImColor(0, 0, 0, 180));
                draw->AddRectFilled({barX, y + height - barHeight}, {barX + barWidth, y + height}, HealthColor(health));
            }

            if (globals::espConfig.healthText)
            {
                char healthLabel[8]{};
                sprintf_s(healthLabel, "%d", health);
                const ImVec2 textSize = ImGui::CalcTextSize(healthLabel);
                draw->AddText({x + width + 5.0f, y + height - textSize.y}, ImColor(255, 255, 255, 235), healthLabel);
            }

            if (globals::espConfig.positionArrow)
            {
                const ImVec2 arrowCenter{feet.x, feet.y + 8.0f};
                draw->AddTriangleFilled({arrowCenter.x, arrowCenter.y + 5.0f},
                    {arrowCenter.x - 4.0f, arrowCenter.y - 2.0f},
                    {arrowCenter.x + 4.0f, arrowCenter.y - 2.0f}, color);
            }

        }
    }
}
