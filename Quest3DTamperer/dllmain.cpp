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
#include <A3d_Channels.h>
#include <A3d_EngineInterface.h>
#include <Aco_String.h>

//DISCLAIMER: I have literally never done DirectX stuff before
typedef long(__stdcall* Reset)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
static Reset oReset = NULL;
typedef long(__stdcall* EndScene)(LPDIRECT3DDEVICE9);
static EndScene oEndScene = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void(__thiscall* TrueCallChannel)(A3d_Channel* self) = nullptr;
//Calling the functions normally results in a game crash with an error about the ESP value not being saved correctly.
//May be related to an SDK version mismatch?
//TODO: Investigate this further at some point
#pragma region Quest3D function definitions
static const char* (__thiscall* ChannelGroup_GetChannelGroupFileName)(A3d_ChannelGroup* self) = nullptr;
static const char* (__thiscall* ChannelGroup_GetPoolName)(A3d_ChannelGroup* self) = nullptr;
static int(__thiscall* ChannelGroup_GetChannelCount)(A3d_ChannelGroup* self) = nullptr;
static A3d_Channel* (__thiscall* ChannelGroup_GetChannel)(A3d_ChannelGroup* self, int) = nullptr;
static bool(__thiscall* ChannelGroup_GetGroupIsProtected)(A3d_ChannelGroup* self) = nullptr;
static bool(__thiscall* ChannelGroup_GetReadOnly)(A3d_ChannelGroup* self) = nullptr;
static void(__thiscall* ChannelGroup_SetGroupIsProtected)(A3d_ChannelGroup* self, bool newValue) = nullptr;
static void(__thiscall* ChannelGroup_SetReadOnly)(A3d_ChannelGroup* self, bool newValue) = nullptr;
static bool(__thiscall* ChannelGroup_SaveChannelGroup)(A3d_ChannelGroup* self, const char* fileName) = nullptr;
static int(__thiscall* ChannelGroup_GetGroupIndex)(A3d_ChannelGroup* self) = nullptr;
static void(__thiscall* ChannelGroup_CallStartChannel)(A3d_ChannelGroup* self) = nullptr;

static const char* (__thiscall* Channel_GetChannelName)(A3d_Channel* self) = nullptr;

static const char* (__thiscall* StringChannel_GetString)(Aco_StringChannel* self) = nullptr;
static const char* (__thiscall* StringOperator_GetString)(void* self) = nullptr;
static const char* (__thiscall* Lua_GetScript)(void* self) = nullptr;
#pragma endregion

static bool init = false;
bool showMenu;
HWND gameHandle;
WNDPROC g_WndProc_o;
ImGui::FileBrowser saveGroupFileDialog(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
ImGui::FileBrowser loadGroupFileDialog(0);
EngineInterface* engine = nullptr;
int channelGroupToUse = 0;
int channelInGroupToUse = 2;
char newGroupName[128] = "New Group";

// Convert a wide Unicode string to an UTF8 string
std::string utf8_encode(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Convert an UTF8 string to a wide Unicode String
std::wstring utf8_decode(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

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
        //It also ignores all other windows above it and registers clicks through them. No workaround for this (yet), this is Quest3D/Audiosurf's fault.
        ImGui::GetIO().MouseDrawCursor = true;

        A3d_ChannelGroup* group = nullptr;
        A3d_Channel* channel = nullptr;
        
        ImGui::Begin("Quest3DTamperer");
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
                    if (ImGui::Button("Save group without protection")) {
                        ChannelGroup_SetReadOnly(group, false);
                        ChannelGroup_SetGroupIsProtected(group, false);
                        saveGroupFileDialog.Open();
                    }
                    ImGui::Spacing();

                    ImGui::Text("Group has %i channels", ChannelGroup_GetChannelCount(group));
                    ImGui::InputInt("Channel in group to get", &channelInGroupToUse, 1, 10);
                    channel = ChannelGroup_GetChannel(group, channelInGroupToUse);
                    if (channel != nullptr) {
                        ImGui::Text(Channel_GetChannelName(channel));
                        
                        ImGui::Text(channel->GetChannelType().name);
                        
                        OLECHAR* guidString;
                        StringFromCLSID(channel->GetChannelType().guid, &guidString);
                        std::wstring wstring = std::wstring(guidString);
                        std::string stdstringGUID = utf8_encode(wstring);
                        ImGui::Text(stdstringGUID.c_str());

                        if (strstr(stdstringGUID.c_str(), "6E6FB247-4627")) {
                            ImGui::Text("Text in channel: %s", StringChannel_GetString((Aco_StringChannel*)channel));
                        }

                        if (strstr(stdstringGUID.c_str(), "F26BB40B-B196")) {
                            ImGui::Text("Text in channel: %s", StringOperator_GetString(channel));
                        }

                        if (strstr(stdstringGUID.c_str(), "6514FE12-88CF")) {
                            ImGui::Text("Script: \n%s", Lua_GetScript(channel));
                        }

                        ImGui::Spacing();
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

#pragma region Find functions
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
            (int(__thiscall*)(A3d_ChannelGroup*))
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
        ChannelGroup_CallStartChannel =
            (void(__thiscall*)(A3d_ChannelGroup*))
            DetourFindFunction("highpoly.dll", "?CallStartChannel@A3d_ChannelGroup@@UAEXXZ");
        ChannelGroup_GetChannel =
            (A3d_Channel * (__thiscall*)(A3d_ChannelGroup*, int))
            DetourFindFunction("highpoly.dll", "?GetChannel@A3d_ChannelGroup@@UAEPAVA3d_Channel@@H@Z");

        Channel_GetChannelName =
            (const char* (__thiscall*)(A3d_Channel*))
            DetourFindFunction("highpoly.dll", "?GetChannelName@A3d_Channel@@QAEPBDXZ");

        StringChannel_GetString =
            (const char* (__thiscall*)(Aco_StringChannel*))
            DetourFindFunction("6E6FB247-4627-4FBE-8973-48344F23881E.dll", "?GetString@Aco_StringChannel@@UAEPBDXZ");
        StringOperator_GetString =
            (const char* (__thiscall*)(void*))
            DetourFindFunction("F26BB40B-B196-4AB9-B59E-FA7C8FF436F9.dll", "?GetString@Aco_StringOperator@@UAEPBDXZ");
        Lua_GetScript =
            (const char* (__thiscall*)(void*))
            DetourFindFunction("6514FE12-88CF-480B-A3D8-7730C0CD23B3.dll", "?GetScript@Aco_Lua@@UAEPBDXZ");
#pragma endregion

        

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)TrueCallChannel, CallChannelHook);
        DetourTransactionCommit();

    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}