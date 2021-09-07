// dllmain.cpp : Definiert den Einstiegspunkt für die DLL-Anwendung.
#include "pch.h"
#include "kiero/kiero.h"
#include <d3d9.h>
#include <d3d9types.h>
#include <assert.h>
#include <detours.h>
#include <fstream>

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
#include <Aco_DX8_Texture.h>
#include <Aco_DX8_ObjectData.h>

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

static void (__thiscall* StringChannel_SetString)(Aco_StringChannel* self, const char* string) = nullptr;
static BOOL (__thiscall* Lua_SetScript)(void* self, const char* string) = nullptr;

static int (__thiscall* Aco_DX8_Texture_GetDesiredWidth)(Aco_DX8_Texture* self) = nullptr;
static int (__thiscall* Aco_DX8_Texture_GetDesiredHeight)(Aco_DX8_Texture* self) = nullptr;
static IDirect3DTexture9* (__thiscall* Aco_DX8_Texture_GetTexture)(Aco_DX8_Texture* self) = nullptr;
static char* (__thiscall* Aco_DX8_Texture_GetTextureBuffer)(Aco_DX8_Texture* self) = nullptr;
static int (__thiscall* Aco_DX8_Texture_GetBufferSize)(Aco_DX8_Texture* self) = nullptr;
static BOOL (__thiscall* Aco_DX8_Texture_LoadTextureFromFile)(Aco_DX8_Texture* self, char* path) = nullptr;
static HRESULT (__thiscall* Aco_DX8_Texture_LockTexture)(Aco_DX8_Texture* self, int level, D3DLOCKED_RECT& pLockedRect) = nullptr;
static void (__thiscall* Aco_DX8_Texture_UnlockTexture)(Aco_DX8_Texture* self, int level) = nullptr;
static int (__thiscall* Aco_DX8_Texture_GetMipMapLevels)(Aco_DX8_Texture* self) = nullptr;
static D3DSURFACE_DESC (__thiscall* Aco_DX8_Texture_GetTextureDescription)(Aco_DX8_Texture* self, int lvl) = nullptr;

static D3DMATERIAL9 (__thiscall* Aco_DX8_MaterialChannel_GetMaterial)(void* self) = nullptr;

static int (__thiscall* Aco_DX8_ObjectDataChannel_GetVertexCount)(Aco_DX8_ObjectDataChannel* self) = nullptr;

static D3DXVECTOR3 (__thiscall* Aco_DX8_ObjectChannel_GetPosition)(void* self) = nullptr;
#pragma endregion

