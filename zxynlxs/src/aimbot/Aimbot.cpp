#define NOMINMAX
#include "Aimbot.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <Windows.h>

#include "../globals/globals.h"
#include "../visuals/visuals.h"

namespace
{
    float NormalizeYaw(float yaw)
    {
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
        return yaw;
    }

    void ClampAngles(Vec3& angles)
    {
        angles.x = std::clamp(angles.x, -89.0f, 89.0f);
        angles.y = NormalizeYaw(angles.y);
        angles.z = 0.0f;
    }

    bool ReadViewAngles(Vec3& angles)
    {
        if (!visuals::clientModule)
            return false;
        __try
        {
            const auto* va = reinterpret_cast<const float*>(
                visuals::clientModule + offsets::dwViewAngles());
            angles = {va[0], va[1], 0.0f};
            ClampAngles(angles);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void WriteViewAngles(const Vec3& angles)
    {
        __try
        {
            auto* va = reinterpret_cast<float*>(
                visuals::clientModule + offsets::dwViewAngles());
            va[0] = angles.x;
            va[1] = angles.y;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool ReadAimPunch(uintptr_t pawn, Vec3& out)
    {
        uintptr_t services = 0;
        __try { services = *reinterpret_cast<const uintptr_t*>(pawn + offsets::m_pAimPunchServices); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (!services) return false;
        __try
        {
            const auto* qa = reinterpret_cast<const float*>(services + offsets::m_aimPunchAngle);
            out = {qa[0], qa[1], 0.0f};
            return std::isfinite(out.x) && std::isfinite(out.y);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Interpolate from `src` toward `dst`.
    //   mode 0 — linear:      constant fraction each frame
    //   mode 1 — exponential: decelerates as it nears the target (more natural)
    Vec3 Smooth(const Vec3& src, const Vec3& dst, float smoothing, int mode)
    {
        const float dx = dst.x - src.x;
        const float dy = NormalizeYaw(dst.y - src.y);
        float t = 0.0f;
        if (mode == 1)
            t = 1.0f - std::expf(-1.0f / std::max(1.0f, smoothing));
        else
            t = 1.0f / std::max(1.0f, smoothing);
        return {src.x + dx * t, NormalizeYaw(src.y + dy * t), 0.0f};
    }
}

void aimbot::Tick()
{
    const auto& cfg = globals::aimConfig;
    if (!cfg.enabled || (cfg.requireKey && !(GetAsyncKeyState(cfg.aimKey) & 0x8000)))
        return;

    const auto clientMod = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    if (!clientMod)
        return;

    visuals::clientModule = clientMod;
    visuals::engineModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine2.dll"));

    uintptr_t localPawn = 0;
    __try { localPawn = *reinterpret_cast<const uintptr_t*>(clientMod + offsets::dwLocalPlayerPawn()); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!localPawn)
        return;

    // Skip when blinded.
    if (cfg.flashCheck)
    {
        float flash = 0.0f;
        __try { flash = *reinterpret_cast<const float*>(localPawn + offsets::m_flFlashDuration); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (flash > 0.01f)
            return;
    }

    // Tighten FOV when scoped.
    float effectiveFov = cfg.fov;
    if (cfg.scopedFov)
    {
        bool scoped = false;
        __try { scoped = *reinterpret_cast<const bool*>(localPawn + offsets::m_bIsScoped); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (scoped) effectiveFov *= 0.5f;
    }

    Vec3 cleanView{};
    if (!ReadViewAngles(cleanView))
        return;

    // --- RCS setup ---
    // Read the dedicated recoil punch angle from CCSPlayer_AimPunchServices.
    // m_predictableBaseAngle contains only the recoil kick, not sway/bob.
    // We subtract it after smoothing so the game adding it back lands us on target.
    Vec3 punch{};
    int shotsFired = 0;
    bool rcsActive = false;
    if (cfg.rcs)
    {
        ReadAimPunch(localPawn, punch);
        __try { shotsFired = *reinterpret_cast<const int*>(localPawn + offsets::m_iShotsFired); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        rcsActive = shotsFired > 1;
    }

    Vec3 boneAngles{};
    if (!visuals::FindBestAimPoint(cleanView, boneAngles, cfg.enemiesOnly, cfg.targetBone, effectiveFov))
        return;

    if (!std::isfinite(boneAngles.x) || !std::isfinite(boneAngles.y))
        return;

    Vec3 target = Smooth(cleanView, boneAngles, cfg.smoothing, cfg.smoothMode);
    ClampAngles(target);

    // Humanization: low-pass filtered random walk that breaks up mechanical patterns.
    if (cfg.humanize)
    {
        static float hX = 0.0f, hY = 0.0f;
        const float s = cfg.humanizeScale * 0.06f;
        hX = hX * 0.80f + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * s;
        hY = hY * 0.80f + (static_cast<float>(rand()) / RAND_MAX - 0.5f) * s;
        target.x = std::clamp(target.x + hX, -89.0f, 89.0f);
        target.y = NormalizeYaw(target.y + hY);
    }

    // RCS: subtract accumulated punch so actual aim (written + punch) lands on target.
    if (rcsActive)
    {
        const float scale = std::clamp(cfg.rcsScale, 0.0f, 2.0f);
        target.x -= punch.x * scale;
        target.y  = NormalizeYaw(target.y - punch.y * scale);
        ClampAngles(target);
    }

    WriteViewAngles(target);
}
