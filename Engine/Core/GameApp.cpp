#include "../Utility/AssetPreparation.h"
#include "../../tests/AssetLoadingTests.h"
#include "../../tests/ScreenTransitionTests.h"
#include "../../tests/CameraSequenceRuntimeTests.h"
#include "../../tests/HotelStageRuntimeTests.h"
#include "../Utility/AssetLoadProfile.h"
#include "scene/Main/GameScene.h"
#include "scene/Flow/TitleScene.h"
#include "scene/Flow/GameOverScene.h"
#include "TemporarySceneTransitionCheck.h"
#include "../../tests/TemporarySceneTransitionCheckTests.h"
#include "Enemy.h"
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include "scene/Main/ShowroomScene.h"
#include "scene/Main/ClothShowroomScene.h"
#endif
#include "GameApp.h"
#include "scene/Debug/EnemyAITestScene.h"
#include "scene/Debug/EnemyPlacementEditorScene.h"
#include "../../tests/EnemyAITestSceneTests.h"
#include "../../tests/EnemyAIStageSmokeTests.h"
#include "../../tests/EnemyPlacementEditorTests.h"
#include "SceneManager.h"

#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "PrimitiveCommon.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "ImGuiManagaer.h"
#include "../DebugAI/DebugAI.h"


#include "RenderManager.h"

#include <Windows.h>
#include <shellapi.h>
#include <string_view>
#pragma comment(lib, "shell32.lib")
#include <chrono>
#include <algorithm>
#include <cmath>
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include "../../tests/EnemyPoolTests.h"
#include "../../tests/TitleSceneTests.h"
#include "../../tests/ClothRuntimeTests.h"
#endif

namespace {
bool HasLaunchArgument(std::wstring_view option) {
    int count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(),&count);
    if (!arguments) return false;
    bool found = false;
    for (int i=1; i<count; ++i) {
        if (std::wstring_view(arguments[i])==option) { found=true; break; }
    }
    LocalFree(arguments);
    return found;
}
}

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

