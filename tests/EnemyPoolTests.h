#pragma once
// Runs in the Debug executable with real D3D resources: --enemy-pool-test.
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include "EnemyModelSource.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include "EncapsulationChecks.h"

inline void RunEnemyPoolTests(GameApp& app) {
    RunEncapsulationChecks();
    const auto check=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
    std::filesystem::create_directories("generated/enemy-pool-tests");
    const auto read=[](const char* path) { std::ifstream f(path); nlohmann::json j; f>>j; return j; };
    auto asset=read("resources/enemy/boss/normal.test.enemy.json");
    asset["hpGroups"][0]["maxHp"]=100;
    // One actual Blender mesh, exercising LocalAndShared and Invincible together.
    for (auto& part:asset["parts"]) {
        if (part["name"]=="LeftArm") { part["localHp"]=60; part["sharedDamageRate"]=.5; }
        if (part["name"]=="RightArm") { part["localHp"]=nullptr; part["sharedDamageRate"]=1; }
        if (part["name"]=="Body") { part["localHp"]=nullptr; part["sharedHpGroup"]=""; part["deathOnZero"]=false; }
    }
    std::ofstream("generated/enemy-pool-tests/fixture.enemy.json")<<asset;
    auto source=read("resources/Data/enemies.json");
    auto fixture=source["enemies"][0];
    for (const auto& d:source["enemies"]) if (d["id"]=="normal_test") fixture=d;
    fixture["id"]="pool_fixture"; fixture["partAsset"]="generated/enemy-pool-tests/fixture.enemy.json";
    source["enemies"].push_back(fixture);
    std::ofstream("generated/enemy-pool-tests/definitions.json")<<source;
    EnemyDefinitions definitions;
    check(definitions.Load("generated/enemy-pool-tests/definitions.json"),"definitions failed");
    Camera camera;
    EnemyPool pool;
    EnemyPoolSettings settings; settings.perDefinition=1;
    pool.Initialize(app.ObjCom(),app.Dx(),&camera,definitions,settings);
    check(pool.Capacity()==definitions.All().size() && pool.Active()==0,"prewarm capacity");
    check(pool.RuntimeAllocations()==0,"prewarm counted as runtime allocation");
    uint64_t next=100;
    const auto verifyReset=[&](Enemy* enemy,const Enemy::DebugState& original,bool alreadyUpdated=false) {
        const auto state=enemy->CaptureDebug();
        check(!enemy->IsDead() && state.ai.state==EnemyState::Idle && state.ai.cooldown==0 && state.ai.attacksThisUpdate==0,"AI reset");
        check(state.ai.searchElapsed==0 && state.ai.searchPointsVisited==0 && state.ai.SearchRemaining()==0 &&
            !state.ai.searchFinishing && !state.ai.lastSeenReached && !state.ai.investigateUnreachable && state.ai.investigateBlockedTime==0,"search reset");
        check(state.ai.memory.count==0 && !state.ai.memory.hasDirection && (alreadyUpdated || state.ai.memory.clock==0),"observation memory reset");
        check(state.attacks==0 && state.damage==0 && state.flash==0 && state.blastHitTime==0 && state.lastBlastDamage==0,"feedback reset");
        check(!state.exploded && state.explosionTime==0 && state.faces.empty() && state.detached.empty(),"death debris reset");
        check(!state.parts.deathProcessed && state.parts.size()==original.parts.size(),"part state reset");
        check(state.models==original.models,"part models changed during reuse");
        for (size_t i=0;i<state.parts.size();++i) {
            const auto& p=state.parts[i]; const auto& expected=original.parts[i];
            check(p.hp==expected.hp && p.maxHp==expected.maxHp && !p.Destroyed() && p.flashRemaining==0,"local HP/destroyed/flash reset");
            check(p.usesLocalHp==expected.usesLocalHp && p.sharedGroup==expected.sharedGroup && p.sharedDamageRate==expected.sharedDamageRate
                && p.breakable==expected.breakable && p.deathOnZero==expected.deathOnZero,"HP mode changed on reuse");
            check(state.visible[i],"part stayed invisible");
        }
        for (size_t i=0;i<state.parts.hpGroups.size();++i)
            check(state.parts.hpGroups[i].hp==original.parts.hpGroups[i].hp,"shared HP reset");
    };
    const auto verifyRenderedCollision=[&](Enemy* enemy) {
        const auto state=enemy->CaptureDebug();
        std::vector<Matrix4x4> matrices;
        for (size_t i=0;i<state.parts.size();++i) {
            const auto& tr=state.visualTransforms[i];
            matrices.push_back(Matrix4x4::Multiply(state.models[i]->GetRootLocalMatrix(),
                Matrix4x4::MakeAffineMatrix(tr.scale,tr.rotate,tr.translate)));
        }
        for (size_t i=0;i<state.parts.size();++i) {
            if (state.parts[i].Destroyed() || !state.visible[i]) continue;
            check(state.parts[i].geometry && !state.parts[i].geometry->faces.empty(),"surface collision data missing");
            const auto& face=state.parts[i].geometry->faces[0];
            const auto a=EnemyPartTransformPoint(face[0],matrices[i]);
            const auto b=EnemyPartTransformPoint(face[1],matrices[i]);
            const auto c=EnemyPartTransformPoint(face[2],matrices[i]);
            const auto normal=Matrix4x4::Normalize(Matrix4x4::Cross(b-a,c-a));
            const auto origin=(a+b+c)*(1.0f/3)+normal*.001f;
            const auto direction=normal*-1;
            EnemyPartHit expected,actual;
            check(RaycastEnemyPartsTransformed(state.parts,[&](size_t part) -> const Matrix4x4* {
                return state.visible[part] ? &matrices[part] : nullptr;
            },origin,direction,.1f,expected),"animated surface fixture ray");
            check(enemy->Raycast(origin,direction,.1f,actual) && expected.partIndex==actual.partIndex
                && std::abs(expected.distance-actual.distance)<.001f,"raycast disagrees with rendered part matrices");
        }
    };
    for (const auto& [id,definition]:definitions.All()) {
        (void)definition;
        const auto gpuBefore=Object3d::DebugInitializationCount();
        auto* enemy=pool.Acquire(id,next++,"first",{0,0,12},{});
        { auto senses=enemy->Senses(); senses.detectionTime=0; senses.fieldOfView=360; enemy->ConfigureSenses(senses); }
        const auto original=enemy->CaptureDebug();
        verifyRenderedCollision(enemy);
        if (id=="normal") {
            const auto closeEnough=[](float a,float b) { return std::abs(a-b)<.001f; };
            const auto verifyCollision=[&]() { verifyRenderedCollision(enemy); };
            auto expectedAI=original.ai; auto expectedPosition=original.position; auto expectedRotation=original.rotation;
            const Vector3 target{6,0,5};
            expectedAI.Update(expectedPosition,expectedRotation,target,.125f,false);
            enemy->Update(.125f,target);
            const auto walking=enemy->CaptureDebug();
            check(closeEnough(walking.position.x,expectedPosition.x) && closeEnough(walking.position.z,expectedPosition.z)
                && closeEnough(walking.position.y,expectedPosition.y),"animation changed AI movement");
            check(!closeEnough(walking.visualTransforms[1].translate.y,walking.position.y),"normal render bob missing");
            for (size_t i=0;i<walking.parts.size();++i) {
                const auto type=EnemyProceduralAnimation::AnimationPart(walking.parts[i]);
                if (type!=EnemyPartType::LeftArm && type!=EnemyPartType::RightArm) continue;
                const auto& arm=walking.parts[i]; const auto& render=walking.visualTransforms[i];
                const auto center=(arm.bounds.min+arm.bounds.max)*.5f;
                const auto drawn=EnemyPartTransformPoint(center,Matrix4x4::MakeAffineMatrix(render.scale,render.rotate,render.translate));
                const auto bind=EnemyPartTransformPoint(center,Matrix4x4::MakeAffineMatrix(render.scale,walking.rotation,walking.position));
                const float armSpan=(arm.bounds.max.z-arm.bounds.min.z)*render.scale.z;
                check(drawn.y<bind.y-armSpan*.25f,"actual model arm remained horizontal");
            }
            verifyCollision();
            enemy->ApplyDamage(EnemyPartType::LeftLeg,10000,{0,0,1}); enemy->UpdateVisuals(0);
            check(EnemyProceduralAnimation::Classify(enemy->CaptureDebug().parts)==EnemyLocomotionPose::MissingLeftLeg,"one-leg render mode");
            verifyCollision();
            enemy->ApplyDamage(EnemyPartType::RightLeg,10000,{0,0,1}); enemy->UpdateVisuals(0);
            const auto crawling=enemy->CaptureDebug();
            check(!enemy->IsDead() && EnemyProceduralAnimation::Classify(crawling.parts)==EnemyLocomotionPose::Crawl,"crawl render mode");
            float minY=crawling.parts[0].bounds.min.y,maxY=crawling.parts[0].bounds.max.y;
            for (const auto& part:crawling.parts) { minY=std::min(minY,part.bounds.min.y); maxY=std::max(maxY,part.bounds.max.y); }
            const float modelHeight=(maxY-minY)*crawling.definition.VisualScale(crawling.scale).y;
            const auto bodyCenter=(crawling.parts[1].bounds.min+crawling.parts[1].bounds.max)*.5f;
            const auto& render=crawling.visualTransforms[1];
            const auto renderedCenter=EnemyPartTransformPoint(bodyCenter,Matrix4x4::MakeAffineMatrix(render.scale,render.rotate,render.translate));
            const auto bindCenter=EnemyPartTransformPoint(bodyCenter,Matrix4x4::MakeAffineMatrix(render.scale,crawling.rotation,crawling.position));
            check(renderedCenter.y<bindCenter.y-modelHeight*.15f,"crawl height missing");
            const auto torsoMatrix=Matrix4x4::MakeAffineMatrix(render.scale,render.rotate,render.translate);
            check(std::abs(torsoMatrix.m[1][1]/render.scale.y)<.2f,"crawl torso remained upright");
            float lowest=std::numeric_limits<float>::max();
            for (size_t i=0;i<crawling.parts.size();++i) {
                const auto& part=crawling.parts[i]; if (part.Destroyed()) continue;
                const auto& tr=crawling.visualTransforms[i];
                const auto matrix=Matrix4x4::MakeAffineMatrix(tr.scale,tr.rotate,tr.translate);
                for (int corner=0;corner<8;++corner) {
                    const Vector3 p{(corner&1)?part.bounds.max.x:part.bounds.min.x,
                        (corner&2)?part.bounds.max.y:part.bounds.min.y,(corner&4)?part.bounds.max.z:part.bounds.min.z};
                    lowest=std::min(lowest,EnemyPartTransformPoint(p,matrix).y);
                }
            }
            check(lowest>=crawling.position.y && lowest<crawling.position.y+modelHeight*.03f,"crawl base-plane clearance");
            enemy->Update(.125f,target);
            const auto stroke=enemy->CaptureDebug();
            for (size_t i : {size_t{2},size_t{3}}) {
                const auto& before=crawling.visualTransforms[i]; const auto& after=stroke.visualTransforms[i];
                check(!closeEnough(before.rotate.x,after.rotate.x) || !closeEnough(before.rotate.z,after.rotate.z),"crawl arm remained still");
            }
            verifyCollision();
            const auto upright=Matrix4x4::MakeAffineMatrix(crawling.definition.VisualScale(crawling.scale),crawling.rotation,crawling.position);
            const auto oldHead=EnemyPartTransformPoint((crawling.parts[0].bounds.min+crawling.parts[0].bounds.max)*.5f,upright);
            EnemyPartHit oldHit;
            check(!enemy->Raycast(oldHead+Vector3{0,0,-2},{0,0,1},4,oldHit),"old upright head still collides during crawl");
            enemy->RestoreDebug(walking); enemy->UpdateVisuals(0);
            const auto restored=enemy->CaptureDebug();
            check(restored.ai.memory.count==walking.ai.memory.count && restored.ai.memory.clock==walking.ai.memory.clock &&
                StageLength(restored.ai.memory.escapeDirection-walking.ai.memory.escapeDirection)==0,"rewind lost observation history");
            for (size_t i=0;i<walking.visualTransforms.size();++i) {
                const auto& a=walking.visualTransforms[i]; const auto& b=restored.visualTransforms[i];
                check(closeEnough(a.translate.x,b.translate.x) && closeEnough(a.translate.y,b.translate.y) && closeEnough(a.translate.z,b.translate.z)
                    && closeEnough(a.rotate.x,b.rotate.x) && closeEnough(a.rotate.y,b.rotate.y) && closeEnough(a.rotate.z,b.rotate.z),"rewind lost animation pose");
            }
            enemy->ResetForSpawn(enemy->GetSpawnId(),"first",original.position,original.rotation);
            verifyReset(enemy,original);
            const auto reset=enemy->CaptureDebug();
            check(closeEnough(reset.visualTransforms[1].translate.y,reset.position.y),"pool retained animation offset");
        }
        check(Object3d::DebugInitializationCount()==gpuBefore,"Acquire initialized Object3d");
        check(!pool.Release(enemy),"living enemy released");
        enemy->Update(.5f,{0,0,12}); enemy->ConfirmAttack(10);
        enemy->ShowHitFeedback(size_t{0});
        if (id=="pool_fixture") {
            check(enemy->ApplyDamage(EnemyPartType::Body,100,{0,0,1})==0,"Invincible damaged");
            check(enemy->ApplyDamage(EnemyPartType::LeftArm,20,{0,0,1})==20,"LocalAndShared damage");
            const auto hit=enemy->CaptureDebug();
            check(hit.parts[2].hp==40 && hit.parts.hpGroups[0].hp==90,"LocalAndShared losses");
            enemy->ApplyDamage(EnemyPartType::RightArm,90,{0,0,1});
        } else if (id=="normal_test") {
            enemy->ApplyDamage(EnemyPartType::LeftArm,50,{0,0,1});
            check(!enemy->IsDead(),"Shared HP died early");
            enemy->ApplyDamage(EnemyPartType::RightArm,10000,{0,0,1});
        } else if (id=="bomber") {
            check(enemy->ApplyBulletDamage(EnemyPartType::Body,1,{0,0,1}).explosion.has_value(),"Bomber detonation");
        } else {
            enemy->ApplyDamage(EnemyPartType::LeftArm,10000,{0,0,1});
            enemy->ApplyDamage(EnemyPartType::Head,10000,{0,0,1});
        }
        if (!enemy->IsDead() || enemy->CanReturnToPool() || pool.Release(enemy))
            throw std::runtime_error("death/debris lifecycle: "+id+" dead="+std::to_string(enemy->IsDead())+
                " faces="+std::to_string(enemy->CaptureDebug().faces.size()));
        const auto dead=enemy->CaptureDebug();
        check(!dead.faces.empty(),"death produced no face shards");
        for (bool visible:dead.visible) check(!visible,"dead body part still visible");
        enemy->UpdateVisuals(10);
        check(enemy->CanReturnToPool() && pool.Release(enemy),"expired enemy not released");
        check(!pool.Release(enemy),"double release");
        const auto gpuReuse=Object3d::DebugInitializationCount();
        auto* reused=pool.Acquire(id,next++,"second",{4,0,16},{0,1,0});
        check(reused==enemy && reused->GetSpawnId()==next-1 && reused->GetSpawnTriggerId()=="second","instance/identity reuse");
        check(reused->GetPosition().x==4 && reused->GetPosition().z==16,"spawn transform reset");
        verifyReset(reused,original);
        check(Object3d::DebugInitializationCount()==gpuReuse,"reuse allocated GPU renderers");
        pool.ReleaseAll();
    }
    check(pool.RuntimeAllocations()==0,"prewarmed path overflowed");
    auto* first=pool.Acquire("normal",next++,"overflow",{},{});
    auto* second=pool.Acquire("normal",next++,"overflow",{},{});
    check(first!=second && pool.RuntimeAllocations()==1,"overflow not counted");
    first->Die();
    first->UpdateVisuals(.1f);
    const auto firstBurst=first->CaptureDebug();
    check(!firstBurst.faces.empty(),"first consecutive death burst missing");
    second->Die();
    second->UpdateVisuals(0);
    first->UpdateVisuals(0);
    const auto preservedBurst=first->CaptureDebug();
    check(!second->CaptureDebug().faces.empty(),"second consecutive death burst missing");
    check(preservedBurst.faces.size()==firstBurst.faces.size(),"second death evicted first burst");
    for (size_t i=0;i<firstBurst.faces.size();++i)
        check(preservedBurst.faces[i].spawnOrder==firstBurst.faces[i].spawnOrder,"first burst shards replaced");
    check(!pool.Release(first) && !pool.Release(second),"consecutive death debris released early");
    first->UpdateVisuals(10); check(pool.Release(first),"overflow release");
    check(!second->CanReturnToPool(),"first expiration cleared second burst");
    second->UpdateVisuals(10); check(pool.Release(second),"second burst did not expire");
    check(pool.Acquire("normal",next++,"reuse",{}, {})==first && pool.RuntimeAllocations()==1,"overflow slot not reused");
    pool.ReleaseAll();

    // Chunk-mode nonfatal destruction must also leave the attached renderer reusable.
    auto* chunk=pool.Acquire("normal",next++,"chunk",{},{});
    const auto chunkOriginal=chunk->CaptureDebug();
    auto chunkState=chunkOriginal; chunkState.breakMode=FragmentMode::Chunk; chunk->RestoreDebug(chunkState);
    chunk->ApplyDamage(EnemyPartType::LeftArm,10000,{1,0,0});
    check(!chunk->CaptureDebug().detached.empty(),"chunk destruction missing");
    chunk->Die(); chunk->UpdateVisuals(10); check(pool.Release(chunk),"chunk release");
    const auto gpuChunk=Object3d::DebugInitializationCount();
    verifyReset(pool.Acquire("normal",next++,"chunk reset",{},{}),chunkOriginal);
    check(Object3d::DebugInitializationCount()==gpuChunk,"chunk respawn initialized renderer");
    pool.ReleaseAll();

    // Real BulletManager traces non-owning active pointers and still delivers damage.
    auto* target=pool.Acquire("normal_test",next++,"bullet",{},{});
    const auto targetState=target->CaptureDebug();
    const auto triangle=targetState.parts[0].geometry->faces[0];
    const auto local=(triangle[0]+triangle[1]+triangle[2])*(1.0f/3);
    const auto point=EnemyPartTransformPoint(local,Matrix4x4::MakeAffineMatrix(target->Definition().VisualScale(targetState.scale),{},{}));
    BulletManager bullets; bullets.Initialize(app.ObjCom(),app.Dx(),&camera);
    WeaponDefinition weapon; weapon.damage=10; weapon.range=100; weapon.bulletSpeed=40; weapon.bulletLifeTime=3;
    std::mt19937 random(123); StageWorld world; std::vector<Enemy*> active{target};
    const auto gun=Matrix4x4::MakeAffineMatrix({1,1,1},{0,1.57079632679f,0},point-Vector3{5,0,0});
    bullets.Spawn(weapon,gun,0,random,world,active);
    int impacts=0;
    bullets.Update(.25f,world,active,[&](const BulletEnemyImpact& hit){check(hit.enemyIndex==0 && hit.result.damage>0,"bullet pool hit"); ++impacts;});
    check(impacts==1,"pooled target bullet missed");
    target->ApplyExplosionDamage({target->GetPosition(),10,10000});
    check(target->IsDead(),"pooled explosion damage");
    StageProgress progress;
    const auto firstId=target->GetSpawnId(); progress.ObserveEnemy(firstId,true);
    target->UpdateVisuals(10); check(pool.Release(target),"explosion release");
    target=pool.Acquire("normal_test",next++,"new identity",{},{});
    progress.ObserveEnemy(target->GetSpawnId(),false);
    check(progress.DefeatedCount()==1 && target->GetSpawnId()!=firstId,"spawn identity aliased previous kill");
    target->Die(); progress.ObserveEnemy(target->GetSpawnId(),true);
    check(progress.DefeatedCount()==2,"StageProgress did not track distinct spawns");
    pool.ReleaseAll(); pool.Clear();

    // Drive the actual scene spawn callback; dead IDs stay dead after slot reuse.
    GameScene game; game.OnEnter(app);
    const nlohmann::json spawnFixture={
        {"spawnPoints",nlohmann::json::array({{{"id","point"},{"position",{0,0,10}},
            {"enemyPool",nlohmann::json::array({{{"id","normal_test"},{"weight",1}}})}}})},
        {"spawnTriggers",nlohmann::json::array({{{"id","trigger"},{"position",{0,0,0}},{"size",{10,10,10}},
            {"spawnPointIds",{"point"}},{"spawnCount",2},{"maxAlive",1},{"spawnInterval",0}}})}};
    std::ofstream("generated/enemy-pool-tests/spawns.json")<<spawnFixture;
    check(game.spawnSystem_.Load("generated/enemy-pool-tests/spawns.json",game.enemyDefinitions_),"spawn fixture");
    game.player_.SetStage(&game.level_.collision,{},{});
    const auto spawnGpu=Object3d::DebugInitializationCount();
    game.UpdateCombat(app,0,false);
    check(game.enemies_.size()==1,"scene trigger did not spawn");
    auto* spawned=game.enemies_[0]; const auto oldId=spawned->GetSpawnId();
    spawned->Die(); spawned->UpdateVisuals(10); game.RecycleEnemies();
    game.UpdateCombat(app,0,false);
    check(game.enemies_.size()==1 && game.enemies_[0]==spawned && spawned->GetSpawnId()!=oldId,"trigger reuse/ID tracking");
    check(game.stage_.DefeatedCount()==1,"scene lost defeat on release");
    check(game.enemyPool_.RuntimeAllocations()==0 && Object3d::DebugInitializationCount()==spawnGpu,"scene spawn allocated renderers");
    for (auto* enemy:game.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    game.RecycleEnemies();

    // Scheduled simultaneous spawn through GameScene's real callback and real pool.
    nlohmann::json groupFixture={{"spawnPoints",nlohmann::json::array()},
        {"spawnGroups",nlohmann::json::array({{{"id","wave"},{"time",3},{"mode","Simultaneous"},{"interval",100},
            {"enemies",nlohmann::json::array()}}})}};
    for (int i=0;i<6;++i) {
        const auto pointId="Point_"+std::to_string(i);
        // Point lottery deliberately differs; group members must override it.
        groupFixture["spawnPoints"].push_back({{"id",pointId},{"position",{i*10,0,40}},{"rotation",{0,.25*i,0}},
            {"enemyPool",nlohmann::json::array({{{"id","bomber"},{"weight",1}}})}});
        groupFixture["spawnGroups"][0]["enemies"].push_back({{"enemy",i<5 ? "normal_test" : "ranged"},{"spawnPoint",pointId}});
    }
    const char* groupPath="generated/enemy-pool-tests/groups.json";
    std::ofstream(groupPath)<<groupFixture;
    check(game.spawnSystem_.Load(groupPath,game.enemyDefinitions_),"group scene fixture");
    game.level_.collision.colliders.clear(); game.player_.SetStage(&game.level_.collision,{1000,0,0},{});
    const auto beforeGroupGpu=Object3d::DebugInitializationCount();
    game.UpdateCombat(app,2.5f,false);
    const auto beforeGroup=game.CaptureDebug();
    check(game.enemies_.empty() && game.enemyPool_.Active()==0,"group spawned before start time");
    game.UpdateCombat(app,.5f,false);
    check(game.enemies_.size()==6 && game.enemyPool_.Active()==6 && game.spawnSystem_.Groups()[0].spawned==6,
        "simultaneous group not entirely active in one scene update");
    check(game.enemyPool_.RuntimeAllocations()==1,"simultaneous overflow bypassed existing pool expansion");
    check(Object3d::DebugInitializationCount()>beforeGroupGpu,"overflow slot was not prepared");
    const auto groupPointers=game.enemies_;
    std::vector<Enemy::DebugState> groupOriginal;
    std::set<uint64_t> groupIds;
    for (size_t i=0;i<game.enemies_.size();++i) {
        auto* enemy=game.enemies_[i]; groupOriginal.push_back(enemy->CaptureDebug());
        check(enemy->Definition().id==(i<5 ? "normal_test" : "ranged") && enemy->GetSpawnTriggerId()=="wave","group definition/source");
        check(enemy->GetPosition().x==static_cast<float>(i*10) && enemy->GetPosition().z==40 &&
            groupOriginal.back().rotation.y==static_cast<float>(.25*i),"group spawn point pose");
        check(groupIds.insert(enemy->GetSpawnId()).second,"duplicate simultaneous spawn ID");
        verifyRenderedCollision(enemy);
    }
    auto* groupTarget=game.enemies_[0];
    check(groupTarget->ApplyDamage(EnemyPartType::LeftArm,50,{0,0,1})==50 && !groupTarget->IsDead(),"group shared HP damage");
    groupTarget->ApplyDamage(EnemyPartType::RightArm,10000,{0,0,1});
    check(groupTarget->IsDead() && !groupTarget->CaptureDebug().faces.empty() && !groupTarget->CanReturnToPool(),"group death/debris lifecycle");
    auto* rangedTarget=game.enemies_.back();
    rangedTarget->ApplyDamage(EnemyPartType::LeftArm,10000,{0,0,1});
    const auto broken=rangedTarget->CaptureDebug();
    check(!rangedTarget->IsDead() && broken.parts[2].Destroyed() && !broken.visible[2],"group nonfatal part destruction");
    for (auto* enemy:game.enemies_) if (!enemy->IsDead()) {
        { auto senses=enemy->Senses(); senses.detectionTime=0; senses.fieldOfView=360; enemy->ConfigureSenses(senses); }
        enemy->Update(.125f,enemy->GetPosition()+Vector3{0,0,2});
        check(enemy->GetState()!=EnemyState::Idle,"group AI did not advance");
        enemy->Die();
    }
    game.RecycleEnemies();
    check(game.enemies_.size()==6,"group debris returned too early");
    for (auto* enemy:game.enemies_) enemy->UpdateVisuals(10);
    game.RecycleEnemies();
    check(game.enemies_.empty() && game.enemyPool_.Active()==0,"group enemies did not return to pool");
    const auto reusedGpu=Object3d::DebugInitializationCount();
    game.spawnSystem_.Reset(); game.UpdateCombat(app,3,false);
    check(game.enemies_==groupPointers && game.enemyPool_.RuntimeAllocations()==1,"group slots not reused after reset");
    check(Object3d::DebugInitializationCount()==reusedGpu,"group reuse initialized renderers");
    for (size_t i=0;i<game.enemies_.size();++i) {
        // UpdateCombat(3) above includes three seconds of fresh simulation after
        // acquire: the clock must advance, but hidden targets must leave no samples.
        verifyReset(game.enemies_[i],groupOriginal[i],true);
        check(game.enemies_[i]->AI().memory.clock==3,"fresh post-spawn observation clock");
        check(!groupIds.count(game.enemies_[i]->GetSpawnId()),"group reuse retained a dead spawn ID");
    }
    game.RestoreDebug(app,beforeGroup);
    check(game.enemies_.empty() && game.spawnSystem_.ElapsedTime()==2.5 && game.spawnSystem_.Groups()[0].spawned==0,"group rewind state");
    game.UpdateCombat(app,.5f,false);
    check(game.enemies_==groupPointers && Object3d::DebugInitializationCount()==reusedGpu,"group rewind refire/reuse");
    game.OnExit(app);
    game.OnEnter(app); // The same scene lifecycle used by Restart Stage.
    check(game.spawnSystem_.Load(groupPath,game.enemyDefinitions_),"restart group reload");
    game.level_.collision.colliders.clear(); game.player_.SetStage(&game.level_.collision,{1000,0,0},{});
    check(game.spawnSystem_.ElapsedTime()==0 && game.spawnSystem_.Groups()[0].spawned==0 && game.enemies_.empty(),"restart group state");
    game.UpdateCombat(app,2.5f,false); check(game.enemies_.empty(),"restart group spawned early");
    game.UpdateCombat(app,.5f,false); check(game.enemies_.size()==6 && game.enemyPool_.Active()==6,"restart group did not refire");

    // All activation methods share GameScene's real pool callback.
    // Reuse this scene/pool instead of creating extra scenes: the engine's SRV
    // allocator is monotonic, and the existing lifecycle tests already cover OnEnter.
    for (auto* enemy:game.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    game.RecycleEnemies();
    const char* independentPath="generated/enemy-pool-tests/independent.json";
    const nlohmann::json independentFixture={
        {"spawnPoints",nlohmann::json::array({
            {{"id","auto"},{"position",{0,0,40}},{"activation","StageStart"},{"enemy","normal_test"},
                {"spawnCount",3},{"maxAlive",1},{"mode","Sequential"},{"spawnInterval",.5}},
            {{"id","linked"},{"position",{10,0,40}},{"activation","OnTrigger"},{"groupId","A"},{"enemy","normal_test"},
                {"spawnCount",2},{"maxAlive",2},{"mode","Simultaneous"}},
            {{"id","manual"},{"position",{-10,0,40}},{"activation","Manual"},{"enemy","normal_test"},
                {"spawnCount",2},{"maxAlive",1},{"mode","Simultaneous"},{"initialDelay",1}}})},
        {"spawnTriggers",nlohmann::json::array({{{"id","activate"},{"groupId","A"},
            {"position",{0,0,0}},{"size",{2,2,2}}}})}};
    std::ofstream(independentPath)<<independentFixture;
    check(game.spawnSystem_.Load(independentPath,game.enemyDefinitions_),"independent scene fixture");
    game.level_.collision.colliders.clear(); game.player_.SetStage(&game.level_.collision,{1000,0,0},{});
    game.UpdateEnemySpawns(0,false); // same preparation used by OnEnter during FadeIn
    check(game.enemies_.size()==1 && !game.spawnSystem_.Triggers()[0].activated,"initial Stage Start preparation");
    auto* automatic=game.enemies_[0]; const auto automaticId=automatic->GetSpawnId();
    const auto preparedFrame=game.CaptureDebug();
    check(game.ActivateEnemySpawn("manual") && !game.ActivateEnemySpawn("manual") && !game.ActivateEnemySpawn("missing"),"manual common API");
    game.player_.SetStage(&game.level_.collision,{},{});
    game.UpdateEnemySpawns(0);
    check(game.enemies_.size()==3 && game.spawnSystem_.Points()[1].spawned==2,"trigger linked simultaneous scene wave");
    game.UpdateEnemySpawns(1);
    check(game.enemies_.size()==4 && game.spawnSystem_.Points()[2].spawned==1,"manual delay/capacity");
    const auto independentGpu=Object3d::DebugInitializationCount();
    automatic->Die(); automatic->UpdateVisuals(10); game.RecycleEnemies();
    game.UpdateEnemySpawns(0);
    check(game.enemies_.size()==4 && game.enemies_.back()==automatic && automatic->GetSpawnId()!=automaticId,"independent pooled refill");
    auto* manual=game.enemies_[2]; // linked[0], linked[1], manual, auto after recycle
    check(manual->GetSpawnTriggerId()=="manual","manual source identity");
    manual->Die(); manual->UpdateVisuals(10); game.RecycleEnemies(); game.UpdateEnemySpawns(0);
    check(game.enemies_.size()==4 && game.spawnSystem_.Points()[2].spawned==2,"simultaneous remainder lost after death");
    check(Object3d::DebugInitializationCount()==independentGpu,"independent refill initialized renderer");
    game.RestoreDebug(app,preparedFrame);
    check(game.enemies_.size()==1 && game.spawnSystem_.Points()[0].spawned==1 && !game.spawnSystem_.Points()[2].activated,
        "independent rewind state");
    for (auto* enemy:game.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    game.RecycleEnemies(); game.spawnSystem_.Reset(); game.UpdateEnemySpawns(0,false);
    check(game.enemies_.size()==1 && game.spawnSystem_.Points()[0].spawned==1 && !game.spawnSystem_.Triggers()[0].activated,
        "independent reset preparation");
    for (auto* enemy:game.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    game.RecycleEnemies();
    check(game.spawnSystem_.Load(independentPath,game.enemyDefinitions_),"independent restart reload");
    game.UpdateEnemySpawns(0,false);
    check(game.enemies_.size()==1 && game.spawnSystem_.Points()[0].spawned==1 && !game.spawnSystem_.Points()[2].activated,
        "independent restart state");
    for (auto* enemy:game.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    game.RecycleEnemies();
    auto mixed=independentFixture;
    mixed["spawnTriggers"]=nlohmann::json::array();
    mixed["spawnPoints"]=nlohmann::json::array({mixed["spawnPoints"][0]});
    auto& mixedPoint=mixed["spawnPoints"][0];
    mixedPoint.erase("enemy"); mixedPoint["spawnCount"]=5; mixedPoint["maxAlive"]=5; mixedPoint["mode"]="Simultaneous";
    mixedPoint["enemyComposition"]=nlohmann::json::array({{{"enemy","normal_test"},{"count",3}},{{"enemy","fast"},{"count",2}}});
    std::ofstream(independentPath)<<mixed;
    check(game.spawnSystem_.Load(independentPath,game.enemyDefinitions_),"mixed counts scene fixture");
    const auto mixedGpu=Object3d::DebugInitializationCount();
    game.UpdateEnemySpawns(0,false);
    check(game.enemies_.size()==5,"mixed counts simultaneous total");
    for (size_t i=0;i<5;++i) check(game.enemies_[i]->Definition().id==(i<3 ? "normal_test" : "fast"),"mixed counts exact definition order");
    check(Object3d::DebugInitializationCount()==mixedGpu,"mixed counts allocated a warmed renderer");
    for (auto* enemy:game.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    game.RecycleEnemies(); game.spawnSystem_.Reset(); game.UpdateEnemySpawns(0,false);
    check(game.enemies_.size()==5 && Object3d::DebugInitializationCount()==mixedGpu,"mixed counts reset/reuse");
    for (size_t i=0;i<5;++i) check(game.enemies_[i]->Definition().id==(i<3 ? "normal_test" : "fast"),"mixed counts reset order");
    game.OnExit(app);

    // Exercise the same reset and rewind functions used by F5 and the timeline.
    GameScene showroom(true); showroom.OnEnter(app);
    const auto showroomPointers=showroom.enemies_;
    const auto originalFrame=showroom.CaptureDebug();
    check(!showroomPointers.empty(),"showroom failed to load");
    for (auto* enemy:showroom.enemies_) enemy->Die();
    const auto deadFrame=showroom.CaptureDebug();
    const auto gpuReset=Object3d::DebugInitializationCount();
    showroom.ResetShowroomEnemies(app);
    check(showroom.enemies_==showroomPointers,"F5 replaced instances");
    check(Object3d::DebugInitializationCount()==gpuReset,"F5 created renderers");
    for (size_t i=0;i<showroom.enemies_.size();++i) verifyReset(showroom.enemies_[i],originalFrame.enemies[i]);
    showroom.RestoreDebug(app,deadFrame);
    size_t restoredFaces=0;
    for (size_t i=0;i<showroom.enemies_.size();++i) {
        const auto state=showroom.enemies_[i]->CaptureDebug();
        check(showroom.enemies_[i]->IsDead() && state.faces.size()==deadFrame.enemies[i].faces.size(),"rewind lost death effect");
        restoredFaces+=state.faces.size();
    }
    check(restoredFaces>0,"rewind lost all shards");
    showroom.RestoreDebug(app,originalFrame);
    for (size_t i=0;i<showroom.enemies_.size();++i) verifyReset(showroom.enemies_[i],originalFrame.enemies[i]);
    for (auto* enemy:showroom.enemies_) { enemy->Die(); enemy->UpdateVisuals(10); }
    showroom.RecycleEnemies();
    check(showroom.enemies_.empty() && showroom.enemyPool_.Active()==0,"scene recycle");
    showroom.ResetShowroomEnemies(app);
    check(showroom.enemies_==showroomPointers,"reset after recycle");
#ifdef USE_IMGUI
    {
        auto model=std::make_shared<EnemyModelSurface>();
        model->bounds={{-1,0,0},{1,2,1}};
        model->materials.resize(2);
        const auto addFace=[&](float x,float z,EnemyModelFaceId id) {
            EnemyModelSurface::Face face{};
            face.id=id;
            face.vertices[0].position={x-.1f,.1f,z};
            face.vertices[1].position={x+.1f,.1f,z};
            face.vertices[2].position={x,.4f,z};
            model->faces.push_back(face);
        };
        addFace(0,.1f,{0,0,0,0}); addFace(0,.8f,{1,1,0,0});
        addFace(-.65f,.3f,{1,1,0,1}); addFace(.65f,.3f,{2,2,1,0});
        EnemyModelEditingData editing;
        editing.topologyVertices={{0,1,2},{3,4,5},{4,5,6},{5,6,7}};
        EnemyEditor editor;
        editor.draft_.model=model;
        editor.draft_.parts.resize(2);
        const std::vector<size_t> initialOwners{0,kNoEnemyPart,kNoEnemyPart,1};
        editor.draft_.owners=initialOwners;
        editor.saved_=editor.draft_;
        editor.facePicking_=EnemyEditor::FacePicking(model.get(),editing);
        editor.selectionSlots_.assign(4,EnemyEditor::noSelection);
        editor.partFaceCounts_={1,1};
        editor.assignedFaceCount_=2;
        const auto viewport=Matrix4x4::Scale({.5f,.5f,.5f})*Matrix4x4::Translation({0,0,.5f});
        const ImVec2 lo{0,0},hi{100,100};
        const auto selected=[&](uint32_t face) { return editor.selectionSlots_[face]!=EnemyEditor::noSelection; };
        const auto hit=editor.PickFace(editor.facePicking_.Fit({0,.2f,-1}),{0,0,1},10);
        check(editor.lockAssignedFaces_ && hit==0,"assigned surface did not remain in the editor raycast");
        editor.ApplyPickedFace(static_cast<uint32_t>(hit));
        check(editor.selectedFaces_.empty(),"locked click picked an assigned face");
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        check(editor.selectedFaces_.size()==2 && selected(1) && selected(2),"select rectangle was not additive with the lock");
        editor.selectionMode_=EnemyEditor::SelectionMode::Deselect;
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,{40,100});
        editor.ApplyPickedFace(3);
        check(editor.selectedFaces_.size()==1 && selected(1),"deselect rectangle or click removed the wrong faces");
        editor.ApplyPickedFace(1);
        check(editor.selectedFaces_.empty() && editor.draft_.owners==initialOwners && !editor.dirty_,
            "selection operation changed the asset");
        editor.selectionMode_=EnemyEditor::SelectionMode::Select;
        editor.activePart_=0;
        editor.SelectPartFaces();
        check(editor.selectedFaces_.empty(),"part selection bypassed the assigned-face lock");
        editor.ApplyPickedFace(2);
        editor.ApplySelection(1);
        check(editor.draft_.owners==std::vector<size_t>{0,kNoEnemyPart,1,1} &&
            editor.partFaceCounts_==std::vector<size_t>{1,2} && editor.selectedFaces_.empty(),
            "locked assignment included another face or left the selection");
        editor.SetAssignedFaceLock(false);
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        check(editor.selectedFaces_.size()==4,"unlocked rectangle skipped assigned faces");
        editor.SetAssignedFaceLock(true);
        check(editor.selectedFaces_.size()==1 && selected(1),"enabling the lock failed to remove assigned selections");
        editor.SetAssignedFaceLock(false);
        editor.ApplyPickedFace(0);
        editor.SetFaceSelected(1,false);
        editor.ApplySelection(1);
        check(editor.draft_.owners[0]==1 && editor.partFaceCounts_==std::vector<size_t>{0,3} && editor.selectedFaces_.empty(),
            "unlocked reassignment failed");
        editor.StepHistory(false);
        check(editor.draft_.owners[0]==0 && editor.partFaceCounts_==std::vector<size_t>{1,2},"undo did not restore the previous owner");
        editor.StepHistory(true);
        editor.ApplyPickedFace(0);
        editor.ApplySelection(kNoEnemyPart);
        editor.SetAssignedFaceLock(true);
        check(selected(0) && editor.draft_.owners[0]==kNoEnemyPart && editor.selectedAssignedCount_==0,
            "unassigned face did not remain selectable");
        editor.ClearFaceSelection();
        editor.ApplyPickedFace(0);
        check(selected(0),"previously assigned face could not be picked again");

        editor.ClearFaceSelection();
        editor.SetAssignedFaceLock(false);
        editor.faceScope_=EnemyEditor::FaceScope::Material;
        editor.ApplyPickedFace(1);
        check(editor.selectedFaces_.size()==3 && selected(0) && selected(1) && selected(2),
            "material scope lost a separate mesh using the same material");
        editor.ClearFaceSelection();
        editor.faceScope_=EnemyEditor::FaceScope::Mesh;
        editor.ApplyPickedRectangle(viewport,lo,hi,{44,0},{56,100});
        check(editor.selectedFaces_.size()==3,"rectangle did not expand each seeded mesh once");
        editor.ClearFaceSelection();
        editor.ApplyPickedFace(1);
        check(editor.selectedFaces_.size()==2 && selected(1) && selected(2),"mesh scope crossed into another mesh");
        editor.ClearFaceSelection();
        editor.faceScope_=EnemyEditor::FaceScope::ConnectedInMaterial;
        editor.ApplyPickedFace(1);
        check(editor.selectedFaces_.size()==2 && selected(1) && selected(2),"material component crossed a material or island boundary");
        editor.ClearFaceSelection();
        editor.faceScope_=EnemyEditor::FaceScope::Connected;
        editor.ApplyPickedFace(1);
        check(editor.selectedFaces_.size()==3 && selected(3),"connected scope did not cross a shared material edge");
        editor.selectionMode_=EnemyEditor::SelectionMode::Deselect;
        editor.ApplyPickedRectangle(viewport,lo,hi,{44,0},{56,100});
        check(editor.selectedFaces_.empty(),"group rectangle did not deselect the entire component");
        editor.selectionMode_=EnemyEditor::SelectionMode::Select;
        editor.faceScope_=EnemyEditor::FaceScope::Triangle;
        editor.facePicking_.materials[1].locked=true;
        editor.SetAssignedFaceLock(true);
        editor.ApplyPickedFace(1);
        editor.SelectLinked();
        check(editor.selectedFaces_.size()==1 && selected(1),"linked selection bypassed locks");
        editor.SetAssignedFaceLock(false);
        editor.SelectLinked();
        check(editor.selectedFaces_.size()==2 && selected(2) && !selected(3),"linked selection bypassed the material lock");
        editor.facePicking_.meshes[0].hidden=true;
        const auto inner=editor.PickFace(editor.facePicking_.Fit({0,.2f,-1}),{0,0,1},10);
        check(inner==1,"hidden mesh occluded the face behind it");
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        check(!selected(0),"rectangle selected a hidden face");
        editor.ShowAllSurfaces();
        check(editor.FaceVisible(0) && !editor.CanSelect(3),"Show All changed a selection lock");
    }
    const auto testEnemy=[&]() -> Enemy& {
        for (auto* enemy:showroom.enemies_) if (enemy->Definition().id=="normal_test") return *enemy;
        throw std::runtime_error("Showroom Normal Test is missing");
    };
    const auto testAssetPath=std::filesystem::path(u8"generated/enemy-pool-tests/差し替え.enemy.json");
    auto testAsset=read("resources/enemy/boss/normal.enemy.json");
    testAsset["parts"][0]["localHp"]=75;
    std::ofstream(testAssetPath)<<testAsset;
    const auto normalAsset=showroom.enemies_.front()->Definition().partAsset;
    const auto capacity=showroom.enemyPool_.Capacity();
    showroom.pendingShowroomTestAsset_=testAssetPath;
    showroom.Update(app,0);
    check(showroom.showroomTestAsset_==testAssetPath && testEnemy().CaptureDebug().parts.front().hp==75,"Showroom switch ignored selected asset");
    check(showroom.enemies_.front()==showroomPointers.front() && showroom.enemies_.front()->Definition().partAsset==normalAsset,
        "Showroom switch replaced the regular enemy definition or pool slot");
    const auto firstGeometry=testEnemy().CaptureDebug().parts.front().geometry;
    testAsset["parts"][0]["localHp"]=90;
    for (auto& triangle:testAsset["parts"][0]["triangles"]) for (auto& vertex:triangle["vertices"])
        vertex["p"][0]=vertex["p"][0].get<float>()+.25f;
    std::ofstream(testAssetPath)<<testAsset;
    showroom.pendingShowroomTestAsset_=testAssetPath;
    showroom.Update(app,0);
    const auto loaded=testEnemy().CaptureDebug();
    check(loaded.parts.front().hp==90 && loaded.parts.front().geometry!=firstGeometry,
        "Showroom reload used the previous asset from cache");
    const auto* reloaded=&testEnemy();
    showroom.ResetShowroomEnemies(app);
    check(&testEnemy()==reloaded && testEnemy().CaptureDebug().parts.front().hp==90 && showroom.enemyPool_.Capacity()==capacity,
        "Showroom reset lost the selected asset or grew the pool");
    std::ofstream(testAssetPath)<<"invalid JSON";
    showroom.pendingShowroomTestAsset_=testAssetPath;
    showroom.Update(app,0);
    check(!showroom.showroomTestAssetError_.empty() && &testEnemy()==reloaded && testEnemy().CaptureDebug().parts.front().hp==90,
        "invalid Showroom JSON replaced the current enemy");

    const auto documentFor=[](const std::string& path,const EnemyModelSurface& model) {
        nlohmann::json faces=nlohmann::json::array();
        for (const auto& face:model.faces) {
            const auto& id=face.id;
            faces.push_back({id.instance,id.mesh,id.material,id.triangle});
        }
        const nlohmann::json part{{"name","Model Body"},{"role","Body"},{"localHp",5},{"breakable",true},
            {"sharedHpGroup","Team"},{"sharedDamageRate",.5},{"faces",std::move(faces)}};
        return nlohmann::json{{"version",2},{"coordinateSystem","yanengine"},
            {"model",{{"path",path},{"geometrySignature",model.signature},{"faceCount",model.faces.size()}}},
            {"hpGroups",nlohmann::json::array({{{"id","Team"},{"maxHp",50}}})},
            {"parts",nlohmann::json::array({part})}};
    };
    {
        auto surface=std::make_shared<EnemyModelSurface>();
        surface->bounds={{-.8f,0,0},{3.8f,1.6f,2}};
        surface->signature="layered-bullet-collision";
        surface->materials.push_back({{}, {1,1,1,1}});
        const auto addLayer=[&](float x,float z) {
            EnemyModelSurface::Face face{{0,0,0,static_cast<uint32_t>(surface->faces.size())},{}};
            face.vertices[0].position={x-.8f,0,z};
            face.vertices[1].position={x+.8f,0,z};
            face.vertices[2].position={x,1.6f,z};
            surface->faces.push_back(face);
        };
        addLayer(0,0); // Hair in front of an unassigned layer and the body.
        addLayer(0,1);
        addLayer(0,2);
        addLayer(3,0); // Hair with nothing behind it.

        auto document=documentFor("resources/fixture.obj",*surface);
        auto& parts=document["parts"];
        parts[0]["faces"]=nlohmann::json::array({{0,0,0,2}});
        parts.insert(parts.begin(),nlohmann::json{{"name","Hair"},{"localHp",40},
            {"faces",nlohmann::json::array({{0,0,0,0},{0,0,0,3}})}});
        const auto replace=[&]() -> Enemy& {
            showroom.ReplaceShowroomTest(app,EnemyAsset::Parse(document,{},surface),"normal");
            return testEnemy();
        };
        const auto shotView=[&](const Enemy& enemy,float x) {
            const auto pose=enemy.CaptureDebug();
            const auto local=surface->Fit({x,.5f,-3});
            const auto transform=Matrix4x4::MakeAffineMatrix(enemy.Definition().VisualScale(pose.scale),pose.rotation,pose.position);
            const auto origin=EnemyPartTransformPoint(local,transform);
            return Matrix4x4::MakeAffineMatrix({1,1,1},pose.rotation,origin);
        };
        const auto fire=[&](Enemy& enemy,float x) -> std::optional<BulletEnemyImpact> {
            auto testWeapon=weapon;
            testWeapon.pelletCount=1;
            testWeapon.hipSpreadDegrees=testWeapon.adsSpreadDegrees=0;
            const std::vector<Enemy*> targets{&enemy};
            showroom.bullets_.Clear();
            showroom.bullets_.Spawn(testWeapon,shotView(enemy,x),0,random,world,targets);
            std::optional<BulletEnemyImpact> impact;
            showroom.bullets_.Update(.25f,world,targets,[&](const BulletEnemyImpact& hit) { impact=hit; });
            return impact;
        };

        auto& defaultOn=replace();
        const auto first=fire(defaultOn,0);
        check(first && first->partName=="Hair" && first->result.damage==10 && defaultOn.CaptureDebug().parts[0].hp==30,
            "omitted bullet collision did not preserve the front hit");

        parts[0]["bulletCollision"]=false;
        auto& blockingUnassigned=replace();
        const auto block=fire(blockingUnassigned,0);
        const auto blocked=blockingUnassigned.CaptureDebug();
        check(block && block->partName=="__Unassigned" && block->result.damage==0 && blocked.parts[1].hp==5 &&
            !blocked.parts[2].Destroyed() && blocked.detached.empty(),"unassigned faces did not block without damage");
        check(blocked.parts[0].hp==40 && blocked.parts[0].flashRemaining==0,"disabled hair got damage or hit feedback");

        document["unassignedFacesBlockBullets"]=false;
        auto& passThrough=replace();
        const auto isolated=shotView(passThrough,3);
        const Vector3 origin{isolated.m[3][0],isolated.m[3][1],isolated.m[3][2]};
        const Vector3 direction{isolated.m[2][0],isolated.m[2][1],isolated.m[2][2]};
        EnemyPartHit hit;
        check(passThrough.Raycast(origin,direction,10,hit) && hit.partIndex==0 &&
            !passThrough.RaycastBullet(origin,direction,10,hit) && !fire(passThrough,3),
            "bullet hit disabled-only geometry or the general-purpose raycast lost it");
        check(passThrough.ApplyBulletDamage(size_t{0},10,direction).damage==0,"direct bullet damage bypassed the filter");
        const auto rear=fire(passThrough,0);
        const auto broken=passThrough.CaptureDebug();
        check(rear && rear->partName=="Model Body" && broken.parts[1].Destroyed() && !broken.visible[1] &&
            broken.parts.hpGroups[0].hp==45,"bullet did not pass through to damage and break the enabled body");
        check(broken.visible[0] && broken.visible[2] && broken.models[0] && broken.models[2] &&
            broken.parts[0].hp==40 && broken.parts[0].flashRemaining==0 && broken.parts[2].flashRemaining==0 && !passThrough.IsDead(),
            "disabled surfaces lost their renderers or generated hit feedback");

        passThrough.Die(direction);
        const auto dead=passThrough.CaptureDebug();
        check(passThrough.IsDead() && dead.ai.state==EnemyState::Dead && !dead.visible[0] && !dead.visible[2] &&
            dead.detached.size()==broken.detached.size()+2,"bullet collision changed death and debris handling");
    }
    {
        using Clock=std::chrono::steady_clock;
        const auto elapsed=[](auto start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); };
        EnemyEditor editor;
        const std::filesystem::path emaPath="resources/ema/SakurabaEma_ByPOWER.pmx";
        auto start=Clock::now();
        editor.OpenDraft(app,{EnemyEditor::Action::Model,emaPath});
        const double openMs=elapsed(start);
        const auto& model=*editor.draft_.model;
        check(model.faces.size()==130712,"EMA test did not load the expected PMX geometry");
        const auto& names=editor.editingData_.materialNames;
        const auto body=static_cast<size_t>(std::find(names.begin(),names.end(),"Mt_SakurabaEma_Body")-names.begin());
        check(body<names.size() && editor.editingData_.meshNames[body]==names[body],"EMA mesh or material names missing");
        const auto isClothing=[&](size_t material) { return names[material].starts_with("Mt_SakurabaEma_Clothes"); };
        // This body triangle's projected centroid is covered by Clothes01 from negative Z.
        const auto bodySample=editor.facePicking_.materials[body].faces.at(142);
        auto eye=editor.facePicking_.centroids[bodySample];
        eye.z=-4;
        const Vector3 direction{0,0,1};
        start=Clock::now();
        const auto clothing=editor.PickFace(eye,direction,8);
        check(clothing!=kNoEnemyPart && isClothing(model.faces[clothing].id.material),"EMA clothing did not occlude the body");
        editor.ApplyPickedFace(static_cast<uint32_t>(clothing));
        editor.SelectLinked();
        const double linkedMs=elapsed(start);
        check(editor.selectedFaces_.size()>1,"EMA clothing seed did not expand to linked faces");
        const auto material=model.faces[clothing].id.material;
        editor.ClearFaceSelection();
        editor.faceScope_=EnemyEditor::FaceScope::Material;
        editor.ApplyPickedFace(static_cast<uint32_t>(clothing));
        const auto materialCount=editor.selectedFaces_.size();
        editor.ClearFaceSelection();
        editor.faceScope_=EnemyEditor::FaceScope::ConnectedInMaterial;
        editor.ApplyPickedFace(static_cast<uint32_t>(clothing));
        const auto componentCount=editor.selectedFaces_.size();
        check(componentCount>1 && componentCount<materialCount && std::all_of(editor.selectedFaces_.begin(),editor.selectedFaces_.end(),
            [&](auto face) { return model.faces[face].id.material==material; }),"EMA material component did not narrow the selection");

        EnemyPart part;
        part.name="EMA clothing";
        part.role=EnemyPartRole::Body;
        part.usesLocalHp=part.breakable=true;
        part.hp=part.maxHp=5;
        editor.draft_.parts.push_back(part);
        editor.partFaceCounts_.push_back(0);
        editor.activePart_=0;
        editor.saved_=editor.draft_;
        editor.ApplySelection(0);
        check(editor.selectedFaces_.empty() && editor.partFaceCounts_[0]==componentCount,"EMA assignment left selection or lost faces");
        editor.StepHistory(false);
        check(!editor.dirty_ && editor.partFaceCounts_[0]==0,"EMA undo did not return to the saved draft");
        editor.StepHistory(true);
        check(editor.dirty_ && editor.partFaceCounts_[0]==componentCount,"EMA redo lost face assignments");
        start=Clock::now();
        editor.Update(app);
        const double rebuildMs=elapsed(start);
        check(!editor.previewDirty_ && editor.message_.empty(),"EMA assigned preview did not rebuild");
        const auto* previewModel=editor.preview_.front().model.get();
        const auto ordinaryColor=editor.preview_.front().object->GetMaterialColor();
        editor.SetBulletCollision(0,false);
        editor.SetBulletCollision(kNoEnemyPart,false);
        editor.StepHistory(false);
        check(!editor.draft_.BulletCollisionEnabled(0) && editor.draft_.unassignedFacesBlockBullets,"undo unassigned bullet collision");
        editor.StepHistory(false);
        check(editor.draft_.BulletCollisionEnabled(0) && editor.partFaceCounts_[0]==componentCount,"undo part bullet collision");
        editor.StepHistory(true);
        editor.StepHistory(true);
        check(!editor.draft_.BulletCollisionEnabled(0) && !editor.draft_.unassignedFacesBlockBullets,"redo bullet collision");
        editor.showBulletCollision_=true;
        editor.Update(app);
        const auto overlay=editor.preview_.front().object->GetMaterialColor();
        check(editor.preview_.front().model.get()==previewModel && overlay.x==1 && overlay.y<.3f && overlay.z>.7f && overlay.w==1,
            "disabled collision was not color-coded without replacing or hiding the geometry");
        editor.showBulletCollision_=false;
        editor.Update(app);
        const auto restored=editor.preview_.front().object->GetMaterialColor();
        check(restored.x==ordinaryColor.x && restored.y==ordinaryColor.y && restored.z==ordinaryColor.z,"collision overlay did not turn off");
        const auto before=editor.draft_.Document();
        start=Clock::now();
        for (size_t i=0;i<names.size();++i) editor.facePicking_.materials[i].hidden=isClothing(i);
        editor.UpdatePreviewVisibility();
        editor.PruneSelection();
        const double hideMs=elapsed(start);
        check(editor.preview_.front().model.get()==previewModel && !editor.preview_.front().object->GetHiddenMeshes().empty(),
            "EMA visibility did not mask the existing preview meshes");
        const auto inner=editor.PickFace(eye,direction,8);
        check(inner!=kNoEnemyPart && model.faces[inner].id.material==body,"EMA body could not be picked after hiding clothes");
        editor.faceScope_=EnemyEditor::FaceScope::Triangle;
        editor.ApplyPickedFace(static_cast<uint32_t>(inner));
        check(editor.selectedFaces_==std::vector<uint32_t>{static_cast<uint32_t>(inner)},"EMA body face was not selectable");
        editor.ClearFaceSelection();
        const auto viewport=Matrix4x4::Scale({.25f,.25f,.25f})*Matrix4x4::Translation({0,0,.5f});
        const ImVec2 lo{0,0},hi{1280,720};
        editor.faceScope_=EnemyEditor::FaceScope::Material;
        start=Clock::now();
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        const double boxMs=elapsed(start);
        check(!editor.selectedFaces_.empty() && std::none_of(editor.selectedFaces_.begin(),editor.selectedFaces_.end(),
            [&](auto face) { return isClothing(model.faces[face].id.material); }),"EMA rectangle picked hidden clothing");
        editor.IsolateSurfaceGroup(editor.facePicking_.materials,body);
        check(editor.selectedFaces_.size()==editor.facePicking_.materials[body].faces.size(),"EMA isolate did not retain only body selection");
        editor.ClearFaceSelection();
        editor.facePicking_.materials[body].locked=true;
        editor.ShowAllSurfaces();
        check(editor.PickFace(eye,direction,8)==clothing && !editor.CanSelect(static_cast<uint32_t>(inner)) &&
            editor.draft_.Document()==before,"EMA Show All lost a lock or modified the asset");

        editor.facePicking_.materials[body].locked=false;
        editor.faceScope_=EnemyEditor::FaceScope::Triangle;
        check(!editor.editingData_.skinWeights.empty(),"EMA bone weights missing");
        editor.activeBone_=editor.editingData_.skinWeights.begin()->first;
        editor.boneThreshold_=.01f;
        editor.SelectBoneCandidates();
        check(!editor.selectedFaces_.empty(),"EMA bone weight selection returned no eligible candidates");
        const auto candidate=editor.selectedFaces_.back();
        editor.selectionMode_=EnemyEditor::SelectionMode::Deselect;
        editor.ApplyPickedFace(candidate);
        check(editor.selectionSlots_[candidate]==EnemyEditor::noSelection,"EMA bone candidate could not be deselected manually");

        editor.IsolateSurfaceGroup(editor.facePicking_.materials,body);
        const std::filesystem::path path="generated/enemy-pool-tests/ema.enemy.json";
        editor.Save(path);
        check(read("generated/enemy-pool-tests/ema.enemy.json")==before,"EMA saved JSON included temporary visibility changes");
        const auto emaAsset=EnemyAsset::Parse(editor.draft_.Document(),path,editor.draft_.model);
        showroom.ReplaceShowroomTest(app,emaAsset,"normal");
        auto& enemy=testEnemy();
        const auto pose=enemy.CaptureDebug();
        const auto worldMat=Matrix4x4::MakeAffineMatrix(enemy.Definition().VisualScale(pose.scale),pose.rotation,pose.position);
        const auto origin=EnemyPartTransformPoint(eye,worldMat);
        const auto shot=Matrix4x4::Normalize(EnemyPartTransformPoint(eye+direction,worldMat)-origin);
        EnemyPartHit hit;
        check(enemy.Raycast(origin,shot,20,hit) && hit.partIndex==0,"EMA hidden editor clothing lost the Showroom collider");
        EnemyPartHit bulletHit;
        check(!enemy.RaycastBullet(origin,shot,20,bulletHit),"EMA draft bullet collision did not reach Showroom");
        enemy.ApplyDamage(hit.partIndex,10,shot);
        const auto broken=enemy.CaptureDebug();
        check(broken.parts.front().Destroyed() && !broken.detached.empty(),"EMA clothing did not break in Showroom");
        editor.OpenDraft(app,{EnemyEditor::Action::Load,path});
        check(editor.draft_.Document()==before && !editor.draft_.BulletCollisionEnabled(0) &&
            !editor.draft_.unassignedFacesBlockBullets && editor.partFaceCounts_[0]==componentCount &&
            editor.FaceVisible(static_cast<uint32_t>(clothing)) && editor.preview_.front().object->GetHiddenMeshes().empty(),
            "EMA reload changed assignments or restored a temporary visibility mask");
        std::ofstream("generated/enemy-pool-tests/ema-selection.txt")<<"faces="<<model.faces.size()<<" linked="<<componentCount
            <<" material="<<materialCount<<" open_ms="<<openMs<<" linked_pick_ms="<<linkedMs<<" preview_rebuild_ms="<<rebuildMs
            <<" hide_ms="<<hideMs<<" rectangle_ms="<<boxMs<<'\n';
    }
    const auto pmxPath=EnemyPathText(std::filesystem::path(u8"resources/MyGtYUhe6t/安比.pmx"));
    const auto pmx=ReadEnemyModel(pmxPath);
    check(pmx->materials.size()>1 && pmx->faces.size()>1000,"PMX multi-material import");
    {
        EnemyEditor editor;
        editor.draft_.model=pmx;
        editor.draft_.owners.resize(pmx->faces.size(),kNoEnemyPart);
        editor.facePicking_=EnemyEditor::FacePicking(pmx.get());
        editor.selectionSlots_.resize(pmx->faces.size(),EnemyEditor::noSelection);
        const auto viewport=Matrix4x4::Scale({.25f,.25f,.25f})*Matrix4x4::Translation({0,0,.5f});
        const ImVec2 lo{0,0},hi{1280,720};
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        check(editor.selectedFaces_.size()==pmx->faces.size(),"PMX rectangle did not select all visible centroids");
        editor.selectionMode_=EnemyEditor::SelectionMode::Deselect;
        editor.ApplyPickedRectangle(viewport,lo,hi,lo,hi);
        check(editor.selectedFaces_.empty(),"PMX rectangle skipped faces during deselection");
    }
    const auto pmxAssetPath="generated/enemy-pool-tests/pmx.enemy.json";
    std::ofstream(pmxAssetPath)<<documentFor(pmxPath,*pmx);
    const auto configPath="generated/enemy-pool-tests/showroom-test.json";
    std::ofstream(configPath)<<nlohmann::json{{"enemyAsset",pmxAssetPath},{"enemyDefinition","normal"}};
    check(showroom.LoadShowroomTestConfig(app,configPath),"Showroom PMX config load");
    check(testEnemy().Definition().partAsset->modelPath==pmxPath &&
        testEnemy().CaptureDebug().models[0]->GetMeshCount()>1,"Showroom PMX render materials");
    check(showroom.enemies_.front()==showroomPointers.front() && showroom.enemies_.front()->Definition().partAsset==normalAsset,
        "model reference replaced regular Normal");
    app.Srv()->CollectRetired();
    const auto pmxDescriptors=app.Srv()->ActiveCount();

    const std::string gltfPath="resources/levels/stage03/shutter_000.gltf";
    const auto gltf=ReadEnemyModel(gltfPath);
    const auto skinnedGltf=ReadEnemyModel("resources/CGTest/RobotExpressive.glb");
    check(!skinnedGltf->faces.empty() && skinnedGltf->materials.size()>1,"skinned GLB rest geometry");
    const auto gltfDoc=documentFor(gltfPath,*gltf);
    const auto gltfAssetPath="generated/enemy-pool-tests/gltf.enemy.json";
    std::ofstream(gltfAssetPath)<<gltfDoc;
    const auto gltfAsset=EnemyAsset::Load(gltfAssetPath,ReadEnemyModel);
    check(gltfAsset->defaults.front().geometry->faces.size()==gltf->faces.size(),"glTF JSON did not restore faces");
    auto* beforeInvalid=&testEnemy();
    auto invalid=gltfDoc;
    invalid["model"]["geometrySignature"]="modified-source";
    std::ofstream(testAssetPath)<<invalid;
    check(!showroom.ApplyShowroomTestAsset(app,testAssetPath) && &testEnemy()==beforeInvalid,
        "invalid v2 geometry signature replaced the active target");
    {
        showroom.ReplaceShowroomTest(app,gltfAsset,"normal");
        showroom.showroomDraftApplied_=true;
        check(testEnemy().Definition().partAsset->modelPath==gltfPath,"glTF draft model switch");
        const auto partGeometry=gltfAsset->defaults.front().geometry;
        const auto pose=testEnemy().CaptureDebug();
        const auto transform=Matrix4x4::MakeAffineMatrix(testEnemy().Definition().VisualScale(pose.scale),pose.rotation,pose.position);
        Matrix4x4 view;
        bool aimed=false;
        for (const auto& tri:partGeometry->faces) {
            const auto a=EnemyPartTransformPoint(tri[0],transform);
            const auto b=EnemyPartTransformPoint(tri[1],transform);
            const auto c=EnemyPartTransformPoint(tri[2],transform);
            const auto n=Matrix4x4::Normalize(Matrix4x4::Cross(b-a,c-a));
            const auto eye=(a+b+c)*(1.f/3)+n*4;
            const auto direction=n*(-1.f);
            EnemyPartHit hit;
            if (!testEnemy().Raycast(eye,direction,10,hit)) continue;
            view=Matrix4x4::MakeAffineMatrix({1,1,1},{-std::asin(direction.y),std::atan2(direction.x,direction.z),0},eye);
            aimed=true;
            break;
        }
        check(aimed,"glTF runtime triangle raycast missed");
        showroom.bullets_.Spawn(weapon,view,0,random,world,showroom.enemies_);
        size_t modelHits=0;
        showroom.bullets_.Update(.25f,world,showroom.enemies_,[&](const BulletEnemyImpact& hit) {
            if (hit.partName=="Model Body" && hit.result.damage>0) ++modelHits;
        });
        const auto damageState=testEnemy().CaptureDebug();
        check(modelHits==1 && damageState.parts.front().Destroyed() && !damageState.detached.empty() &&
            damageState.parts.hpGroups.front().hp==45,"glTF bullet did not break referenced part or route shared HP");
    }
    showroom.resetEnemiesPending_=true;
    showroom.Update(app,0);
    app.Srv()->CollectRetired();
    check(!showroom.showroomDraftApplied_ && testEnemy().Definition().partAsset->modelPath==pmxPath &&
        testEnemy().CaptureDebug().parts.front().hp==5 && testEnemy().CaptureDebug().detached.empty(),
        "F5 did not restore saved model, HP and debris");
    for (int i=0;i<3;++i) {
        check(showroom.ApplyShowroomTestAsset(app,gltfAssetPath),"repeated glTF asset load");
        app.Srv()->CollectRetired();
        check(showroom.LoadShowroomTestConfig(app,configPath),"repeated PMX asset load");
        app.Srv()->CollectRetired();
        check(app.Srv()->ActiveCount()==pmxDescriptors && showroom.enemyPool_.Capacity()==capacity,
            "model switching leaked texture descriptors or pool capacity");
    }
#endif
    showroom.OnExit(app);
    std::ofstream("generated/enemy-pool-tests/result.txt")<<"PASS: real D3D pool reuse, all HP modes, full-body death, chunks, types, bullet/explosion, per-part and unassigned bullet filtering, group spawning/AI/rewind, Enemy Editor selection scopes, visibility, locks, bone candidates, bullet collision overlay, undo/redo and EMA JSON round trip, Showroom reset and PMX / glTF switching, runtime hit and break, shared HP, descriptor lifetime, animation and animated mesh surface raycasts.\n";
}
