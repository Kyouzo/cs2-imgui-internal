#pragma once

namespace globals
{
    extern bool esp;

    struct EspConfig
    {
        bool onlyEnemies = true;
        bool cornerBox = true;
        bool skeleton = true;
        bool snapline = true;
        bool healthBar = true;
        bool healthText = true;
        bool headMarker = true;
        bool positionArrow = true;
        bool teamColors = true;
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
