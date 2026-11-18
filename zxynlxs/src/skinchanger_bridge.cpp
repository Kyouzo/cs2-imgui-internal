#include "skinchanger_bridge.h"
#include "debug/debug.h"

// Skinchanger subsystem (Unboxer port)
#include "chadware2/interfaces/interfaces.h"
#include "chadware2/globals/globals.h"
#include "chadware2/utils/schema/schema.h"
#include "chadware2/features/skinchanger/skinchanger.h"
#include "chadware2/features/skinchanger/item_schema.h"
#include "chadware2/config/config.h"
#include "chadware2/config/configmanager.h"
#include "cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "chadware2/features/unboxer/game_inventory.h"

#include "imgui/imgui.h"
#include "chadware2/menu/skinchanger/images/skin_images.h"
#include "chadware2/utils/memory/patternscan/patternscan.h"
#include "skinchanger/patterns.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace SkinChangerBridge
{
    bool g_enabled        = true;
    bool g_initialized    = false;
    bool g_applyBothTeams = false;
    static bool g_imagesInitialized = false;
    static bool g_invInited         = false;

    static Schema g_schema;

    // ── Inventory state ──────────────────────────────────────────────────────────
    struct InvEntry
    {
        int   paintKit  = 0;
        int   seed      = 0;
        float wear      = 0.0f;
        bool  statTrak  = false;
    };
    static std::unordered_map<std::uint64_t, InvEntry> s_invItems;

    // Inventory writes allowed per pass.  Enabling with a big config loaded used to create every
    // item (all weapons x both teams) inside one frame; now it is spread over several passes.
    static int  s_invBudget  = 0;
    static bool s_invPending = false;

    // Build a deterministic fake item ID from (team, defIdx).
    static constexpr std::uint64_t FakeItemId(int team, std::uint16_t defIdx)
    {
        return 0xF000000000000000ULL | (static_cast<std::uint64_t>(team & 0xF) << 48) | defIdx;
    }

    // Sync one weapon map's entries into the game inventory.
    static void SyncMap(int team,
                        const char* teamStr,
                        const std::unordered_map<std::uint16_t, Config::item_skin_t>& weapons)
    {
        for (const auto& [defIdx, skin] : weapons)
        {
            const std::uint64_t fakeId = FakeItemId(team, defIdx);

            // If disabled or no paint kit, remove any existing fake item.
            if (!skin.enabled || skin.paintKit == 0)
            {
                if (s_invItems.count(fakeId))
                {
                    if (s_invBudget <= 0) { s_invPending = true; continue; }
                    --s_invBudget;
                    GameInv::RemoveItem(fakeId);
                    s_invItems.erase(fakeId);
                }
                continue;
            }

            InvEntry cur{ skin.paintKit, skin.seed, skin.wear, skin.statTrakEnabled };

            auto it = s_invItems.find(fakeId);
            if (it != s_invItems.end())
            {
                // Already in inventory — skip if unchanged.
                if (it->second.paintKit == cur.paintKit &&
                    it->second.seed     == cur.seed     &&
                    it->second.wear     == cur.wear     &&
                    it->second.statTrak == cur.statTrak)
                    continue;

                if (s_invBudget <= 0) { s_invPending = true; continue; }
                --s_invBudget;
                GameInv::RemoveItem(fakeId);
                s_invItems.erase(fakeId);
            }
            else
            {
                if (s_invBudget <= 0) { s_invPending = true; continue; }
                --s_invBudget;
            }

            GameInv::NewItem item;
            item.id         = fakeId;
            item.definition = defIdx;
            item.quality    = skin.statTrakEnabled ? 9 : 4;
            item.rarity     = 4;
            item.paintKit   = skin.paintKit;
            item.seed       = skin.seed;
            item.wear       = skin.wear;
            item.statTrak   = skin.statTrakEnabled;
            item.statTrakKills = skin.statTrakKills;
            item.customName = skin.customName;

            if (GameInv::CreateItem(item))
            {
                s_invItems[fakeId] = cur;
                std::string slot = GameInv::DefaultSlot(fakeId);
                if (!slot.empty())
                    GameInv::LoadoutEquip(teamStr, fakeId, slot.c_str());
            }
        }
    }

    // Remove every fake item we put in the inventory (skinchanger switched off).
    static void RemoveAllFakeItems()
    {
        if (!g_invInited || !GameInv::InventoryReady())
            return;

        for (auto it = s_invItems.begin(); it != s_invItems.end();)
        {
            if (s_invBudget <= 0) { s_invPending = true; return; }
            --s_invBudget;
            GameInv::RemoveItem(it->first);
            it = s_invItems.erase(it);
        }
    }

    // Game thread, every few seconds; updates the game inventory to match the skin config.
    static void SyncInventory()
    {
        if (!g_invInited || !GameInv::InventoryReady())
            return;

        for (int team = 0; team <= 1; ++team)
        {
            const char* teamStr = (team == Config::TEAM_CT) ? "ct" : "t";
            const auto& loadout = Config::Game().loadouts[team];   // game thread: the published copy, not the menu's
            SyncMap(team, teamStr, loadout.weapons);
            SyncMap(team, teamStr, loadout.knives);
            SyncMap(team, teamStr, loadout.gloves);
        }
    }

    // ── Game-thread driver ───────────────────────────────────────────────────────────────
    // Everything that touches entities, the inventory or game functions must run on the GAME thread
    // (the skinchanger was written for FrameStageNotify / FRAME_NET_UPDATE_END).  It used to run from
    // hkPresent on the render thread, so it raced entity teardown on map change and the game's own
    // update when toggled mid-match.  We now route it through FrameStageNotify by shadowing the
    // Source2Client vtable (heap copy - no code bytes patched, same trick as the swap chain).
    constexpr int FRAME_NET_UPDATE_END = 7;   // same value knife_animation.cpp uses

    using FrameStageNotifyFn = void(__fastcall*)(void*, int);

    static FrameStageNotifyFn s_origFrameStage = nullptr;
    static void**             s_shadowVtbl     = nullptr;
    static void**             s_realVtbl       = nullptr;
    static void*              s_hookedClient   = nullptr;
    static std::atomic_bool   s_frameHooked{ false };
    static std::atomic_bool   s_shuttingDown{ false };
    static std::atomic_uint   s_frameActive{ 0 };

    // Game-thread-only state.
    static bool        s_inSession     = false;   // we saw the engine in a match
    static bool        s_prevEnabled   = false;
    static const void* s_entitySystem  = nullptr; // a new entity system = new map
    static ULONGLONG   s_settleUntilMs = 0;       // don't touch entities until the world has settled
    static ULONGLONG   s_pauseUntilMs  = 0;       // circuit breaker after a fault
    static ULONGLONG   s_nextInvSyncMs = 0;
    static int         s_faultStreak   = 0;

    constexpr ULONGLONG kSessionSettleMs = 1500;  // after (re)joining a map
    constexpr ULONGLONG kEnableSettleMs  = 400;   // after switching the changer on mid-match

    static void ResetWorldState(const char* why)
    {
        SkinChanger::Reset();   // every cache is keyed by weapon address / pawn: all of it is stale now
        g_ctx->local_pawn = nullptr;
        debug::Log("[skin] state reset: %s", why);
    }

    // SEH wrappers - no C++ objects with destructors in these scopes (MSVC requirement).
    static void SyncInventorySafe(bool removeAll)
    {
        __try
        {
            s_invBudget  = 6;
            s_invPending = false;
            if (removeAll) RemoveAllFakeItems();
            else           SyncInventory();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_invInited = false; // stop future syncs rather than crash on every pass
            debug::Log("[inv] inventory sync crashed - disabled until DLL reload");
        }
    }

    static void InventoryFrame(ULONGLONG now, bool enabled)
    {
        if (!g_invInited || now < s_nextInvSyncMs)
            return;

        if (!enabled && s_invItems.empty())
            return;

        s_nextInvSyncMs = now + 5000;
        SyncInventorySafe(!enabled);

        if (s_invPending)               // budget ran out: continue soon
            s_nextInvSyncMs = now + 500;
    }

    static void GameFrame()
    {
        const ULONGLONG now = GetTickCount64();
        const bool enabled  = Config::Game().enabled;

        const bool connected = I::EngineClient && I::EngineClient->connected();
        const bool inGame    = connected && I::EngineClient->in_game();
        const bool loading   = connected && !inGame;

        // Not in a match (main menu, loading screen, map change, disconnect): don't read any entity.
        if (!inGame)
        {
            if (s_inSession)
            {
                s_inSession = false;
                ResetWorldState("left match / map change");
            }
            s_entitySystem    = nullptr;
            g_ctx->local_pawn = nullptr;
            SkinChanger::UpdateLiveInfo(nullptr);

            if (!loading)
                InventoryFrame(now, enabled);
            return;
        }

        const void* entitySystem = I::GameEntity ? static_cast<const void*>(I::GameEntity->Instance) : nullptr;

        // Joined a match, or the map changed without us seeing "not in game" (new entity system).
        if (!s_inSession || entitySystem != s_entitySystem)
        {
            s_inSession     = true;
            s_entitySystem  = entitySystem;
            s_settleUntilMs = now + kSessionSettleMs;
            ResetWorldState("entered match / new map");
        }

        // Switched on/off mid-match: start from a clean slate and give the world a moment.
        if (enabled != s_prevEnabled)
        {
            s_prevEnabled = enabled;
            ResetWorldState(enabled ? "enabled in game" : "disabled in game");
            if (enabled)
                s_settleUntilMs = (std::max)(s_settleUntilMs, now + kEnableSettleMs);
        }

        if (now < s_settleUntilMs || now < s_pauseUntilMs)
        {
            g_ctx->local_pawn = nullptr;
            SkinChanger::UpdateLiveInfo(nullptr);
            return;
        }

        C_CSPlayerPawn* pawn = I::GetLocalPawn();
        g_ctx->local_pawn = pawn;

        SkinChanger::UpdateLiveInfo(pawn);
        SkinChanger::Run();
        SkinChanger::RunAgentChanger();
        SkinChanger::ProcessPendingKeychains();

        InventoryFrame(now, enabled);
    }

    static void OnGameFault()
    {
        g_ctx->local_pawn = nullptr;
        s_inSession       = false;   // forces a full reset + settle once the world is readable again
        ++s_faultStreak;
        s_pauseUntilMs = GetTickCount64() + (s_faultStreak >= 3 ? 10000 : 2000);
        debug::Log("[skin] frame faulted (%d in a row) - state reset, paused", s_faultStreak);
    }

    static void GameFrameSafe()
    {
        __try
        {
            GameFrame();
            s_faultStreak = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            OnGameFault();
        }
    }

    static void GameTick()
    {
        if (!g_initialized || s_shuttingDown.load(std::memory_order_acquire))
            return;

        Config::SyncGameSkins();   // take the newest copy the render thread published
        GameFrameSafe();
    }

    static void __fastcall hkFrameStageNotify(void* self, int stage)
    {
        struct Guard
        {
            Guard()  { ++s_frameActive; }
            ~Guard() { --s_frameActive; }
        } guard;   // covers the original call too: our code is still on the stack until it returns

        if (stage == FRAME_NET_UPDATE_END)
            GameTick();

        s_origFrameStage(self, stage);
    }

    static bool InstallFrameHook()
    {
        if (s_frameHooked.load() || !I::Source2Client)
            return s_frameHooked.load();

        const std::uint8_t* target = M::scan("client.dll", Patterns::FrameStageNotify);
        if (!target)
        {
            debug::Log("[skin] FrameStageNotify signature not found");
            return false;
        }

        void* client = I::Source2Client;
        void** vtbl  = *reinterpret_cast<void***>(client);

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(vtbl, &mbi, sizeof(mbi)))
            return false;

        const std::size_t avail = static_cast<std::size_t>(
            (reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<std::uintptr_t>(vtbl)) / sizeof(void*));
        const std::size_t slots = (std::min<std::size_t>)(avail, 512);

        int slot = -1;
        for (std::size_t i = 0; i < slots; ++i)
        {
            if (vtbl[i] == target)
            {
                slot = static_cast<int>(i);
                break;
            }
        }

        // GetEconItemSystem is vfunc 129: the copy must cover at least that.
        if (slot < 0 || slots <= 130)
        {
            debug::Log("[skin] FrameStageNotify not in Source2Client vtable (slot %d, %zu slots)", slot, slots);
            return false;
        }

        void** shadow = static_cast<void**>(VirtualAlloc(nullptr, slots * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!shadow)
            return false;

        std::memcpy(shadow, vtbl, slots * sizeof(void*));
        s_origFrameStage = reinterpret_cast<FrameStageNotifyFn>(vtbl[slot]);
        shadow[slot]     = reinterpret_cast<void*>(&hkFrameStageNotify);

        s_shadowVtbl   = shadow;
        s_realVtbl     = vtbl;
        s_hookedClient = client;
        *reinterpret_cast<void***>(client) = shadow;   // one aligned pointer write

        s_frameHooked.store(true, std::memory_order_release);
        debug::Log("[skin] FrameStageNotify hooked via vtable slot %d - skin pass runs on the game thread", slot);
        return true;
    }

    void Shutdown()
    {
        s_shuttingDown.store(true, std::memory_order_release);

        if (s_hookedClient && s_realVtbl)
        {
            *reinterpret_cast<void***>(s_hookedClient) = s_realVtbl;
            s_hookedClient = nullptr;
        }

        while (s_frameActive.load() != 0)
            Sleep(1);
        Sleep(100);   // a call that loaded the shadow slot but hasn't entered the hook yet

        s_frameHooked.store(false);
        if (s_shadowVtbl)
        {
            VirtualFree(s_shadowVtbl, 0, MEM_RELEASE);
            s_shadowVtbl = nullptr;
        }
    }

    bool Init()
    {
        debug::Log("[skin] initializing interfaces");
        if (!I::Interfaces().init())
        {
            debug::Log("[skin] interfaces init failed");
            return false;
        }

        debug::Log("[skin] initializing schema");
        if (!g_schema.init("client.dll", 0))
        {
            debug::Log("[skin] schema init failed");
            return false;
        }

        debug::Log("[skin] initializing item schema");
        for (int attempt = 1; !Skins::g_ItemSchema.Initialize(); ++attempt)
        {
            debug::Log("[skin] item schema init failed (attempt %d), retrying in 2s...", attempt);
            Sleep(2000);
        }

        g_initialized = true;
        debug::Log("[skin] ready");

        if (GameInv::Initialize())
        {
            g_invInited = true;
            debug::Log("[inv] GameInv ready");
        }
        else
        {
            debug::Log("[inv] GameInv init failed: %s", GameInv::LastError());
        }

        // Last: once this is in, the game thread starts calling GameTick().
        if (!InstallFrameHook())
            debug::Log("[skin] frame hook unavailable - falling back to the render thread (less stable)");

        return true;
    }

    void InitImages(ID3D11Device* device)
    {
        if (!g_initialized || g_imagesInitialized || !device)
            return;
        g_imagesInitialized = SkinImages::Initialize(device);
    }

    // Render thread, every frame.  Only publishes menu state to the game thread; the actual skin pass
    // runs in hkFrameStageNotify.  Fallback when the hook is missing: run it here (old behaviour).
    void Tick()
    {
        if (g_imagesInitialized)
            SkinImages::BeginFrame();

        if (!g_initialized)
            return;

        // The master switch is authoritative: the game thread sees "disabled" and cleans up properly
        // (it used to stop being called, leaving stale state behind for the next enable).
        Config::skins.enabled = g_enabled;

        if (g_enabled && g_applyBothTeams)
        {
            const int other = (Config::editTeam == Config::TEAM_CT) ? Config::TEAM_T : Config::TEAM_CT;
            Config::skins.loadouts[other].weapons = Config::skins.loadouts[Config::editTeam].weapons;
        }

        Config::PublishSkins();

        if (!s_frameHooked.load(std::memory_order_acquire))
            GameTick();
    }

    // ── Helpers ─────────────────────────────────────────────────────────────────────────

    static ImVec4 RarityColor(int rarity)
    {
        switch (rarity)
        {
        case 1:  return { 0.75f, 0.75f, 0.75f, 1.f }; // Consumer  – grey
        case 2:  return { 0.69f, 0.85f, 1.00f, 1.f }; // Industrial – light blue
        case 3:  return { 0.33f, 0.44f, 0.90f, 1.f }; // Mil-Spec   – blue
        case 4:  return { 0.57f, 0.26f, 0.89f, 1.f }; // Restricted – purple
        case 5:  return { 0.85f, 0.22f, 0.56f, 1.f }; // Classified – pink
        case 6:  return { 0.90f, 0.19f, 0.19f, 1.f }; // Covert     – red
        case 7:  return { 0.90f, 0.55f, 0.00f, 1.f }; // Contraband – orange
        case 99: return { 0.87f, 0.80f, 0.21f, 1.f }; // ★ knife/glove – gold
        default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    }

    static const char* RarityLabel(int rarity)
    {
        switch (rarity)
        {
        case 1:  return "Consumer";
        case 2:  return "Industrial";
        case 3:  return "Mil-Spec";
        case 4:  return "Restricted";
        case 5:  return "Classified";
        case 6:  return "Covert";
        case 7:  return "Contraband";
        case 99: return "★ Special";
        default: return "";
        }
    }

    static const char* WearLabel(float wear)
    {
        if (wear < 0.07f) return "Factory New";
        if (wear < 0.15f) return "Minimal Wear";
        if (wear < 0.38f) return "Field-Tested";
        if (wear < 0.45f) return "Well-Worn";
        return "Battle-Scarred";
    }

    static const char* WeaponCategory(const std::string& typeName)
    {
        if (typeName.find("Pistol")       != std::string::npos) return "Pistols";
        if (typeName.find("Rifle")        != std::string::npos) return "Rifles";
        if (typeName.find("SMG")          != std::string::npos) return "SMGs";
        if (typeName.find("SubMachine")   != std::string::npos) return "SMGs";
        if (typeName.find("Sniper")       != std::string::npos) return "Snipers";
        if (typeName.find("Shotgun")      != std::string::npos) return "Shotguns";
        if (typeName.find("Machine")      != std::string::npos) return "Machine Guns";
        return "Other";
    }

    // ── Skin preview image (shown above the skin combo) ─────────────────────────────────
    static void RenderSkinImage(const ItemDefinitionInfo& item, const Config::item_skin_t& skin)
    {
        if (!g_imagesInitialized)
            return;

        const SkinImages::Image* img = nullptr;

        if (skin.paintKit != 0)
        {
            const PaintKitInfo* pk = Skins::g_ItemSchema.GetPaintKit(skin.paintKit);
            if (pk)
                img = SkinImages::GetPaintKitImage(item, *pk);
        }
        if (!img)
            img = SkinImages::GetItemIcon(item);

        if (img && img->texture)
        {
            const float maxW = ImGui::GetContentRegionAvail().x;
            const float ratio = static_cast<float>(img->height) / static_cast<float>((std::max)(1u, img->width));
            const float dispW = (std::min)(maxW, 260.0f);
            const float dispH = dispW * ratio;
            ImGui::Image(reinterpret_cast<ImTextureID>(img->texture), ImVec2(dispW, dispH));
        }
        else
        {
            ImGui::TextDisabled("Loading image...");
        }
    }

    // ── Core skin options (skin combo + wear + seed + stattrak + nametag) ───────────────
    static void RenderSkinOptions(Config::item_skin_t& skin,
                                   const ItemDefinitionInfo& item,
                                   const char* uid,
                                   char* nameBuf,
                                   std::size_t nameBufSize)
    {
        const std::uint16_t defIdx = item.definitionIndex;
        ImGui::PushID(uid);

        ImGui::Checkbox("Enable##en", &skin.enabled);

        if (skin.enabled)
        {
            RenderSkinImage(item, skin);

            const auto* kits = Skins::g_ItemSchema.GetPaintKitsForItem(defIdx);

            // Find current skin + its rarity
            const PaintKitInfo* currentPk = nullptr;
            int currentRarity = 0;
            if (skin.paintKit != 0 && kits)
            {
                for (const auto& pk : *kits)
                {
                    if (pk.id == skin.paintKit)
                    {
                        currentPk = &pk;
                        currentRarity = Skins::g_ItemSchema.GetSkinRarity(item.weaponName, pk);
                        break;
                    }
                }
            }

            const char* comboLabel = currentPk ? currentPk->displayName.c_str() : "Default (no skin)";

            // Rarity badge next to the combo label
            if (currentRarity > 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, RarityColor(currentRarity));
                ImGui::TextUnformatted(RarityLabel(currentRarity));
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            ImGui::Text("Skin:");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##skin", comboLabel))
            {
                if (ImGui::Selectable("Default (no skin)", skin.paintKit == 0))
                    skin.paintKit = 0;

                if (kits)
                {
                    for (const auto& pk : *kits)
                    {
                        const int r = Skins::g_ItemSchema.GetSkinRarity(item.weaponName, pk);
                        ImGui::PushID(pk.id);
                        ImGui::PushStyleColor(ImGuiCol_Text, RarityColor(r));
                        bool sel = (skin.paintKit == pk.id);
                        if (ImGui::Selectable(pk.displayName.c_str(), sel))
                            skin.paintKit = pk.id;
                        ImGui::PopStyleColor();
                        if (sel)
                            ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();

            // Wear with label
            ImGui::SliderFloat("Wear##wear", &skin.wear, 0.0001f, 1.0f, "%.4f");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.65f, 0.65f, 0.65f, 1.f });
            ImGui::TextUnformatted(WearLabel(skin.wear));
            ImGui::PopStyleColor();

            ImGui::InputInt("Seed##seed", &skin.seed);
            skin.seed = (std::max)(0, (std::min)(skin.seed, 1000));

            ImGui::Spacing();
            ImGui::Checkbox("StatTrak##st", &skin.statTrakEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Souvenir##souv", &skin.souvenir);

            ImGui::Spacing();
            ImGui::Text("Name tag:");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##nametag", nameBuf, nameBufSize))
                skin.customName = nameBuf;
        }

        ImGui::PopID();
    }

    // ── Config save / load / reset ───────────────────────────────────────────────────────
    static void RenderConfigRow()
    {
        static char s_cfgName[64] = "default";
        static std::string s_status;

        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##cfgname", s_cfgName, sizeof(s_cfgName));
        ImGui::SameLine();

        if (ImGui::Button("Save"))
        {
            s_status = internal_config::ConfigManager::Save(s_cfgName)
                ? "Saved." : "Save failed.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            s_status = internal_config::ConfigManager::Load(s_cfgName)
                ? "Loaded." : "Load failed.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            Config::skins = Config::skins_t{};
            s_status = "Reset.";
        }

        if (!s_status.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", s_status.c_str());
        }
    }

    // ── Main tab ─────────────────────────────────────────────────────────────────────────
    void RenderSkinsTab()
    {
        if (!g_initialized)
        {
            ImGui::TextDisabled("Skinchanger not initialized.");
            return;
        }

        // Enable + team
        if (ImGui::Checkbox("Enable##master", &g_enabled))
            Config::skins.enabled = g_enabled;

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::Checkbox("Both Teams##bothteams", &g_applyBothTeams);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::Text("Side:");
        ImGui::SameLine();
        if (ImGui::RadioButton("T##team", Config::editTeam == Config::TEAM_T))
            Config::editTeam = Config::TEAM_T;
        ImGui::SameLine();
        if (ImGui::RadioButton("CT##team", Config::editTeam == Config::TEAM_CT))
            Config::editTeam = Config::TEAM_CT;

        ImGui::Separator();
        RenderConfigRow();
        ImGui::Separator();

        if (!g_enabled)
            return;

        const auto& allItems = Skins::g_ItemSchema.GetItems();
        auto& loadout = Config::Edit();

        if (!ImGui::BeginTabBar("##skincats"))
            return;

        // ── WEAPONS ─────────────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Weapons##wtab"))
        {
            // Build categorized weapon lists once
            static std::vector<std::pair<std::string, std::vector<const ItemDefinitionInfo*>>> s_cats;
            if (s_cats.empty())
            {
                std::unordered_map<std::string, std::vector<const ItemDefinitionInfo*>> bycat;
                for (const auto& it : allItems)
                {
                    if (!it.isWeapon || it.isKnife || it.isGlove || it.isAgent)
                        continue;
                    bycat[WeaponCategory(it.typeName)].push_back(&it);
                }
                // Fixed category order
                for (const char* cat : { "Pistols", "SMGs", "Rifles", "Snipers", "Shotguns", "Machine Guns", "Other" })
                {
                    auto it = bycat.find(cat);
                    if (it != bycat.end() && !it->second.empty())
                    {
                        std::sort(it->second.begin(), it->second.end(),
                            [](const ItemDefinitionInfo* a, const ItemDefinitionInfo* b)
                            { return a->displayName < b->displayName; });
                        s_cats.emplace_back(cat, std::move(it->second));
                    }
                }
            }

            static int s_selCat = 0;
            static int s_selWep = 0;
            if (s_selCat >= static_cast<int>(s_cats.size())) s_selCat = 0;

            // Category buttons
            for (int c = 0; c < static_cast<int>(s_cats.size()); c++)
            {
                if (c > 0) ImGui::SameLine();
                if (ImGui::SmallButton(s_cats[c].first.c_str()))
                { s_selCat = c; s_selWep = 0; }
            }
            ImGui::Separator();

            if (s_selCat < static_cast<int>(s_cats.size()))
            {
                const auto& weps = s_cats[s_selCat].second;
                if (s_selWep >= static_cast<int>(weps.size())) s_selWep = 0;

                // Left: weapon list for active category
                ImGui::BeginChild("##weplist", ImVec2(150.0f, 0.0f), true);
                for (int i = 0; i < static_cast<int>(weps.size()); i++)
                {
                    // Highlight if a skin is active on this weapon
                    auto it = loadout.weapons.find(weps[i]->definitionIndex);
                    bool hasSkin = (it != loadout.weapons.end() && it->second.enabled);
                    if (hasSkin)
                        ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f, 1.0f, 0.4f, 1.f });
                    bool sel = (s_selWep == i);
                    if (ImGui::Selectable(weps[i]->displayName.c_str(), sel))
                        s_selWep = i;
                    if (hasSkin)
                        ImGui::PopStyleColor();
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##wepopts", ImVec2(0.0f, 0.0f), false);
                {
                    static int s_lastWep = -1;
                    static int s_lastCat = -1;
                    static char s_wepName[64] = {};
                    const auto* info = weps[s_selWep];
                    auto& skin = loadout.weapons[info->definitionIndex];

                    if (s_lastWep != s_selWep || s_lastCat != s_selCat)
                    {
                        strncpy_s(s_wepName, skin.customName.c_str(), sizeof(s_wepName) - 1);
                        s_lastWep = s_selWep;
                        s_lastCat = s_selCat;
                    }

                    ImGui::Text("%s", info->displayName.c_str());
                    ImGui::Separator();
                    RenderSkinOptions(skin, *info, "wep", s_wepName, sizeof(s_wepName));
                }
                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }

        // ── KNIFE ───────────────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Knife##ktab"))
        {
            static std::vector<const ItemDefinitionInfo*> s_knives;
            if (s_knives.empty())
            {
                for (const auto& it : allItems)
                    if (it.isKnife)
                        s_knives.push_back(&it);
                std::sort(s_knives.begin(), s_knives.end(),
                    [](const ItemDefinitionInfo* a, const ItemDefinitionInfo* b)
                    { return a->displayName < b->displayName; });
            }

            const char* knifeLabel = "Default (stock knife)";
            const ItemDefinitionInfo* knifeInfo = nullptr;
            for (const auto* k : s_knives)
            {
                if (k->definitionIndex == loadout.knifeDefinition)
                { knifeLabel = k->displayName.c_str(); knifeInfo = k; break; }
            }

            ImGui::Text("Knife type:");
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::BeginCombo("##knifetype", knifeLabel))
            {
                if (ImGui::Selectable("Default (stock knife)", loadout.knifeDefinition == 0))
                    loadout.knifeDefinition = 0;
                for (const auto* k : s_knives)
                {
                    bool sel = (loadout.knifeDefinition == k->definitionIndex);
                    if (ImGui::Selectable(k->displayName.c_str(), sel))
                        loadout.knifeDefinition = k->definitionIndex;
                }
                ImGui::EndCombo();
            }

            if (loadout.knifeDefinition != 0 && knifeInfo)
            {
                ImGui::Separator();
                static std::uint16_t s_lastKnife = 0;
                static char s_knifeName[64] = {};
                auto& skin = loadout.knives[loadout.knifeDefinition];
                if (s_lastKnife != loadout.knifeDefinition)
                {
                    strncpy_s(s_knifeName, skin.customName.c_str(), sizeof(s_knifeName) - 1);
                    s_lastKnife = loadout.knifeDefinition;
                }
                RenderSkinOptions(skin, *knifeInfo, "knife", s_knifeName, sizeof(s_knifeName));
            }

            ImGui::EndTabItem();
        }

        // ── GLOVES ──────────────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Gloves##gtab"))
        {
            static std::vector<const ItemDefinitionInfo*> s_gloves;
            if (s_gloves.empty())
            {
                for (const auto& it : allItems)
                    if (it.isGlove)
                        s_gloves.push_back(&it);
                std::sort(s_gloves.begin(), s_gloves.end(),
                    [](const ItemDefinitionInfo* a, const ItemDefinitionInfo* b)
                    { return a->displayName < b->displayName; });
            }

            const char* gloveLabel = "Default gloves";
            const ItemDefinitionInfo* gloveInfo = nullptr;
            for (const auto* g : s_gloves)
            {
                if (g->definitionIndex == loadout.gloveDefinition)
                { gloveLabel = g->displayName.c_str(); gloveInfo = g; break; }
            }

            ImGui::Text("Glove type:");
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::BeginCombo("##glovetype", gloveLabel))
            {
                if (ImGui::Selectable("Default gloves", loadout.gloveDefinition == 0))
                    loadout.gloveDefinition = 0;
                for (const auto* g : s_gloves)
                {
                    bool sel = (loadout.gloveDefinition == g->definitionIndex);
                    if (ImGui::Selectable(g->displayName.c_str(), sel))
                        loadout.gloveDefinition = g->definitionIndex;
                }
                ImGui::EndCombo();
            }

            if (loadout.gloveDefinition != 0 && gloveInfo)
            {
                ImGui::Separator();
                static std::uint16_t s_lastGlove = 0;
                static char s_gloveName[64] = {};
                auto& skin = loadout.gloves[loadout.gloveDefinition];
                if (s_lastGlove != loadout.gloveDefinition)
                {
                    strncpy_s(s_gloveName, skin.customName.c_str(), sizeof(s_gloveName) - 1);
                    s_lastGlove = loadout.gloveDefinition;
                }
                RenderSkinOptions(skin, *gloveInfo, "glove", s_gloveName, sizeof(s_gloveName));
            }

            ImGui::EndTabItem();
        }

        // ── AGENT ───────────────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Agent##atab"))
        {
            static std::vector<const ItemDefinitionInfo*> s_agents;
            if (s_agents.empty())
            {
                for (const auto& it : allItems)
                    if (it.isAgent)
                        s_agents.push_back(&it);
                std::sort(s_agents.begin(), s_agents.end(),
                    [](const ItemDefinitionInfo* a, const ItemDefinitionInfo* b)
                    { return a->displayName < b->displayName; });
            }

            bool isCT = (Config::editTeam == Config::TEAM_CT);
            std::uint16_t& agentDef = Config::AgentFor(Config::editTeam);

            const char* agentLabel = "Default agent";
            const ItemDefinitionInfo* agentInfo = nullptr;
            for (const auto* a : s_agents)
            {
                if (a->definitionIndex == agentDef)
                { agentLabel = a->displayName.c_str(); agentInfo = a; break; }
            }

            ImGui::Text("Agent (%s):", isCT ? "CT" : "T");
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::BeginCombo("##agenttype", agentLabel))
            {
                if (ImGui::Selectable("Default agent", agentDef == 0))
                    agentDef = 0;
                for (const auto* a : s_agents)
                {
                    if (isCT && !a->isCounterTerroristAgent) continue;
                    if (!isCT && !a->isTerroristAgent) continue;
                    bool sel = (agentDef == a->definitionIndex);
                    if (ImGui::Selectable(a->displayName.c_str(), sel))
                        agentDef = a->definitionIndex;
                }
                ImGui::EndCombo();
            }

            if (agentDef != 0 && agentInfo && g_imagesInitialized)
            {
                ImGui::Separator();
                const SkinImages::Image* img = SkinImages::GetModelPreview(*agentInfo);
                if (img && img->texture)
                    ImGui::Image(reinterpret_cast<ImTextureID>(img->texture), ImVec2(120.0f, 180.0f));
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