int GameApp::Run() {
    if(std::wstring(GetCommandLineW()).find(L"--prepare-assets")!=std::wstring::npos) return PrepareAssetCaches();
    if (!Initialize_()) {
        Finalize_();
        return -1;
    }
    if (HasLaunchArgument(L"--camera-sequence-test")) {
        int result=0;
        try { RunCameraSequenceRuntimeTests(*this); }
        catch (const std::exception& error) {
            std::filesystem::create_directories("generated/camera-tests");
            std::ofstream("generated/camera-tests/runtime-failure.txt")<<"FAIL: "<<error.what()<<'\n'; result=1;
        }
        Finalize_(); return result;
    }
    if (HasLaunchArgument(L"--enemy-ai-scene-test")) {
        int result=0; std::filesystem::create_directories("generated/enemy-ai-tests");
        try { RunEnemyAITestSceneTests(*this); }
        catch (const std::exception& error) {
            std::ofstream("generated/enemy-ai-tests/scene-result.txt")<<"FAIL: "<<error.what()<<'\n'; result=1;
        }
        Finalize_(); return result;
    }
    if (HasLaunchArgument(L"--enemy-placement-test")) {
        int result=0; std::filesystem::create_directories("generated/placement-tests");
        try { RunEnemyPlacementEditorTests(*this); }
        catch (const std::exception& error) {
            std::ofstream("generated/placement-tests/result.txt")<<"FAIL: "<<error.what()<<'\n'; result=1;
        }
        Finalize_(); return result;
    }
    for (const auto& stage: {std::string("stage01"),std::string("stage02"),std::string("stage03")}) {
        const auto option=L"--enemy-ai-"+std::wstring(stage.begin(),stage.end())+L"-smoke";
        if (!HasLaunchArgument(option)) continue;
        int result=0; std::filesystem::create_directories("generated/enemy-ai-tests");
        try { RunEnemyAIStageSmokeTests(*this,stage); }
        catch (const std::exception& error) {
            std::ofstream("generated/enemy-ai-tests/"+stage+"-smoke.txt")<<"FAIL: "<<error.what()<<'\n'; result=1;
        }
        Finalize_(); return result;
    }
    if (std::wstring(GetCommandLineW()).find(L"--hotel-stage-test")!=std::wstring::npos) {
        int result=0;
        std::filesystem::create_directories("generated/hotel-tests");
        try { RunHotelStageRuntimeTests(*this); }
        catch (const std::exception& error) {
            std::ofstream("generated/hotel-tests/result.txt")<<"FAIL: "<<error.what()<<'\n'; result=1;
        }
        Finalize_(); return result;
    }
    if (std::wstring(GetCommandLineW()).find(L"--asset-loading-test")!=std::wstring::npos) {
        int result=0;
        std::filesystem::create_directories("generated/loading-tests");
        try { RunAssetLoadingTests(*this); }
        catch(const std::exception& error) { std::ofstream("generated/loading-tests/result.txt")<<"FAIL: "<<error.what()<<'\n'; result=1; }
        Finalize_(); return result;
    }
    if (std::wstring(GetCommandLineW()).find(L"--screen-transition-test")!=std::wstring::npos) {
        int result = 0;
        std::filesystem::create_directories("generated/transition-tests");
        try { RunScreenTransitionTests(*this); }
        catch (const std::exception& error) {
            std::ofstream("generated/transition-tests/result.txt") << "FAIL: " << error.what() << '\n';
            result = 1;
        }
        Finalize_(); return result;
    }
    if (HasLaunchArgument(L"--scene-transition-check-test")) {
        int result = 0;
        std::filesystem::create_directories("generated/scene-transition-check");
        try { RunTemporarySceneTransitionCheckTests(*this,*transitionCheck_); }
        catch (const std::exception& error) {
            std::ofstream("generated/scene-transition-check/result.txt") << "FAIL: " << error.what() << '\n';
            result = 1;
        }
        Finalize_(); return result;
    }
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    if (std::wstring(GetCommandLineW()).find(L"--cloth-performance-test")!=std::wstring::npos) {
        int result=0;
        try { RunClothPerformanceTests(*this); }
        catch(const std::exception& error) { std::ofstream("generated/cloth-tests/performance-result.txt")<<"FAIL: "<<error.what()<<'\n'; result=1; }
        Finalize_(); return result;
    }
    if (std::wstring(GetCommandLineW()).find(L"--cloth-test")!=std::wstring::npos) {
        int result=0;
        std::filesystem::create_directories("generated/cloth-tests");
        try { RunClothRuntimeTests(*this); }
        catch (const std::exception& error) {
            std::ofstream("generated/cloth-tests/result.txt") << "FAIL: " << error.what() << '\n';
            result=1;
        }
        Finalize_(); return result;
    }
    if (std::wstring(GetCommandLineW()).find(L"--title-scene-test")!=std::wstring::npos) {
        int result=0;
        std::filesystem::create_directories("generated/title-tests");
        try { RunTitleSceneTests(*this); }
        catch (const std::exception& error) {
            std::ofstream("generated/title-tests/result.txt") << "FAIL: " << error.what() << '\n';
            result=1;
        }
        Finalize_(); return result;
    }
    if (std::wstring(GetCommandLineW()).find(L"--enemy-pool-test")!=std::wstring::npos) {
        int result=0;
        try { RunEnemyPoolTests(*this); }
        catch (const std::exception& error) {
            std::filesystem::create_directories("generated/enemy-pool-tests");
            std::ofstream("generated/enemy-pool-tests/result.txt")<<"FAIL: "<<error.what()<<'\n';
            result=1;
        }
        Finalize_(); return result;
    }
#endif

    auto previousFrame = std::chrono::steady_clock::now();
    // ループ
    while (!quit_) {
        if (win_->ProcessMessage()) break;

        const auto now = std::chrono::steady_clock::now();
        const float unscaledDt = std::chrono::duration<float>(now - previousFrame).count();
        const float dt = std::min(unscaledDt, 0.1f);
        previousFrame = now;

#ifdef USE_IMGUI
        // ★ ImGui フレーム開始（ここで1回だけ）
        imgui_->Begin();
#endif // DEBUG

        // Input, DebugAI control messages, and scene simulation must pass
        // through the same update path. Bypassing this function leaves
        // external DebugAI requests queued forever.
        Update(dt, unscaledDt);


        //描画
        Draw();
    }

    Finalize_();
    return 0;
}


