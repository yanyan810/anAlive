#pragma once
#include "UpperHotelRouteChecks.h"
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "DirectXCommon.h"
#include "ImGuiManagaer.h"
#include "RenderManager.h"
#include "DirectXTex.h"

inline void RunUpperHotelRuntimeTests(GameApp& app) {
    using UpperHotelChecks::Require;
    app.Scenes().Change(app,"Stage04");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->stageLoaded_ && game->doorVisuals_.size()==30,"Stage04 GameScene loading");
    Require(game->removableMeshBindings_.size()==33 && std::all_of(game->removableMeshBindings_.begin(),game->removableMeshBindings_.end(),
        [](const auto& meshes) { return !meshes.empty(); }),
        "breakable window mesh bindings");
    Require(game->enemies_.size()==8,"All eight non-defense enemies must spawn at StageStart");
    for (const auto& point : game->spawnSystem_.Points()) {
        const bool defense=point.id.starts_with("F1_Defense_");
        Require(point.activation==(defense ? SpawnActivation::Manual : SpawnActivation::StageStart),
            "unexpected spawn activation: "+point.id);
    }
    const auto capture=[&](const char* name,const Vector3& feet,const Vector3& rotation) {
        game->player_.SetStage(&game->level_.collision,feet,rotation);
        game->player_.RefreshVisuals();
        game->ground_.Update(0);
        game->SyncDoorVisuals();
        game->SyncShutterVisuals();
        game->SyncWeaponVisuals(app);
        for (auto* enemy : game->enemies_) enemy->UpdateVisuals(0);
#ifdef USE_IMGUI
        app.ImGui()->Begin();
#endif
        app.Draw();
        app.Dx()->PreDraw(false);
        app.Render()->GetOffscreen()->BeginForPostEffect();
        game->DrawOverlay2D(app);
        app.Render()->GetOffscreen()->End();
        app.Dx()->SetBackBufferRenderTarget();
        app.Dx()->PostDraw();
        app.Dx()->WaitForGPU();
        DirectX::ScratchImage image;
        const auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Require(SUCCEEDED(DirectX::CaptureTexture(app.Dx()->GetCommandQueue(),
            app.Render()->GetOffscreen()->GetResource(),false,image,state,state)),"Capture upper hotel");
        const auto path=std::filesystem::path("generated/upper-hotel-tests")/(std::string(name)+".png");
        Require(SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str())),"Save upper hotel capture");
    };
    const auto shootWindow=[&](std::string_view name,const Vector3& feet,float yaw) {
        const auto& windows=game->level_.removableMeshes;
        const auto window=std::find_if(windows.begin(),windows.end(),
            [&](const auto& entry) { return entry.meshNode==name; });
        Require(window!=windows.end(),"missing breakable window: "+std::string(name));
        auto& collider=game->level_.collision.colliders[window->colliderIndex];
        Require(collider.enabled,"pane was already broken");
        game->player_.SetStage(&game->level_.collision,feet,{0,yaw,0});
        game->player_.RefreshVisuals();
        auto weapon=game->player_.CurrentWeapon().Definition();
        weapon.hipSpreadDegrees=weapon.adsSpreadDegrees=0;
        game->bullets_.Spawn(weapon,game->camera_.GetWorldMatrix(),0,game->pelletRandom_,game->level_.collision,game->enemies_);
        Require(collider.enabled,"aiming or bullet spawn removed glass before impact");
        game->UpdatePlayerBullets(.1f);
        Require(!collider.enabled,"player bullet did not break "+std::string(name));
        const auto& hidden=game->ground_.GetHiddenMeshes();
        const auto binding=static_cast<size_t>(window-windows.begin());
        Require(std::find(hidden.begin(),hidden.end(),game->removableMeshBindings_[binding].front())!=hidden.end(),
            "broken pane still rendered");
    };
    const auto countEnemies=[&](std::string_view source) {
        return std::count_if(game->enemies_.begin(),game->enemies_.end(),[&](const Enemy* enemy) {
            return enemy->GetSpawnTriggerId().starts_with(source);
        });
    };
    const auto breachHidden=[&](std::string_view name) {
        const auto& meshes=game->level_.removableMeshes;
        const auto mesh=std::find_if(meshes.begin(),meshes.end(),[&](const auto& item) { return item.meshNode==name; });
        Require(mesh!=meshes.end() && !mesh->breakOnBullet,"missing event-only breach mesh");
        const auto binding=static_cast<size_t>(mesh-meshes.begin());
        const auto& hidden=game->ground_.GetHiddenMeshes();
        return std::find(hidden.begin(),hidden.end(),game->removableMeshBindings_[binding].front())!=hidden.end();
    };
    const Vector3 startDoor{6.35f,UpperHotelChecks::Upper,36.5f};
    capture("01-start-door",startDoor,{0,1.5708f,0});
    UpperHotelChecks::WalkRoute(game->level_,game->stage_,[&](const Vector3& feet,const std::string& name) {
        game->player_.SetStage(&game->level_.collision,feet,{});
        const auto index=game->level_.doors.Nearest(game->level_.collision,feet);
        Require(index && game->level_.doors.doors[*index].closed.id==name,"E target "+name);
        auto& door=game->level_.doors.doors[*index];
        game->HandleInteraction(false,true);
        Require(!door.targetOpen,"door moved before E");
        game->HandleInteraction(true,true);
        for (int i=0;i<30;++i) game->UpdateDoors(1.f/60);
        Require(door.targetOpen && door.angle==door.openAngle,"E opened "+name);
    },[&](std::string_view part,const Vector3& feet) {
        game->player_.SetStage(&game->level_.collision,feet,{});
        game->UpdateEnemySpawns(1.f/60);
        if (part=="item") {
            Require(game->HandleInteraction(true,true),"E failed to pick up the starter Sub");
            shootWindow("F5_Room516_WindowGlass",feet,1.5707963f);
            capture("07-fifth-floor-lounge",{14.5f,UpperHotelChecks::Upper,38},{0,-2.9f,0});
        } else if (part=="corner") {
            Require(game->enemies_.size()==8 && countEnemies("F5_")==3,"5F enemies are present before the corner");
            capture("02-fifth-floor-corner",feet,{0,1.5708f,0});
        } else if (part=="arrival") {
            Require(game->enemies_.size()==8 && countEnemies("F4_")==1,"4F room enemy is present before arrival");
            capture("03-fourth-floor-fire",{42,UpperHotelChecks::Lower,9.5f},{0,-1.5708f,0});
        } else if (part=="detour") {
            capture("08-fifth-floor-room-detour",feet,{0,1.5708f,0});
        } else if (part=="ledge") {
            capture("04-exterior-ledge",feet,{.18f,-1.75f,0});
        } else if (part=="ledge-extension") {
            capture("09-fourth-floor-ledge-extension",feet,{.12f,1.75f,0});
        } else if (part=="elevator") {
            capture("10-fourth-floor-elevator",feet,{});
        } else if (part=="transfer") {
            Require(countEnemies("F1_ArrivalEnemy_")==4 && countEnemies("F1_Defense_")==0 &&
                !game->transferUsed_,"lobby enemies exist before transfer; defense has not started");
            const auto weapon=game->player_.CurrentWeapon();
            game->player_.ApplyDamage(8);
            const auto hp=game->player_.GetHP();
            game->Update(app,1.f/60);
            const auto& equipped=game->player_.CurrentWeapon();
            Require(game->transferUsed_ && game->stage_.IsPlaying() &&
                StageLength(game->player_.GetTransform().translate-game->level_.transfer->arrivalPosition)<.01f,
                "entering 4F cabin transfers to 1F");
            Require(equipped.Definition().id==weapon.Definition().id && equipped.Magazine()==weapon.Magazine() &&
                equipped.Reserve()==weapon.Reserve() && game->player_.GetHP()==hp,"transfer must retain inventory and HP");
            Require(countEnemies("F1_ArrivalEnemy_")==4 && countEnemies("F1_Defense_")==0,
                "1F arrival must not spawn a duplicate group");
            for (const auto* enemy : game->enemies_)
                Require(enemy->AI().paused==!enemy->GetSpawnTriggerId().starts_with("F1_"),
                    "only enemies left upstairs are paused after transfer");
            capture("17-first-floor-elevator-arrival",game->level_.transfer->arrivalPosition,
                game->level_.transfer->arrivalRotation);
        } else if (part=="lobby") {
            capture("11-first-floor-lobby",feet,{0,1.32f,0});
        } else if (part=="stair-collapse") {
            capture("15-fourth-floor-stair-collapse",{50.5f,UpperHotelChecks::Lower,10.6f},{.12f,3.14159f,0});
        } else if (part=="stairwell") {
            capture("16-first-floor-stairwell",feet,{-.16f,0,0});
        } else if (part=="regular") {
            Require(countEnemies("F1_Defense_")==3 && countEnemies("F1_Defense_Breach")==0,
                "first wave comes only from three regular sources");
            const auto stair=std::find_if(game->enemies_.begin(),game->enemies_.end(),[](const Enemy* enemy) {
                return enemy->GetSpawnTriggerId()=="F1_Defense_Stairs";
            });
            Require(stair!=game->enemies_.end() && std::abs((*stair)->GetPosition().x-50.5f)<.02f,
                "stair reinforcements spawn on the connected bottom flight");
            capture("12-first-floor-shutter",{42,.08f,16.5f},{0,1.5708f,0});
        } else if (part=="breachA") {
            Require(countEnemies("F1_Defense_BreachA")==1 && countEnemies("F1_Defense_BreachB")==0 &&
                breachHidden("F1_BreachA_Panel") && !breachHidden("F1_BreachB_Panel"),"A opening and spawn at 12s");
            capture("13-first-floor-breach-a",{23,.08f,9},{0,3.14159f,0});
        } else if (part=="breachB") {
            Require(countEnemies("F1_Defense_BreachB")==1 && countEnemies("F1_Defense_BreachC")==0 &&
                breachHidden("F1_BreachB_Panel") && !breachHidden("F1_BreachC_Panel"),"B opening and spawn at 20s");
        } else if (part=="breachC") {
            Require(countEnemies("F1_Defense_BreachC")==1 && breachHidden("F1_BreachC_Panel"),"C opening and spawn at 26s");
        } else if (part=="lastWave") {
            for (const auto* name : {"Stairs","Staff","Side"})
                Require(countEnemies(std::string("F1_Defense_")+name)==4,"regular source count");
            Require(countEnemies("F1_Defense_BreachA")==3 && countEnemies("F1_Defense_BreachB")==2 &&
                countEnemies("F1_Defense_BreachC")==2 && countEnemies("F1_Defense_")==19,"all six sources in final wave");
        } else if (part=="open") {
            capture("14-first-floor-exit-open",{42,.08f,16.5f},{0,1.5708f,0});
        } else if (part=="goal") {
            game->Update(app,1.f/60);
            Require(game->stage_.State()==StageState::Cleared && countEnemies("F1_Defense_")==19,
                "walk-through goal without another encounter or a kill requirement");
        } else if (part.starts_with("window")) {
            const bool finalPane=part=="window403";
            const auto name="F4_Room"+std::string(part.substr(6))+"_WindowGlass";
            if (finalPane) capture("05-window-intact",feet,{});
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
            if (part=="window405") {
                const auto intact=game->CaptureDebug();
                const auto previouslyHidden=game->ground_.GetHiddenMeshes();
                shootWindow(name,feet,0);
                game->RestoreDebug(app,intact);
                Require(game->ground_.GetHiddenMeshes()==previouslyHidden,"rewind restores the hit pane");
            }
#endif
            shootWindow(name,feet,0);
            if (finalPane) capture("06-window-broken",feet,{});
        }
    },[&](const Vector3& feet) {
        game->player_.SetStage(&game->level_.collision,feet,{});
        game->HandleInteraction(true,true);
        game->ProcessShutterEvents();
    },[&]() { game->ProcessShutterEvents(); });
    Require(game->stage_.State()==StageState::Cleared,"1F final goal");
    game->player_.SetStage(&game->level_.collision,startDoor,{});
    const auto index=game->level_.doors.Nearest(game->level_.collision,startDoor);
    Require(index.has_value(),"open door must still be reachable");
    game->HandleInteraction(true,true);
    for (int i=0;i<30;++i) game->UpdateDoors(1.f/60);
    Require(!game->level_.doors.doors[*index].targetOpen && game->level_.doors.doors[*index].angle==0,"E closes door");

    const auto fallToDeath=[&](const Vector3& from) {
        game->stage_.Reset();
        game->player_.SetStage(&game->level_.collision,from,{});
        for (int i=0;i<90 && !game->gameOver_.Active();++i) game->Update(app,1.f/60);
        Require(game->player_.IsDead() && game->gameOver_.Active(),"fall must allow a retry");
    };
    fallToDeath({11,UpperHotelChecks::Upper,-.83f});
    Require(game->player_.GetTransform().translate.y>UpperHotelChecks::Lower,"5F fall only triggered below 4F");

    app.Scenes().Change(app,game->SceneName());
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    Require(game && game->stageLoaded_ && game->stage_.IsPlaying() && game->enemies_.size()==8 &&
        StageLength(game->player_.GetTransform().translate-game->level_.playerPosition)<.001f &&
        game->weapons_.Pickups().size()==1 && game->weapons_.Pickups()[0].visible,"Restart resets upper hotel");
    for (const auto& door : game->level_.doors.doors)
        Require(door.targetOpen==door.initiallyOpen && door.angle==(door.initiallyOpen ? door.openAngle : 0),
            "Restart resets authored state for "+door.closed.id);
    for (const auto& mesh : game->level_.removableMeshes)
        Require(game->level_.collision.colliders[mesh.colliderIndex].enabled,"Restart restores glass and breach collision");
    Require(game->ground_.GetHiddenMeshes().empty(),"Restart restores glass meshes");
    Require(!game->transferUsed_ && game->level_.shutters.shutters.front().state==ShutterState::Closed &&
        countEnemies("F1_ArrivalEnemy_")==4 && countEnemies("F1_Defense_")==0,
        "Restart restores the initial lobby enemies and resets defense");
    const Vector3 outsideLedge{11,UpperHotelChecks::Lower,-2.2f};
    game->ConfigurePlacementTest(EnemyPlacements{});
    game->player_.SetStage(&game->level_.collision,outsideLedge,{});
    for (int i=0;i<90 && !game->PlacementTestFinished();++i) game->Update(app,1.f/60);
    Require(game->player_.IsDead() && game->PlacementTestFinished(),"F7 test play must end after a fatal fall");
    const auto stopped=game->player_.GetTransform().translate;
    game->Update(app,1.f/60);
    Require(StageLength(game->player_.GetTransform().translate-stopped)==0,"F7 test continued after fall death");
    game->placementTest_.reset();
    game->player_.ResetHPForDebug();
    fallToDeath(outsideLedge);
    std::ofstream("generated/upper-hotel-tests/result.txt")<<
        "PASS: Stage04 eight initial enemies on 5F/4F/1F, active lobby enemies after transfer, shutter-only waves, six ingress routes, straight exit, restart.\n";
}