static bool init = false;
bool showMenu;
HWND gameHandle;
WNDPROC g_WndProc_o;
ImGui::FileBrowser saveGroupFileDialog(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
ImGui::FileBrowser saveTextureFileDialog(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
ImGui::FileBrowser loadGroupFileDialog(0);
ImGui::FileBrowser loadTextureFileDialog(0);
EngineInterface* engine = nullptr;
int channelGroupToUse = 0;
int channelInGroupToUse = 1;
int mipmapLevelToUse = 0;
char newGroupName[128] = "New Group";
char newText[1024] = "New Text";
bool textureLocked = false;
bool previewTexture = true;

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

        loadGroupFileDialog.SetTitle("Load channel group");
        loadGroupFileDialog.SetTypeFilters({ ".cgr" });

        saveTextureFileDialog.SetTitle("Save texture");
        saveTextureFileDialog.SetTypeFilters({ ".tga", ".png", ".jpg"});

        loadTextureFileDialog.SetTitle("Load texture");
        loadTextureFileDialog.SetTypeFilters({ ".tga", ".png", ".jpg" });

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
        std::string channelGUID;
        
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
                        channelGUID = stdstringGUID;

                        ImGui::Text(stdstringGUID.c_str());

                        if (strstr(stdstringGUID.c_str(), "6E6FB247-4627")) {
                            ImGui::Text("Text in channel: %s", StringChannel_GetString((Aco_StringChannel*)channel));
                            
                            ImGui::InputText("New text to set", newText, IM_ARRAYSIZE(newText));
                            if (ImGui::Button("Set text")) {
                                StringChannel_SetString((Aco_StringChannel*)channel, newText);
                            }
                        }

                        if (strstr(stdstringGUID.c_str(), "F26BB40B-B196")) {
                            ImGui::Text("Text in channel: %s", StringOperator_GetString(channel));
                        }

                        if (strstr(stdstringGUID.c_str(), "6514FE12-88CF")) {
                            ImGui::Text("Script: \n%s", Lua_GetScript(channel));
                        }

                        if (strstr(stdstringGUID.c_str(), "BC052C38-2D5D")) {
                            Aco_DX8_Texture* texture = (Aco_DX8_Texture*)channel;
                            ImGui::Text("Mipmap level count: %d", Aco_DX8_Texture_GetMipMapLevels(texture));
                            ImGui::InputInt("Select Mipmap level", &mipmapLevelToUse, 1, 10);
                            ImGui::Checkbox("Enable preview", &previewTexture);

                            //You should turn off the preview when swapping textures
                            //Else the game WILL crash
                            if (!textureLocked && previewTexture) {
                                IDirect3DTexture9* d3dTexture = Aco_DX8_Texture_GetTexture(texture);
                                D3DSURFACE_DESC description = Aco_DX8_Texture_GetTextureDescription(texture, mipmapLevelToUse);
                                ImGui::Text("Texture size: %dx%d", description.Width, description.Height);
                                ImGui::Image((void*)d3dTexture, ImVec2(description.Width, description.Height));
                            }

                            if(ImGui::Button("Save texture")) {
                                saveTextureFileDialog.Open();
                            }
                            if (ImGui::Button("Load texture")) {
                                loadTextureFileDialog.Open();
                            }
                        }

                        if (strstr(stdstringGUID.c_str(), "376A9C13-8D66")) {
                            D3DMATERIAL9 material = Aco_DX8_MaterialChannel_GetMaterial(channel);
                            ImGui::Text("Power: %f", material.Power);

                            ImVec4 specular = ImVec4(material.Specular.r, material.Specular.g, material.Specular.b, material.Specular.a);
                            ImGui::TextColored(specular, "Specular color");
                            ImVec4 emissive = ImVec4(material.Emissive.r, material.Emissive.g, material.Emissive.b, material.Emissive.a);
                            ImGui::TextColored(emissive, "Emissive color");
                            ImVec4 ambient = ImVec4(material.Ambient.r, material.Ambient.g, material.Ambient.b, material.Ambient.a);
                            ImGui::TextColored(ambient, "Ambient color");
                            ImVec4 diffuse = ImVec4(material.Diffuse.r, material.Diffuse.g, material.Diffuse.b, material.Diffuse.a);
                            ImGui::TextColored(diffuse, "Diffuse color");
                        }

                        if (strstr(stdstringGUID.c_str(), "21A8923D-B908")) {
                            Aco_DX8_ObjectDataChannel* objectData = (Aco_DX8_ObjectDataChannel*)channel;
                            ImGui::Text("Vertex count: %d", Aco_DX8_ObjectDataChannel_GetVertexCount(objectData));
                        }

                        if (strstr(stdstringGUID.c_str(), "10C20C0A-7A55")) {
                            //Commenting this out for now because it just results in an access violation
                            //D3DXVECTOR3 objectPosition = Aco_DX8_ObjectChannel_GetPosition(channel);
                            //ImGui::Text("Object position:\nX - %d\nY - %d\nZ - %d", objectPosition.x, objectPosition.y, objectPosition.z);
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
        saveTextureFileDialog.Display();
        loadTextureFileDialog.Display();

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
        if (saveTextureFileDialog.HasSelected())
        {
            std::ofstream binaryFile(saveTextureFileDialog.GetSelected().string().c_str(), std::ios::out | std::ios::binary);
            if (binaryFile.is_open())
            {
                Aco_DX8_Texture* texture = (Aco_DX8_Texture*)channel;
                char* data = Aco_DX8_Texture_GetTextureBuffer(texture);
                int size = Aco_DX8_Texture_GetBufferSize(texture);
                binaryFile.write(data, size);
            }
            loadGroupFileDialog.ClearSelected();
        }
        if (loadTextureFileDialog.HasSelected())
        {
            if (strstr(channelGUID.c_str(), "BC052C38-2D5D")) {
                textureLocked = true;
                Aco_DX8_Texture* texture = (Aco_DX8_Texture*)channel;
                D3DSURFACE_DESC description = Aco_DX8_Texture_GetTextureDescription(texture, mipmapLevelToUse);
                D3DLOCKED_RECT lockedRect;
                lockedRect.pBits = (void*)Aco_DX8_Texture_GetTextureBuffer(texture);
                lockedRect.Pitch = description.Width * 4;

                Aco_DX8_Texture_LockTexture(texture, mipmapLevelToUse, lockedRect);
                Aco_DX8_Texture_LoadTextureFromFile(texture, (char*)loadTextureFileDialog.GetSelected().string().c_str());
                Aco_DX8_Texture_UnlockTexture(texture, mipmapLevelToUse);
                textureLocked = false;
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

        StringChannel_SetString =
            (void (__thiscall*)(Aco_StringChannel*, const char*))
            DetourFindFunction("6E6FB247-4627-4FBE-8973-48344F23881E.dll", "?SetString@Aco_StringChannel@@UAEXPBD@Z");
        Lua_SetScript =
            (BOOL (__thiscall*)(void*, const char*))
            DetourFindFunction("6514FE12-88CF-480B-A3D8-7730C0CD23B3.dll", "?SetScript@Aco_Lua@@UAE_NPBD@Z");

        Aco_DX8_Texture_GetDesiredWidth =
            (int (__thiscall*)(Aco_DX8_Texture*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetDesiredWidth@Aco_DX8_Texture@@UAEHXZ");
        Aco_DX8_Texture_GetDesiredHeight =
            (int (__thiscall*)(Aco_DX8_Texture*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetDesiredHeight@Aco_DX8_Texture@@UAEHXZ");
        Aco_DX8_Texture_GetTexture =
            (IDirect3DTexture9* (__thiscall*)(Aco_DX8_Texture*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetTexture@Aco_DX8_Texture@@UAEPAUIDirect3DTexture9@@XZ");
        Aco_DX8_Texture_GetTextureBuffer =
            (char* (__thiscall*)(Aco_DX8_Texture*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetTextureBuffer@Aco_DX8_Texture@@UAEPADXZ");
        Aco_DX8_Texture_GetBufferSize =
            (int (__thiscall*)(Aco_DX8_Texture*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetBufferSize@Aco_DX8_Texture@@UAEHXZ");
        Aco_DX8_Texture_LoadTextureFromFile =
            (BOOL (__thiscall*)(Aco_DX8_Texture*, char*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?LoadTextureFromFile@Aco_DX8_Texture@@UAE_NPAD@Z");
        Aco_DX8_Texture_LockTexture =
            (HRESULT (__thiscall*)(Aco_DX8_Texture*, int, D3DLOCKED_RECT&))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?LockTexture@Aco_DX8_Texture@@UAEJHAAU_D3DLOCKED_RECT@@@Z");
        Aco_DX8_Texture_UnlockTexture =
            (void (__thiscall*)(Aco_DX8_Texture*, int))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?UnlockTexture@Aco_DX8_Texture@@UAEXH@Z");
        Aco_DX8_Texture_GetMipMapLevels =
            (int (__thiscall*)(Aco_DX8_Texture*))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetMipMapLevels@Aco_DX8_Texture@@UAEHXZ");
        Aco_DX8_Texture_GetTextureDescription =
            (D3DSURFACE_DESC (__thiscall*)(Aco_DX8_Texture*, int))
            DetourFindFunction("BC052C38-2D5D-4f0c-A0CA-654D0AFC584A.dll", "?GetTextureDescription@Aco_DX8_Texture@@UAE?AU_D3DSURFACE_DESC@@H@Z");

        Aco_DX8_MaterialChannel_GetMaterial =
            (D3DMATERIAL9 (__thiscall*)(void*))
            DetourFindFunction("376A9C13-8D66-49EC-BAE5-D59BE13BC519.dll", "?GetMaterialValue@Aco_DX8_MaterialChannel@@UAEMH@Z");

        Aco_DX8_ObjectDataChannel_GetVertexCount =
            (int (__thiscall*)(Aco_DX8_ObjectDataChannel*))
            DetourFindFunction("21A8923D-B908-4104-AE88-B6718D8A8678.dll", "?GetVertexCount@Aco_DX8_ObjectDataChannel@@UAEHXZ");

        Aco_DX8_ObjectChannel_GetPosition =
            (D3DXVECTOR3 (__thiscall*)(void*))
            DetourFindFunction("10C20C0A-7A55-4084-8676-95E5699BCEC2.dll", "?GetPosition@Aco_DX8_ObjectChannel@@UAE?AUD3DXVECTOR3@@XZ");
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