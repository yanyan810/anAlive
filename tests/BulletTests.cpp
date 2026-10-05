#include "Bullet.h"
#include "EnemyExplosion.h"
#include "TitleStartSequence.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

static bool Near(float a,float b) { return std::abs(a-b)<.001f; }
struct Target {
    EnemyParts parts=MakeEnemyParts({{-.5f,0,-.5f},{.5f,2.5f,.5f}});
    Matrix4x4 world=Matrix4x4::MakeIdentity4x4();
};
static StageCollider Wall(float z) {
    StageCollider c; c.local={{-5,-5,z},{5,5,z+.01f}}; return c;
}
int main() {
    StageWorld world;
    std::vector<Target> targets(1);
    targets[0].world=Matrix4x4::Translation({0,0,10});
    const BulletTrace trace=[&](const Vector3& origin,const Vector3& direction,float distance) {
        return TraceBulletPath(origin,direction,distance,world,targets.size(),
            [&](size_t i,const Vector3& start,const Vector3& unit,float range,EnemyPartHit& hit) {
                return RaycastEnemyParts(targets[i].parts,targets[i].world,start,unit,range,hit);
            });
    };
    int hitCount=0;
    const BulletImpact impact=[&](const Bullet& bullet,const BulletHit& hit) {
        if (!hit.wall) { ++hitCount; DamageEnemyPart(targets[hit.enemyIndex].parts,hit.part,bullet.damage); }
    };
    Bullet bullet; bullet.position={0,1.5f,0}; bullet.speed=10; bullet.damage=25;
    BulletSimulation simulation;
    assert(simulation.Spawn(bullet));
    simulation.Update(.5f,trace,impact);
    assert(hitCount==0 && Near(simulation.Bullets()[0].position.z,5));
    assert(Near(simulation.Bullets()[0].previousPosition.z,0));
    simulation.Update(.5f,trace,impact);
    assert(hitCount==1 && targets[0].parts[1].hp==75 && simulation.Bullets().empty());
    simulation.Update(1,trace,impact); assert(hitCount==1);
    // High-speed travel crosses the entire enemy in one update without tunnelling.
    bullet.speed=10000; assert(simulation.Spawn(bullet)); simulation.Update(.016f,trace,impact);
    assert(hitCount==2 && targets[0].parts[1].hp==50);
    // A thin wall wins and consumes the bullet before it can reach the enemy.
    world.colliders.push_back(Wall(5));
    assert(simulation.Spawn(bullet)); simulation.Update(.016f,trace,impact);
    assert(hitCount==2 && simulation.Bullets().empty());
    world.colliders.clear();
    // Enemy before wall still takes damage. Nearest enemy wins regardless of vector order.
    world.colliders.push_back(Wall(15));
    targets.push_back(Target{}); targets[1].world=Matrix4x4::Translation({0,0,5});
    assert(simulation.Spawn(bullet)); simulation.Update(.016f,trace,impact);
    assert(hitCount==3 && targets[1].parts[1].hp==75 && targets[0].parts[1].hp==50);
    world.colliders.clear(); targets.resize(1);
    // Wall and enemy at the exact same distance: wall wins, not iteration order.
    auto closest=trace(bullet.position,bullet.direction,100); assert(closest && !closest->wall);
    world.colliders.push_back(Wall(closest->distance));
    assert(trace(bullet.position,bullet.direction,100)->wall);
    world.colliders.clear();

    // Additional scene shootables use the same nearest-hit ordering as enemies.
    const BulletTrace startTarget=[](const Vector3& origin,const Vector3& direction,float range) -> std::optional<BulletHit> {
        float distance;
        if (!RaycastAABB(origin,direction,{{-1,0,4},{1,3,4.1f}},range,distance)) return {};
        BulletHit hit; hit.distance=distance; hit.position=origin+direction*distance;
        hit.wall=false; hit.targetIndex=7; return hit;
    };
    const auto customTrace=[&](const Vector3& origin,const Vector3& direction,float range) {
        return TraceBulletPath(origin,direction,range,world,targets.size(),
            [&](size_t i,const Vector3& o,const Vector3& d,float r,EnemyPartHit& hit) {
                return RaycastEnemyParts(targets[i].parts,targets[i].world,o,d,r,hit);
            },startTarget);
    };
    assert(customTrace({0,1.5f,0},{0,0,1},100)->targetIndex==7);
    world.colliders.push_back(Wall(4));
    assert(customTrace({0,1.5f,0},{0,0,1},100)->wall); // wall wins exact tie
    world.colliders.clear();
    targets[0].world=Matrix4x4::Translation({0,0,2});
    assert(customTrace({0,1.5f,0},{0,0,1},100)->targetIndex==std::numeric_limits<size_t>::max());
    targets[0].world=Matrix4x4::Translation({0,0,10});
    int startImpacts=0;
    assert(simulation.Spawn(bullet));
    simulation.Update(.016f,customTrace,[&](const Bullet&,const BulletHit& hit) { assert(hit.targetIndex==7); ++startImpacts; });
    assert(startImpacts==1 && simulation.Bullets().empty());
    simulation.Update(1,customTrace,[&](const Bullet&,const BulletHit&) { ++startImpacts; });
    assert(startImpacts==1);

    // Sweep each of the six real part boxes with rotation + nonuniform scale.
    targets[0]=Target{};
    targets[0].world=Matrix4x4::MakeAffineMatrix({2,1.3f,.8f},{0,.6f,0},{0,0,10});
    for (size_t i=0;i<targets[0].parts.size();++i) {
        const auto& part=targets[0].parts[i];
        const auto center=(part.bounds.min+part.bounds.max)*.5f;
        const auto from=StagePoint({-2,center.y,center.z},targets[0].world);
        const auto to=StagePoint(center,targets[0].world);
        Bullet aimed=bullet; aimed.position=from; aimed.direction=to-from; aimed.damage=5;
        assert(simulation.Spawn(aimed));
        simulation.Update(.016f,trace,[&](const Bullet& b,const BulletHit& hit) {
            assert(!hit.wall && hit.part==part.type && hit.enemyIndex==0);
            DamageEnemyPart(targets[0].parts,hit.part,b.damage);
        });
        assert(part.hp==part.maxHp-5 && simulation.Bullets().empty());
    }
    // Lifetime clips the final segment: it cannot reach a target beyond that segment.
    targets[0]=Target{}; targets[0].world=Matrix4x4::Translation({0,0,10});
    bullet.speed=10; bullet.remainingLife=.5f;
    assert(simulation.Spawn(bullet)); simulation.Update(2,trace,impact);
    assert(hitCount==3 && simulation.Bullets().empty());
    bullet.remainingLife=1; assert(simulation.Spawn(bullet)); simulation.Update(2,trace,impact);
    assert(hitCount==4 && simulation.Bullets().empty());
    bullet.remainingLife=3; bullet.remainingRange=5;
    assert(simulation.Spawn(bullet)); simulation.Update(2,trace,impact);
    assert(hitCount==4 && simulation.Bullets().empty());
    bullet.remainingRange=100;
    // Zero/invalid time cannot move or damage. A saved snapshot restores flight exactly.
    assert(simulation.Spawn(bullet)); const auto snapshot=simulation;
    simulation.Update(0,trace,impact); simulation.Update(-1,trace,impact);
    simulation.Update(std::numeric_limits<float>::quiet_NaN(),trace,impact);
    assert(simulation.Bullets()[0].position.z==0);
    simulation.Update(.2f,trace,impact); simulation=snapshot;
    assert(simulation.Bullets()[0].position.z==0);
    simulation.Clear();

    WeaponSystem weapons;
    assert(weapons.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json"));
    std::mt19937 random(42);
    const auto camera=Matrix4x4::Translation({0,1.5f,0});
    auto pistol=*weapons.Find("pistol"); pistol.hipSpreadDegrees=pistol.adsSpreadDegrees=0;
    pistol.bulletSpeed=10;
    const int beforeShot=hitCount;
    assert(simulation.SpawnShot(pistol,camera,0,random,trace)==1);
    assert(hitCount==beforeShot && targets[0].parts[1].hp==75); // Camera aiming never deals damage.
    const auto spawned=simulation.Bullets()[0];
    assert(Near(spawned.position.x,.2f) && Near(spawned.position.y,1.35f) && Near(spawned.position.z,.5f));
    const auto aim=trace({0,1.5f,0},{0,0,1},pistol.range)->position;
    assert(StageLength(spawned.position+spawned.direction*StageLength(aim-spawned.position)-aim)<.001f);
    simulation.Update(.1f,trace,impact); assert(hitCount==beforeShot);
    // Moving target leaves the flight path: no stored camera hit is used at impact.
    targets[0].world=Matrix4x4::Translation({10,0,10});
    simulation.Update(3,trace,impact); assert(hitCount==beforeShot && simulation.Bullets().empty());
    targets[0].world=Matrix4x4::Translation({0,0,10});

    WeaponRuntime shotgun; shotgun.Equip(*weapons.Find("shotgun"));
    const int ammo=shotgun.Magazine();
    assert(shotgun.Step(0,true,true,false)==1);
    assert(simulation.SpawnShot(shotgun.Definition(),camera,0,random,trace)==8);
    assert(shotgun.Magazine()==ammo-1 && simulation.Bullets().size()==8);
    bool spread=false;
    for (const auto& pellet:simulation.Bullets()) {
        assert(Near(StageLength(pellet.direction),1));
        spread=spread || StageLength(pellet.direction-simulation.Bullets()[0].direction)>.001f;
    }
    assert(spread); simulation.Clear();
    WeaponRuntime smg; smg.Equip(*weapons.Find("smg"));
    int shots=0;
    for (int frame=0;frame<60;++frame) {
        const int fired=smg.Step(1.0f/60,frame==0,true,false); shots+=fired;
        for (int shot=0;shot<fired;++shot)
            assert(simulation.SpawnShot(smg.Definition(),camera,0,random,trace)==1);
    }
    assert(shots>1 && simulation.Bullets().size()==static_cast<size_t>(shots));
    assert(smg.Magazine()==smg.Definition().magazineSize-shots); simulation.Clear();
    // Obstructed muzzle is moved to the near side, never beyond the wall.
    world.colliders.push_back(Wall(.2f));
    assert(simulation.SpawnShot(pistol,camera,1,random,trace)==1);
    assert(simulation.Bullets()[0].position.z<.2f);
    simulation.Update(1,trace,impact); assert(hitCount==beforeShot && simulation.Bullets().empty());
    world.colliders.clear(); targets.clear();
    // A bounded pool and automatic expiry keep sustained fire from accumulating forever.
    for (size_t i=0;i<BulletSimulation::kMaxBullets;++i) assert(simulation.Spawn(bullet));
    assert(!simulation.Spawn(bullet)); simulation.Update(5,trace,impact); assert(simulation.Bullets().empty());
    for (int frame=0;frame<1000;++frame) {
        for (int i=0;i<8;++i) assert(simulation.Spawn(bullet));
        simulation.Update(.1f,trace,impact);
        assert(simulation.Bullets().size()<=240);
    }
    simulation.Update(5,trace,impact); assert(simulation.Bullets().empty());
    // Optional JSON values keep older weapon files compatible; invalid edits are rejected.
    nlohmann::json json; std::ifstream("../../resources/Data/weapons.json")>>json;
    auto legacy=json; for(auto& item:legacy["weapons"]) { item.erase("bulletSpeed"); item.erase("bulletLifeTime"); }
    std::ofstream("bullet-weapons-test.json")<<legacy;
    assert(weapons.Load("bullet-weapons-test.json","../../resources/levels/fps_spawns.json"));
    assert(weapons.Find("pistol")->bulletSpeed==120 && weapons.Find("pistol")->bulletLifeTime==3);
    for (auto field:{"bulletSpeed","bulletLifeTime"}) for(auto value:{nlohmann::json(0),nlohmann::json(-1),nlohmann::json("bad")}) {
        auto bad=json; bad["weapons"][0][field]=value; std::ofstream("bullet-weapons-test.json")<<bad;
        assert(!weapons.Load("bullet-weapons-test.json","../../resources/levels/fps_spawns.json"));
    }
    std::cout<<"Bullet tests passed: delayed impact, high-speed sweeps, six parts, closest enemy/wall/ties, lifetime/range, rewind, muzzle convergence/obstruction, moving targets, shotgun 8/ammo 1, full-auto, bounded soak, JSON defaults/validation.\n";
}
