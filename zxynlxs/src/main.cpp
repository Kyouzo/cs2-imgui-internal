#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "includes.h"
#include "globals/globals.h"
#include "visuals/visuals.h"
#include "debug/debug.h"
#include "kiero/minhook/include/MinHook.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;

void InitImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	if (true && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

bool init = false;
bool draw = false;
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
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
		}

		else
			return oPresent(pSwapChain, SyncInterval, Flags);
	}

	if (GetAsyncKeyState(VK_INSERT) & 1) {
		draw = !draw;
		debug::Log("[ui] menu %s", draw ? "opened" : "closed");
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
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
	if (globals::esp) {
		static bool loggedEspEntry = false;
		if (!loggedEspEntry) {
			debug::Log("[esp] entering render pass");
			loggedEspEntry = true;
		}
		__try
		{
			visuals::PlayerESP();
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
		ImGui::SetNextWindowSize({360.0f, 330.0f}, ImGuiCond_FirstUseEver);
		ImGui::Begin("Visuals", &draw);
		const bool previousEsp = globals::esp;
		ImGui::TextUnformatted("Player Info In World");
		ImGui::Separator();
		ImGui::Checkbox("Master switch", &globals::esp);
		ImGui::Checkbox("Enemies only", &globals::espConfig.onlyEnemies);
		if (ImGui::CollapsingHeader("Player position", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Cornered box", &globals::espConfig.cornerBox);
			ImGui::Checkbox("Skeleton", &globals::espConfig.skeleton);
			ImGui::Checkbox("Snapline", &globals::espConfig.snapline);
			ImGui::Checkbox("Head marker", &globals::espConfig.headMarker);
			ImGui::Checkbox("Position arrow", &globals::espConfig.positionArrow);
			ImGui::Checkbox("Team colors", &globals::espConfig.teamColors);
		}
		if (ImGui::CollapsingHeader("Player health", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Health bar", &globals::espConfig.healthBar);
			ImGui::Checkbox("Health text", &globals::espConfig.healthText);
		}
		if (ImGui::CollapsingHeader("Crosshair"))
		{
			ImGui::Checkbox("Enabled", &globals::espConfig.crosshair);
			ImGui::Checkbox("Dot", &globals::espConfig.crosshairDot);
			ImGui::Checkbox("Outline", &globals::espConfig.crosshairOutline);
			ImGui::SliderFloat("Length", &globals::espConfig.crosshairLength, 0.0f, 20.0f);
			ImGui::SliderFloat("Gap", &globals::espConfig.crosshairGap, 0.0f, 20.0f);
			ImGui::SliderFloat("Thickness", &globals::espConfig.crosshairThickness, 1.0f, 5.0f);
		}
		if (previousEsp != globals::esp)
			debug::Log("[ui] ESP %s", globals::esp ? "enabled" : "disabled");
		ImGui::End();
	}


	ImGui::Render();

	pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	return oPresent(pSwapChain, SyncInterval, Flags);
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

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hMod);
		CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr);
		break;
	case DLL_PROCESS_DETACH:
		kiero::shutdown();
		break;
	}
	return TRUE;
}
