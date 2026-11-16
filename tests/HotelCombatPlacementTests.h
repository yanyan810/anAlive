#pragma once
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "DirectXTex.h"
#include "ImGuiManagaer.h"
#include <fstream>

inline void RunHotelCombatPlacementTests(GameApp& app) {
    const auto check=[](bool ok,const std::string& why) { if (!ok) throw std::runtime_error(why); };
    app.Scenes().Register("HotelCombatTest",[] {
        auto scene=std::make_unique<GameScene>(false,"stage03"); scene->SetStartSequenceEnabled(false); return scene;
    });
    app.Scenes().Change(app,"HotelCombatTest");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->stageLoaded_,"Hotel combat stage failed to initialize");
    EnemyPlacements placements;
    check(placements.Load(EnemyPlacements::PathFor(game->LevelPath()),game->enemyDefinitions_,"stage03"),"Failed to read hotel placements");
    size_t count=0;
    for (const auto& [id,g]:placements.groups) {
        const auto* point=game->spawnSystem_.FindPoint(id); check(point!=nullptr,"Orphan placement: "+id);
        for (const auto& p:g.enemies) if (p.enabled) {
            const auto* d=game->enemyDefinitions_.Find(p.enemy); check(d!=nullptr,"Unknown enemy: "+p.enemy);
            const auto position=EnemyPlacements::WorldPosition(p,point->position,point->rotation);
            check(EnemyPlacements::InvalidReason(position,*d,game->level_.collision).empty(),"Blocked enemy placement: "+id+" / "+p.id);
            if (point->activation!=SpawnActivation::Manual) ++count;
        }
    }
    check(count==15 && placements.groups.size()==16,"Expected 7 room enemies, 1 tank and 7 hall enemies");
    check(game->enemies_.size()==15,"Expected all 15 room/hall enemies on stage start");
    size_t rooms=0,tanks=0;
    for (auto* enemy:game->enemies_) {
        enemy->SetAIPaused(true);
        if (enemy->Definition().type==EnemyType::Tank) { ++tanks; check(enemy->GetPosition().y==12 && enemy->Definition().collisionHeight<3,"Tank must fit the third-floor room"); }
        if (enemy->GetPosition().y==12) ++rooms;
        check(EnemyAI::distanceTo(enemy->GetPosition(),game->level_.playerPosition)>3,"Enemy inside respawn room");
    }
    check(rooms==8 && tanks==1,"Room enemy/tank count mismatch");
    // The stair-lobby doorway is sealed; the foyer entrance remains usable.
    StageHit closureHit;
    check(game->level_.collision.Raycast({9,7.5f,10},{1,0,0},6,closureHit) && closureHit.distance<3.2f,"Sealed lobby doorway does not block sight/shots");
    StageWorld::PlayerMotion closureMotion;
    const auto stopped=game->level_.collision.MovePlayer({9,6,10},{15,6,10},1,closureMotion);
    check(stopped.x<11.5f && stopped.y==6,"Player crossed sealed lobby doorway");
    closureMotion={};
    check(game->level_.collision.MovePlayer({15,6,10},{9,6,10},1,closureMotion).x>12.5f,"Player crossed sealed doorway from hall side");
    check(!EnemyNavigation::Clear(&game->level_.collision,{9,6,10},{15,6,10},.4f,2.1f),"Enemy crossed sealed lobby doorway");
    check(EnemyNavigation::Clear(&game->level_.collision,{9,6,26},{15,6,26},.4f,2.1f),"Foyer entrance was sealed too");
    EnemyNavigation detour;
    check(detour.Plan(&game->level_.collision,{9,6,10},{15,6,10},.4f,2.1f) && detour.path.size()>1,"Lobby closure left no enemy route through foyer");
    nlohmann::json roomLayout,roomTags;
    std::ifstream("resources/levels/stage03/stage03-layout.json")>>roomLayout;
    std::ifstream("resources/levels/stage03/editor_metadata.json")>>roomTags;
    for (const auto* e:game->enemies_) if (e->GetPosition().y==12) {
        const auto tag=roomTags["spawns"].at(e->GetSpawnTriggerId())["room"].get<std::string>();
        const int number=std::stoi(tag.substr(4)); Vector3 doorway{};
        for (const auto& room:roomLayout["rooms"]) if (room["number"]==number) doorway={room["door"][0].get<float>(),12,room["door"][1].get<float>()};
        const auto delta=doorway-e->GetPosition(); const float length=std::hypot(delta.x,delta.z),yaw=e->GetRotation().y;
        check((-std::cos(yaw)*delta.x+std::sin(yaw)*delta.z)/length>.999f,"3F enemy is not facing its corridor doorway");
    }

    check(game->weapons_.Points().size()==5,"Expected five weapon spawns");
    check(std::count_if(game->weapons_.Points().begin(),game->weapons_.Points().end(),[](const auto& p) { return p.id.starts_with("WP_F2_"); })==1,"Expected one second-floor weapon");
    for (const auto& point:game->weapons_.Points()) {
        const bool start=point.id=="WP_F3_308_Sub",floor2=point.id.starts_with("WP_F2_");
        check(point.filter.slot==(start ? WeaponSlot::Sub : WeaponSlot::Main),"Wrong weapon slot: "+point.id);
        check(point.filter.minRarity==(start ? 1 : floor2 ? 2 : 3) && point.filter.maxRarity==(start ? 2 : 4),"Wrong rarity filter: "+point.id);
        check(!point.weaponPool.empty(),"Empty filtered weapon pool");
        for (const auto& candidate:point.weaponPool) {
            const auto* d=game->weapons_.Find(candidate.id);
            check(d && point.filter.Matches(*d),"Out-of-filter weapon");
            if (start) check(d->infiniteReserveAmmo,"Starting Sub has finite reserve");
        }
        auto feet=point.position; feet.y-=.25f;
        check(EnemyNavigation::Clear(&game->level_.collision,feet,feet,.25f,.5f),"Blocked weapon: "+point.id);
    }
    check(game->player_.CurrentWeapon().Definition().id.empty(),"Game must start unarmed");
    auto unarmed=game->player_.CurrentWeapon();
    check(unarmed.Step(1,true,true,true)==0 && !unarmed.StartReload(),"Unarmed player can fire/reload");
    const auto capture=[&](const wchar_t* name) {
        game->player_.RefreshVisuals(); game->ground_.Update(0); game->SyncShutterVisuals(); game->SyncWeaponVisuals(app);
        for (auto* e:game->enemies_) e->UpdateVisuals(0);
        app.ImGui()->Begin(); app.Draw();
        auto* dx=app.Dx(); dx->PreDraw(false); app.Render()->GetOffscreen()->BeginForPostEffect();
        game->DrawOverlay2D(app); app.Render()->GetOffscreen()->End(); dx->SetBackBufferRenderTarget(); dx->PostDraw(); dx->WaitForGPU();
        DirectX::ScratchImage pixels; constexpr auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        check(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),app.Render()->GetOffscreen()->GetResource(),false,pixels,state,state)),"GPU readback failed");
        check(SUCCEEDED(DirectX::SaveToWICFile(*pixels.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),name)),"PNG save failed");
    };
    capture(L"generated/hotel-combat-tests/00-unarmed-start.png");
    check(game->player_.TryPickupWeapon(game->weapons_) && game->player_.CurrentWeapon().InfiniteReserveAmmo(),"Starting Sub cannot be picked up from respawn");
    check(game->weapons_.Pickups().size()==5,"Unarmed pickup created a phantom dropped weapon");
    auto firedSub=game->player_.CurrentWeapon();
    check(firedSub.TryFire(),"Starting Sub fixture shot failed");
    game->player_.RestoreWeaponForDebug(firedSub);
    check(game->HandleInteraction(true,true),"Same weapon pickup failed");
    const auto& refilledSub=game->player_.CurrentWeapon();
    check(refilledSub.Definition().id==firedSub.Definition().id && refilledSub.Magazine()==firedSub.Definition().magazineSize &&
        refilledSub.Cooldown()==firedSub.Cooldown() && game->weapons_.Pickups().size()==5,"Same weapon pickup changed weapon or created a drop");
    capture(L"generated/hotel-combat-tests/01-respawn-sub.png");
    game->player_.SetStage(&game->level_.collision,{9,6,10},{0,1.5707963f,0});
    capture(L"generated/hotel-combat-tests/01-sealed-stair-lobby.png");
    game->player_.SetStage(&game->level_.collision,{28,12,6},{0,3.14159265f,0});
    capture(L"generated/hotel-combat-tests/02-room304-tank.png");
    game->player_.SetStage(&game->level_.collision,{9,6,18},{0,1.5707963f,0});
    game->initialCapturePending_=false; app.GetInput()->SetCameraControlEnabled(false);
    game->Update(app,1.f/60); check(game->enemies_.size()==15,"Unexpected foyer spawn");
    capture(L"generated/hotel-combat-tests/02-foyer-weapons.png");
    // Production interaction: a reusable source exchanges a worn weapon onto
    // the actual foyer floor, renders it, and restores its exact ammunition.
    WeaponRuntime worn; check(worn.Equip(*game->weapons_.Find("rifle")),"Missing rifle");
    for (int i=0;i<5;++i) { check(worn.TryFire(),"Fixture shot failed"); worn.Update(1); }
    check(worn.StartReload(),"Fixture reload failed"); worn.Update(100);
    for (int i=0;i<3;++i) { check(worn.TryFire(),"Fixture second shot failed"); worn.Update(1); }
    check(worn.StartReload(),"Fixture partial reload failed");
    const int magazine=worn.Magazine(),reserve=worn.Reserve();
    game->player_.RestoreWeaponForDebug(worn);
    check(game->HandleInteraction(true,true),"Foyer source interaction failed");
    game->SyncWeaponVisuals(app);
    check(game->weapons_.Pickups().size()==6 && game->weaponVisuals_.size()>=6,"Exchanged weapon has no ground visual");
    const auto dropped=game->weapons_.Pickups().back();
    check(dropped.savedWeapon && dropped.weaponId=="rifle" && dropped.savedWeapon->Magazine()==magazine && dropped.savedWeapon->Reserve()==reserve,"Dropped rifle lost ammunition");
    check(!dropped.savedWeapon->Reloading() && dropped.position.y>6 && dropped.position.y<6.5f,"Dropped rifle reload continued or floor height wrong");
    game->player_.SetStage(&game->level_.collision,{dropped.position.x,6,dropped.position.z},{});
    check(game->HandleInteraction(true,true),"Dropped rifle cannot be picked up again");
    game->SyncWeaponVisuals(app);
    check(game->player_.CurrentWeapon().Definition().id=="rifle" && game->player_.CurrentWeapon().Magazine()==magazine && game->player_.CurrentWeapon().Reserve()==reserve,"Production pickup refilled worn rifle");
    check(game->weapons_.Pickups().size()==6,"Ground exchange duplicated a dropped item");
    game->player_.SetStage(&game->level_.collision,{9,6,18},{});
    const auto nearest=game->weapons_.Nearest(game->player_.GetTransform().translate);
    check(nearest && !game->weapons_.Pickups()[*nearest].savedWeapon && game->HandleInteraction(true,true),"Foyer source cannot be reused");
    game->SyncWeaponVisuals(app);
    game->player_.SetStage(&game->level_.collision,{10,6,21},{.65f,3.14159265f,0});
    capture(L"generated/hotel-combat-tests/02-foyer-exchanged-weapons.png");
    game->weapons_.ResetPickups(); game->SyncWeaponVisuals(app);
    check(game->weapons_.Pickups().size()==5,"Pickup reset retained loose weapons");
    game->player_.SetStage(&game->level_.collision,{13,6,18},{0,1.5707963f,0});
    game->Update(app,1.f/60);
    check(game->enemies_.size()==15,"Entering hall duplicated initial enemies");
    for (auto* e:game->enemies_) e->SetAIPaused(true);
    capture(L"generated/hotel-combat-tests/03-ballroom-7.png");
    game->player_.SetStage(&game->level_.collision,{9,6,18},{}); game->Update(app,1.f/60);
    game->player_.SetStage(&game->level_.collision,{13,6,18},{}); game->Update(app,1.f/60);
    check(game->enemies_.size()==15,"Hall trigger repeated on reentry");
    game->player_.SetStage(&game->level_.collision,{18,0,18},{0,-1.5707963f,0});
    capture(L"generated/hotel-combat-tests/04-shutter-weapons.png");
    // A nearby ally receives the discovery; a wall or distance must block it.
    std::vector<Enemy*> hall; std::vector<EnemySenses> originalSenses; std::vector<Vector3> originalPositions;
    for (auto* e:game->enemies_) if (e->GetSpawnTriggerId()=="SP_F2_Ballroom") {
        hall.push_back(e); originalSenses.push_back(e->Senses()); originalPositions.push_back(e->GetPosition());
        e->ResetAI(); auto senses=e->Senses(); senses.viewDistance=0; e->ConfigureSenses(senses); e->SetAIPaused(false);
    }
    check(hall.size()==7,"Missing hall alert members");
    auto observerSenses=originalSenses.front(); observerSenses.viewDistance=6;
    hall.front()->ConfigureSenses(observerSenses);
    hall.front()->SetPosition({15,6,14}); hall.front()->SetRotation({});
    hall[1]->SetPosition({18,6,14}); // Near, same side of the west wall.
    hall[2]->SetPosition({24,6,14}); // Outside the observer's six-metre radius.
    hall[3]->SetPosition({9,6,14});  // Nearby but across the solid west wall.
    for (size_t i=0;i<4;++i) check(EnemyNavigation::Clear(&game->level_.collision,hall[i]->GetPosition(),hall[i]->GetPosition(),hall[i]->Definition().collisionRadius,hall[i]->Definition().collisionHeight),"Alert fixture inside geometry");
    game->player_.SetStage(&game->level_.collision,{13,6,14},{}); game->initialCapturePending_=false;
    app.GetInput()->SetCameraControlEnabled(false);
    game->Update(app,.2f); check(!hall.front()->AI().pursuitOrdered && !hall[1]->AI().pursuitOrdered,"Alert bypassed detection time");
    game->Update(app,.2f);
    check(hall.front()->AI().pursuitOrdered && hall[1]->AI().pursuitOrdered,"Discovery did not persist for observer/nearby ally");
    check(!hall[1]->AI().visible,"Near ally had direct sight despite zero view distance");
    check(!hall[2]->AI().pursuitOrdered,"Discovery escaped observer radius");
    check(!hall[3]->AI().pursuitOrdered,"Discovery crossed west wall");
    const auto beforeBlind=hall[1]->GetPosition(); game->Update(app,.1f);
    check(StageLength(hall[1]->GetPosition()-beforeBlind)>.01f,"Nearby alerted ally did not move");
    // Remove all new direct sightings to isolate persistence and prevent hidden relays.
    observerSenses.viewDistance=0; hall.front()->ConfigureSenses(observerSenses);
    game->player_.SetStage(&game->level_.collision,{9,6,18},{});
    for (int frame=0;frame<70;++frame) {
        game->player_.ResetHPForDebug(); game->Update(app,.1f);
        for (size_t i:{size_t{0},size_t{1}}) {
            const auto* e=hall[i];
            check(e->AI().pursuitOrdered && StageLength(e->AI().lastSeen-game->player_.GetTransform().translate)<.001f && e->AI().state!=EnemyState::Search,"Alerted enemy stopped pursuit after losing sight");
            check(EnemyNavigation::Clear(&game->level_.collision,e->GetPosition(),e->GetPosition(),e->Definition().collisionRadius,e->Definition().collisionHeight),"Alerted enemy crossed hotel wall");
        }
        check(!hall[2]->AI().pursuitOrdered && !hall[3]->AI().pursuitOrdered,"Hidden alerted ally relayed discovery through walls/range");
    }
    for (auto* e:game->enemies_) if (e->GetPosition().y==12) check(!e->AI().pursuitOrdered,"Hall alert crossed the floor into 3F");
    for (size_t i=0;i<hall.size();++i) { hall[i]->ConfigureSenses(originalSenses[i]); hall[i]->SetPosition(originalPositions[i]); hall[i]->SetAIPaused(true); }
    std::ofstream("generated/hotel-combat-tests/pursuit.txt")<<"PASS: discovery persists for observer and nearby unobstructed ally, outside-radius and wall-separated allies stay idle, no hidden relay, 7s pursuit after sight loss, no hotel wall penetration, 3F isolation.\n";
    // Full production shutter timeline: six batches, including the 60-second batch.
    auto& shutter=game->level_.shutters.shutters[0];
    check(shutter.definition.openDuration==60 && shutter.definition.enemyWaveInterval==10,"Wrong shutter timing");
    game->player_.SetStage(&game->level_.collision,{14,0,21.5f},{});
    game->initialCapturePending_=false; app.GetInput()->SetCameraControlEnabled(false);
    const auto tick=[&](float dt) {
        game->player_.ResetHPForDebug(); game->Update(app,dt);
        for (auto* e:game->enemies_) e->SetAIPaused(true);
    };
    tick(20); check(game->enemies_.size()==15 && shutter.state==ShutterState::Closed,"Waves started before switch");
    game->spawnSystem_.SetSeed(12345);
    check(game->level_.shutters.TryInteract(game->player_.GetTransform().translate),"Shutter interaction failed");
    game->ProcessShutterEvents();
    tick(9.5f); check(game->enemies_.size()==15 && shutter.state==ShutterState::Opening,"Wave before 10 seconds");
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    game->debugPaused_=true; tick(10); game->debugPaused_=false;
    check(game->enemies_.size()==15 && shutter.elapsedTime==9.5,"Debug pause advanced wave timer");
