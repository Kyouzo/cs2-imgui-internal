#define NOMINMAX
#include "visuals.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
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
    using CreateInterfaceFn = void* (*)(const char*, int*);
    using IsInGameFn = bool(__fastcall*)(void*);

    constexpr std::size_t kIsInGameVtableIndex = 39;

    bool IsInGame(uintptr_t engineModule)
    {
        static void* engineToClient = nullptr;
        static bool loggedFailure = false;

        if (!engineToClient)
        {
            const auto createInterface = reinterpret_cast<CreateInterfaceFn>(
                GetProcAddress(reinterpret_cast<HMODULE>(engineModule), "CreateInterface"));
            if (createInterface)
                engineToClient = createInterface("Source2EngineToClient001", nullptr);
        }

        if (!engineToClient)
        {
            if (!loggedFailure)
            {
                debug::Log("[esp] Source2EngineToClient001 is unavailable");
                loggedFailure = true;
            }
            return false;
        }

        __try
        {
            const auto vtable = *reinterpret_cast<void***>(engineToClient);
            const auto isInGame = reinterpret_cast<IsInGameFn>(vtable[kIsInGameVtableIndex]);
            return isInGame && isInGame(engineToClient);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

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
        __try
        {
            value = *reinterpret_cast<const T*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            value = T{};
            return false;
        }
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

    uintptr_t ResolveObserverTarget(uintptr_t pawn, uintptr_t entityList, const char* stage)
    {
        uintptr_t observerServices = 0;
        std::uint32_t observerTarget = 0;
        if (!ReadValue(pawn + offsets::m_pObserverServices,
            observerServices, stage) || !observerServices ||
            !ReadValue(observerServices + offsets::m_hObserverTarget,
                observerTarget, stage) || observerTarget == 0xFFFFFFFFu)
            return 0;

        return GetEntityByIndex(entityList, observerTarget & 0x7FFFu, stage);
    }

    const char* TeamName(std::uint8_t team)
    {
        if (team == 2)
            return "T";
        if (team == 3)
            return "CT";
        return "?";
    }

    ImU32 SpectatorTeamColor(std::uint8_t team)
    {
        if (team == 2)
            return ImColor(255, 181, 72, 255); // T
        if (team == 3)
            return ImColor(91, 169, 255, 255); // CT
        return ImColor(210, 210, 215, 255);
    }

    struct SpectatorEntry
    {
        std::array<char, 128> name{};
        std::uint8_t team = 0;
    };

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

    bool ReadBonePosition(uintptr_t pawn, std::size_t boneIndex, Vec3& position)
    {
        uintptr_t sceneNode = 0;
        uintptr_t boneArray = 0;
        if (!ReadValue(pawn + offsets::m_pGameSceneNode, sceneNode, "aim.m_pGameSceneNode") || !sceneNode ||
            !ReadValue(sceneNode + offsets::m_modelState + offsets::m_boneCache,
                boneArray, "aim.boneArray") || !boneArray)
            return false;

        struct BoneEntry
        {
            float x, y, z, padding;
            float rotation[4];
        };
        BoneEntry bone{};
        if (!ReadValue(boneArray + boneIndex * sizeof(BoneEntry), bone, "aim.bone"))
            return false;

        position = {bone.x, bone.y, bone.z};
        return position.x != 0.0f || position.y != 0.0f || position.z != 0.0f;
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

    void Draw360Alert(ImDrawList* draw, const Vec3& worldPosition,
        float(*viewMatrix)[4][4], ImU32 color, float distanceMeters)
    {
        if (!draw || !viewMatrix)
            return;

        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
            return;

        float w = (*viewMatrix)[3][0] * worldPosition.x +
            (*viewMatrix)[3][1] * worldPosition.y +
            (*viewMatrix)[3][2] * worldPosition.z + (*viewMatrix)[3][3];
        if (std::fabs(w) < 0.001f)
            w = 0.001f;

        float clipX = ((*viewMatrix)[0][0] * worldPosition.x +
            (*viewMatrix)[0][1] * worldPosition.y +
            (*viewMatrix)[0][2] * worldPosition.z + (*viewMatrix)[0][3]) / w;
        float clipY = ((*viewMatrix)[1][0] * worldPosition.x +
            (*viewMatrix)[1][1] * worldPosition.y +
            (*viewMatrix)[1][2] * worldPosition.z + (*viewMatrix)[1][3]) / w;

        // Behind-camera points need their direction flipped before finding the
        // nearest point on the screen perimeter.
        if (w < 0.0f)
        {
            clipX = -clipX;
            clipY = -clipY;
        }

        const ImVec2 center{displaySize.x * 0.5f, displaySize.y * 0.5f};
        const float directionX = clipX;
        const float directionY = -clipY;
        const float length = std::sqrt(directionX * directionX + directionY * directionY);
        if (length < 0.001f)
            return;

        const float radius = std::max(30.0f, std::min(displaySize.x, displaySize.y) * 0.42f);
        const float scale = radius / std::max(std::fabs(directionX), std::fabs(directionY));
        const ImVec2 tip{center.x + directionX * scale, center.y + directionY * scale};
        const float unitX = directionX / length;
        const float unitY = directionY / length;
        const ImVec2 baseCenter{tip.x - unitX * 13.0f, tip.y - unitY * 13.0f};
        const ImVec2 normal{-unitY * 6.0f, unitX * 6.0f};
        const ImVec2 left{baseCenter.x + normal.x, baseCenter.y + normal.y};
        const ImVec2 right{baseCenter.x - normal.x, baseCenter.y - normal.y};

        draw->AddTriangleFilled(tip, left, right, color);
        draw->AddTriangle(tip, left, right, ImColor(0, 0, 0, 210), 1.0f);

        char distanceLabel[32]{};
        sprintf_s(distanceLabel, "%.0fm", distanceMeters);
        const ImVec2 textSize = ImGui::CalcTextSize(distanceLabel);
        draw->AddText({tip.x - textSize.x * 0.5f, tip.y + 8.0f},
            ImColor(255, 255, 255, 235), distanceLabel);
    }
}

bool visuals::FindBestAimPoint(const Vec3& currentView, Vec3& targetAngles,
    bool enemiesOnly, int targetBone, float maxFov)
{
    clientModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    engineModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine2.dll"));
    if (!clientModule || !engineModule || !IsInGame(engineModule))
        return false;

    uintptr_t localController = 0;
    uintptr_t localPawnFromOffset = 0;
    if (!ReadValue(clientModule + offsets::dwLocalPlayerController, localController, "aim.localController") ||
        !ReadValue(clientModule + offsets::dwLocalPlayerPawn, localPawnFromOffset, "aim.localPawn"))
        return false;

    uintptr_t entitySystem = 0;
    const auto entitySystemGlobal = FindEntitySystemGlobal(clientModule);
    if (entitySystemGlobal && ReadValue(entitySystemGlobal, entitySystem, "aim.entitySystem"))
    {
        // resolved global path
    }
    else if (!ReadValue(clientModule + offsets::dwEntityList,
        entitySystem, "aim.dwGameEntitySystem"))
    {
        return false;
    }
    if (!entitySystem)
        return false;

    const auto entityListOffset = FindEntityListOffset(clientModule);
    if (!entityListOffset)
        return false;
    const auto entityList = entitySystem + entityListOffset;

    uintptr_t localPawn = 0;
    uint32_t localPawnHandle = 0;
    if (localController && ReadValue(localController + offsets::m_hPlayerPawn,
        localPawnHandle, "aim.localPawnHandle") && localPawnHandle != 0xFFFFFFFFu)
    {
        localPawn = GetEntityByIndex(entityList, localPawnHandle & 0x7FFFu, "aim.localPawnIdentity");
    }
    if (!localPawn)
        localPawn = localPawnFromOffset;
    if (!localPawn)
        return false;

    uint8_t lifeState = 0;
    uint8_t localTeam = 0;
    Vec3 localEye{};
    uintptr_t localSceneNode = 0;
    Vec3 localSceneOrigin{};
    Vec3 localViewOffset{};
    if (!ReadValue(localPawn + offsets::m_lifeState, lifeState, "aim.localLifeState") || lifeState != 0 ||
        !ReadValue(localPawn + offsets::m_iTeamNum, localTeam, "aim.localTeam") ||
        !ReadValue(localPawn + offsets::m_pGameSceneNode, localSceneNode, "aim.localSceneNode") ||
        !localSceneNode ||
        !ReadValue(localSceneNode + offsets::m_vecAbsOrigin, localSceneOrigin, "aim.localAbsOrigin") ||
        !ReadValue(localPawn + offsets::m_vecViewOffset, localViewOffset, "aim.localViewOffset"))
        return false;
    localEye = {localSceneOrigin.x + localViewOffset.x,
        localSceneOrigin.y + localViewOffset.y,
        localSceneOrigin.z + localViewOffset.z};

    const float clampedFov = std::clamp(maxFov, 0.1f, 180.0f);
    float bestDistance = clampedFov;
    bool found = false;
    for (uint32_t index = 1; index < 64; ++index)
    {
        const auto controller = GetEntityByIndex(entityList, index, "aim.controllerIdentity");
        if (!controller)
            continue;

        uint32_t pawnHandle = 0;
        if (!ReadValue(controller + offsets::m_hPlayerPawn, pawnHandle, "aim.pawnHandle") ||
            pawnHandle == 0 || pawnHandle == 0xFFFFFFFFu)
            continue;

        const auto pawn = GetEntityByIndex(entityList, pawnHandle & 0x7FFFu, "aim.pawnIdentity");
        if (!pawn || pawn == localPawn)
            continue;

        bool dormant = false;
        if (ReadValue(pawn + offsets::m_bDormant, dormant, "aim.dormant") && dormant)
            continue;

        bool gunGameImmune = false;
        if (ReadValue(pawn + offsets::m_bGunGameImmunity, gunGameImmune, "aim.gunGameImmune") && gunGameImmune)
            continue;

        uint8_t targetLifeState = 0;
        uint8_t targetTeam = 0;
        int health = 0;
        Vec3 targetOrigin{};
        if (!ReadValue(pawn + offsets::m_lifeState, targetLifeState, "aim.targetLifeState") || targetLifeState != 0 ||
            !ReadValue(pawn + offsets::m_iHealth, health, "aim.targetHealth") || health <= 0 ||
            !ReadValue(pawn + offsets::m_iTeamNum, targetTeam, "aim.targetTeam") ||
            !ReadValue(pawn + offsets::m_vOldOrigin, targetOrigin, "aim.targetOrigin"))
            continue;
        if (enemiesOnly && targetTeam == localTeam)
            continue;

        // visCheck: skip unspotted enemies (any teammate has LoS — implicitly blocks smokes/walls).
        // smokeCheck: same mechanism but as a dedicated toggle for smoke/wall filter.
        if (globals::aimConfig.visCheck || globals::aimConfig.smokeCheck)
        {
            bool spotted = false;
            if (!ReadValue(pawn + offsets::m_entitySpottedState + offsets::m_bSpotted,
                spotted, "aim.spotted") || !spotted)
                continue;
        }

        // Shared player rig: head=7, neck=6, chest=23, spine2/torso=4.
        const std::size_t boneIndex = targetBone == 0 ? 7u :
            (targetBone == 1 ? 6u : (targetBone == 2 ? 23u : 4u));
        if (!ReadBonePosition(pawn, boneIndex, targetOrigin))
        {
            // Fallback only when the skeleton cache is unavailable.
            targetOrigin.z += targetBone == 0 ? 64.0f :
                (targetBone == 1 ? 56.0f : (targetBone == 2 ? 48.0f : 36.0f));
        }
        const Vec3 delta{targetOrigin.x - localEye.x, targetOrigin.y - localEye.y,
            targetOrigin.z - localEye.z};
        const float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (horizontal < 0.01f)
            continue;

        const float pitch = -std::atan2(delta.z, horizontal) * 57.2957795f;
        const float yaw = std::atan2(delta.y, delta.x) * 57.2957795f;
        float yawDelta = yaw - currentView.y;
        while (yawDelta > 180.0f) yawDelta -= 360.0f;
        while (yawDelta < -180.0f) yawDelta += 360.0f;
        const float pitchDelta = pitch - currentView.x;
        const float distance = std::sqrt(pitchDelta * pitchDelta + yawDelta * yawDelta);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            targetAngles = {pitch, yaw, 0.0f};
            found = true;
        }
    }
    return found;
}

void visuals::PlayerESP(bool renderPlayers)
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

    if (!IsInGame(engineModule))
    {
        return;
    }

    auto viewMatrixAddress = FindWorldToProjectionMatrix(clientModule);
    if (!viewMatrixAddress)
        viewMatrixAddress = clientModule + offsets::dwViewMatrix;
    auto ViewMatrix = reinterpret_cast<float(*)[4][4]>(viewMatrixAddress);

    // dwLocalPlayerPawn can follow the camera/observer state while dead. Resolve
    // the pawn from the local controller as the authoritative player identity,
    // and keep dwLocalPlayerPawn only as a compatibility fallback.
    uintptr_t localPlayerController = 0;
    uintptr_t localPlayerPawnFromOffset = 0;
    if (!ReadValue(clientModule + offsets::dwLocalPlayerController,
        localPlayerController, "dwLocalPlayerController") ||
        !ReadValue(clientModule + offsets::dwLocalPlayerPawn,
            localPlayerPawnFromOffset, "dwLocalPlayerPawn"))
        return;

    if (!loggedFirstPass)
    {
        debug::Log("[esp] first pass: client=0x%p engine=0x%p localPawn=0x%p",
            reinterpret_cast<void*>(clientModule),
            reinterpret_cast<void*>(engineModule),
            reinterpret_cast<void*>(localPlayerPawnFromOffset));
        loggedFirstPass = true;
    }

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

    uintptr_t localPlayerPawn = 0;
    if (localPlayerController)
    {
        std::uint32_t localPawnHandle = 0;
        if (ReadValue(localPlayerController + offsets::m_hPlayerPawn,
            localPawnHandle, "localController.m_hPlayerPawn") &&
            localPawnHandle != 0xFFFFFFFFu)
        {
            localPlayerPawn = GetEntityByIndex(
                entityList, localPawnHandle & 0x7FFFu, "localPawn.identity");
        }
    }
    if (!localPlayerPawn)
        localPlayerPawn = localPlayerPawnFromOffset;
    if (!localPlayerPawn)
        return;

    uint8_t localLifeState = 0;
    if (!ReadValue(localPlayerPawn + offsets::m_lifeState,
        localLifeState, "localPawn.m_lifeState"))
        return;

    const bool localIsDead = localLifeState != 0;

    uint8_t localTeam = 0;
    if (!ReadValue(localPlayerPawn + offsets::m_iTeamNum,
        localTeam, "localPawn.m_iTeamNum"))
        return;

    Vec3 localFeetPos;
    if (!ReadValue(localPlayerPawn + offsets::m_vOldOrigin,
        localFeetPos, "localPawn.m_vOldOrigin"))
        return;

    uintptr_t spectatedPawn = 0;
    if (localIsDead)
        spectatedPawn = ResolveObserverTarget(
            localPlayerPawn, entityList, "localPawn.observerTarget");

    std::array<SpectatorEntry, 64> spectators{};
    std::size_t spectatorCount = 0;

    for (int i = 1; i < 64; i++) {
        const auto playerController = GetEntityByIndex(
            entityList, static_cast<std::uint32_t>(i), "controller.identity");
        if (!playerController)
            continue;

        uint32_t playerPawn = 0;
        uint32_t observerPawn = 0;
        const bool hasPlayerPawn = ReadValue(playerController + offsets::m_hPlayerPawn,
            playerPawn, "controller.m_hPlayerPawn") &&
            playerPawn != 0 && playerPawn != 0xFFFFFFFFu;
        const bool hasObserverPawn = ReadValue(playerController + offsets::m_hObserverPawn,
            observerPawn, "controller.m_hObserverPawn") &&
            observerPawn != 0 && observerPawn != 0xFFFFFFFFu;
        if (!hasPlayerPawn && !hasObserverPawn)
            continue;

        const auto pCSPlayerPawnPointer = hasPlayerPawn
            ? GetEntityByIndex(entityList, playerPawn & 0x7FFFu, "pawn.identity")
            : 0;
        const auto observerPawnPointer = hasObserverPawn
            ? GetEntityByIndex(entityList, observerPawn & 0x7FFFu, "observerPawn.identity")
            : 0;
        const auto observationPawn = pCSPlayerPawnPointer ? pCSPlayerPawnPointer : observerPawnPointer;
        if (!observationPawn)
            continue;

        uint8_t lifeState = 0;
        if (!ReadValue(observationPawn + offsets::m_lifeState,
            lifeState, "pawn.m_lifeState"))
            continue;

        // Dead players normally expose m_hPlayerPawn. Spectator-only controllers
        // can instead expose m_hObserverPawn, so resolve both paths.
        if (lifeState != 0 || !pCSPlayerPawnPointer)
        {
            auto watchedPawn = ResolveObserverTarget(
                observationPawn, entityList, "spectator.observerTarget");
            if (!watchedPawn && observerPawnPointer && observerPawnPointer != observationPawn)
                watchedPawn = ResolveObserverTarget(
                    observerPawnPointer, entityList, "spectator.observerPawnTarget");
            if (globals::espConfig.spectatorList && spectatorCount < spectators.size() &&
                watchedPawn == localPlayerPawn)
            {
                ReadValue(playerController + offsets::m_iszPlayerName,
                    spectators[spectatorCount].name, "controller.m_iszPlayerName");
                ReadValue(observationPawn + offsets::m_iTeamNum,
                    spectators[spectatorCount].team, "spectator.pawn.m_iTeamNum");
                ++spectatorCount;
            }
            continue;
        }

        if (!pCSPlayerPawnPointer)
            continue;

        if (!renderPlayers)
            continue;

        // When teammate ESP is enabled, keep the local pawn out of the entity pass.
        if (!globals::espConfig.onlyEnemies && pCSPlayerPawnPointer == localPlayerPawn)
            continue;

        // Do not draw the player currently being watched after our death. The
        // local controller-derived pawn above keeps this correct even when the
        // engine's local-pawn pointer follows the observer camera.
        if (localIsDead && spectatedPawn && pCSPlayerPawnPointer == spectatedPawn)
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

        std::array<char, 128> playerName{};
        if (!ReadValue(playerController + offsets::m_iszPlayerName,
            playerName, "controller.m_iszPlayerName"))
            playerName[0] = '\0';
        playerName.back() = '\0';

        const float deltaX = feetPos.x - localFeetPos.x;
        const float deltaY = feetPos.y - localFeetPos.y;
        const float deltaZ = feetPos.z - localFeetPos.z;
        const float distanceMeters = std::sqrt(
            deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) / 39.37f;

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
        const bool feetOnScreen = feetPos.WorldToScreen(feet, ViewMatrix);
        const bool headOnScreen = headWorld.WorldToScreen(head, ViewMatrix);
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const bool outsideViewport = feetOnScreen && headOnScreen &&
            (std::max(feet.x, head.x) < 0.0f || std::min(feet.x, head.x) > displaySize.x ||
                std::max(feet.y, head.y) < 0.0f || std::min(feet.y, head.y) > displaySize.y);

        if (!feetOnScreen || !headOnScreen || outsideViewport)
        {
            if (globals::espConfig.alert360)
            {
                auto draw = ImGui::GetBackgroundDrawList();
                Draw360Alert(draw, feetPos, ViewMatrix, TeamColor(team, enemy), distanceMeters);
            }
            continue;
        }

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

            if (globals::espConfig.name || globals::espConfig.distance)
            {
                char infoLabel[192]{};
                const char* name = playerName[0] ? playerName.data() : "Unknown";
                if (globals::espConfig.name && globals::espConfig.distance)
                    sprintf_s(infoLabel, "%s  [%.0fm]", name, distanceMeters);
                else if (globals::espConfig.name)
                    sprintf_s(infoLabel, "%s", name);
                else
                    sprintf_s(infoLabel, "[%.0fm]", distanceMeters);

                const ImVec2 infoSize = ImGui::CalcTextSize(infoLabel);
                draw->AddText({x + (width - infoSize.x) * 0.5f, y - infoSize.y - 3.0f},
                    ImColor(255, 255, 255, 235), infoLabel);
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

    if (!globals::espConfig.spectatorList)
        return;

    auto draw = ImGui::GetBackgroundDrawList();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float panelWidth = 184.0f;
    const float rowHeight = 18.0f;
    const float panelHeight = 28.0f + static_cast<float>(spectatorCount) * rowHeight;
    const ImVec2 panelMin{displaySize.x - panelWidth - 10.0f, 10.0f};
    const ImVec2 panelMax{panelMin.x + panelWidth, panelMin.y + panelHeight};
    draw->AddRectFilled(panelMin, panelMax, ImColor(18, 18, 22, 225), 4.0f);
    draw->AddRect(panelMin, panelMax, ImColor(92, 98, 112, 235), 4.0f, 0, 1.0f);

    char header[64]{};
    sprintf_s(header, "Spectators: %zu", spectatorCount);
    draw->AddText({panelMin.x + 8.0f, panelMin.y + 6.0f},
        ImColor(255, 255, 255, 255), header);

    for (std::size_t index = 0; index < spectatorCount; ++index)
    {
        const char* name = spectators[index].name.data();
        if (!name[0])
            name = "Unknown";

        const ImVec2 rowPosition{panelMin.x + 8.0f,
            panelMin.y + 25.0f + static_cast<float>(index) * rowHeight};
        draw->AddText(rowPosition, ImColor(228, 228, 232, 255), name);
        const ImVec2 nameSize = ImGui::CalcTextSize(name);
        char teamLabel[8]{};
        sprintf_s(teamLabel, "[%s]", TeamName(spectators[index].team));
        draw->AddText({rowPosition.x + nameSize.x + 8.0f, rowPosition.y},
            SpectatorTeamColor(spectators[index].team), teamLabel);
    }
}
