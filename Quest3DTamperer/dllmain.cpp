// dllmain.cpp : Definiert den Einstiegspunkt für die DLL-Anwendung.
#include "pch.h"
#include "kiero/kiero.h"
#include <d3d9.h>
#include <assert.h>
#include <detours.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx9.h"

//DISCLAIMER: I have literally never done DirectX stuff before

typedef long(__stdcall* Reset)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
static Reset oReset = NULL;

typedef long(__stdcall* EndScene)(LPDIRECT3DDEVICE9);
static EndScene oEndScene = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool showMenu;

HWND gameHandle;
WNDPROC g_WndProc_o;

int* channelGroupToUse = 0;

LRESULT __stdcall CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam) && showMenu)
        return true;

    return CallWindowProc(g_WndProc_o, hWnd, uMsg, wParam, lParam);
}


long __stdcall hkReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    long result = oReset(pDevice, pPresentationParameters);
    ImGui_ImplDX9_CreateDeviceObjects();

    return result;
}

long __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice)
{
    static bool init = false;

    if (!init)
    {
        D3DDEVICE_CREATION_PARAMETERS params;
        pDevice->GetCreationParameters(&params);

        ImGui::CreateContext();
        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        init = true;
    }

    if (showMenu) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        //Because audiosurf has its own cursor and it'll just be below the ImGUI window
        ImGui::GetIO().MouseDrawCursor = true;

        ImGui::Begin("Quest3DTamperer");
        ImGui::Text("yeah sex is cool but have you ever used Dear ImGUI");
        ImGui::InputInt("A3d_ChannelGroup to use", channelGroupToUse, 1, 10);
        ImGui::End();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    return oEndScene(pDevice);
}

void d3d9_init()
{
    assert(kiero::bind(16, (void**)&oReset, hkReset) == kiero::Status::Success);
    assert(kiero::bind(42, (void**)&oEndScene, hkEndScene) == kiero::Status::Success);

    gameHandle = FindWindow(NULL, L"Audiosurf");

    g_WndProc_o = (WNDPROC)SetWindowLong(gameHandle, GWL_WNDPROC, (LRESULT)hkWndProc);
}

int kieroExampleThread()
{
    if (kiero::init(kiero::RenderType::Auto) == kiero::Status::Success)
    {
        if (kiero::getRenderType() == kiero::RenderType::D3D9)
        {
            d3d9_init();
        }

        while (true)
        {
            if (GetAsyncKeyState(VK_END) & 1)
            {
                if (showMenu) {
                    showMenu = false;
                }
                else {
                    showMenu = true;
                }
            }
        }

        return 1;
    }

    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    DisableThreadLibraryCalls(hModule);

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)kieroExampleThread, NULL, 0, NULL);
    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}