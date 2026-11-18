#include "math.h"
#include "../visuals/visuals.h"

#include <cstdint>

const bool Vec3::WorldToScreen(Vec2& out, float(*ViewMatrix)[4][4]) {
    if (!ViewMatrix || !visuals::engineModule)
        return false;

    float w = 0.0f;
    __try
    {
        w = (*ViewMatrix)[3][0] * x + (*ViewMatrix)[3][1] * y + (*ViewMatrix)[3][2] * z + (*ViewMatrix)[3][3];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (w <= 0.01)
        return false;

    const float invW = 1.0f / w;

    static uint32_t screenWidth = 0;
    static uint32_t screenHeight = 0;
    static uintptr_t cachedEngineModule = 0;
    static unsigned refreshCounter = 0;
    if (cachedEngineModule != visuals::engineModule || (++refreshCounter % 120) == 0)
    {
        __try
        {
            screenWidth = *(uint32_t*)(visuals::engineModule + offsets::dwWindowWidth());
            screenHeight = *(uint32_t*)(visuals::engineModule + offsets::dwWindowHeight());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            screenWidth = screenHeight = 0;
            cachedEngineModule = 0;
            return false;
        }
        cachedEngineModule = visuals::engineModule;
    }

    if (!screenWidth || !screenHeight)
        return false;

    float clipX = 0.0f;
    float clipY = 0.0f;
    __try
    {
        clipX = ((*ViewMatrix)[0][0] * x + (*ViewMatrix)[0][1] * y + (*ViewMatrix)[0][2] * z + (*ViewMatrix)[0][3]) * invW;
        clipY = ((*ViewMatrix)[1][0] * x + (*ViewMatrix)[1][1] * y + (*ViewMatrix)[1][2] * z + (*ViewMatrix)[1][3]) * invW;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    out.x = (static_cast<float>(screenWidth) * 0.5f) * (1.0f + clipX);
    out.y = (static_cast<float>(screenHeight) * 0.5f) * (1.0f - clipY);

    return true;
}
