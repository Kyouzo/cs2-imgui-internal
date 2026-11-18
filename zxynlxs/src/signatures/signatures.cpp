#define NOMINMAX
#include "signatures.h"

#include <Windows.h>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../debug/debug.h"

namespace signatures
{
    std::ptrdiff_t dwEntityList = 0;
    std::ptrdiff_t dwLocalPlayerController = 0;
    std::ptrdiff_t dwLocalPlayerPawn = 0;
    std::ptrdiff_t dwViewAngles = 0;
    std::ptrdiff_t dwViewMatrix = 0;
    std::ptrdiff_t dwCSGOInput = 0;
    std::ptrdiff_t dwGameEntitySystem = 0;
    std::ptrdiff_t dwGameRules = 0;
    std::ptrdiff_t dwGlowManager = 0;
    std::ptrdiff_t dwPrediction = 0;

    std::ptrdiff_t dwWindowWidth = 0;
    std::ptrdiff_t dwWindowHeight = 0;
    std::ptrdiff_t dwNetworkGameClient = 0;

    std::ptrdiff_t dwNetworkGameClient_signOnState = 0;
}

namespace
{
    bool g_initialized = false;

    struct PatternByte
    {
        std::uint8_t value;
        bool wildcard;
    };

    std::vector<PatternByte> ParsePattern(const char* pattern)
    {
        std::vector<PatternByte> bytes;
        std::istringstream stream(pattern);
        std::string token;
        while (stream >> token)
        {
            if (token == "?" || token == "??")
                bytes.push_back({0, true});
            else
                bytes.push_back({static_cast<std::uint8_t>(std::strtoul(token.c_str(), nullptr, 16)), false});
        }
        return bytes;
    }

