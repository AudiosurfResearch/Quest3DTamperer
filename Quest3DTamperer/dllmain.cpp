// dllmain.cpp : Definiert den Einstiegspunkt für die DLL-Anwendung.
#include "pch.h"
#include "kiero/kiero.h"
#include <d3d9.h>
#include <assert.h>
#include <detours.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/imfilebrowser.h"

//The Quest3D SDK isn't included in this repo!
//You'll have to get it yourself.
//Sorry.
#include <A3d_List.h>
#include <A3d_ChannelGroup.h>
#include <A3d_EngineInterface.h>

//DISCLAIMER: I have literally never done DirectX stuff before

typedef long(__stdcall* Reset)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
static Reset oReset = NULL;
typedef long(__stdcall* EndScene)(LPDIRECT3DDEVICE9);
static EndScene oEndScene = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void(__thiscall* TrueCallChannel)(A3d_Channel* self) = nullptr;
//because calling the functions like a normal human being results in something something ESP value not being saved properly
//and then the game dies
//maybe because my SDK version is too new but how am i supposed to get an old version of the SDK
//it might be too old actually, what kind of parallel universe is this now?
static const char* (__thiscall* ChannelGroup_GetChannelGroupFileName)(A3d_ChannelGroup* self) = nullptr;
static const char* (__thiscall* ChannelGroup_GetPoolName)(A3d_ChannelGroup* self) = nullptr;
static int (__thiscall* ChannelGroup_GetChannelCount)(A3d_ChannelGroup* self) = nullptr;
static bool(__thiscall* ChannelGroup_GetGroupIsProtected)(A3d_ChannelGroup* self) = nullptr;
static bool(__thiscall* ChannelGroup_GetReadOnly)(A3d_ChannelGroup* self) = nullptr;
static void(__thiscall* ChannelGroup_SetGroupIsProtected)(A3d_ChannelGroup* self, bool newValue) = nullptr;
static void(__thiscall* ChannelGroup_SetReadOnly)(A3d_ChannelGroup* self, bool newValue) = nullptr;
static bool(__thiscall* ChannelGroup_SaveChannelGroup)(A3d_ChannelGroup* self, const char* fileName) = nullptr;
static int(__thiscall* ChannelGroup_GetGroupIndex)(A3d_ChannelGroup* self) = nullptr;
static void(__thiscall* ChannelGroup_CallStartChannel)(A3d_ChannelGroup* self) = nullptr;

