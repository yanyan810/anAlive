#pragma once
// Runs in the Debug executable with real D3D resources: --enemy-pool-test.
#include "GameApp.h"
#include "scene/Main/GameScene.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

inline void RunEnemyPoolTests(GameApp& app) {
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
    const auto verifyReset=[&](Enemy* enemy,const Enemy::DebugState& original) {
        const auto state=enemy->CaptureDebug();
        check(!enemy->IsDead() && state.ai.state==EnemyState::Idle && state.ai.cooldown==0 && state.ai.attacksThisUpdate==0,"AI reset");
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
        const auto gpuBefore=Object3d::debugInitializationCount;
        auto* enemy=pool.Acquire(id,next++,"first",{0,0,12},{});
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
        check(Object3d::debugInitializationCount==gpuBefore,"Acquire initialized Object3d");
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
        const auto gpuReuse=Object3d::debugInitializationCount;
        auto* reused=pool.Acquire(id,next++,"second",{4,0,16},{0,1,0});
        check(reused==enemy && reused->GetSpawnId()==next-1 && reused->GetSpawnTriggerId()=="second","instance/identity reuse");
        check(reused->GetPosition().x==4 && reused->GetPosition().z==16,"spawn transform reset");
        verifyReset(reused,original);
        check(Object3d::debugInitializationCount==gpuReuse,"reuse allocated GPU renderers");
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
    const auto gpuChunk=Object3d::debugInitializationCount;
    verifyReset(pool.Acquire("normal",next++,"chunk reset",{},{}),chunkOriginal);
    check(Object3d::debugInitializationCount==gpuChunk,"chunk respawn initialized renderer");
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
    const auto spawnGpu=Object3d::debugInitializationCount;
    game.UpdateCombat(app,0,false);
    check(game.enemies_.size()==1,"scene trigger did not spawn");
    auto* spawned=game.enemies_[0]; const auto oldId=spawned->GetSpawnId();
    spawned->Die(); spawned->UpdateVisuals(10); game.RecycleEnemies();
    game.UpdateCombat(app,0,false);
    check(game.enemies_.size()==1 && game.enemies_[0]==spawned && spawned->GetSpawnId()!=oldId,"trigger reuse/ID tracking");
    check(game.stage_.DefeatedCount()==1,"scene lost defeat on release");
    check(game.enemyPool_.RuntimeAllocations()==0 && Object3d::debugInitializationCount==spawnGpu,"scene spawn allocated renderers");
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
    const auto beforeGroupGpu=Object3d::debugInitializationCount;
    game.UpdateCombat(app,2.5f,false);
    const auto beforeGroup=game.CaptureDebug();
    check(game.enemies_.empty() && game.enemyPool_.Active()==0,"group spawned before start time");
    game.UpdateCombat(app,.5f,false);
    check(game.enemies_.size()==6 && game.enemyPool_.Active()==6 && game.spawnSystem_.Groups()[0].spawned==6,
        "simultaneous group not entirely active in one scene update");
    check(game.enemyPool_.RuntimeAllocations()==1,"simultaneous overflow bypassed existing pool expansion");
    check(Object3d::debugInitializationCount>beforeGroupGpu,"overflow slot was not prepared");
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
        enemy->Update(.125f,enemy->GetPosition()+Vector3{0,0,2});
        check(enemy->GetState()!=EnemyState::Idle,"group AI did not advance");
        enemy->Die();
    }
    game.RecycleEnemies();
    check(game.enemies_.size()==6,"group debris returned too early");
    for (auto* enemy:game.enemies_) enemy->UpdateVisuals(10);
    game.RecycleEnemies();
    check(game.enemies_.empty() && game.enemyPool_.Active()==0,"group enemies did not return to pool");
    const auto reusedGpu=Object3d::debugInitializationCount;
    game.spawnSystem_.Reset(); game.UpdateCombat(app,3,false);
    check(game.enemies_==groupPointers && game.enemyPool_.RuntimeAllocations()==1,"group slots not reused after reset");
    check(Object3d::debugInitializationCount==reusedGpu,"group reuse initialized renderers");
    for (size_t i=0;i<game.enemies_.size();++i) {
        verifyReset(game.enemies_[i],groupOriginal[i]);
        check(!groupIds.count(game.enemies_[i]->GetSpawnId()),"group reuse retained a dead spawn ID");
    }
    game.RestoreDebug(app,beforeGroup);
    check(game.enemies_.empty() && game.spawnSystem_.ElapsedTime()==2.5 && game.spawnSystem_.Groups()[0].spawned==0,"group rewind state");
    game.UpdateCombat(app,.5f,false);
    check(game.enemies_==groupPointers && Object3d::debugInitializationCount==reusedGpu,"group rewind refire/reuse");
    game.OnExit(app);
    game.OnEnter(app); // The same scene lifecycle used by Restart Stage.
    check(game.spawnSystem_.Load(groupPath,game.enemyDefinitions_),"restart group reload");
    game.level_.collision.colliders.clear(); game.player_.SetStage(&game.level_.collision,{1000,0,0},{});
    check(game.spawnSystem_.ElapsedTime()==0 && game.spawnSystem_.Groups()[0].spawned==0 && game.enemies_.empty(),"restart group state");
    game.UpdateCombat(app,2.5f,false); check(game.enemies_.empty(),"restart group spawned early");
    game.UpdateCombat(app,.5f,false); check(game.enemies_.size()==6 && game.enemyPool_.Active()==6,"restart group did not refire");
    game.OnExit(app);

    // Exercise the same reset and rewind functions used by F5 and the timeline.
    GameScene showroom(true); showroom.OnEnter(app);
    const auto showroomPointers=showroom.enemies_;
    const auto originalFrame=showroom.CaptureDebug();
    check(!showroomPointers.empty(),"showroom failed to load");
    for (auto* enemy:showroom.enemies_) enemy->Die();
    const auto deadFrame=showroom.CaptureDebug();
    const auto gpuReset=Object3d::debugInitializationCount;
    showroom.ResetShowroomEnemies(app);
    check(showroom.enemies_==showroomPointers,"F5 replaced instances");
    check(Object3d::debugInitializationCount==gpuReset,"F5 created renderers");
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
    showroom.OnExit(app);
    std::ofstream("generated/enemy-pool-tests/result.txt")<<"PASS: real D3D pool reuse, no Object3d Initialize on acquire/reset, all HP modes, full-body death, chunks, types, overflow, bullet/explosion, spawn IDs, same-update SpawnGroup activation/poses/definitions, group AI/HP/parts/death/recycle/reuse/overflow/rewind/restart, Showroom reset, procedural animation modes/render transforms, unchanged AI, animated mesh surface raycasts, old upright hitbox removed, animation reset/rewind.\n";
}
