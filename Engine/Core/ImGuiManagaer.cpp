#include "ImGuiManagaer.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "ParticleManager.h"

#ifdef USE_IMGUI
#include <imgui_internal.h>
#endif // USE_IMGUI

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_IMGUI
ImVec2 gSceneImageMin = ImVec2(0.0f, 0.0f);
ImVec2 gSceneImageMax = ImVec2(0.0f, 0.0f);
bool gHasSceneImageRect = false;
bool gParticleTestEditorModeSwitcherVisible = false;
int gParticleTestEditorMode = 0;
std::vector<std::string> gParticleTestBlenderHierarchyNames;
int gParticleTestBlenderHierarchySelected = -1;
bool gParticleTestBlenderHierarchySelectionChanged = false;
bool gParticleTestAnimationCameraPreviewVisible = false;
bool gParticleTestAnimationCameraPreviewSwapped = false;
#endif


void ImGuiManagaer::Initialize([[maybe_unused]]WinApp* winApp, [[maybe_unused]] DirectXCommon* dxCommon, [[maybe_unused]] SrvManager* srvManager)
{
#ifdef USE_IMGUI



    if (initialized_) return;

    winApp_ = winApp;
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    // Context・域里縺ｫ縺ゅｌ縺ｰ菴懊ｉ縺ｪ縺・ｼ・
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    const char* japaneseFontPaths[] = {
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
    };
    for (const char* fontPath : japaneseFontPaths) {
        if (std::filesystem::exists(fontPath)) {
            io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
            break;
        }
    }
    // Win32 backend・域里縺ｫ縺ゅｌ縺ｰInit縺励↑縺・ｼ・
    if (ImGui::GetIO().BackendPlatformUserData == nullptr) {
        ImGui_ImplWin32_Init(winApp_->GetHwnd());
    }

    // DX12 backend
    ID3D12Device* device = dxCommon_->GetDevice();


    // 笘・≠縺ｪ縺溘・SwapChain譫壽焚/format縺ｫ蜷医ｏ縺帙ｋ・医→繧翫≠縺医★2譫・UNORM縺ｧOK縺ｪ繧峨％縺ｮ縺ｾ縺ｾ・・
    const int backBufferCount = 2;
    const DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = device;
    initInfo.CommandQueue = dxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = backBufferCount;
    initInfo.RTVFormat = rtvFormat;
    initInfo.SrvDescriptorHeap = srvManager_->GetDescriptorHeap();
    initInfo.UserData = srvManager_;
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        auto* srvManager = static_cast<SrvManager*>(info->UserData);
        const uint32_t index = srvManager->Allocate();
        *outCpu = srvManager->GetCPUDescriptionHandle(index);
        *outGpu = srvManager->GetGPUDescriptionHandle(index);
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
    };

    bool ok = ImGui_ImplDX12_Init(&initInfo);
    assert(ok && "ImGui_ImplDX12_Init failed");
    ImGui_ImplDX12_CreateDeviceObjects(); // 笘・％繧後ｒ霑ｽ蜉・磯㍾隕・ｼ・

    initialized_ = true;
#endif // USE_IMGUI


}

void ImGuiManagaer::SetSceneTexture(uint32_t srvIndex)
{
    sceneSrvIndex_ = srvIndex;
    hasSceneTexture_ = true;
}

void ImGuiManagaer::SetPreviewTexture(uint32_t srvIndex)
{
    previewSrvIndex_ = srvIndex;
    hasPreviewTexture_ = true;
}

void ImGuiManagaer::Begin()
{
#ifdef USE_IMGUI


    // Workspace image is submitted by the scene after Update; retain its last
    // rectangle for this frame's FPS input/capture, unlike the docked Scene panel.
    if (!weaponWorkspace_) { sceneImageHovered_ = false; gHasSceneImageRect = false; }
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (!weaponWorkspace_) { BeginDockSpace_(); DrawEditorPanels_(); }

#endif // USE_IMGUI


}

