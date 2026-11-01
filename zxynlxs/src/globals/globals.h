#pragma once

namespace globals
{
    extern bool esp;

    struct AimConfig
    {
        bool enabled = false;
        bool showFov = true;
        bool enemiesOnly = true;
        bool requireKey = true;
        int aimKey = 0x05; // VK_XBUTTON1 / Mouse 4
        int targetBone = 0; // 0=head, 1=neck, 2=chest, 3=torso
        float fov = 5.0f;
        float smoothing = 4.0f;
    };

    extern AimConfig aimConfig;

    struct EspConfig
    {
        bool onlyEnemies = true;
        bool cornerBox = true;
        bool skeleton = true;
        bool snapline = true;
        bool healthBar = true;
        bool healthText = true;
        bool name = true;
        bool distance = true;
        bool alert360 = true;
        bool headMarker = true;
        bool positionArrow = true;
        bool teamColors = true;
        bool spectatorList = true;
        bool showMenu = true;
        bool crosshair = false;
        bool crosshairDot = true;
        bool crosshairOutline = true;
        float crosshairLength = 5.0f;
        float crosshairGap = 3.0f;
        float crosshairThickness = 1.0f;
    };

    extern EspConfig espConfig;
}
