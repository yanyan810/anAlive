#include "EnemyDefinition.h"
#include "EnemyAI.h"
#include "DetachedEnemyPart.h"
#include "EnemyProjectile.h"
#include "EnemyExplosion.h"
#include "EnemySpawnSystem.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
using nlohmann::json;
static void Save(const json& j) { std::ofstream("enemy-types-test.json")<<j; }
static bool Near(float a,float b) { return std::abs(a-b)<.001f; }
int main() {
    EnemyDefinitions definitions;
    assert(definitions.Load("../../resources/Data/enemies.json"));
    json valid; std::ifstream("../../resources/Data/enemies.json")>>valid;
    for (auto type : {EnemyType::Normal,EnemyType::Ranged,EnemyType::Fast,EnemyType::Tank,EnemyType::Bomber}) {
        std::string id=EnemyTypeName(type); id[0]=static_cast<char>(std::tolower(id[0]));
        const auto* d=definitions.Find(id); assert(d && d->type==type);
    }
    const auto reject=[&](json data) {
        Save(data); assert(!definitions.Load("enemy-types-test.json"));
        assert(definitions.Find("tank") && definitions.Find("tank")->hpMultiplier==2.5f);
    };
    // Optional visual fields preserve legacy definitions; malformed data stays transactional.
    auto legacy=valid;
    for (auto& item : legacy["enemies"]) { item.erase("visualScale"); item.erase("typeMarker"); item.erase("collisionRadius"); item.erase("collisionHeight"); }
    EnemyDefinitions old; Save(legacy); assert(old.Load("enemy-types-test.json"));
    assert(Near(old.Find("tank")->collisionRadius,.55f) && Near(old.Find("tank")->collisionHeight,4.96f));
    for (const auto* field : {"collisionRadius","collisionHeight"}) {
        for (auto value : {json(-1),json(0),json("bad"),json(nullptr)}) {
            auto invalidCollision=valid; invalidCollision["enemies"][0][field]=value; reject(invalidCollision);
        }
    }
    const auto& tank=*definitions.Find("tank"); const auto& fast=*definitions.Find("fast");
    assert(Near(tank.collisionRadius,.45f) && Near(tank.collisionHeight,4.07f));
    assert(Near(fast.collisionRadius,.29f) && Near(fast.collisionHeight,2.58f));
    const auto offset=EnemySeparationOffset({},tank.collisionRadius,{.5f,0,0},fast.collisionRadius,1);
    assert(Near(.5f-2*offset.x,tank.collisionRadius+fast.collisionRadius));
    assert(EnemySeparationOffset({},fast.collisionRadius,{.7f,0,0},fast.collisionRadius,1).x==0);
    assert(Near(EnemySeparationOffset({},.8f,{1.2f,0,0},.8f,1).x,-.2f));
    assert(Near(EnemySeparationOffset({},.45f,{},.29f,1).x,.37f));
    auto altered=tank; altered.visualScaleMultiplier={4,3,2};
    assert(altered.collisionRadius==tank.collisionRadius && altered.collisionHeight==tank.collisionHeight);
    assert(old.Find("tank")->visualScaleMultiplier.x==1 && !old.Find("tank")->typeMarker.enabled);
    for (const auto& value : {json::array({0,1,1}),json::array({-1,1,1}),json::array({1,1}),json::array({1,"bad",1})}) {
        auto invalid=valid; invalid["enemies"][0]["visualScale"]=value; reject(invalid);
    }
    for (const auto& field : {"scale","offset","color"}) {
        auto invalid=valid; invalid["enemies"][1]["typeMarker"][field]=json::array({1,2}); reject(invalid);
    }
    for (const auto& field : {"scale","color"}) {
        auto invalid=valid; invalid["enemies"][1]["typeMarker"][field]=json::array({-1,1,1}); reject(invalid);
    }
    auto invalid=valid; invalid["enemies"][1]["typeMarker"]["color"]={1,2,1}; reject(invalid);
    invalid=valid; invalid["enemies"][1]["typeMarker"]=true; reject(invalid);
    invalid=valid; invalid["enemies"][1]["typeMarker"]["enabled"]="yes"; reject(invalid);
    // Saved tuning values are editable; verify JSON-to-world scaling, not old defaults.
    for (const auto& item : valid["enemies"]) {
        const auto& d=*definitions.Find(item["id"].get<std::string>());
        const auto scale=d.VisualScale({2,2,2});
        const auto& expected=item.at("visualScale");
        assert(Near(scale.x,2*expected.at(0).get<float>()));
        assert(Near(scale.y,2*expected.at(1).get<float>()));
        assert(Near(scale.z,2*expected.at(2).get<float>()));
        assert(d.typeMarker.enabled==(d.type!=EnemyType::Normal));
        const Vector3 position{3,0,7}, rotation{0,.7f,0};
        const auto world=Matrix4x4::MakeAffineMatrix(scale,rotation,position);
        const auto parts=MakeEnemyParts({{0,0,0},{1,2.48f,1}});
        const auto& body=parts[1]; const auto center=(body.bounds.min+body.bounds.max)*.5f;
        const auto origin=EnemyPartTransformPoint({-2,center.y,center.z},world);
        const auto target=EnemyPartTransformPoint(center,world);
        EnemyPartHit hit;
        assert(RaycastEnemyParts(parts,world,origin,target-origin,20,hit));
        assert(hit.part==EnemyPartType::Body && Near(hit.distance,2*scale.x));
        DetachedPartMotion detached;
        detached.Initialize(body.bounds,position,rotation,scale,{1,0,0},body.type,{},{});
        const auto detachedWorld=Matrix4x4::MakeAffineMatrix(scale,rotation,detached.Translation());
        const auto vertex=EnemyPartTransformPoint(body.bounds.max,world);
        const auto detachedVertex=EnemyPartTransformPoint(body.bounds.max,detachedWorld);
        assert(EnemyVectorLength(vertex-detachedVertex)<.001f);
        // Head-top marker is outside the parts and cannot intercept a shot on its own.
        const auto markerPosition=EnemyPartTransformPoint({0,2.8f,0},world);
        assert(Near(markerPosition.y,2.8f*scale.y));
        const auto markerOrigin=EnemyPartTransformPoint({-2,2.8f,0},world);
        assert(!RaycastEnemyParts(parts,world,markerOrigin,markerPosition-markerOrigin,20,hit));
    }
    auto bad=valid; bad["enemies"][0]["type"]="Unknown"; reject(bad);
    bad=valid; bad["enemies"][0]["id"]=""; reject(bad);
    bad=valid; bad["enemies"].push_back(bad["enemies"][0]); reject(bad);
    for (auto key : {"detectionRange","moveSpeed","attackRange","attackDamage","minRange","preferredRange","maxRange","explosionDamage","fuseTime"}) {
        bad=valid; bad["enemies"][0][key]=-1; reject(bad);
    }
    for (auto key : {"attackInterval","hpMultiplier","projectileSpeed","projectileRadius","projectileLifetime","explosionRadius","bombGravity","collisionRadius","collisionHeight"}) {
        bad=valid; bad["enemies"][0][key]=0; reject(bad);
    }
    bad=valid; bad["enemies"][1]["minRange"]=100; reject(bad);
    bad=valid; bad["enemies"][0]["moveSpeed"]="bad"; reject(bad);
    assert(!definitions.Load("missing-enemy-definition.json"));
    for (const auto& item : valid["enemies"]) {
        const auto& d=*definitions.Find(item["id"].get<std::string>());
        auto parts=MakeEnemyParts({{0,0,0},{1,1,1}});
        ApplyEnemyHpMultiplier(parts,d.hpMultiplier);
        for (const auto& p : parts) assert(Near(p.hp,EnemyPartMaxHp(p.type)*d.hpMultiplier) && p.hp==p.maxHp);
        assert(!EnemyPartsDead(parts));
        DamageEnemyPart(parts,EnemyPartType::Head,10000); assert(EnemyPartsDead(parts));
        ApplyEnemyHpMultiplier(parts,d.hpMultiplier);
        DamageEnemyPart(parts,EnemyPartType::Body,10000); assert(EnemyPartsDead(parts));
    }
    assert(definitions.Find("fast")->moveSpeed>definitions.Find("normal")->moveSpeed);
    assert(definitions.Find("tank")->hpMultiplier>definitions.Find("normal")->hpMultiplier);
    for (auto id : {"ranged"}) {
        const auto& d=*definitions.Find(id);
        EnemyAI ai; ai.settings={d.detectionRange,d.attackRange,d.moveSpeed,d.attackDamage,d.attackInterval,d.IsRanged(),d.minRange,d.maxRange};
        ai.settings.senses={d.detectionRange,360,0};
        Vector3 pos{}, rot{};
        ai.Update(pos,rot,{0,0,d.maxRange+2},.1f,false); assert(pos.z>0 && ai.attacksThisUpdate==0);
        pos={}; ai.Update(pos,rot,{0,0,d.minRange-2},.1f,false); assert(pos.z<0 && ai.attacksThisUpdate==0);
        pos={}; ai.Update(pos,rot,{0,0,d.preferredRange},.1f,false); assert(pos.z==0 && ai.attacksThisUpdate==1);
        ai.Update(pos,rot,{0,0,d.preferredRange},.01f,false); assert(ai.attacksThisUpdate==0);
        ai.Update(pos,rot,{0,0,d.preferredRange},10,true); assert(ai.attacksThisUpdate==0 && ai.state==EnemyState::Dead);
    }
    // Bomber now uses normal pursuit, including inside its former retreat range.
    const auto& bomber=*definitions.Find("bomber");
    assert(!bomber.IsRanged() && definitions.Find("ranged")->IsRanged());
    assert(bomber.moveSpeed>0 && Near(bomber.moveSpeed,definitions.Find("normal")->moveSpeed));
    EnemyAI slow; slow.settings={bomber.detectionRange,bomber.attackRange,bomber.moveSpeed,
        bomber.attackDamage,bomber.attackInterval,bomber.IsRanged(),bomber.minRange,bomber.maxRange};
    slow.settings.senses={bomber.detectionRange,360,0};
    Vector3 slowPosition{},slowRotation{};
    slow.Update(slowPosition,slowRotation,{0,0,5},1,false);
    assert(Near(slowPosition.z,bomber.moveSpeed) && slow.state==EnemyState::Chase);
    slow.Update(slowPosition,slowRotation,{0,0,5},10,false);
    assert(Near(5-slowPosition.z,bomber.attackRange));
    const auto stopped=slowPosition;
    slow.Update(slowPosition,slowRotation,{0,0,20},1,true);
    assert(slowPosition.z==stopped.z && slow.state==EnemyState::Dead);

    assert(IsBomberDetonationHit(EnemyType::Bomber,EnemyPartType::Body,1,false));
    assert(IsBomberDetonationHit(EnemyType::Bomber,EnemyPartType::Body,10000,false));
    for (auto part : {EnemyPartType::Head,EnemyPartType::LeftArm,EnemyPartType::RightArm,
        EnemyPartType::LeftLeg,EnemyPartType::RightLeg,EnemyPartType::None})
        assert(!IsBomberDetonationHit(EnemyType::Bomber,part,10,false));
    for (auto type : {EnemyType::Normal,EnemyType::Ranged,EnemyType::Fast,EnemyType::Tank})
        assert(!IsBomberDetonationHit(type,EnemyPartType::Body,10,false));
    for (float damage : {0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        assert(!IsBomberDetonationHit(EnemyType::Bomber,EnemyPartType::Body,damage,false));
    assert(!IsBomberDetonationHit(EnemyType::Bomber,EnemyPartType::Body,10,true));
    auto exploded=MakeEnemyParts({{0,0,0},{1,2,1}});
    DamageEnemyPart(exploded,EnemyPartType::Body,10000);
    assert(!IsBomberDetonationHit(EnemyType::Bomber,EnemyPartType::Body,10,EnemyPartsDead(exploded)));

    // 頭部で倒した後に胴体へ何度当てても起爆しない（散弾の後続弾も含む）。
    auto headKilled=MakeEnemyParts({{0,0,0},{1,2,1}});
    DamageEnemyPart(headKilled,EnemyPartType::Head,10000);
    assert(EnemyPartsDead(headKilled));
    for (int i=0; i<3; ++i) {
        assert(!IsBomberDetonationHit(EnemyType::Bomber,EnemyPartType::Body,50,EnemyPartsDead(headKilled)));
        DamageEnemyPart(headKilled,EnemyPartType::Body,50);
    }
    // 実際の設定値で、中心・中間・範囲端のいずれも通常系の敵を一撃で倒せること。
    const EnemyExplosion configuredBlast{{0,1,0},bomber.explosionRadius,bomber.explosionDamage};
    for (auto id : {"normal", "ranged", "fast"}) {
        const auto& target=*definitions.Find(id);
        for (float distance : {0.0f, bomber.explosionRadius*.5f, bomber.explosionRadius}) {
            auto parts=MakeEnemyParts({{0,0,0},{1,2,1}});
            ApplyEnemyHpMultiplier(parts,target.hpMultiplier);
            const float damage=EnemyExplosionDamage(configuredBlast,{distance,0,0},
                target.collisionRadius,target.collisionHeight);
            assert(damage==bomber.explosionDamage);
            DamageEnemyPart(parts,EnemyPartType::Body,damage);
            assert(EnemyPartsDead(parts));
        }
        assert(EnemyExplosionDamage(configuredBlast,
            {bomber.explosionRadius+target.collisionRadius+.01f,0,0},
            target.collisionRadius,target.collisionHeight)==0);
    }
    const EnemyExplosion blast{{0,1,0},4,25};
    assert(EnemyExplosionDamage(blast,{0,0,0},.4f,1.8f)==25); // Player at the center.
    assert(EnemyExplosionDamage(blast,{4.5f,0,0},.5f,2)==25); // Cylinder touches blast boundary.
    assert(EnemyExplosionDamage(blast,{4.51f,0,0},.5f,2)==0);
    assert(EnemyExplosionDamage(blast,{0,6,0},.5f,2)==0); // Different floor.
    assert(EnemyExplosionDamage(blast,{0,-5,0},.5f,1)==0);
    for (auto id : {"normal","ranged","fast","tank","bomber"}) {
        const auto& target=*definitions.Find(id);
        assert(EnemyExplosionDamage(blast,{1,0,0},target.collisionRadius,target.collisionHeight)==25);
        assert(EnemyExplosionDamage(blast,{10,0,0},target.collisionRadius,target.collisionHeight)==0);
    }
    // 周囲の敵へ爆風を適用したときのHP遷移と、範囲外・死亡済みの扱いを確認する。
    auto nearby=MakeEnemyParts({{0,0,0},{1,2,1}});
    auto outsideBlast=nearby;
    const auto applyBlast=[&](EnemyParts& target, const Vector3& feet) {
        if (EnemyPartsDead(target)) return 0.0f;
        return DamageEnemyPart(target, EnemyPartType::Body, EnemyExplosionDamage(blast,feet,.4f,2));
    };
    for (int i=1; i<=4; ++i) {
        assert(applyBlast(nearby,{1,0,0})==25);
        assert(nearby[1].hp==100-i*25);
        assert(EnemyPartsDead(nearby)==(i==4));
        assert(applyBlast(outsideBlast,{10,0,0})==0 && outsideBlast[1].hp==100);
    }
    assert(applyBlast(nearby,{1,0,0})==0);
    assert(EnemyExplosionDamage({{},4,0},{},.5f,2)==0);
    std::cout<<"Bomber tests passed: normal-speed pursuit, no ranged mode, body-only live bullet trigger, lethal/dead hits, radial player/enemy damage and vertical/radius boundaries.\n";

    auto bullet=MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{0,0,12});
    assert(StepEnemyProjectile(bullet,.25f,{100,0,0})==0 && Near(bullet.position.z,3));
    assert(StepEnemyProjectile(bullet,1,{0,0,9})==8 && !bullet.active);
    assert(StepEnemyProjectile(bullet,1,{0,0,9})==0);
    bullet=MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{0,0,12});
    StepEnemyProjectile(bullet,10,{1000,0,0}); assert(!bullet.active && Near(bullet.position.z,60));
    auto bomb=MakeEnemyProjectile(*definitions.Find("bomber"),{0,1.2f,0},{0,0,14});
    const float vy=bomb.velocity.y;
    assert(StepEnemyProjectile(bomb,.1f,{0,0,14})==0 && bomb.position.y>1.2f && bomb.velocity.y<vy);
    StepEnemyProjectile(bomb,1.65f,{100,0,0}); assert(bomb.grounded && Near(bomb.position.y,bomb.radius));
    assert(Near(bomb.position.z,14) && Near(bomb.fuseRemaining,1.2f));
    const auto outside=bomb;
    assert(StepEnemyProjectile(bomb,1,{0,0,14})==0 && bomb.active && Near(bomb.position.z,14));
    assert(StepEnemyProjectile(bomb,.21f,{0,0,14})==bomber.explosionDamage && !bomb.active);
    assert(StepEnemyProjectile(bomb,1,{0,0,14})==0);
    bomb=outside; assert(StepEnemyProjectile(bomb,1.21f,{0,0,30})==0 && !bomb.active);
    bomb=outside; bomb.lifetime=.1f; assert(StepEnemyProjectile(bomb,10,{0,0,14})==0 && !bomb.active);
    EnemyProjectileSystem projectiles; projectiles.Spawn(outside); projectiles.Clear();
    assert(projectiles.Projectiles().empty() && projectiles.Update(10,{0,0,14})==0);
    EnemySpawnSystem spawns;
    json map={{"spawnPoints",json::array({{{"id","A"},{"position",{0,0,0}}}})},{"spawnTriggers",json::array()}};
    Save(map); assert(spawns.Load("enemy-types-test.json",definitions));
    assert(spawns.SelectEnemyId(spawns.Points()[0])=="normal");
    map["spawnPoints"][0]["enemyPool"]=json::array({{{"id","normal"},{"weight",5}},{{"id","fast"},{"weight",2}},{{"id","tank"},{"weight",0}}});
    Save(map); assert(spawns.Load("enemy-types-test.json",definitions));
    spawns.SetSeed(42); std::vector<std::string> sequence; int normal=0;
    for (int i=0;i<7000;++i) { auto id=spawns.SelectEnemyId(spawns.Points()[0]); assert(id!="tank"); normal+=id=="normal"; sequence.push_back(id); }
    assert(normal>4700 && normal<5300);
    spawns.SetSeed(42); for (const auto& id : sequence) assert(id==spawns.SelectEnemyId(spawns.Points()[0]));
    const auto rejectPool=[&](json data) { Save(data); assert(!spawns.Load("enemy-types-test.json",definitions)); assert(spawns.Points()[0].enemyPool.size()==3); };
    bad=map; bad["spawnPoints"][0]["enemyPool"][0]["id"]="missing"; rejectPool(bad);
    bad=map; bad["spawnPoints"][0]["enemyPool"][0]["weight"]=-1; rejectPool(bad);
    bad=map; for (auto& e : bad["spawnPoints"][0]["enemyPool"]) e["weight"]=0; rejectPool(bad);
    bad=map; bad["spawnPoints"][0]["enemyPool"]=json::array(); rejectPool(bad);
    map["enemyRandom"]={{"useFixedSeed",true},{"seed",12345}};
    Save(map); assert(spawns.Load("enemy-types-test.json",definitions));
    sequence.clear();
    for (int i=0;i<100;++i) sequence.push_back(spawns.SelectEnemyId(spawns.Points()[0]));
    assert(spawns.Load("enemy-types-test.json",definitions));
    for (const auto& id : sequence) assert(id==spawns.SelectEnemyId(spawns.Points()[0]));
    bad=map; bad["enemyRandom"]["seed"]=-1; rejectPool(bad);
    assert(spawns.Load("../../resources/levels/fps_spawns.json",definitions));
    std::map<std::string,int> counts;
    for (int i=0;i<2000;++i) ++counts[spawns.SelectEnemyId(*spawns.FindPoint("SP_B_01"))];
    assert(counts.size()==5);
    auto single=MakeEnemyProjectile(*definitions.Find("bomber"),{0,1.2f,0},{0,0,14});
    auto sliced=single;
    StepEnemyProjectile(single,2,{100,0,0});
    for (int i=0;i<200;++i) StepEnemyProjectile(sliced,.01f,{100,0,0});
    assert(Near(single.position.z,sliced.position.z) && Near(single.fuseRemaining,sliced.fuseRemaining));
    std::cout<<"Enemy types: definitions, validation, HP/death, ranged AI, bullet sweep/lifetime, bomb gravity/ground/fuse/explosion, pools and cleanup passed\n";
}