void ImGuiManagaer::BeginDockSpace_()
{
#ifdef USE_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("MainDockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(2);

    ImGuiID dockspaceId = ImGui::GetID("CG5MainDockSpace");
    ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
    BuildDefaultDockLayout_(dockspaceId);
    ImGui::End();
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
void ImGuiManagaer::BuildDefaultDockLayout_(ImGuiID dockspaceId)
{
    static bool built = false;
    if (built) {
        return;
    }
    built = true;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID mainNode = dockspaceId;
    ImGuiID leftNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Left, 0.26f, nullptr, &mainNode);
    ImGuiID rightNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Right, 0.22f, nullptr, &mainNode);
    ImGuiID bottomNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Down, 0.24f, nullptr, &mainNode);
    ImGuiID rightBottomNode = ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Down, 0.45f, nullptr, &rightNode);

    ImGui::DockBuilderDockWindow("Hierarchy", leftNode);
    ImGui::DockBuilderDockWindow("Inspector", rightNode);
    ImGui::DockBuilderDockWindow("Title Lighting", rightNode);
    ImGui::DockBuilderDockWindow("Scene", mainNode);
    ImGui::DockBuilderDockWindow("Console", bottomNode);

    ImGui::DockBuilderDockWindow("Sprite Position Control", bottomNode);
    ImGui::DockBuilderDockWindow("Post Effect", bottomNode);
    ImGui::DockBuilderDockWindow("FPS Controls", bottomNode);
    ImGui::DockBuilderDockWindow("Debug AI", bottomNode);
    ImGui::DockBuilderDockWindow("VideoPlane SRT", bottomNode);
    ImGui::DockBuilderDockWindow("Object Specific Effects", bottomNode);
    ImGui::DockBuilderDockWindow("Camera Debug", bottomNode);
    ImGui::DockBuilderDockWindow("Ground PointLight", bottomNode);
    ImGui::DockBuilderDockWindow("Ground SpotLight", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Manager", rightNode);
    ImGui::DockBuilderDockWindow("Effect Editor", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Mode", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Camera", bottomNode);
    ImGui::DockBuilderDockWindow("Particle Test Scene", bottomNode);

    ImGui::DockBuilderDockWindow("Model Switchers", rightBottomNode);
    ImGui::DockBuilderDockWindow("Phong Check", rightBottomNode);
    ImGui::DockBuilderDockWindow("Object SRT (Per-Object)", rightBottomNode);
    ImGui::DockBuilderDockWindow("Primitive Check", rightBottomNode);
    ImGui::DockBuilderDockWindow("Video", rightBottomNode);
    ImGui::DockBuilderDockWindow("DebugScene - LevelLoader", rightBottomNode);

    ImGui::DockBuilderFinish(dockspaceId);
}
#endif // USE_IMGUI

