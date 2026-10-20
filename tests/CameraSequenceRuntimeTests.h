#pragma once
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "DirectXCommon.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

inline void RunCameraSequenceRuntimeTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    const std::wstring args=GetCommandLineW();
    const std::string stage=args.find(L"--camera-stage03")!=std::wstring::npos ? "stage03" :
        args.find(L"--camera-stage02")!=std::wstring::npos ? "stage02" : "stage01";
    const std::string dir="generated/camera-tests/"+stage;
    std::filesystem::create_directories(dir);
    std::ofstream trace(dir+"/trace.txt"); trace<<"Entered runtime test"<<std::endl;
    const auto render=[&]() {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
    };
    const auto capture=[&](const char* name) {
        render();
        // Debug post effects leave this source render target in RT state.
        // Use its tracked transition API before readback and include the real HUD.
        app.Dx()->PreDraw(false);
        app.Render()->GetOffscreen()->BeginForPostEffect();
        if (auto* current=dynamic_cast<GameScene*>(app.Scenes().Current())) current->DrawOverlay2D(app);
        app.Render()->GetOffscreen()->End();
        app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),false,image,state,state)),"Camera GPU readback failed");
        const auto path=std::filesystem::path(dir)/(std::string(name)+".png");
        check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Camera PNG failed");
    };
    auto& scenes=app.Scenes();
    scenes.Register("CameraTest",[stage]{return std::make_unique<GameScene>(false,stage);});
    check(scenes.TransitionTo("CameraTest",0,.5f),"Title to camera transition rejected");
    scenes.Update(app,0); trace<<"Before outgoing draw"<<std::endl; render(); trace<<"Before loading incoming scene"<<std::endl; scenes.Update(app,100);
    trace<<"Loaded incoming scene"<<std::endl;
    auto* game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->stageLoaded_ && game->sequenceOwnsCamera_,"Incoming scene did not prepare start camera");
    check(game->startPlayerVisual_ && std::abs(game->startPlayerVisual_->Rotation().y-
        game->level_.playerVisual.WorldYaw(game->player_.GetTransform().rotate.y))<.0001f,"intro model ignored saved Player Visual yaw");
    const auto first=game->camera_.GetTranslate();
    trace<<"Before incoming draw"<<std::endl; render(); trace<<"Before FadeIn"<<std::endl; scenes.Update(app,100); // begin FadeIn, discard loading delta
    scenes.Update(app,.25f); render();
    check(game->startSequence_.Elapsed()==0 && StageLength(game->camera_.GetTranslate()-first)<.0001f,"Fade advanced sequence or moved camera");
    scenes.Update(app,.25f); scenes.Update(app,100);
    check(!scenes.IsTransitioning() && game->startSequence_.Elapsed()==0,"First sequence update consumed fade/loading dt");
    capture("00-start");
    auto& input=*app.GetInput();
    input.keys_[DIK_W]=0x80; input.keys_[DIK_R]=0x80; input.keys_[DIK_E]=0x80;
    input.leftMouseDown_=true; input.prevLeftMouseDown_=false; input.mouseDelta_={500,500};
    const auto player=game->player_.GetTransform(); const auto hp=game->player_.GetHP();
    const auto count=game->enemyPool_.Active(); const auto spawnTime=game->spawnSystem_.ElapsedTime();
    const auto ammo=game->player_.CurrentWeapon().Magazine();
    std::vector<Vector3> enemies; for (auto* enemy:game->enemies_) enemies.push_back(enemy->GetPosition());
    float previous=0;
    for (const auto& checkpoint:std::vector<std::pair<float,const char*>>{{.5f,"05-hold"},{1.5f,"15-orbit"},{2.5f,"25-behind"},{3.5f,"35-ingress"},{3.9f,"39-eye"}}) {
        game->Update(app,checkpoint.first-previous); previous=checkpoint.first;
        check(game->sequenceOwnsCamera_ && game->stage_.Time()==0 && game->spawnSystem_.ElapsedTime()==spawnTime,"Intro advanced gameplay/spawns");
        check(StageLength(game->player_.GetTransform().translate-player.translate)==0 && game->player_.GetHP()==hp,"Intro moved/damaged player");
        check(game->shotCount_==0 && game->player_.CurrentWeapon().Magazine()==ammo && game->enemyPool_.Active()==count,"Intro fired/reloaded/changed pool");
        for (size_t i=0;i<enemies.size();++i) check(StageLength(game->enemies_[i]->GetPosition()-enemies[i])==0,"Intro advanced enemy AI");
        capture(checkpoint.second);
    }
    game->Update(app,.2f);
    check(!game->sequenceOwnsCamera_ && game->sequenceInputGuard_,"Natural finish failed");
    check(StageLength(game->camera_.GetTranslate()-game->player_.EyePosition())<1e-6f,"FPS position mismatch");
    check(StageLength(game->camera_.GetRotate()-player.rotate)<1e-6f && game->camera_.GetFovY()==game->sequenceEye_.fov,"FPS rotation/FOV mismatch");
    game->FinishStartSequence(app); // idempotent
    game->Update(app,1); check(game->stage_.Time()>0 && game->shotCount_==0,"Held input escaped guard or froze resumed gameplay");
    std::fill(std::begin(input.keys_),std::end(input.keys_),0);
    input.leftMouseDown_=false; input.prevLeftMouseDown_=false; input.mouseDelta_={};
    game->Update(app,0); game->initialCapturePending_=false;
    // Capture exact FPS view, then the real gameplay HUD after resuming.
    capture("40-fps"); game->Update(app,1.0f/60);
    check(game->stage_.Time()>0 && !game->sequenceInputGuard_,"Gameplay did not resume");
    game->BeginStartSequence(app); game->Update(app,0); game->Update(app,1);
    // Exercise the same DIK_SPACE trigger branch used during real gameplay.
    input.keys_[DIK_SPACE]=0x80; input.prevKeys_[DIK_SPACE]=0;
    // Hidden runtime window may lack focus. Skip is tested through the evaluator
    // in that case; report the input limitation explicitly in the test output.
    const bool focused=input.HasFocus();
    if (focused) game->Update(app,.016f);
    else { game->startSequence_.Skip(); game->Update(app,0); }
    check(!game->sequenceOwnsCamera_ && StageLength(game->camera_.GetTranslate()-game->player_.EyePosition())<1e-6f,"Skip handoff failed");
    game->Update(app,1); check(game->sequenceInputGuard_,"Held skip key was not guarded");
    input.keys_[DIK_SPACE]=0;
    // Use the same scene factory as Restart Stage; OnExit cancels the old owner.
    scenes.Change(app,"CameraTest"); game=dynamic_cast<GameScene*>(scenes.Current());
    check(game && game->sequenceOwnsCamera_ && game->startSequence_.Elapsed()==0,"Restart did not prepare a fresh sequence");
    game->SetStartSequenceEnabled(false); game->Update(app,0);
    check(!game->sequenceOwnsCamera_,"Debug disabling did not restore FPS camera");
    std::ofstream(dir+"/result.txt")<<"PASS: "<<stage<<" D3D12 frames 0/.5/1.5/2.5/3.5/3.9/4s, title fade hold, frozen player/weapons/AI/pool/spawn/clock, exact FPS handoff, held input guard, skip, restart and disable. Space input branch "<<(focused?"tested":"unverified (unfocused hidden window; evaluator skip tested)")<<".\n";
}

