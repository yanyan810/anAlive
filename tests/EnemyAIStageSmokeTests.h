#pragma once
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "ImGuiManagaer.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

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
    if (!game->level_.shutters.shutters.empty()) {
        auto& shutter=game->level_.shutters.shutters[0];
        check(game->level_.shutters.TryInteract(shutter.definition.buttonPosition),"shutter interaction failed");
        game->level_.shutters.Update(31,game->level_.collision); game->SyncShutterVisuals();
        check(shutter.state==ShutterState::Open,"shutter update failed"); render();
    }
    std::ofstream("generated/enemy-ai-tests/"+stage+"-smoke.txt")
        <<"PASS: "<<stage<<" current data loaded, 180 gameplay updates, shared enemy AI/parts/pool/spawns, finite transforms, real D3D rendering, shutter open when present. Full stage playthrough not tested.\n";
}
