#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "includes.h"
#include "globals/globals.h"
#include "visuals/visuals.h"
#include "aimbot/Aimbot.h"
#include "debug/debug.h"
#include "kiero/minhook/include/MinHook.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include "Z:/Projects/JetBrainsMonoNerdFontRegular.h"
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	const char* AimKeyName(int key)
	{
		switch (key)
		{
		case VK_XBUTTON1: return "Mouse 4";
		case VK_XBUTTON2: return "Mouse 5";
		case VK_LBUTTON: return "Left mouse";
		case VK_RBUTTON: return "Right mouse";
		case VK_MBUTTON: return "Middle mouse";
		case VK_SHIFT: return "Shift";
		case VK_CONTROL: return "Ctrl";
		case VK_MENU: return "Alt";
		case VK_SPACE: return "Space";
		case VK_TAB: return "Tab";
		case VK_CAPITAL: return "Caps Lock";
		case VK_ESCAPE: return "Escape";
		default: break;
		}

		static char keyName[16]{};
		if (key >= 'A' && key <= 'Z')
		{
			keyName[0] = static_cast<char>(key);
			keyName[1] = '\0';
			return keyName;
		}
		if (key >= '0' && key <= '9')
		{
			keyName[0] = static_cast<char>(key);
			keyName[1] = '\0';
			return keyName;
		}
		return "Unassigned";
	}
}

Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;
HMODULE gModule = nullptr;
std::atomic_bool gUnloadRequested = false;
std::atomic_bool gUnloadThreadStarted = false;
std::atomic_uint gPresentCallbacks = 0;
std::atomic_uint gWndProcCallbacks = 0;
std::atomic_bool gImGuiInitialized = false;

static void**          g_shadowVtable   = nullptr;
static void**          g_realVtableOrig = nullptr;
static IDXGISwapChain* g_hookedSwapChain = nullptr;

void RequestUnload();

void InitImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImFontConfig fontConfig;
	fontConfig.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF(JetBrainsMonoNerdFontRegular,
		static_cast<int>(sizeof(JetBrainsMonoNerdFontRegular)), 14.0f, &fontConfig,
		io.Fonts->GetGlyphRangesDefault());
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 8.0f;
	style.ChildRounding = 6.0f;
	style.FrameRounding = 5.0f;
	style.PopupRounding = 5.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding = 5.0f;
	style.WindowPadding = {14.0f, 12.0f};
	style.FramePadding = {8.0f, 5.0f};
	style.ItemSpacing = {8.0f, 6.0f};
	style.Colors[ImGuiCol_WindowBg] = {0.035f, 0.045f, 0.065f, 0.98f};
	style.Colors[ImGuiCol_ChildBg] = {0.055f, 0.065f, 0.090f, 0.72f};
	style.Colors[ImGuiCol_FrameBg] = {0.090f, 0.105f, 0.140f, 1.0f};
	style.Colors[ImGuiCol_FrameBgHovered] = {0.120f, 0.170f, 0.230f, 1.0f};
	style.Colors[ImGuiCol_Button] = {0.100f, 0.160f, 0.230f, 1.0f};
	style.Colors[ImGuiCol_ButtonHovered] = {0.150f, 0.330f, 0.520f, 1.0f};
	style.Colors[ImGuiCol_Header] = {0.100f, 0.180f, 0.270f, 1.0f};
	style.Colors[ImGuiCol_HeaderHovered] = {0.140f, 0.300f, 0.460f, 1.0f};
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
	gImGuiInitialized = true;
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	++gWndProcCallbacks;

	if (!gUnloadRequested && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
	{
		--gWndProcCallbacks;
		return true;
	}

	const auto result = CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
	--gWndProcCallbacks;
	return result;
}

