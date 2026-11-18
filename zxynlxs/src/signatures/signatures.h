#pragma once

#include <cstddef>
#include <cstdint>

namespace signatures
{
    bool Init();
    bool IsInitialized();

    // client.dll global offsets (resolved from signatures at runtime)
    extern std::ptrdiff_t dwEntityList;
    extern std::ptrdiff_t dwLocalPlayerController;
    extern std::ptrdiff_t dwLocalPlayerPawn;
    extern std::ptrdiff_t dwViewAngles;
    extern std::ptrdiff_t dwViewMatrix;
    extern std::ptrdiff_t dwCSGOInput;
    extern std::ptrdiff_t dwGameEntitySystem;
    extern std::ptrdiff_t dwGameRules;
    extern std::ptrdiff_t dwGlowManager;
    extern std::ptrdiff_t dwPrediction;

    // engine2.dll global offsets
    extern std::ptrdiff_t dwWindowWidth;
    extern std::ptrdiff_t dwWindowHeight;
    extern std::ptrdiff_t dwNetworkGameClient;

    // engine2.dll struct member offsets (read as u32 from instructions)
    extern std::ptrdiff_t dwNetworkGameClient_signOnState;
}
