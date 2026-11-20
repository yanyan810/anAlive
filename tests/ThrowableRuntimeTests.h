#pragma once
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include "scene/Main/GameScene.h"
#include <fstream>
#include <stdexcept>

inline void RunThrowableRuntimeTests(GameApp& app) {
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    const auto close=[](float a,float b) { return std::abs(a-b)<.005f; };
    const auto aim=[](const Vector3& eye,const Vector3& forward) {
        auto camera=Matrix4x4::MakeIdentity4x4();
        camera.m[3][0]=eye.x; camera.m[3][1]=eye.y; camera.m[3][2]=eye.z;
        camera.m[2][0]=forward.x; camera.m[2][1]=forward.y; camera.m[2][2]=forward.z;
        return camera;
    };
    const auto addBox=[](StageWorld& world,const Vector3& center,const Vector3& halfSize) {
        StageCollider box;
        box.position=center; box.scale=halfSize;
        box.local={{-1,-1,-1},{1,1,1}};
        box.UpdateMatrices();
        world.colliders.push_back(box);
    };
    const auto place=[](Enemy& enemy,const Vector3& pos) {
        enemy.SetPosition(pos);
        enemy.UpdateVisuals(0);
    };
    const auto health=[](const Enemy::DebugState& state) {
        float hp=0;
        for (const auto& p:state.parts) if (p.usesLocalHp) hp+=p.hp;
        for (const auto& g:state.parts.hpGroups) hp+=g.hp;
        return hp;
    };
    constexpr auto definitions="resources/Data/throwables.json";
    for (const auto* id:{"stage01","stage02","stage03"}) {
        const auto path=std::string("resources/levels/")+id+"/"+id+".json";
        StageLoader level;
        WeaponSystem guns;
        ThrowableSystem items;
        check(level.Load(path) && guns.Load("resources/Data/weapons.json",path),"campaign stage load");
        check(items.Load(definitions,path) && items.Pickups().size()==3 && items.Count()>0,"campaign throwable config");
        for (const auto& pickup:items.Pickups()) {
            StageHit floor;
            check(level.collision.Raycast(pickup.position,{0,-1,0},.5f,floor) && floor.normal.y>.55f,
                "campaign pickup floor support");
            const auto feet=pickup.position-Vector3{0,floor.distance,0};
            check(!guns.Nearest(feet),"campaign pickup overlaps weapon interaction");
            items.Throw(aim(pickup.position,{0,0,1}));
            check(items.TryPickup(feet,level.collision),"campaign pickup inaccessible");
        }
        items.Reset();
        check(items.GetState().flights.empty() && items.GetState().effects.empty() &&
            std::none_of(items.GetState().consumed.begin(),items.GetState().consumed.end(),[](bool used){return used;}),
            "campaign throwable reset");
    }

    app.Scenes().Change(app,"Showroom");
    auto* game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->showroom_ && game->stageLoaded_ && game->enemies_.size()>=6,"showroom initialization");
    auto& items=game->throwables_;
    const auto pickup=[&](size_t i) {
        game->player_.SetPositionForDebug(items.Pickups()[i].position-Vector3{0,.35f,0});
        check(!game->HandleInteraction(true,true),"throwable pickup reported a gun swap");
    };
    const auto camera=aim({0,1.5f,0},{0,0,1});
    check(items.Count()==1 && items.Equipped()->type==ThrowableType::Corrosive,"showroom initial loadout");
    check(items.Throw(camera) && !items.Throw(camera) && items.Count()==0,"zero count");
    pickup(0);
    check(items.Count()==1,"same-type refill");
    pickup(2);
    check(items.Equipped()->type==ThrowableType::Dart && items.Count()==5,"one-slot replacement using E handler");
    check(items.Throw(camera) && items.Count()==4,"dart consumption");
    pickup(2);
    check(items.Count()==5,"reusable showroom pickup");

    StageWorld world;
    addBox(world,{0,-.5f,0},{40,.5f,40});
    auto& target=*game->enemies_[0];
    auto& mixed=*game->enemies_.back();
    auto& bystander=*game->enemies_[1];
    Enemy* crowd[]={&target,&mixed,&bystander};
    const auto targetStart=target.CaptureDebug();
    const auto mixedStart=mixed.CaptureDebug();
    const auto resetTarget=[&](Enemy& e,const Enemy::DebugState& state,const Vector3& p) {
        app.Dx()->WaitForGPU();
        e.RestoreDebug(state);
        place(e,p);
    };
    const auto aimAtBody=[&](const Enemy& enemy,const ThrowableDefinition& item,EnemyPartHit& hit) {
        const auto state=enemy.CaptureDebug();
        constexpr float distance=.25f;
        const float flightTime=distance/item.speed;
        const float gravity=9.81f*item.gravityMultiplier;
        for (size_t i=0;i<state.parts.size();++i) {
            const auto& part=state.parts[i];
            if (part.role!=EnemyPartRole::Body || !part.geometry || !state.models[i]) continue;
            const auto& pose=state.visualTransforms[i];
            const auto matrix=Matrix4x4::Multiply(state.models[i]->GetRootLocalMatrix(),
                Matrix4x4::MakeAffineMatrix(pose.scale,pose.rotate,pose.translate));
            for (const auto& face:part.geometry->faces) {
                const auto point=EnemyPartTransformPoint((face[0]+face[1]+face[2])*(1.0f/3),matrix);
                const auto eye=point-Vector3{0,(item.lift-gravity*flightTime*.5f)*flightTime,distance};
                const auto velocity=Vector3{0,item.lift-gravity*flightTime*.5f,item.speed};
                if (enemy.Raycast(eye,velocity,distance+.01f,hit) && hit.partIndex==i) return aim(eye,{0,0,1});
            }
        }
        throw std::runtime_error("No unobstructed body shot in test fixture");
    };

    items.Reset();
    StageWorld wall=world, ceiling=world;
    addBox(wall,{0,2,3},{10,2,.1f});
    addBox(ceiling,{0,3,0},{10,.1f,10});
    check(items.Throw(camera),"wall throw");
    items.Update(.4f,wall,{});
    check(items.GetState().flights.size()==1 && items.GetState().flights.front().bounces==1 &&
        items.GetState().flights.front().velocity.z<0,"wall reflection");
    items.Update(1,wall,{});
    check(items.GetState().effects.size()==1 && close(items.GetState().effects.front().position.y,0),"wall then floor");
    items.Reset();
    items.Throw(aim({0,1.5f,0},{0,1,0}));
    items.Update(.3f,ceiling,{});
    check(items.GetState().flights.size()==1 && items.GetState().flights.front().bounces==1 &&
        items.GetState().flights.front().velocity.y<0,"ceiling reflection");
    items.Update(1,ceiling,{});
    check(items.GetState().effects.size()==1,"ceiling then floor");
    items.Reset();
    items.Throw(camera);
    items.Update(6,StageWorld{},{});
    check(items.GetState().flights.empty() && items.GetState().effects.empty(),"off-world expiry");

    items.Reset();
    resetTarget(target,targetStart,{0,0,2});
    EnemyPartHit expected;
    items.Throw(aimAtBody(target,*items.Equipped(),expected));
    items.Update(.3f,world,crowd);
    check(items.GetState().effects.size()==1,"direct corrosion impact did not form a pool");
    const auto pool=items.GetState().effects.front().position;
    check(close(pool.y,0) && close(pool.x,0) && close(pool.z,2),"direct corrosion feet projection");
    check(items.EffectFor(target).speedMultiplier==.5f && !items.EffectFor(target).attraction,"corrosion slow and no lure");
    const auto beforeCorrosion=target.CaptureDebug();
    items.Update(1,world,crowd);
    const auto corroded=target.CaptureDebug();
    for (size_t i=0;i<corroded.parts.size();++i)
        check(close(beforeCorrosion.parts[i].hp-corroded.parts[i].hp,corroded.parts[i].role==EnemyPartRole::Leg ? 10.0f : 0),
            "corrosion damage role or rate");
    const auto destroyLeg=[](Enemy& enemy,EnemyPartType leg) { enemy.ApplyDamage(leg,10000,{0,1,0}); };
    destroyLeg(target,EnemyPartType::LeftLeg);
    const auto oneLeg=target.CaptureDebug();
    items.Update(1,world,crowd);
    const auto afterOneLeg=target.CaptureDebug();
    for (size_t i=0;i<oneLeg.parts.size();++i)
        check(close(oneLeg.parts[i].hp-afterOneLeg.parts[i].hp,oneLeg.parts[i].type==EnemyPartType::RightLeg ? 10.0f : 0),
            "corrosion bypassed the surviving leg");
    destroyLeg(target,EnemyPartType::RightLeg);
    resetTarget(mixed,mixedStart,{.6f,0,2});
    destroyLeg(mixed,EnemyPartType::LeftLeg);
    destroyLeg(mixed,EnemyPartType::RightLeg);
    const auto legless=target.CaptureDebug();
    const auto invincibleBody=mixed.CaptureDebug();
    items.Update(1,world,crowd);
    const auto afterLegless=target.CaptureDebug();
    const auto afterInvincible=mixed.CaptureDebug();
    for (size_t i=0;i<legless.parts.size();++i)
        check(close(legless.parts[i].hp-afterLegless.parts[i].hp,legless.parts[i].role==EnemyPartRole::Body ? 10.0f : 0),
            "legless corrosion did not apply exactly one body hit");
    check(!invincibleBody.parts.hpGroups.empty() &&
        close(invincibleBody.parts.hpGroups[0].hp-afterInvincible.parts.hpGroups[0].hp,10),
        "legless corrosion did not reach shared HP with an invincible body");
    for (size_t i=0;i<invincibleBody.parts.size();++i)
        if (invincibleBody.parts[i].role==EnemyPartRole::Body)
            check(invincibleBody.parts[i].hp==afterInvincible.parts[i].hp,"legless corrosion damaged an invincible body");
    place(target,{0,0,-2});
    items.Update(1,world,crowd);
    check(items.EffectFor(target).speedMultiplier==1 && close(health(target.CaptureDebug()),health(afterLegless)),"leaving corrosion");
    items.Update(8,world,crowd);
    check(items.GetState().effects.empty(),"corrosion expiry");

    items.Reset();
    pickup(1);
    resetTarget(target,targetStart,{-1,0,4});
    resetTarget(mixed,mixedStart,{1,0,4});
    StageWorld route=world;
    addBox(route,{0,1,2.5f},{.4f,1,.2f});
    items.Throw(aim({0,.9f,0},{0,-1,0}));
    items.Update(.2f,route,crowd);
    check(items.GetState().effects.size()==1 && !items.EffectFor(bystander).attraction,"ground arming and lure radius");
    const Vector3 player{0,0,15};
    target.PursuePlayer(player);
    const auto chaseState=target.GetState();
    mixed.SetAIPaused(true);
    mixed.Update(.02f,player,&route,1.65f,128,1,items.EffectFor(mixed).attraction);
    check(!mixed.IsAttracted() && close(mixed.GetPosition().z,4),"paused enemy was lured");
    mixed.SetAIPaused(false);
    for (int i=0;i<180;++i) {
        for (auto* e:{&target,&mixed}) {
            const auto effect=items.EffectFor(*e);
            e->Update(.02f,player,&route,1.65f,128,effect.speedMultiplier,effect.attraction);
            const auto& def=e->Definition();
            check(EnemyNavigation::Clear(&route,e->GetPosition(),e->GetPosition(),def.collisionRadius,def.collisionHeight),
                "lured enemy walked through an obstacle");
        }
        items.Update(.02f,route,crowd);
    }
    check(target.IsAttracted() && target.GetState()==chaseState && target.PendingAttackCount()==0 &&
        StageLength(target.GetPosition())<2 && StageLength(mixed.GetPosition())<2,"lured enemies did not approach via navigation");
    const float beforeTarget=health(target.CaptureDebug()), beforeMixed=health(mixed.CaptureDebug());
    items.Update(.6f,route,crowd);
    check(items.GetState().effects.empty() && items.GetState().flashes.size()==1 &&
        health(target.CaptureDebug())<beforeTarget && health(mixed.CaptureDebug())<beforeMixed,"grenade blast missed targets");
    target.Update(.02f,player,&route,1.65f,128,1,items.EffectFor(target).attraction);
    check(!target.IsAttracted() && !items.EffectFor(mixed).attraction,"lure failed to expire");

    resetTarget(target,targetStart,{0,0,2});
    resetTarget(mixed,mixedStart,{.8f,0,2});
    items.Reset();
    pickup(2);
    items.Throw(aimAtBody(target,*items.Equipped(),expected));
    items.Update(.2f,world,crowd);
    const auto dart=target.CaptureDebug();
    check(items.GetState().flights.empty() && items.GetState().flashes.size()==1 &&
        dart.parts[expected.partIndex].hp==std::max(0.0f,targetStart.parts[expected.partIndex].hp-80),
        "dart direct damage or same-part blast duplication");
    check(health(mixed.CaptureDebug())<health(mixedStart),"dart blast did not hit the other enemy");

    resetTarget(mixed,mixedStart,{0,0,2});
    auto fixture=mixed.CaptureDebug();
    size_t direct=kNoEnemyPart;
    for (size_t i=0;i<fixture.parts.size();++i) {
        auto& p=fixture.parts[i];
        if (p.role!=EnemyPartRole::Arm) continue;
        p.hp=p.maxHp=150;
        if (direct==kNoEnemyPart) direct=i;
    }
    check(direct!=kNoEnemyPart && !fixture.parts.hpGroups.empty(),"shared HP fixture missing");
    mixed.RestoreDebug(fixture); mixed.UpdateVisuals(0);
    mixed.ApplyThrowableBlast({{0,1,2},5,20,10},{},{direct,80,25},{});
    const auto damaged=mixed.CaptureDebug();
    check(close(damaged.parts[direct].hp,70) && close(fixture.parts.hpGroups[0].hp-damaged.parts.hpGroups[0].hp,25),
        "direct plus blast applied shared HP more than once");
    for (size_t i=0;i<fixture.parts.size();++i) {
        if (fixture.parts[i].role==EnemyPartRole::Arm && i!=direct) check(damaged.parts[i].hp<150,"nearby part missed");
        if (fixture.parts[i].role==EnemyPartRole::Body) check(damaged.parts[i].hp==fixture.parts[i].hp,"invincible part damaged");
    }

    // Load distinct profiles through the same JSON path used by each stage.
    constexpr auto tunedPath="generated/throwable-tests/part-multipliers.json";
    nlohmann::json tunedDefinitions;
    std::ifstream(definitions)>>tunedDefinitions;
    for (auto& item:tunedDefinitions.at("items")) {
        if (item.at("id")=="corrosive_bottle")
            item["corrosion"]["partMultipliers"]={{"Leg",1.5},{"Body",0},{"Arm",.5}};
        if (item.at("id")=="lure_grenade")
            item["blast"]["partMultipliers"]={{"Armor",.25}};
        if (item.at("id")=="burst_dart") {
            item["directHit"]["partMultipliers"]={{"Body",.5},{"Arm",1.5}};
            item["blast"]["partMultipliers"]={{"Body",2},{"Arm",2},{"Head",0}};
        }
    }
    std::ofstream(tunedPath)<<tunedDefinitions;
    ThrowableSystem tuned;
    check(tuned.Load(tunedPath,"resources/levels/showroom/showroom.json"),"load throwable part multipliers");
    resetTarget(target,targetStart,{0,0,2});
    resetTarget(mixed,mixedStart,{.6f,0,2});
    tuned.Throw(aim({0,.9f,2},{0,-1,0}));
    tuned.Update(.2f,world,{});
    tuned.Update(1,world,crowd);
    const auto scaledCorrosion=target.CaptureDebug();
    for (size_t i=0;i<scaledCorrosion.parts.size();++i)
        check(close(targetStart.parts[i].hp-scaledCorrosion.parts[i].hp,
            scaledCorrosion.parts[i].role==EnemyPartRole::Leg ? 15.0f : 0),"corrosion part multiplier");
    for (auto leg:{EnemyPartType::LeftLeg,EnemyPartType::RightLeg}) {
        destroyLeg(target,leg);
        destroyLeg(mixed,leg);
    }
    const auto zeroFallback=target.CaptureDebug();
    const auto sharedFallback=mixed.CaptureDebug();
    tuned.Update(1,world,crowd);
    check(close(health(zeroFallback),health(target.CaptureDebug())) &&
        close(sharedFallback.parts.hpGroups[0].hp-mixed.CaptureDebug().parts.hpGroups[0].hp,5),
        "corrosion fallback category multiplier was ignored");

    tuned.Reset();
    check(tuned.TryPickup(tuned.Pickups()[1].position-Vector3{0,.35f,0},game->level_.collision) &&
        tuned.Equipped()->blastPartMultipliers.For(EnemyPartRole::Armor)==.25f,"grenade armor multiplier");
    check(tuned.TryPickup(tuned.Pickups()[2].position-Vector3{0,.35f,0},game->level_.collision),"tuned dart pickup");
    const auto& tuning=*tuned.Equipped();
    check(tuning.directPartMultipliers.For(EnemyPartRole::Generic)==1 &&
        tuning.blastPartMultipliers.For(EnemyPartRole::Core)==1,"unspecified part category default");
    resetTarget(target,targetStart,{0,0,2});
    resetTarget(mixed,mixedStart,{.8f,0,2});
    tuned.Throw(aimAtBody(target,tuning,expected));
    tuned.Update(.2f,world,crowd);
    check(close(targetStart.parts[expected.partIndex].hp-target.CaptureDebug().parts[expected.partIndex].hp,40),
        "dart direct multiplier used blast settings");

    resetTarget(mixed,fixture,{0,0,2});
    mixed.ApplyThrowableBlast({{0,1,2},5,20,10},tuning.blastPartMultipliers,{direct,80,25},tuning.directPartMultipliers);
    const auto scaledBlast=mixed.CaptureDebug();
    check(close(scaledBlast.parts[direct].hp,30) && close(fixture.parts.hpGroups[0].hp-scaledBlast.parts.hpGroups[0].hp,37.5f),
        "part multipliers overcounted direct plus blast shared HP");
    for (size_t i=0;i<fixture.parts.size();++i) {
        if (fixture.parts[i].role==EnemyPartRole::Arm && i!=direct)
            check(close(150-scaledBlast.parts[i].hp,2*(150-damaged.parts[i].hp)),"blast used the wrong arm multiplier");
        if (fixture.parts[i].role==EnemyPartRole::Head || fixture.parts[i].role==EnemyPartRole::Body)
            check(scaledBlast.parts[i].hp==fixture.parts[i].hp,"zero blast multiplier or invincibility ignored");
    }
    resetTarget(mixed,fixture,{0,0,2});
    auto zeroDirect=tuning.directPartMultipliers;
    zeroDirect.Set(EnemyPartRole::Arm,0);
    mixed.ApplyThrowableBlast({{0,1,2},5,20,10},tuning.blastPartMultipliers,{direct,80,25},zeroDirect);
    check(mixed.CaptureDebug().parts[direct].hp==150,"zero direct multiplier reapplied blast to the hit part");
    mixed.Update(.02f,player,&world,1.65f,128,1,Vector3{0,0,0});
    check(mixed.IsAttracted(),"pool attraction fixture");
    app.Dx()->WaitForGPU();
    const auto id=mixed.Definition().id;
    check(game->enemyPool_.ReleaseForReset(&mixed),"release for reuse");
    auto* reused=game->enemyPool_.Acquire(id,999,"test",{0,0,15},{});
    check(reused==&mixed && !reused->IsAttracted() && reused->AI().state==EnemyState::Idle &&
        close(health(reused->CaptureDebug()),health(mixedStart)),"pooled enemy retained throwable state");

#ifdef USE_IMGUI
    game->weaponEditor_.mode=WeaponEditorMode::TestThrowables;
    app.ImGui()->Begin();
#endif
    app.Draw();
    app.Scenes().Change(app,"Showroom");
    game=dynamic_cast<GameScene*>(app.Scenes().Current());
    check(game && game->throwables_.Count()==1 && game->throwables_.Equipped()->type==ThrowableType::Corrosive &&
        game->throwables_.GetState().flights.empty() && game->throwables_.GetState().effects.empty() &&
        game->throwables_.GetState().flashes.empty(),"scene restart did not restore throwables");
    std::ofstream("generated/throwable-tests/result.txt")
        <<"PASS: campaign pickup placement, inventory/refill, wall and ceiling bounces, direct corrosion, slow/DoT/expiry, legless corrosion, "
          "navigation/lure, multi-target blasts, exact dart impact, per-effect part multipliers, shared and invincible HP, "
          "pool reuse, D3D rendering and restart.\n";
}
