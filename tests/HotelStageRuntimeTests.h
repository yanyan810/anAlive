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
    HotelStageChecks::Validate("resources");
    app.Scenes().Change(app,"Stage03");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Check(game && game->stageLoaded_ && game->level_.id=="stage03","Hotel GameScene initialization");
    Check(game->enemies_.empty() && game->spawnSystem_.Points().size()==6,"Hotel placeholder enemies");
    Check(game->LevelPath()=="resources/levels/stage03/stage03.json","Hotel editor/restart path");
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
    const Vector3 views[]={{7,12,10},{16,6,26},{36.4f,4.5f,6},{30,0,18},{-2,0,18}};
    const char* names[]={"3f-corridor","2f-ballroom","stair-to-1f","1f-ballroom","outdoor"};
    std::set<uint64_t> viewSignatures;
    for (size_t i=0;i<std::size(views);++i) {
        const float yaw=i<2 ? std::numbers::pi_v<float>*.5f : i==2 ? std::numbers::pi_v<float> : -std::numbers::pi_v<float>*.5f;
        game->player_.SetStage(&game->level_.collision,views[i],{i==2 ? .35f : 0,yaw,0});
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
    Check(game && game->stageLoaded_ && HotelStageChecks::Near(game->player_.GetTransform().translate.y,12),"Hotel restart spawn");
    app.Scenes().Change(app,"Game");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Check(game && game->stageLoaded_ && game->level_.id=="stage01" && !game->level_.collision.playerGravity,"Legacy Stage01 regression");
#ifdef _DEBUG
    app.Scenes().Change(app,"Showroom");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Check(game && game->stageLoaded_ && game->showroom_,"Showroom regression");
#endif
    std::ofstream("generated/hotel-tests/result.txt")<<"PASS: hotel movement/route checks; actual loaded GameScene route traversal and outdoor Goal; five D3D12 captures; hotel restart; Stage01 and Debug Showroom initialization.\n";
}
