#pragma once
#include <cstdint>
#include <d3d11.h>

namespace SkinChangerBridge
{
    // Call once from the init thread after kiero+ImGui are set up.
    bool Init();

    // Call after pDevice is known (first hkPresent frame). Safe to call multiple times.
    void InitImages(ID3D11Device* device);

    // Call every frame from hkPresent (render thread). Publishes menu state; the skin pass itself runs
    // on the game thread via a FrameStageNotify hook installed by Init().
    void Tick();

    // Call first thing in the unload thread: removes the FrameStageNotify hook, waits for in-flight calls.
    void Shutdown();

    // Call inside the ImGui "Skins" tab item — renders the full skin picker UI.
    void RenderSkinsTab();

    extern bool g_enabled;
    extern bool g_initialized;
}
