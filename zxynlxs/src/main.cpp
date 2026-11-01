#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "includes.h"
#include "globals/globals.h"
#include "visuals/visuals.h"
#include "debug/debug.h"
#include <algorithm>
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
	pGetInaccuracy = reinterpret_cast<void*>(clientModule + 0x80D870);

	debug::Log("[accuracy] UpdateAccuracyPenalty=%p GetSpread=%p GetInaccuracy=%p",
		pUpdateAccuracyPenality, pGetSpread, pGetInaccuracy);
	return true;
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
	debug::Initialize();
	debug::Log("[init] starting D3D11 hook thread");
	while (!minhooks())
		Sleep(100);
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