bool init = false;
bool draw = false;
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	++gPresentCallbacks;
	if (gUnloadRequested)
	{
		const auto result = oPresent(pSwapChain, SyncInterval, Flags);
		--gPresentCallbacks;
		return result;
	}

	if (!init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)& pDevice)))
		{
			pDevice->GetImmediateContext(&pContext);
			DXGI_SWAP_CHAIN_DESC sd;
			pSwapChain->GetDesc(&sd);
			window = sd.OutputWindow;
			ID3D11Texture2D* pBackBuffer;
			pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)& pBackBuffer);
			pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
			pBackBuffer->Release();
			oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
			InitImGui();
			init = true;

			// Shadow the real swap chain's vtable so the MinHook inline JMP
			// in d3d11.dll can be removed immediately — all future Present
			// calls go through heap memory only (no module bytes modified).
			void** realVtable = *reinterpret_cast<void***>(pSwapChain);
			constexpr size_t kSwapChainSlots = 18;
			void** shadow = static_cast<void**>(
				VirtualAlloc(nullptr, kSwapChainSlots * sizeof(void*),
					MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (shadow)
			{
				memcpy(shadow, realVtable, kSwapChainSlots * sizeof(void*));
				shadow[8] = reinterpret_cast<void*>(&hkPresent);
				// Point oPresent directly at the real function (not the MinHook trampoline)
				// before the trampoline is freed by MH_RemoveHook.
				oPresent = reinterpret_cast<Present>(realVtable[8]);
				*reinterpret_cast<void***>(pSwapChain) = shadow;
				g_shadowVtable   = shadow;
				g_realVtableOrig = realVtable;
				g_hookedSwapChain = pSwapChain;
				// Remove the inline JMP from d3d11.dll — module bytes are clean again.
				if (auto* table = kiero::getMethodsTable())
				{
					void* presentTarget = reinterpret_cast<void*>(table[8]);
					MH_DisableHook(presentTarget);
					MH_RemoveHook(presentTarget);
				}
			}
		}

		else
		{
			const auto result = oPresent(pSwapChain, SyncInterval, Flags);
			--gPresentCallbacks;
			return result;
		}
	}

	if (GetAsyncKeyState(VK_INSERT) & 1) {
		draw = !draw;
		debug::Log("[ui] menu %s", draw ? "opened" : "closed");
	}
	if (GetAsyncKeyState(VK_END) & 1)
	{
		draw = false;
		RequestUnload();
		const auto result = oPresent(pSwapChain, SyncInterval, Flags);
		--gPresentCallbacks;
		return result;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	aimbot::Tick();
	if (globals::aimConfig.enabled && globals::aimConfig.showFov)
	{
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const ImVec2 center{display.x * 0.5f, display.y * 0.5f};
		const float fovRadians = std::clamp(globals::aimConfig.fov, 0.1f, 180.0f) * 0.5f * 0.0174532925f;
		const float radius = std::tan(fovRadians) * (display.y * 0.5f);
		ImGui::GetBackgroundDrawList()->AddCircle(center, std::clamp(radius, 4.0f, display.y),
			ImColor(110, 190, 255, 185), 96, 1.2f);
	}
	if (globals::espConfig.crosshair)
	{
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const float cx = display.x * 0.5f;
		const float cy = display.y * 0.5f;
		const float thickness = std::max(1.0f, globals::espConfig.crosshairThickness);
		const float length = std::max(0.0f, globals::espConfig.crosshairLength);
		const float gap = std::max(0.0f, globals::espConfig.crosshairGap);
		auto* crosshair = ImGui::GetBackgroundDrawList();
		const ImU32 color = ImColor(255, 255, 255, 235);
		const ImU32 outline = ImColor(0, 0, 0, 220);
		auto bar = [&](ImVec2 min, ImVec2 max)
		{
			if (globals::espConfig.crosshairOutline)
				crosshair->AddRectFilled({min.x - 1.0f, min.y - 1.0f}, {max.x + 1.0f, max.y + 1.0f}, outline);
			crosshair->AddRectFilled(min, max, color);
		};
		bar({cx - gap - length, cy - thickness * 0.5f}, {cx - gap, cy + thickness * 0.5f});
		bar({cx + gap, cy - thickness * 0.5f}, {cx + gap + length, cy + thickness * 0.5f});
		bar({cx - thickness * 0.5f, cy + gap}, {cx + thickness * 0.5f, cy + gap + length});
		bar({cx - thickness * 0.5f, cy - gap - length}, {cx + thickness * 0.5f, cy - gap});
		if (globals::espConfig.crosshairDot)
			bar({cx - thickness * 0.5f, cy - thickness * 0.5f}, {cx + thickness * 0.5f, cy + thickness * 0.5f});
	}
	if (globals::esp || globals::espConfig.spectatorList) {
		static bool loggedEspEntry = false;
		if (!loggedEspEntry) {
			debug::Log("[esp] entering render pass");
			loggedEspEntry = true;
		}
		__try
		{
			visuals::PlayerESP(globals::esp);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			const DWORD exceptionCode = GetExceptionCode();
			debug::Log("[esp] memory exception 0x%08lX; disabling ESP", exceptionCode);
			globals::esp = false;
		}
	}
		
	if (draw)
	{
		ImGui::SetNextWindowSize({560.0f, 390.0f}, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints({500.0f, 340.0f}, {820.0f, 620.0f});
		ImGui::Begin("CS2 INTERNAL", &draw, ImGuiWindowFlags_NoCollapse);
		ImGui::Separator();
		const bool previousEsp = globals::esp;
		if (ImGui::BeginTabBar("menu_tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
		{
			if (ImGui::BeginTabItem("Overview"))
			{
	
				ImGui::Checkbox("Enable ESP", &globals::esp);
				ImGui::SameLine(240.0f);
				ImGui::Checkbox("Enemies only", &globals::espConfig.onlyEnemies);
				ImGui::Checkbox("Spectator list", &globals::espConfig.spectatorList);
				ImGui::Checkbox("Team colors", &globals::espConfig.teamColors);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Player"))
			{
				ImGui::Columns(2, "player_columns", false);
				ImGui::TextDisabled("POSITION");
				ImGui::Checkbox("Cornered box", &globals::espConfig.cornerBox);
				ImGui::Checkbox("Skeleton", &globals::espConfig.skeleton);
				ImGui::Checkbox("Snapline", &globals::espConfig.snapline);
				ImGui::Checkbox("Head marker", &globals::espConfig.headMarker);
				ImGui::Checkbox("Position arrow", &globals::espConfig.positionArrow);
				ImGui::NextColumn();
				ImGui::TextDisabled("INFO");
				ImGui::Checkbox("Health bar", &globals::espConfig.healthBar);
				ImGui::Checkbox("Health text", &globals::espConfig.healthText);
				ImGui::Checkbox("Name", &globals::espConfig.name);
				ImGui::Checkbox("Distance", &globals::espConfig.distance);
				ImGui::Checkbox("360 alert", &globals::espConfig.alert360);
				ImGui::Columns(1);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Crosshair"))
			{
				ImGui::Checkbox("Enabled", &globals::espConfig.crosshair);
				ImGui::Checkbox("Dot", &globals::espConfig.crosshairDot);
				ImGui::Checkbox("Outline", &globals::espConfig.crosshairOutline);
				ImGui::SliderFloat("Length", &globals::espConfig.crosshairLength, 0.0f, 20.0f);
				ImGui::SliderFloat("Gap", &globals::espConfig.crosshairGap, 0.0f, 20.0f);
				ImGui::SliderFloat("Thickness", &globals::espConfig.crosshairThickness, 1.0f, 5.0f);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Aim"))
			{
				static bool captureAimKey = false;
				ImGui::Checkbox("Enable aim assist", &globals::aimConfig.enabled);
				ImGui::Checkbox("Show FOV circle", &globals::aimConfig.showFov);
				ImGui::Checkbox("Hold bind to aim", &globals::aimConfig.requireKey);
				ImGui::Checkbox("Enemies only", &globals::aimConfig.enemiesOnly);
				ImGui::Combo("Target bone", &globals::aimConfig.targetBone, "Head\0Neck\0Chest\0Torso\0");
				ImGui::Text("Aim key: %s", AimKeyName(globals::aimConfig.aimKey));
				if (ImGui::Button(captureAimKey ? "Press a key or mouse button..." : "Bind aim key"))
					captureAimKey = true;
				if (captureAimKey)
				{
					if (GetAsyncKeyState(VK_ESCAPE) & 1)
						captureAimKey = false;
					else
					{
						for (int key = 3; key <= 255; ++key)
						{
							if (key == VK_ESCAPE || key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON)
								continue;
							if (GetAsyncKeyState(key) & 1)
							{
								globals::aimConfig.aimKey = key;
								captureAimKey = false;
								break;
							}
						}
					}
				}
				ImGui::SliderFloat("FOV", &globals::aimConfig.fov, 0.5f, 30.0f, "%.1f deg");
				ImGui::SliderFloat("Smoothing", &globals::aimConfig.smoothing, 1.0f, 20.0f, "%.1f");
				ImGui::TextDisabled("Escape cancels key binding.");
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		if (previousEsp != globals::esp)
			debug::Log("[ui] ESP %s", globals::esp ? "enabled" : "disabled");
		ImGui::End();
	}


	ImGui::Render();

	pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	const auto result = oPresent(pSwapChain, SyncInterval, Flags);
	--gPresentCallbacks;
	return result;
}
inline void* pUpdateAccuracyPenality = nullptr;
inline void* pGetSpread = nullptr;
inline void* pGetInaccuracy = nullptr;
inline void* pFrameStageNotify = nullptr;
inline void* pCreateMove = nullptr;
inline void* pCsgoInput = nullptr;

using CreateMoveFn = void(__fastcall*)(void*, int, char);
using FrameStageNotifyFn = void(__fastcall*)(void*, int);
inline CreateMoveFn oCreateMove = nullptr;
inline FrameStageNotifyFn oFrameStageNotify = nullptr;

namespace
{
	std::vector<int> ParsePattern(const char* pattern)
	{
		std::vector<int> bytes;
		std::istringstream stream(pattern);
		std::string token;
		while (stream >> token)
			bytes.push_back(token == "?" || token == "??" ? -1 : std::stoi(token, nullptr, 16));
		return bytes;
	}

	uintptr_t FindPattern(uintptr_t module, const char* pattern)
	{
		if (!module)
			return 0;

		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return 0;
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
			return 0;

		const auto bytes = ParsePattern(pattern);
		const auto* section = IMAGE_FIRST_SECTION(nt);
		for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
		{
			if ((section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
				continue;

			const auto* begin = reinterpret_cast<const std::uint8_t*>(module + section[i].VirtualAddress);
			const auto size = static_cast<size_t>(section[i].Misc.VirtualSize);
			if (bytes.empty() || size < bytes.size())
				continue;

			for (size_t offset = 0; offset <= size - bytes.size(); ++offset)
			{
				bool matched = true;
				for (size_t j = 0; j < bytes.size(); ++j)
				{
					if (bytes[j] >= 0 && begin[offset + j] != static_cast<std::uint8_t>(bytes[j]))
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

	uintptr_t ResolveRelativeAddress(uintptr_t instruction, size_t displacementOffset, size_t instructionSize)
	{
		if (!instruction)
			return 0;
		const auto displacement = *reinterpret_cast<const std::int32_t*>(instruction + displacementOffset);
		return instruction + instructionSize + displacement;
	}
}

void UpdateAccuracyPenality(void* weapon)
{
	using fn_update_accuracy_penality_t = void(__fastcall*)(void*);
	const auto fn = reinterpret_cast<fn_update_accuracy_penality_t>(pUpdateAccuracyPenality);

	if (fn && weapon)
		fn(weapon);
}

float GetSpread(void* weapon)
{
	using fn_get_spread_t = float(__fastcall*)(void*);
	const auto fn = reinterpret_cast<fn_get_spread_t>(pGetSpread);
	return fn && weapon ? fn(weapon) : 0.0f;
}

float GetInaccuracy(void* weapon)
{
	float x = 0.0f;
	float y = 0.0f;
	using fn_get_inaccuracy_t = float(__fastcall*)(void*, float*, float*);
	const auto fn = reinterpret_cast<fn_get_inaccuracy_t>(pGetInaccuracy);
	return fn && weapon ? fn(weapon, &x, &y) : 0.0f;
}

void __fastcall hkCreateMove(void* input, int slot, char active)
{
	if (oCreateMove)
		oCreateMove(input, slot, active);

	// Feature processing belongs here once the command and weapon SDK wrappers
	// are connected. Keeping the original call first preserves game processing.
}

void __fastcall hkFrameStageNotify(void* thisptr, int stage)
{
	if (oFrameStageNotify)
		oFrameStageNotify(thisptr, stage);
}

bool InstallGameplayHooks()
{
	if (!pCreateMove || !pFrameStageNotify)
	{
		debug::Log("[hooks] gameplay targets unresolved; CreateMove=%p FrameStageNotify=%p",
			pCreateMove, pFrameStageNotify);
		return false;
	}

	const auto initStatus = MH_Initialize();
	if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED)
	{
		debug::Log("[hooks] MinHook initialization failed: %d", static_cast<int>(initStatus));
		return false;
	}

	const auto createStatus = MH_CreateHook(pCreateMove, reinterpret_cast<void*>(&hkCreateMove),
		reinterpret_cast<void**>(&oCreateMove));
	const auto frameStatus = MH_CreateHook(pFrameStageNotify, reinterpret_cast<void*>(&hkFrameStageNotify),
		reinterpret_cast<void**>(&oFrameStageNotify));
	debug::Log("[hooks] CreateMove hook status=%d FrameStageNotify hook status=%d",
		static_cast<int>(createStatus), static_cast<int>(frameStatus));
	if (createStatus != MH_OK || frameStatus != MH_OK)
	{
		debug::Log("[hooks] failed to create gameplay hooks");
		return false;
	}

	const auto createEnableStatus = MH_EnableHook(pCreateMove);
	const auto frameEnableStatus = MH_EnableHook(pFrameStageNotify);
	debug::Log("[hooks] CreateMove enable status=%d FrameStageNotify enable status=%d",
		static_cast<int>(createEnableStatus), static_cast<int>(frameEnableStatus));
	if (createEnableStatus != MH_OK || frameEnableStatus != MH_OK)
	{
		debug::Log("[hooks] failed to enable gameplay hooks");
		return false;
	}

	debug::Log("[hooks] CreateMove and FrameStageNotify enabled");
	return true;
}

bool minhooks()
{
	const auto clientModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
	if (!clientModule)
	{
		debug::Log("[accuracy] client.dll is not loaded yet");
		return false;
	}

	pUpdateAccuracyPenality = reinterpret_cast<void*>(clientModule + 0x829410);
	pGetSpread = reinterpret_cast<void*>(clientModule + 0x80EB60);
	pGetInaccuracy = reinterpret_cast<void*>(FindPattern(clientModule,
		"48 89 5C 24 ? 55 56 57 48 81 EC ? ? ? ? 44 0F 29 84 24"));
	pFrameStageNotify = reinterpret_cast<void*>(FindPattern(clientModule,
		"48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC ? 48 8B F9 33 ED"));
	const auto createMoveReference = FindPattern(clientModule,
		"FF FF FF FF 48 8D 05 ? ? ? ? 48 89 0D ? ? ? ?");
	const auto createMoveLeaTarget = ResolveRelativeAddress(createMoveReference, 7, 11);
	const auto createMoveStoreTarget = ResolveRelativeAddress(createMoveReference, 14, 18);
	__try
	{
		pCreateMove = createMoveStoreTarget ? *reinterpret_cast<void**>(createMoveStoreTarget) : nullptr;
		if (!pCreateMove && createMoveLeaTarget)
			pCreateMove = *reinterpret_cast<void**>(createMoveLeaTarget);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		pCreateMove = nullptr;
	}
	debug::Log("[memory] CreateMove reference=%p leaTarget=%p storeTarget=%p target=%p",
		reinterpret_cast<void*>(createMoveReference),
		reinterpret_cast<void*>(createMoveLeaTarget),
		reinterpret_cast<void*>(createMoveStoreTarget), pCreateMove);

	const auto inputInstruction = FindPattern(clientModule,
		"84 C0 74 0C 48 8D 0D ? ? ? ? E8 ? ? ? ?");
	const auto inputGlobal = ResolveRelativeAddress(inputInstruction, 7, 11);
	__try
	{
		pCsgoInput = inputGlobal ? *reinterpret_cast<void**>(inputGlobal) : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		pCsgoInput = nullptr;
	}

	debug::Log("[memory] UpdateAccuracyPenalty=%p GetSpread=%p GetInaccuracy=%p",
		pUpdateAccuracyPenality, pGetSpread, pGetInaccuracy);
	debug::Log("[memory] FrameStageNotify=%p CreateMove=%p csgo_input=%p",
		pFrameStageNotify, pCreateMove, pCsgoInput);
	return true;
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
	debug::Initialize();
	debug::Log("[init] starting D3D11 hook thread");
	
	bool init_hook = false;
	do
	{
		if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
		{
			kiero::bind(8, (void**)& oPresent, hkPresent);
			debug::Log("[init] D3D11 Present hook installed");
			init_hook = true;
		}
	} while (!init_hook);
	return TRUE;
}

DWORD WINAPI UnloadThread(LPVOID)
{
	// Disable any remaining MinHook hooks, then wait for in-flight callbacks to drain.
	MH_DisableHook(MH_ALL_HOOKS);
	debug::Log("[unload] waiting for active render callbacks");
	while (gPresentCallbacks.load() != 0 || gWndProcCallbacks.load() != 0)
		Sleep(1);

	// Restore the real vtable pointer in the swap chain object (safe — no concurrent calls).
	if (g_hookedSwapChain && g_realVtableOrig)
	{
		*reinterpret_cast<void***>(g_hookedSwapChain) = g_realVtableOrig;
		g_hookedSwapChain = nullptr;
		g_realVtableOrig  = nullptr;
	}
	if (g_shadowVtable)
	{
		VirtualFree(g_shadowVtable, 0, MEM_RELEASE);
		g_shadowVtable = nullptr;
	}

	kiero::shutdown();
	MH_Uninitialize();

	if (window && oWndProc)
	{
		SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));
		oWndProc = nullptr;
	}

	if (gImGuiInitialized.exchange(false))
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	if (mainRenderTargetView)
	{
		mainRenderTargetView->Release();
		mainRenderTargetView = nullptr;
	}
	if (pContext)
	{
		pContext->Release();
		pContext = nullptr;
	}
	if (pDevice)
	{
		pDevice->Release();
		pDevice = nullptr;
	}

	debug::Log("[unload] cleanup complete");
	FreeLibraryAndExitThread(gModule, 0);
	return 0;
}

void RequestUnload()
{
	gUnloadRequested = true;
	bool expected = false;
	if (gUnloadThreadStarted.compare_exchange_strong(expected, true))
		CreateThread(nullptr, 0, UnloadThread, nullptr, 0, nullptr);
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		gModule = hMod;
		DisableThreadLibraryCalls(hMod);
		CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr);
		break;
	case DLL_PROCESS_DETACH:
		// Explicit unload is performed by UnloadThread. Avoid teardown from
		// DllMain while the loader lock is held.
		break;
	}
	UNREFERENCED_PARAMETER(lpReserved);
	return TRUE;
}