#endif
    std::set<std::string> specialTypes;
    std::ofstream waveTrace("generated/hotel-combat-tests/waves.txt");
    for (int wave=1;wave<=6;++wave) {
        const size_t before=game->enemies_.size(); tick(wave==1 ? .5f : 10.f);
        check(game->enemies_.size()==before+7,"Wave did not contain exactly seven enemies");
        size_t normal=0,special=0;
        for (size_t i=before;i<game->enemies_.size();++i) {
            const auto* e=game->enemies_[i]; check(e->GetPosition().y==0,"Wave spawned outside first floor");
            check(e->AI().pursuitOrdered && e->AI().hasLastSeen && StageLength(e->AI().lastSeen-game->player_.GetTransform().translate)<.001f,"Reinforcement did not pursue immediately on spawn");
            if (e->Definition().type==EnemyType::Normal) ++normal;
            else { ++special; specialTypes.insert(e->Definition().id); waveTrace<<wave*10<<"s: "<<e->Definition().id<<"\n"; }
        }
        check(normal==6 && special==1,"Wave composition must be 6 Normal and 1 random non-Normal");
        if (wave<6) check(shutter.state==ShutterState::Opening,"Shutter opened before 60 seconds");
        if (wave==1) {
            auto* chasing=game->enemies_[before]; const auto start=chasing->GetPosition();
            chasing->SetAIPaused(false);
            for (int frame=0;frame<60;++frame) chasing->Update(1.f/60,game->player_.GetTransform().translate,&game->level_.collision,game->player_.Settings().cameraHeight);
            check(StageLength(chasing->GetPosition()-start)>.1f,"Spawned reinforcement did not leave its spawn toward player");
            chasing->SetAIPaused(true);
            capture(L"generated/hotel-combat-tests/05-shutter-first-wave.png");
        }
    }
    check(shutter.state==ShutterState::Open && shutter.elapsedTime==60 && game->enemies_.size()==57,"Final opening/wave count mismatch");
    check(specialTypes.size()>1,"Random special slot never varied with fixed test seed");
    tick(120); check(game->enemies_.size()==57,"Waves continued after full opening");
    capture(L"generated/hotel-combat-tests/06-shutter-fully-open.png");
    // A new scene resets timers, living counts and wave progress. A long frame catches up all six waves.
    app.Scenes().Change(app,"HotelCombatTest"); game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->player_.CurrentWeapon().Definition().id.empty(),"Restart retained the equipped weapon");
    check(game && game->enemies_.size()==15 && game->level_.shutters.shutters[0].state==ShutterState::Closed,
        "Restart state: scene="+app.Scenes().CurrentName()+" count="+(game ? std::to_string(game->enemies_.size()) : "null")+
        " state="+(game && !game->level_.shutters.shutters.empty() ? std::to_string(static_cast<int>(game->level_.shutters.shutters[0].state)) : "none")+
        " srv="+std::to_string(app.Srv()->ActiveCount())+" high="+std::to_string(app.Srv()->HighWaterMark()));
    for (auto* e:game->enemies_) { check(!e->AI().pursuitOrdered,"Restart retained pursuit state"); e->SetAIPaused(true); }
    game->player_.SetStage(&game->level_.collision,{14,0,21.5f},{}); game->initialCapturePending_=false;
    check(game->level_.shutters.TryInteract(game->player_.GetTransform().translate),"Restart switch failed");
    tick(60); check(game->enemies_.size()==57 && game->level_.shutters.shutters[0].state==ShutterState::Open,"Large-dt wave catch-up lost a batch");
    std::ofstream("generated/hotel-combat-tests/result.txt")<<"PASS: 3F doorway facing and room-sized Tank, local radius/LOS alert with blocked/out-of-range allies excluded and persistent pursuit, immediate reinforcement pursuit, 7 hall enemies on stage start, unarmed start/retry and first Sub pickup, five source weapons with one on 2F, reusable pickup and rendered exchange with exact ammunition, all initial geometry/weapon filters, 60-second shutter, no pre-switch waves, 6x7 first-floor reinforcements at 10-60s (6 Normal + 1 random non-Normal), pause/stop/restart and large-dt catch-up, real GameScene/EnemyPool/GPU captures.\n";
}