bool GameApp::Initialize_() {
    OutputDebugStringA("[GameApp] Initialize START\n");

    win_ = std::make_unique<WinApp>();
    win_->Initialize();
    SetWindowTextW(win_->GetHwnd(), L"アンアライブ");

    dx_ = std::make_unique<DirectXCommon>();
    dx_->Initialize(win_.get());

    srv_ = std::make_unique<SrvManager>();
    srv_->Initialize(dx_.get());

    TextureManager::GetInstance()->Initialize(dx_.get(), srv_.get());

	//RenderManagerを作る
    render_ = std::make_unique<RenderManager>();
    render_->Initialize(dx_.get(), srv_.get());

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dx_.get());

    ModelManager::GetInstance()->Initialize(dx_.get());

    objCommon_ = std::make_unique<Object3dCommon>();
    objCommon_->Initialize(dx_.get());

    objCommon_->SetSrvManager(srv_.get());

    primitiveCommon_ = std::make_unique<PrimitiveCommon>();
    primitiveCommon_->Initialize(dx_.get());
    primitiveCommon_->SetSrvManager(srv_.get());
  

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dx_.get());

    ParticleManager::GetInstance()->Initialize(dx_.get(), srv_.get(), particleCommon_.get());

    skyboxCommon_ = std::make_unique<SkyboxCommon>();
    skyboxCommon_->Initialize(dx_.get());


#ifdef USE_IMGUI
    imgui_ = std::make_unique<ImGuiManagaer>();
    imgui_->Initialize(win_.get(), dx_.get(), srv_.get());
    imgui_->SetSceneTexture(render_->GetOffscreenSrvIndex());
#endif

    // GameApp::Initialize など
    skinCom_ = std::make_unique<SkinningCommon>();
    skinCom_->Initialize(dx_.get());
    objCommon_->SetSkinningCommon(skinCom_.get());

    // Load enemy models, textures and destruction data before gameplay starts.
    // ゲーム中の初回出現で読み込み待ちが発生しないよう、共通リソースの準備後に先読みする。
    Enemy::PreloadAssets();

    // ★ Input は Scene を動かす前に作る（最重要）
    input_ = std::make_unique<Input>();
    input_->Initialize(win_.get());
    input_->Update(); // 初回

    debugAI_ = std::make_unique<DebugAIManager>();
    DebugAIConfig debugAIConfig;
    debugAIConfig.gameId = "CG5";
    debugAIConfig.gameVersion = "0.1.0";
    debugAIConfig.controlEndpoint = "DebugAI_CG5";
    debugAIConfig.logDirectory = "generated/debug_ai";
    debugAIConfig.playerLogDirectory = "generated/debug_ai/player";
    debugAIConfig.aiLogDirectory = "generated/debug_ai/ai";
    debugAIConfig.detectNegativeHp = false;
    debugAIConfig.detectInvalidCounts = false;
    debugAIConfig.detectInvalidPosition = false;
    debugAIConfig.detectMapBounds = false;
    debugAIConfig.detectSameState = false;
    debugAIConfig.detectNoProgress = false;
    debugAIConfig.detectLowFps = false;
    debugAIConfig.recordBotActions = false;
    debugAIConfig.logActionResults = false;
    debugAIConfig.logFrames = false;
    debugAIConfig.idleSampleIntervalFrames = 30;
    debugAI_->Initialize(debugAIConfig);

    // SceneManager
    sceneMgr_ = std::make_unique<SceneManager>();
    sceneMgr_->Initialize(*this);
    sceneMgr_->Register("Title", [] { return std::make_unique<TitleScene>(); });
    sceneMgr_->Register("EnemyAITest", [] { return std::make_unique<EnemyAITestScene>(); });
    sceneMgr_->Register("EnemyPlacementEditor", [] { return std::make_unique<EnemyPlacementEditorScene>(); });
    sceneMgr_->Register("GameOver", [] { return std::make_unique<GameOverScene>(); });
    sceneMgr_->Register("Game", [] { return std::make_unique<GameScene>(); });
    sceneMgr_->Register("Stage03", [] { return std::make_unique<GameScene>(false,"stage03"); });
    #if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    sceneMgr_->Register("Showroom", [] { return std::make_unique<ShowroomScene>(); });
    sceneMgr_->Register("ClothShowroom", [] { return std::make_unique<ClothShowroomScene>(); });
    #endif
    sceneMgr_->Change(*this, "Title");
    if (HasLaunchArgument(L"--enemy-ai-test")) sceneMgr_->Change(*this,"EnemyAITest");
    if (HasLaunchArgument(L"--enemy-placement-editor")) sceneMgr_->Change(*this,"EnemyPlacementEditor");
    // Deliberately opt-in; this check is available without Debug/ImGui.
    if (HasLaunchArgument(L"--scene-transition-check") || HasLaunchArgument(L"--scene-transition-check-test")) {
        transitionCheck_ = std::make_unique<TemporarySceneTransitionCheck>();
        transitionCheck_->Initialize(*this);
    }
    if (std::wstring(GetCommandLineW()).find(L"--hotel")!=std::wstring::npos)
        sceneMgr_->Change(*this, "Stage03");
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    if (std::wstring(GetCommandLineW()).find(L"--cloth-showroom")!=std::wstring::npos)
        sceneMgr_->Change(*this, "ClothShowroom");