void ImGuiManagaer::DrawEditorPanels_()
{
#ifdef USE_IMGUI
    auto* particleManager = ParticleManager::GetInstance();
    const std::vector<std::string> groupNames = particleManager->GetGroupNames();
    constexpr int fixedItemCount = 3;
    const int itemCount = fixedItemCount + static_cast<int>(groupNames.size());
    selectedParticleItem_ = std::clamp(selectedParticleItem_, 0, std::max(0, itemCount - 1));
    if (selectedParticleItem_ >= fixedItemCount) {
        particleManager->SetEditorSelectedGroupName(groupNames[selectedParticleItem_ - fixedItemCount]);
    } else {
        particleManager->SetEditorSelectedGroupName("");
    }
    const bool blenderHierarchyMode = gParticleTestEditorModeSwitcherVisible && (gParticleTestEditorMode == 0);

    if (gParticleTestEditorModeSwitcherVisible) {
        ImGui::SetNextWindowSize(ImVec2(220.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(8.0f, 56.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Hierarchy");
        if (blenderHierarchyMode) {
            particleManager->SetEditorSelectedGroupName("");
            ImGui::TextUnformatted("Blender Root");
            ImGui::Separator();
            if (ImGui::TreeNodeEx("Scene Models", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (gParticleTestBlenderHierarchyNames.empty()) {
                    ImGui::TextDisabled("No models.");
                }
                for (int i = 0; i < static_cast<int>(gParticleTestBlenderHierarchyNames.size()); ++i) {
                    const bool selected = i == gParticleTestBlenderHierarchySelected;
                    if (ImGui::Selectable(gParticleTestBlenderHierarchyNames[i].c_str(), selected)) {
                        gParticleTestBlenderHierarchySelected = i;
                        gParticleTestBlenderHierarchySelectionChanged = true;
                    }
                }
                ImGui::TreePop();
            }
            if (gParticleTestAnimationCameraPreviewVisible) {
                ImGui::Separator();
                ImGui::TextUnformatted(gParticleTestAnimationCameraPreviewSwapped ? "Editor Camera" : "Preview Camera");
                const uint32_t imageSrv = hasPreviewTexture_ ? previewSrvIndex_ : sceneSrvIndex_;
                if ((hasPreviewTexture_ || hasSceneTexture_) && srvManager_) {
                    constexpr float sceneAspect = 1280.0f / 720.0f;
                    const float width = std::max(32.0f, ImGui::GetContentRegionAvail().x);
                    ImVec2 imageSize{ width, width / sceneAspect };
                    const float maxHeight = std::max(72.0f, ImGui::GetContentRegionAvail().y * 0.45f);
                    if (imageSize.y > maxHeight) {
                        imageSize.y = maxHeight;
                        imageSize.x = imageSize.y * sceneAspect;
                    }
                    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvManager_->GetGPUDescriptionHandle(imageSrv);
                    ImGui::Image(static_cast<ImTextureID>(handle.ptr), imageSize);
                    if (ImGui::Button("Swap Preview / Editor")) {
                        gParticleTestAnimationCameraPreviewSwapped = !gParticleTestAnimationCameraPreviewSwapped;
                    }
                } else {
                    ImGui::TextDisabled("Scene texture is not ready.");
                }
            }
        } else {
            ImGui::TextUnformatted("Particle Root");
            ImGui::Separator();
            if (ImGui::Selectable("Create New Group", selectedParticleItem_ == 0)) {
                selectedParticleItem_ = 0;
            }
            if (ImGui::Selectable("HitEffect Preset", selectedParticleItem_ == 1)) {
                selectedParticleItem_ = 1;
            }
            if (ImGui::Selectable("Save / Load", selectedParticleItem_ == 2)) {
                selectedParticleItem_ = 2;
            }
            ImGui::Separator();
            if (ImGui::TreeNodeEx("Particle Groups", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (groupNames.empty()) {
                    ImGui::TextDisabled("No particle groups.");
                }
                for (int i = 0; i < static_cast<int>(groupNames.size()); ++i) {
                    const int itemIndex = fixedItemCount + i;
                    if (ImGui::Selectable(groupNames[i].c_str(), selectedParticleItem_ == itemIndex)) {
                        selectedParticleItem_ = itemIndex;
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::End();
    }

    if (gParticleTestEditorModeSwitcherVisible) {
        ImGui::SetNextWindowSize(ImVec2(280.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(980.0f, 56.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector");
        static char groupName[64] = "HitEffect";
        static char texturePath[256] = "resources/circle.png";
        static char fileName[256] = "test_particles.json";
        static int emitCount = 24;
        static Vector3 emitPosition{ 0.0f, 1.0f, 0.0f };

        if (blenderHierarchyMode) {
            // Blender-mode inspector content is supplied by ParticleTestScene.
        } else if (selectedParticleItem_ == 0) {
            ImGui::TextUnformatted("Create New Group");
            ImGui::Separator();
            ImGui::InputText("Name", groupName, sizeof(groupName));
            ImGui::InputText("Texture", texturePath, sizeof(texturePath));
            if (ImGui::Button("Create Particle Group")) {
                particleManager->CreateParticleGroup(groupName, texturePath);
            }
            ImGui::TextDisabled("Tune details in Particle Manager.");
        } else if (selectedParticleItem_ == 1) {
            ImGui::TextUnformatted("HitEffect Preset");
            ImGui::Separator();
            ImGui::InputText("Name", groupName, sizeof(groupName));
            ImGui::InputText("Texture", texturePath, sizeof(texturePath));
            if (ImGui::Button("Create / Reset HitEffect")) {
                if (!particleManager->HasGroup(groupName)) {
                    particleManager->CreateParticleGroup(groupName, texturePath);
                }
                particleManager->ConfigureHitEffectPreset(groupName);
            }
            ImGui::DragFloat3("Emit Position", &emitPosition.x, 0.1f);
            ImGui::DragInt("Emit Count", &emitCount, 1, 1, 1024);
            if (ImGui::Button("Emit Preview")) {
                particleManager->Emit(groupName, emitPosition, static_cast<uint32_t>(std::max(1, emitCount)));
            }
        } else if (selectedParticleItem_ == 2) {
            ImGui::TextUnformatted("Save / Load");
            ImGui::Separator();
            ImGui::InputText("File", fileName, sizeof(fileName));
            if (ImGui::Button("Save Particles")) {
                particleManager->Save(fileName);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Particles")) {
                particleManager->Load(fileName);
            }
            if (ImGui::Button("Clear Scene Particles")) {
                particleManager->ClearGroups();
                selectedParticleItem_ = 0;
            }
        } else {
            const int groupIndex = selectedParticleItem_ - fixedItemCount;
            const std::string& selectedGroupName = groupNames[groupIndex];
            ImGui::TextUnformatted(selectedGroupName.c_str());
            ImGui::Separator();
            ImGui::DragFloat3("Emit Position", &emitPosition.x, 0.1f);
            ImGui::DragInt("Emit Count", &emitCount, 1, 1, 1024);
            if (ImGui::Button("Emit Preview")) {
                particleManager->Emit(selectedGroupName, emitPosition, static_cast<uint32_t>(std::max(1, emitCount)));
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply HitEffect Look")) {
                particleManager->ConfigureHitEffectPreset(selectedGroupName);
            }
            ImGui::TextDisabled("Tune color, lifetime, shape, blend, model, and texture in Particle Manager.");
        }

        if (gParticleTestEditorModeSwitcherVisible && gParticleTestEditorMode == 1) {
            ImGui::Separator();
            ImGui::TextUnformatted("Particle Manager");
            particleManager->DrawImGuiContents();
        }
        ImGui::End();
    }

    if (gParticleTestEditorModeSwitcherVisible) {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 160.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(260.0f, 560.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Console");
        ImGui::TextUnformatted("[Particle Editor] Create groups from Hierarchy.");
        ImGui::TextUnformatted("[Particle Editor] Select a group and preview it in Inspector.");
        ImGui::TextUnformatted("[Particle Manager] Tune detailed parameters and save JSON.");
        ImGui::End();
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 220.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(260.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene");
    if (gParticleTestEditorModeSwitcherVisible) {
        ImGui::TextUnformatted("Effect Editor Mode");
        ImGui::SameLine();
        ImGui::RadioButton("Blender Mode", &gParticleTestEditorMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Particle Mode", &gParticleTestEditorMode, 1);
        ImGui::Separator();
    }
    DrawScenePreview();
    ImGui::End();
#endif // USE_IMGUI
}

void ImGuiManagaer::DrawScenePreview(bool fillRegion) {
#ifdef USE_IMGUI
    if (hasSceneTexture_ && srvManager_) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 8.0f || avail.y < 8.0f) {
            gHasSceneImageRect = false;
            ImGui::TextDisabled("Scene view is too small.");
            return;
        }
        constexpr float sceneAspect = 1280.0f / 720.0f;
        ImVec2 imageSize = avail;
        if (!fillRegion && imageSize.x / imageSize.y > sceneAspect) {
            imageSize.x = imageSize.y * sceneAspect;
        } else if (!fillRegion) {
            imageSize.y = imageSize.x / sceneAspect;
        }
        imageSize.x = std::max(1.0f, imageSize.x);
        imageSize.y = std::max(1.0f, imageSize.y);

        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(cursor.x + (avail.x - imageSize.x) * 0.5f);
        ImGui::SetCursorPosY(cursor.y + (avail.y - imageSize.y) * 0.5f);

        D3D12_GPU_DESCRIPTOR_HANDLE handle = srvManager_->GetGPUDescriptionHandle(sceneSrvIndex_);
        gSceneImageMin = ImGui::GetCursorScreenPos();
        gSceneImageMax = ImVec2(gSceneImageMin.x + imageSize.x, gSceneImageMin.y + imageSize.y);
        gHasSceneImageRect = true;
        ImGui::Image(static_cast<ImTextureID>(handle.ptr), imageSize);
        sceneImageHovered_ = ImGui::IsItemHovered();
    } else {
        gHasSceneImageRect = false;
        ImGui::TextUnformatted("Scene texture is not ready.");
    }
#else
    (void)fillRegion;
#endif
}

void ImGuiManagaer::End(ID3D12GraphicsCommandList* cmd)
{
#ifdef USE_IMGUI


    ImGui::Render();

    // ImGui謠冗判蜑阪↓ SRV heap 繧偵そ繝・ヨ
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

#endif // USE_IMGUI


}

void ImGuiManagaer::Shutdown()
{

#ifdef USE_IMGUI


    if (!initialized_) return;
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;

#endif // USE_IMGUI


}


bool ImGuiManagaer::GetSceneImageRect(RECT& rect) const {
#ifdef USE_IMGUI
    if (!gHasSceneImageRect) return false;
    rect = {static_cast<LONG>(gSceneImageMin.x), static_cast<LONG>(gSceneImageMin.y),
        static_cast<LONG>(gSceneImageMax.x), static_cast<LONG>(gSceneImageMax.y)};
    return true;
#else
    (void)rect;
    return false;
#endif
}

