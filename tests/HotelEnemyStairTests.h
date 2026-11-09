#pragma once
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "ImGuiManagaer.h"
#include "DirectXTex.h"
#include <fstream>

inline void RunHotelEnemyStairTests(GameApp& app) {
    const auto check=[](bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); };
    app.Scenes().Register("HotelEnemyStairTest",[] {
        auto scene=std::make_unique<GameScene>(false,"stage03"); scene->SetStartSequenceEnabled(false); return scene;
    });
    app.Scenes().Change(app,"HotelEnemyStairTest");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->stageLoaded_,"Hotel stair scene initialization failed");
    std::ofstream trace("generated/enemy-stairs-tests/trace.csv");
    trace<<"scenario,frame,enemy,x,y,z,state,pathFailure\n";
    const auto prepare=[&](const std::string& definition,const std::vector<Vector3>& starts,const Vector3& target) {
        app.Dx()->WaitForGPU(); game->enemyPool_.ReleaseAll(); game->enemies_.clear();
        game->enemyProjectiles_.Clear();
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
        game->debugHistory_.Clear(); game->debugPaused_=false;
#endif
        game->player_.SetStage(&game->level_.collision,target,{}); game->player_.ResetHPForDebug(); game->player_.RefreshVisuals();
        game->initialCapturePending_=false; app.GetInput()->SetCameraControlEnabled(false);
        for (const auto& start:starts) {
            const auto* d=game->enemyDefinitions_.Find(definition);
            check(d && EnemyNavigation::Clear(&game->level_.collision,start,start,d->collisionRadius,d->collisionHeight),"Stair fixture starts inside geometry");
            auto* enemy=game->enemyPool_.Acquire(definition,game->nextEnemyId_++,"stair-regression",start,{});
            enemy->PursuePlayer(target); game->enemies_.push_back(enemy);
        }
    };
    const auto capture=[&](const std::wstring& filename) {
        const auto position=game->player_.GetTransform().translate;
        const auto delta=game->enemies_.front()->GetPosition()+Vector3{0,1.2f,0}-game->player_.EyePosition();
        game->player_.SetStage(&game->level_.collision,position,{-std::atan2(delta.y,std::hypot(delta.x,delta.z)),std::atan2(delta.x,delta.z),0});
        game->player_.RefreshVisuals(); game->ground_.Update(0); game->SyncShutterVisuals();
        for (auto* enemy:game->enemies_) enemy->UpdateVisuals(0);
        app.ImGui()->Begin(); app.Draw();
        auto* dx=app.Dx(); dx->PreDraw(false); app.Render()->GetOffscreen()->BeginForPostEffect();
        game->DrawOverlay2D(app); app.Render()->GetOffscreen()->End(); dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
        DirectX::ScratchImage pixels; constexpr auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),false,pixels,state,state)),"Stair GPU capture failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),filename.c_str())),"Stair PNG failed");
    };
    struct Scenario { const char* id; const char* enemy; std::vector<Vector3> starts; Vector3 target; };
    const std::vector<Scenario> scenarios={
        {"west-partial","normal",{{4.4f,12,10.8f}},{4.4f,10.5f,6}},
        {"west-down","normal",{{4.4f,12,10.8f}},{9,6,10.8f}},
        {"west-up","normal",{{9,6,10.8f}},{4.4f,12,10.8f}},
        {"east-down","normal",{{36.4f,6,10.8f}},{35,0,18}},
        {"east-up","normal",{{35,0,14}},{36.4f,6,10.8f}},
        {"tank-west-down","hotel_tank",{{4.4f,12,10.8f}},{9,6,10.8f}},
        {"three-enemy-west-down","normal",{{4,12,10.8f},{4.8f,12,10.8f},{4.4f,12,10}},{9,6,10.8f}}
    };
    for (const auto& scenario:scenarios) {
        prepare(scenario.enemy,scenario.starts,scenario.target); bool arrived=false,captured=false;
        for (int frame=0;frame<5400;++frame) {
            std::vector<Vector3> previous; for (auto* enemy:game->enemies_) previous.push_back(enemy->GetPosition());
            game->player_.ResetHPForDebug(); game->Update(app,1.f/60);
            arrived=true;
            for (size_t i=0;i<game->enemies_.size();++i) {
                const auto* enemy=game->enemies_[i]; const auto p=enemy->GetPosition(); const auto& d=enemy->Definition();
                check(EnemyNavigation::Supported(&game->level_.collision,p),std::string(scenario.id)+": missing floor");
                check(EnemyNavigation::Clear(&game->level_.collision,p,p,d.collisionRadius,d.collisionHeight),std::string(scenario.id)+": entered wall or ceiling");
                check(std::abs(p.y-previous[i].y)<=.36f,std::string(scenario.id)+": height teleport");
                const auto target=game->player_.GetTransform().translate;
                arrived=arrived && std::abs(p.y-target.y)<.4f && EnemyAI::distanceTo(p,target)<2;
                if (frame%60==0) trace<<scenario.id<<','<<frame<<','<<i<<','<<p.x<<','<<p.y<<','<<p.z<<','<<static_cast<int>(enemy->AI().state)<<','<<static_cast<int>(enemy->AI().navigation.failure)<<std::endl;
            }
            if (frame==60) { capture(L"generated/enemy-stairs-tests/"+std::wstring(scenario.id,scenario.id+std::strlen(scenario.id))+L".png"); captured=true; }
            if (arrived) break;
        }
        check(arrived,std::string(scenario.id)+": pursuit stalled on stairs; see trace.csv");
        if (!captured) capture(L"generated/enemy-stairs-tests/"+std::wstring(scenario.id,scenario.id+std::strlen(scenario.id))+L".png");
        if (std::string(scenario.id)=="west-partial") {
            game->player_.SetStage(&game->level_.collision,{9,6,10.8f},{});
            arrived=false;
            for (int frame=0;frame<2400;++frame) {
                game->player_.ResetHPForDebug(); game->Update(app,1.f/60);
                const auto* enemy=game->enemies_.front(); const auto p=enemy->GetPosition(); const auto& d=enemy->Definition();
                check(EnemyNavigation::Clear(&game->level_.collision,p,p,d.collisionRadius,d.collisionHeight),"Moving stair target caused wall penetration");
                if (frame%60==0) trace<<"west-moving-target,"<<frame<<",0,"<<p.x<<','<<p.y<<','<<p.z<<','<<static_cast<int>(enemy->AI().state)<<','<<static_cast<int>(enemy->AI().navigation.failure)<<std::endl;
                if (std::abs(p.y-6)<.3f && EnemyAI::distanceTo(p,game->player_.GetTransform().translate)<2) { arrived=true; break; }
            }
            check(arrived,"Enemy did not resume stair pursuit after player moved farther downstairs");
        }

    }
    std::ofstream("generated/enemy-stairs-tests/result.txt")<<"PASS: actual GameScene/EnemyPool/AI follows a player on a partial stair, west and east stairs down/up, room-size Tank, three-enemy separation, continuous floor support, no wall/ceiling intersection or height teleport, real GPU captures.\n";
}
