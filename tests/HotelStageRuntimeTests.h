#pragma once
#include "HotelStageChecks.h"
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "DirectXCommon.h"
#include "ImGuiManagaer.h"
#include "RenderManager.h"
#include "DirectXTex.h"
#include <numbers>

inline void RunHotelStageRuntimeTests(GameApp& app) {
    using HotelStageChecks::Check;
    if (std::wstring(GetCommandLineW()).find(L"--hotel-regression-test")!=std::wstring::npos) {
        // The existing SRV allocator is monotonic. Test unrelated stages in a fresh
        // process rather than exhaust its 512 descriptors with repeated scene loads.
        app.Scenes().Register("HotelTestStage02",[] { return std::make_unique<GameScene>(false,"stage02"); });
        app.Scenes().Change(app,"HotelTestStage02");
        auto* regression=dynamic_cast<GameScene*>(app.Scenes().Current());
        Check(regression && regression->stageLoaded_ && regression->level_.id=="stage02" && regression->shutterVisuals_.empty(),"Stage02 regression");
#ifdef _DEBUG
        app.Scenes().Change(app,"Showroom");
        regression=dynamic_cast<GameScene*>(app.Scenes().Current());
        Check(regression && regression->stageLoaded_ && regression->showroom_ && regression->shutterVisuals_.empty(),"Showroom regression");
#endif
        std::ofstream("generated/hotel-tests/regression.txt")<<"PASS: Stage02 and Debug Showroom initialize without shutters.\n";
        return;
    }
    // The standalone hotel test covers the full down/up route at all three FPS.
    // Below we traverse it once in the actual scene and capture the GPU output.
    HotelStageChecks::Validate("resources",false);
    if (app.Scenes().CurrentName()!="Stage03") app.Scenes().Change(app,"Stage03");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Check(game && game->stageLoaded_ && game->level_.id=="stage03","Hotel GameScene initialization");
    Check(game->enemies_.empty() && game->spawnSystem_.Points().size()==6,"Hotel placeholder enemies");
    Check(game->LevelPath()=="resources/levels/stage03/stage03.json","Hotel editor/restart path");
    const auto capture=[&](const std::string& name) {
        game->ground_.Update(0); game->SyncShutterVisuals();
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
        app.Dx()->PreDraw(false);
        app.Render()->GetOffscreen()->BeginForPostEffect();
        game->DrawOverlay2D(app); // Capture the same HUD with the source 3D frame.
        app.Render()->GetOffscreen()->End();
        app.Dx()->SetBackBufferRenderTarget(); app.Dx()->PostDraw(); app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),false,image,state,state)),"Shutter GPU capture");
        const auto path=std::filesystem::path("generated/hotel-tests")/(name+".png");
        Check(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Shutter PNG capture");
    };
    Check(game->level_.shutters.shutters[0].state==ShutterState::Closed,"Actual scene starts Closed");
    game->player_.SetStage(&game->level_.collision,{14,0,21.5f},{0,-2.25f,0});
    game->player_.RefreshVisuals();
    Check(game->level_.shutters.Nearest(game->player_.GetTransform().translate).has_value(),"Switch reachable in actual scene");
    capture("shutter-00-closed");
    const auto originalWeapons=game->weapons_;
    const auto originalWeapon=game->player_.CurrentWeapon();
    nlohmann::json interactionFixture;
    std::ifstream("resources/levels/stage01/stage01.json")>>interactionFixture;
    interactionFixture["weaponSpawnPoints"].erase(interactionFixture["weaponSpawnPoints"].begin()+1,interactionFixture["weaponSpawnPoints"].end());
    interactionFixture["weaponSpawnPoints"][0]["position"]={14,0,21.5f};
    std::ofstream("generated/hotel-tests/shutter-interaction.json")<<interactionFixture;
    Check(game->weapons_.Load("resources/Data/weapons.json","generated/hotel-tests/shutter-interaction.json"),"Overlapping pickup fixture");
    game->HandleInteraction(false,true);
    Check(game->level_.shutters.shutters[0].state==ShutterState::Closed,"No E leaves shutter Closed");
    // Exercise the exact handler called by the gated DIK_E input path.
    size_t started=0,markers=0,completed=0;
    game->SetShutterEventHandler([&](const ShutterEvent& event) {
        if (event.type==ShutterEventType::Started) ++started;
        if (event.type==ShutterEventType::Progress) ++markers;
        if (event.type==ShutterEventType::Completed) ++completed;
    });
    game->HandleInteraction(true,true); game->ProcessShutterEvents();
    Check(game->level_.shutters.shutters[0].state==ShutterState::Opening && !game->shutterAudio_,"E path / missing audio is silent");
    Check(!game->weapons_.Pickups()[0].pickedUp,"Switch priority consumes E before pickup");
    Check(game->HandleInteraction(true,true) && game->weapons_.Pickups()[0].pickedUp &&
        game->level_.shutters.shutters[0].state==ShutterState::Opening,"Used switch cannot close; next E may pick up weapon");
    game->weapons_=originalWeapons; game->player_.CurrentWeapon()=originalWeapon;
    for (int second=1;second<=30;++second) {
        for (int frame=0;frame<60;++frame) {
            game->Update(app,1.0f/60);
            const auto& shutter=game->level_.shutters.shutters[0];
            Check(HotelStageChecks::Near(game->shutterVisuals_[0].model->GetTranslate().y,shutter.Position().y,.001f),"Actual Object3d position sync");
            const auto world=game->shutterVisuals_[0].model->GetWorldMatrix();
            Check(HotelStageChecks::Near(world.m[3][1],shutter.Position().y,.001f),"Actual GPU world translation sync");
        }
        if (second==15) capture("shutter-15-opening");
    }
    Check(started==1 && markers==1 && completed==1,"GameScene event delivery exactly once");
    Check(game->level_.shutters.shutters[0].state==ShutterState::Open,"Actual scene opens in 30 seconds");
    capture("shutter-30-open");
    game->SetShutterEventHandler({});
    game->level_.shutters.Reset(game->level_.collision);
    Check(game->level_.shutters.TryInteract(game->player_.GetTransform().translate),"Restart test activation");
    Check(game->stage_.Update(0,{-16,0,18}),"Early Goal fixture");
    game->Update(app,30);
    Check(game->level_.shutters.shutters[0].state==ShutterState::Open,"Goal reached early must not stop opening");
    game->stage_.Reset();
    game->player_.SetStage(&game->level_.collision,game->level_.playerPosition,game->level_.playerRotation);
    game->player_.RefreshVisuals();
    // Traverse the world loaded by the actual GameScene, then check its Goal.
    // The route uses real frame-by-frame FPSMotion/MovePlayer, with no input injection.
    StageWorld::PlayerMotion routeMotion;
    auto routePosition=game->player_.GetTransform().translate;
    std::ofstream trace("generated/hotel-tests/route-trace.csv");
    trace<<"waypoint,x,y,z\n";
    size_t waypoint=0;
    for (const auto& target : HotelStageChecks::route) {
        HotelStageChecks::Walk(game->level_.collision,routePosition,target,60,routeMotion);
        game->player_.SetStage(&game->level_.collision,routePosition,{});
        game->player_.RefreshVisuals();
        trace<<waypoint++<<","<<routePosition.x<<","<<routePosition.y<<","<<routePosition.z<<"\n";
    }
    Check(game->stage_.Update(0,routePosition),"Actual GameScene outdoor goal");
    game->stage_.Reset();
    const Vector3 views[]={{34,12,19},{35,12,6},{30,12,11.5f},{16,6,26},{36.4f,4.5f,6},{30,0,18},{-2,0,18}};
    const char* names[]={"3f-room308","3f-elevator-hall","3f-corridor","2f-ballroom","stair-to-1f","1f-ballroom","outdoor"};
    std::set<uint64_t> viewSignatures;
    for (size_t i=0;i<std::size(views);++i) {
        const float yaw=i<2 || i==4 ? std::numbers::pi_v<float> : i==3 ? std::numbers::pi_v<float>*.5f : -std::numbers::pi_v<float>*.5f;
        game->player_.SetStage(&game->level_.collision,views[i],{i==4 ? .35f : 0,yaw,0});
        game->player_.RefreshVisuals();
        game->ground_.Update(0); // Refresh the stage WVP after each camera move.
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
        // Post effects leave the source in RENDER_TARGET. Use the pass's state
        // tracking to make it shader-readable before DirectXTex copies it.
        app.Dx()->PreDraw(false);
        app.Render()->GetOffscreen()->BeginForPostEffect();
        app.Render()->GetOffscreen()->End();
        app.Dx()->SetBackBufferRenderTarget();
        app.Dx()->PostDraw();
        app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Check(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),
            app.Render()->GetOffscreen()->GetResource(),false,image,state,state)),"Hotel GPU capture");
        const auto* pixels=image.GetImage(0,0,0);
        Check(pixels && pixels->width>=640 && pixels->height>=360,"Hotel render dimensions");
        uint64_t signature=1469598103934665603ULL;
        for (size_t y=0;y<pixels->height;y+=8) for (size_t x=0;x<pixels->width;x+=8)
            for (size_t channel=0;channel<3;++channel) {
                signature^=pixels->pixels[y*pixels->rowPitch+x*4+channel]; signature*=1099511628211ULL;
            }
        Check(viewSignatures.insert(signature).second,"Hotel views did not change after camera movement");
        const auto output=std::filesystem::path("generated/hotel-tests")/(std::string(names[i])+".png");
        Check(SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),output.c_str())),"Hotel PNG capture");
    }
    // Restart reloads hotel spawn, and both the legacy game and Showroom still load.
    app.Scenes().Change(app,"Stage03");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Check(game && game->stageLoaded_ && HotelStageChecks::Near(game->player_.GetTransform().translate.y,12) &&
        HotelStageChecks::Near(game->player_.GetTransform().translate.x,34) &&
        HotelStageChecks::Near(game->player_.GetTransform().translate.z,19),"Hotel restart Room308 spawn");
    Check(game->level_.shutters.shutters[0].state==ShutterState::Closed &&
        game->level_.shutters.shutters[0].Progress()==0,"Actual restart closes shutter");
    app.Scenes().Change(app,"Game");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Check(game && game->stageLoaded_ && game->level_.id=="stage01" && !game->level_.collision.playerGravity,"Legacy Stage01 regression");
    Check(game->shutterVisuals_.empty(),"No Stage01 shutter");
    std::ofstream("generated/hotel-tests/result.txt")<<"PASS: shutter timing at 30/60/144 FPS, E handler, collision/rays/projectiles, event delivery, Object3d/GPU matrices, silent missing audio, three shutter and seven hotel D3D12 captures; Room308/eight-room route/Goal; restart Closed; Stage01.\n";
}
