#pragma once
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "ImGuiManagaer.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>

inline void RunEnemyAIStageSmokeTests(GameApp& app,const std::string& stage) {
    const auto check=[](bool ok,const char* text) { if (!ok) throw std::runtime_error(text); };
    app.Scenes().Register("EnemyAIStageSmoke",[stage] { return std::make_unique<GameScene>(false,stage); });
    app.Scenes().Change(app,"EnemyAIStageSmoke");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->stageLoaded_ && game->level_.id==stage,"stage failed to initialize");
    game->initialCapturePending_=false; app.GetInput()->SetCameraControlEnabled(false);
    const auto render=[&]() {
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
    };
    for (int frame=0;frame<180;++frame) {
        game->player_.ResetHPForDebug(); game->Update(app,1.0f/60);
        for (auto* e:game->enemies_) {
            const auto p=e->GetPosition();
            check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),"nonfinite enemy position");
            check(!e->AI().hasLastSeen || std::isfinite(e->AI().lastSeen.x),"nonfinite sight memory");
        }
        if (frame%60==0) render();
    }
    if (stage=="stage03") {
        // Exercise the production scene's separation, wall sliding and replanning
        // with three enemies sharing the hotel doorway, then a player turning away.
        std::vector<Enemy*> doorwayEnemies;
        for (auto* e:game->enemies_) {
            e->SetAIPaused(true);
            if (e->GetSpawnTriggerId()=="enemySpawn") doorwayEnemies.push_back(e);
        }
        check(doorwayEnemies.size()==3,"hotel doorway fixture requires three normal enemies");
        const Vector3 starts[]={{27.4185f,12,18.5f},{27.6185f,12,18.5f},{29.336f,12,18.5f}};
        for (size_t i=0;i<doorwayEnemies.size();++i) {
            auto* e=doorwayEnemies[i]; e->ResetAI(); e->SetAIPaused(false);
            e->SetPosition(starts[i]); e->SetRotation({0,-1.57079632679f,0});
        }
        game->player_.SetMovementEnabled(false);
        const auto follow=[&](const Vector3& target,int frames) {
            game->player_.SetStage(&game->level_.collision,target,{}); game->player_.RefreshVisuals();
            for (int frame=0;frame<frames;++frame) {
                game->player_.ResetHPForDebug(); game->Update(app,1.0f/60);
                for (auto* e:doorwayEnemies) {
                    const auto& d=e->Definition();
                    if (!EnemyNavigation::Clear(&game->level_.collision,e->GetPosition(),e->GetPosition(),d.collisionRadius,d.collisionHeight)) {
                        const auto p=e->GetPosition(); std::ostringstream reason;
                        reason<<"production separation invalid: "<<e->GetId()<<" frame="<<frame<<" position="<<p.x<<','<<p.y<<','<<p.z;
                        for (const auto& collider:game->level_.collision.colliders) {
                            StageWorld one; one.colliders.push_back(collider);
                            if (!EnemyNavigation::Clear(&one,p,p,d.collisionRadius,d.collisionHeight)) reason<<" collider="<<collider.id;
                        }
                        throw std::runtime_error(reason.str());
                    }
                }
                if (frame%120==0) render();
            }
        };
        follow({28,12,12},60);
        follow({29,12,11.5f},600);
        for (auto* e:doorwayEnemies)
            check(e->AI().hasLastSeen && e->GetPosition().z<12.8f,"production doorway chase stalled");
        follow({32,12,11.5f},600);
        for (auto* e:doorwayEnemies)
            check(EnemyAI::distanceTo(e->GetPosition(),{32,12,11.5f})<2,"production chase stopped at corridor corner");
    }
    if (!game->level_.shutters.shutters.empty()) {
        auto& shutter=game->level_.shutters.shutters[0];
        check(game->level_.shutters.TryInteract(shutter.definition.buttonPosition),"shutter interaction failed");
        game->level_.shutters.Update(31,game->level_.collision); game->SyncShutterVisuals();
        check(shutter.state==ShutterState::Open,"shutter update failed"); render();
    }
    std::ofstream("generated/enemy-ai-tests/"+stage+"-smoke.txt")
        <<"PASS: "<<stage<<" current data loaded, gameplay updates, shared enemy AI/parts/pool/spawns, finite transforms, real D3D rendering, shutter open when present. "
        <<(stage=="stage03" ? "Three-enemy doorway separation and corridor corner pursuit passed. " : "")
        <<"Full stage playthrough not tested.\n";
}
