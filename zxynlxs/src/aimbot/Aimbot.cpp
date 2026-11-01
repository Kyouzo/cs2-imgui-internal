#define NOMINMAX
#include "Aimbot.h"

#include <algorithm>
#include <cmath>
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
            const auto viewAngles = reinterpret_cast<const float*>(
                visuals::clientModule + offsets::dwViewAngles);
            angles = {viewAngles[0], viewAngles[1], 0.0f};
            ClampAngles(angles);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            angles = {};
            return false;
        }
    }

    void WriteViewAngles(const Vec3& angles)
    {
        const auto viewAngles = reinterpret_cast<float*>(
            visuals::clientModule + offsets::dwViewAngles);
        __try
        {
            viewAngles[0] = angles.x;
            viewAngles[1] = angles.y;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
}

void aimbot::Tick()
{
    const auto& config = globals::aimConfig;
    if (!config.enabled || (config.requireKey &&
        !(GetAsyncKeyState(config.aimKey) & 0x8000)))
        return;

    visuals::clientModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    visuals::engineModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine2.dll"));

    Vec3 currentView{};
    if (!ReadViewAngles(currentView))
        return;

    Vec3 targetAngles{};
    if (!visuals::FindBestAimPoint(currentView, targetAngles,
        config.enemiesOnly, config.targetBone, config.fov))
        return;

	if (!std::isfinite(targetAngles.x) || !std::isfinite(targetAngles.y))
		return;
    const float smoothing = std::max(1.0f, config.smoothing);
    targetAngles.x = currentView.x + (targetAngles.x - currentView.x) / smoothing;
    targetAngles.y = currentView.y + NormalizeYaw(targetAngles.y - currentView.y) / smoothing;
    ClampAngles(targetAngles);
    WriteViewAngles(targetAngles);
}