    uintptr_t ScanModule(uintptr_t moduleBase, const char* pattern)
    {
        if (!moduleBase)
            return 0;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(moduleBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const auto bytes = ParsePattern(pattern);
        if (bytes.empty())
            return 0;

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if ((section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const auto* begin = reinterpret_cast<const std::uint8_t*>(moduleBase + section[i].VirtualAddress);
            const auto size = static_cast<size_t>(section[i].Misc.VirtualSize);
            if (size < bytes.size())
                continue;

            for (size_t offset = 0; offset <= size - bytes.size(); ++offset)
            {
                bool matched = true;
                for (size_t j = 0; j < bytes.size(); ++j)
                {
                    if (!bytes[j].wildcard && begin[offset + j] != bytes[j].value)
                    {
                        matched = false;
                        break;
                    }
                }
                if (matched)
                    return reinterpret_cast<uintptr_t>(begin + offset);
            }
        }
        return 0;
    }

    // Resolve a RIP-relative address: at match+dispOffset there is a 4-byte
    // displacement, and the instruction ends at match+instrEnd.
    uintptr_t ResolveRIP(uintptr_t match, size_t dispOffset, size_t instrEnd)
    {
        if (!match)
            return 0;
        const auto disp = *reinterpret_cast<const std::int32_t*>(match + dispOffset);
        return match + instrEnd + disp;
    }

    // Read a uint32 value from match+offset (for struct member offsets
    // encoded in instructions like `mov eax, [rcx+offset]`).
    std::uint32_t ReadU32(uintptr_t match, size_t offset)
    {
        if (!match)
            return 0;
        return *reinterpret_cast<const std::uint32_t*>(match + offset);
    }

    bool ResolveClientSignatures(uintptr_t base)
    {
        using namespace signatures;
        int resolved = 0;
        int total = 0;

        // Signatures from a2x/cs2-dumper offsets.rs, converted to IDA-style.
        // Each pattern corresponds to a RIP-relative LEA/MOV that references
        // the global we need.

        // dwCSGOInput: 48 89 05 [rel32] 0F 57 C0 0F 11 05
        ++total;
        if (auto m = ScanModule(base, "48 89 05 ? ? ? ? 0F 57 C0 0F 11 05"))
        {
            dwCSGOInput = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwCSGOInput = 0x%X", static_cast<unsigned>(dwCSGOInput));
            ++resolved;

            // dwViewAngles: secondary pattern, offset within CSGOInput struct
            if (auto m2 = ScanModule(base, "F2 42 0F 10 84 28 ? ? ? ?"))
            {
                auto viewAnglesMemberOffset = static_cast<std::ptrdiff_t>(ReadU32(m2, 6));
                dwViewAngles = dwCSGOInput + viewAnglesMemberOffset;
                debug::Log("[sig] dwViewAngles = 0x%X (CSGOInput + 0x%X)",
                    static_cast<unsigned>(dwViewAngles), static_cast<unsigned>(viewAnglesMemberOffset));
            }
            else
                debug::Log("[sig] FAILED: dwViewAngles secondary pattern");
        }
        else
            debug::Log("[sig] FAILED: dwCSGOInput");

        // dwEntityList: 48 89 0D [rel32] E9 [rel32] CC
        ++total;
        if (auto m = ScanModule(base, "48 89 0D ? ? ? ? E9 ? ? ? ? CC"))
        {
            dwEntityList = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwEntityList = 0x%X", static_cast<unsigned>(dwEntityList));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwEntityList");

        // dwGameEntitySystem: 48 8B 1D [rel32] 48 89 1D [4 skip] 4C 63 B3
        ++total;
        if (auto m = ScanModule(base, "48 8B 1D ? ? ? ? 48 89 1D ? ? ? ? 4C 63 B3"))
        {
            dwGameEntitySystem = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwGameEntitySystem = 0x%X", static_cast<unsigned>(dwGameEntitySystem));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwGameEntitySystem");

        // dwGameRules: F6 C1 01 0F 85 [4] 4C 8B 05 [rel32] 4D 85
        ++total;
        if (auto m = ScanModule(base, "F6 C1 01 0F 85 ? ? ? ? 4C 8B 05 ? ? ? ? 4D 85"))
        {
            dwGameRules = static_cast<std::ptrdiff_t>(ResolveRIP(m, 12, 16) - base);
            debug::Log("[sig] dwGameRules = 0x%X", static_cast<unsigned>(dwGameRules));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwGameRules");

        // dwGlowManager: 48 8B 05 [rel32] C3 CC CC CC CC CC CC CC CC 8B 41
        ++total;
        if (auto m = ScanModule(base, "48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 8B 41"))
        {
            dwGlowManager = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwGlowManager = 0x%X", static_cast<unsigned>(dwGlowManager));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwGlowManager");

        // dwLocalPlayerController: 48 8B 05 [rel32] 41 89 BE
        ++total;
        if (auto m = ScanModule(base, "48 8B 05 ? ? ? ? 41 89 BE"))
        {
            dwLocalPlayerController = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwLocalPlayerController = 0x%X", static_cast<unsigned>(dwLocalPlayerController));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwLocalPlayerController");

        // dwPrediction: 48 8D 05 [rel32] C3 CC CC CC CC CC CC CC CC 40 53 56 41 54
        ++total;
        if (auto m = ScanModule(base, "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 40 53 56 41 54"))
        {
            dwPrediction = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwPrediction = 0x%X", static_cast<unsigned>(dwPrediction));
            ++resolved;

            // dwLocalPlayerPawn: secondary pattern, offset within Prediction struct
            if (auto m2 = ScanModule(base, "4C 39 B6 ? ? ? ? 74 ? 44 88 BE"))
            {
                auto pawnMemberOffset = static_cast<std::ptrdiff_t>(ReadU32(m2, 3));
                dwLocalPlayerPawn = dwPrediction + pawnMemberOffset;
                debug::Log("[sig] dwLocalPlayerPawn = 0x%X (Prediction + 0x%X)",
                    static_cast<unsigned>(dwLocalPlayerPawn), static_cast<unsigned>(pawnMemberOffset));
            }
            else
                debug::Log("[sig] FAILED: dwLocalPlayerPawn secondary pattern");
        }
        else
            debug::Log("[sig] FAILED: dwPrediction");

        // dwViewMatrix: 48 8D 0D [rel32] 48 C1 E0 06
        ++total;
        if (auto m = ScanModule(base, "48 8D 0D ? ? ? ? 48 C1 E0 06"))
        {
            dwViewMatrix = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwViewMatrix = 0x%X", static_cast<unsigned>(dwViewMatrix));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwViewMatrix");

        debug::Log("[sig] client.dll: resolved %d/%d global signatures", resolved, total);
        return resolved == total;
    }

    bool ResolveEngine2Signatures(uintptr_t base)
    {
        using namespace signatures;
        int resolved = 0;
        int total = 0;

        // dwNetworkGameClient: 48 89 3D [rel32] FF 87
        ++total;
        if (auto m = ScanModule(base, "48 89 3D ? ? ? ? FF 87"))
        {
            dwNetworkGameClient = static_cast<std::ptrdiff_t>(ResolveRIP(m, 3, 7) - base);
            debug::Log("[sig] dwNetworkGameClient = 0x%X", static_cast<unsigned>(dwNetworkGameClient));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwNetworkGameClient");

        // dwNetworkGameClient_signOnState: 44 8B 81 [u32] 48 8D 0D
        ++total;
        if (auto m = ScanModule(base, "44 8B 81 ? ? ? ? 48 8D 0D"))
        {
            dwNetworkGameClient_signOnState = static_cast<std::ptrdiff_t>(ReadU32(m, 3));
            debug::Log("[sig] dwNetworkGameClient_signOnState = 0x%X",
                static_cast<unsigned>(dwNetworkGameClient_signOnState));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwNetworkGameClient_signOnState");

        // dwWindowWidth: 8B 05 [rel32] 89 07
        ++total;
        if (auto m = ScanModule(base, "8B 05 ? ? ? ? 89 07"))
        {
            dwWindowWidth = static_cast<std::ptrdiff_t>(ResolveRIP(m, 2, 6) - base);
            debug::Log("[sig] dwWindowWidth = 0x%X", static_cast<unsigned>(dwWindowWidth));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwWindowWidth");

        // dwWindowHeight: 8B 05 [rel32] 89 03
        ++total;
        if (auto m = ScanModule(base, "8B 05 ? ? ? ? 89 03"))
        {
            dwWindowHeight = static_cast<std::ptrdiff_t>(ResolveRIP(m, 2, 6) - base);
            debug::Log("[sig] dwWindowHeight = 0x%X", static_cast<unsigned>(dwWindowHeight));
            ++resolved;
        }
        else
            debug::Log("[sig] FAILED: dwWindowHeight");

        debug::Log("[sig] engine2.dll: resolved %d/%d signatures", resolved, total);
        return resolved == total;
    }
}

bool signatures::Init()
{
    if (g_initialized)
        return true;

    debug::Log("[sig] initializing signature scanner...");

    const auto clientBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    const auto engineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine2.dll"));

    if (!clientBase || !engineBase)
    {
        debug::Log("[sig] modules not loaded yet: client=0x%p engine=0x%p",
            reinterpret_cast<void*>(clientBase), reinterpret_cast<void*>(engineBase));
        return false;
    }

    const bool clientOk = ResolveClientSignatures(clientBase);
    const bool engineOk = ResolveEngine2Signatures(engineBase);

    g_initialized = clientOk && engineOk;

    if (g_initialized)
        debug::Log("[sig] all signatures resolved successfully");
    else
        debug::Log("[sig] WARNING: some signatures failed — features may not work");

    return g_initialized;
}

bool signatures::IsInitialized()
{
    return g_initialized;
}