#endif

    OutputDebugStringA("[GameApp] Initialize END\n");
    return true;
}


void GameApp::Finalize_() {

    if (dx_) dx_->WaitForGPU();
    if (sceneMgr_ && sceneMgr_->Current()) sceneMgr_->Current()->OnExit(*this);
    transitionCheck_.reset();
    sceneMgr_.reset();
    Enemy::ReleasePreloadedAssets();

    if (imgui_) imgui_->Shutdown();
    if (debugAI_) debugAI_->Shutdown();

    ParticleManager::GetInstance()->Finalize();
    TextureManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    if (win_) win_->Finalize();

    render_.reset();

    sceneMgr_.reset();
    input_.reset();
    debugAI_.reset();
    skyboxCommon_.reset();
    imgui_.reset();
    primitiveCommon_.reset();
    particleCommon_.reset();
    objCommon_.reset();
    spriteCommon_.reset();
    skinCom_.reset(); // SkinningCommonも確実にリセット
    srv_.reset();
    
    dx_.reset();
    win_.reset();
    if(AssetLoading::profiling) AssetLoading::SaveProfile("generated/loading/session.csv");
}

void GameApp::Update(float dt) {
    Update(dt, dt);
}

void GameApp::Update(float dt, float unscaledDt) {

    input_->Update();
    if (transitionCheck_) transitionCheck_->Update(*this);

    unsigned int simulationUpdates = 1;
    if (debugAI_) {
        debugAI_->ProcessControlCommands();
        std::string requestedScene;
        if (debugAI_->ConsumeSceneLoadRequest(requestedScene) &&
            !requestedScene.empty()) {
            if (sceneMgr_->CurrentName() == requestedScene) {
                if (debugAI_->HasPendingReplay()) {
                    debugAI_->StartPendingReplay();
                }
            } else if (sceneMgr_->HasRegisteredScene(requestedScene)) {
                sceneMgr_->Change(*this, requestedScene);
            }
        }
        simulationUpdates = debugAI_->ReplaySimulationUpdatesForHostFrame();
    }

    // Presentation uses wall time once, even if replay pauses/accelerates the
    // simulation or gameplay later applies slow motion to its own dt.
    // A cold stage's first GPU frames (or a window/debugger stall) can be much
    // longer than the fade itself. Do not jump straight into its middle/end.
    // This is still unscaled wall time, independent of simulation/replay dt.
    const float transitionDt = std::isfinite(unscaledDt) && unscaledDt > 0 ? std::min(unscaledDt, .05f) : 0;
    sceneMgr_->UpdateTransition(*this, transitionDt);
    for (unsigned int update = 0; update < simulationUpdates; ++update) {
        if (debugAI_) {
            debugAI_->PrepareSimulationFrame();
        }
        sceneMgr_->UpdateScene(*this, debugAI_ && debugAI_->IsReplayPlaying() ? 1.0f / 60.0f : dt);
        if (debugAI_ && update + 1 < simulationUpdates &&
            !debugAI_->IsReplayPlaying()) {
            break;
        }
    }
}