static bool init = false;
bool showMenu;
HWND gameHandle;
WNDPROC g_WndProc_o;
ImGui::FileBrowser saveGroupFileDialog(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
ImGui::FileBrowser loadGroupFileDialog(0);
EngineInterface* engine = nullptr;
int channelGroupToUse = 0;
char newGroupName[128] = "New Group";

static void __fastcall CallChannelHook(A3d_Channel* self, DWORD edx)
{
    TrueCallChannel(self);
    if (engine == nullptr) {
        engine = self->engine;
    }
}

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
    if (!init)
    {
        D3DDEVICE_CREATION_PARAMETERS params;
        pDevice->GetCreationParameters(&params);

        ImGui::CreateContext();
        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        saveGroupFileDialog.SetTitle("Save channel group");
        saveGroupFileDialog.SetTypeFilters({ ".cgr" });

        saveGroupFileDialog.SetTitle("Load channel group");
        saveGroupFileDialog.SetTypeFilters({ ".cgr" });

        init = true;
    }

    if (showMenu) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        //Because Audiosurf has its own cursor and it'll just be below the ImGUI window
        ImGui::GetIO().MouseDrawCursor = true;

        A3d_ChannelGroup* group = nullptr;
        
        ImGui::Begin("Quest3DTamperer");
        // :^)
        //ImGui::Text("yeah sex is cool but have you ever used Dear ImGUI");
        ImGui::Spacing();
        if (engine != nullptr) {
            if (ImGui::CollapsingHeader("Channel groups")) {
                ImGui::Text("There is %i channel groups.", engine->GetChannelGroupCount());
                ImGui::Spacing();
                ImGui::InputText("Pool name for new group", newGroupName, IM_ARRAYSIZE(newGroupName));
                if (ImGui::Button("Load channel group file")) {
                    loadGroupFileDialog.Open();
                }
                ImGui::Spacing();
                ImGui::InputInt("A3d_ChannelGroup to use", &channelGroupToUse, 1, 10);
                ImGui::Spacing();
                group = engine->GetChannelGroup(channelGroupToUse);
                if (group != nullptr) {
                    ImGui::Text("Info of current channel group:");
                    ImGui::Text(ChannelGroup_GetPoolName(group));
                    ImGui::Text(ChannelGroup_GetChannelGroupFileName(group));
                    ImGui::Text("Is group protected: %s", ChannelGroup_GetGroupIsProtected ? "true" : "false");
                    ImGui::Text("Is group read-only: %s", ChannelGroup_GetGroupIsProtected ? "true" : "false");
                    //returns a stupidly large number, most likely bugged?
                    //ImGui::Text("Group has %i channels", ChannelGroup_GetChannelCount(group));
                    if (ImGui::Button("Save group without protection")) {
                        ChannelGroup_SetReadOnly(group, false);
                        ChannelGroup_SetGroupIsProtected(group, false);
                        saveGroupFileDialog.Open();
                    }
                }
                else {
                    ImGui::Text("Group not found!");
                }
            }
        }
        else {
            ImGui::Text("Please perform any action in-game.\nThis is needed to get the pointer to the EngineInterface,\nwhich is neccessary for Quest3DTamperer to work.");
        }
        ImGui::End();

        saveGroupFileDialog.Display();
        loadGroupFileDialog.Display();

        if (saveGroupFileDialog.HasSelected())
        {
            ChannelGroup_SaveChannelGroup(group, saveGroupFileDialog.GetSelected().string().c_str());
            saveGroupFileDialog.ClearSelected();
        }
        if (loadGroupFileDialog.HasSelected())
        {
            A3d_ChannelGroup* newGroup = engine->LoadChannelGroup(loadGroupFileDialog.GetSelected().string().c_str(), newGroupName);
            //Call the group's start channel
            if (newGroup != nullptr)
            {
                ChannelGroup_CallStartChannel(newGroup);
            }
            //literally crashes the game
            //channelGroupToUse = ChannelGroup_GetGroupIndex(newGroup);
            loadGroupFileDialog.ClearSelected();
        }

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
            //toggle menu
            if (GetAsyncKeyState(VK_END) & 1)
            {
                if (showMenu) {
                    showMenu = false;
                }
                else {
                    showMenu = true;
                }
            }

            //force re-init of ImGUI
            if (GetAsyncKeyState(VK_INSERT) & 1)
            {
                init = false;
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

        DetourRestoreAfterWith();
        //welcome to HELL
        TrueCallChannel =
            (void(__thiscall*)(A3d_Channel*))
            DetourFindFunction("highpoly.dll", "?CallChannel@A3d_Channel@@UAEXXZ");
        ChannelGroup_GetChannelGroupFileName =
            (const char* (__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?GetChannelGroupFileName@A3d_ChannelGroup@@UAEPBDXZ");
        ChannelGroup_GetPoolName =
            (const char* (__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?GetPoolName@A3d_ChannelGroup@@UAEPBDXZ");
        ChannelGroup_GetChannelCount =
            (int (__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?GetPoolName@A3d_ChannelGroup@@UAEPBDXZ");
        ChannelGroup_GetGroupIsProtected =
            (bool(__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?GetGroupIsProtected@A3d_ChannelGroup@@UAE_NXZ");
        ChannelGroup_GetReadOnly =
            (bool(__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?GetReadOnly@A3d_ChannelGroup@@UAE_NXZ");
        ChannelGroup_SetGroupIsProtected =
            (void(__thiscall*)(A3d_ChannelGroup*, bool))
            DetourFindFunction("highpoly.dll", "?SetGroupIsProtected@A3d_ChannelGroup@@UAEX_N@Z");
        ChannelGroup_SetReadOnly =
            (void(__thiscall*)(A3d_ChannelGroup*, bool))
            DetourFindFunction("highpoly.dll", "?SetReadOnly@A3d_ChannelGroup@@UAEX_N@Z");
        ChannelGroup_SaveChannelGroup =
            (bool(__thiscall*)(A3d_ChannelGroup*, const char*))
            DetourFindFunction("highpoly.dll", "?SaveChannelGroup@A3d_ChannelGroup@@UAE_NPBD@Z");
        ChannelGroup_GetGroupIndex =
            (int(__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?GetGroupIndex@A3d_ChannelGroup@@UAEHXZ");
        ChannelGroup_CallStartChannel =
            (void(__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?CallStartChannel@A3d_ChannelGroup@@UAEXXZ");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)TrueCallChannel, CallChannelHook);
        DetourTransactionCommit();

    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}