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

#include <algorithm>
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
    static int s_invSyncTimer = 0;

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

                GameInv::RemoveItem(fakeId);
                s_invItems.erase(fakeId);
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

    // Called from Tick() periodically; updates the game inventory to match the skin config.
    static void SyncInventory()
    {
        if (!g_invInited || !GameInv::InventoryReady())
            return;

        for (int team = 0; team <= 1; ++team)
        {
            const char* teamStr = (team == Config::TEAM_CT) ? "ct" : "t";
            const auto& loadout = Config::skins.loadouts[team];
            SyncMap(team, teamStr, loadout.weapons);
            SyncMap(team, teamStr, loadout.knives);
            SyncMap(team, teamStr, loadout.gloves);
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
        if (!Skins::g_ItemSchema.Initialize())
        {
            debug::Log("[skin] item schema init failed");
            return false;
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

        return true;
    }

    void InitImages(ID3D11Device* device)
    {
        if (!g_initialized || g_imagesInitialized || !device)
            return;
        g_imagesInitialized = SkinImages::Initialize(device);
    }

    // SEH wrapper for inventory sync — no C++ objects with destructors in this scope.
    static void SyncInventorySafe()
    {
        __try { SyncInventory(); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_invInited = false; // stop future syncs rather than crash on every timer tick
            debug::Log("[inv] SyncInventory crashed — inventory sync disabled until DLL reload");
        }
    }

    // All game-entity reads go through here.  During a map change or game end the engine
    // may tear down entity memory while our render hook still fires with a stale pawn
    // address.  Wrapping every dereference in one SEH block (no C++ objects with
    // destructors in *this* function's scope — the MSVC requirement for __try/__except)
    // catches the access violation and lets the next tick recover cleanly.
    static void RunSkinChangerSafe(uintptr_t pawnAddr)
    {
        __try
        {
            g_ctx->local_pawn = reinterpret_cast<C_CSPlayerPawn*>(pawnAddr);
            SkinChanger::UpdateLiveInfo(g_ctx->local_pawn);
            SkinChanger::Run();
            SkinChanger::RunAgentChanger();
            SkinChanger::ProcessPendingKeychains();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_ctx->local_pawn = nullptr;
            debug::Log("[skin] tick crashed (map change / game end) — pawn reset, will retry");
        }
    }

    void Tick(uintptr_t localPawnAddr)
    {
        if (g_imagesInitialized)
            SkinImages::BeginFrame();

        if (!g_initialized || !g_enabled)
            return;

        Config::skins.enabled = true;

        if (g_applyBothTeams)
        {
            const int other = (Config::editTeam == Config::TEAM_CT) ? Config::TEAM_T : Config::TEAM_CT;
            Config::skins.loadouts[other].weapons = Config::skins.loadouts[Config::editTeam].weapons;
        }

        Config::PublishSkins();
        Config::SyncGameSkins();

        RunSkinChangerSafe(localPawnAddr);

        // Sync inventory every ~5 s so skins appear in the inventory panel too.
        if (++s_invSyncTimer >= 300)
        {
            s_invSyncTimer = 0;
            SyncInventorySafe();
        }
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