void GameApp::Draw() {

    srv_->PreDraw();

    sceneMgr_->DrawShadow(*this);

    // ① Offscreenへ描く
    render_->BeginOffscreen();
    sceneMgr_->DrawRender(*this);
    sceneMgr_->Draw3D(*this);
    sceneMgr_->Draw2D(*this);
    sceneMgr_->Draw(*this);

    if (sceneMgr_->HasObjectBloomTargets() ||
        sceneMgr_->HasObjectOutlineBloomTargets() ||
        sceneMgr_->HasObjectLuminanceOutlineTargets()) {
        render_->BeginObjectPostLayer(
            sceneMgr_->HasObjectBloomTargets(),
            sceneMgr_->HasObjectOutlineBloomTargets(),
            sceneMgr_->HasObjectLuminanceOutlineTargets());
        sceneMgr_->DrawPostEffectTargets(*this);
        render_->EndObjectPostLayer();
    } else {
        render_->ClearObjectPostLayer();
    }

    auto* particleManager = ParticleManager::GetInstance();
    if (particleManager->HasPostEffectTargets()) {
        render_->SetParticleLayerBloomColor(particleManager->GetPrimaryPostEffectBloomColor());
        render_->SetParticleLayerOutlineBloomColor(particleManager->GetPrimaryPostEffectOutlineBloomColor());
        render_->BeginParticlePostLayer(
            particleManager->HasBloomPostEffectTargets(),
            particleManager->HasOutlineBloomPostEffectTargets());
        particleManager->Draw(dx_->GetCommandList(), true);
        render_->EndParticlePostLayer();
    } else {
        render_->ClearParticlePostLayer();
    }
    render_->EndOffscreen();

#ifdef USE_IMGUI
    render_->BeginPreview();
    sceneMgr_->DrawPreview(*this);
    render_->EndPreview();
#endif

    // ② BackBufferへ
    dx_->PreDraw(false);

    ParticleManager::GetInstance()->UpdateCompute(dx_->GetComputeCommandList());
#ifndef USE_IMGUI
    render_->DrawOffscreenToBackBuffer();
    sceneMgr_->DrawOverlay2D(*this);
    if (transitionCheck_) transitionCheck_->Draw(*this);
#endif

    // ③ Offscreenの中身を画面へ貼る

    // ④ 直接描く3D/2D/最終演出

#ifdef USE_IMGUI
    if (imgui_) {
#ifdef PLACEMENT_EDITOR_RELEASE
        if (!imgui_->IsWeaponWorkspace() && sceneMgr_->CurrentName()!="EnemyAITest") {
            render_->DrawOffscreenToBackBuffer(); sceneMgr_->DrawOverlay2D(*this);
            if (transitionCheck_) transitionCheck_->Draw(*this);
            imgui_->End(dx_->GetCommandList()); dx_->PostDraw(); return;
        }
#endif
        imgui_->SetSceneTexture(render_->RenderPostEffectsForSceneTexture());
        if (render_->BeginSceneTextureOverlay()) {
            sceneMgr_->DrawOverlay2D(*this);
            if (transitionCheck_) transitionCheck_->Draw(*this);
            render_->EndSceneTextureOverlay();
        }
        imgui_->SetPreviewTexture(render_->GetPreviewSrvIndex());
            sceneMgr_->DrawImGui(*this);
            if (!imgui_->IsWeaponWorkspace()) render_->DrawImGui(); // ポストエフェクト切り替えUI
        imgui_->End(dx_->GetCommandList());
    }
#endif

    dx_->PostDraw();
}





