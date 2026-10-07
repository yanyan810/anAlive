#include "StageLoader.h"
#include "StageProjectile.h"
#include "EnemyParts.h"
#include "EnemySpawnSystem.h"
#include "WeaponSystem.h"
#include "StageProgress.h"
#include <cassert>
#include <iostream>
#include <set>
using nlohmann::json;
static bool Near(float a,float b) { return std::abs(a-b)<.002f; }
static void Save(const json& data) { std::ofstream("stage-loader-test.json")<<data; }
static StageCollider Box(Vector3 position,Vector3 scale={1,3,10},Vector3 rotation={}) {
    StageCollider c; c.id="wall"; c.position=position; c.scale=scale; c.rotation=rotation;
    c.local={{-1,-1,-1},{1,1,1}}; c.world=Matrix4x4::MakeAffineMatrix(scale,rotation,position); c.inverse=Matrix4x4::Inverse(c.world); return c;
}
int main() {
    const std::string path="../../resources/levels/stage01/stage01.json";
    StageLoader loader;
    assert(loader.Load(path) && loader.ValidateAssets("../../resources"));
    assert(loader.id=="stage01" && loader.model=="levels/stage01/stage01.gltf");
    assert(loader.playerPosition.x==3 && loader.playerPosition.y==0 && loader.playerPosition.z==-6);
    assert(Near(loader.playerRotation.y,0) && loader.collision.colliders.size()==8);
    json valid; std::ifstream(path)>>valid;
    const auto reject=[&](const json& data) {
        Save(data); assert(!loader.Load("stage-loader-test.json"));
        assert(loader.id=="stage01" && loader.collision.colliders.size()==8 && loader.playerPosition.z==-6);
    };
    auto bad=valid; bad["colliders"].push_back(bad["colliders"][0]); reject(bad);
    bad=valid; bad["colliders"][0]["scale"]={0,1,1}; reject(bad);
    bad=valid; bad["colliders"][0]["localBounds"]["max"]=bad["colliders"][0]["localBounds"]["min"]; reject(bad);
    bad=valid; bad["colliders"][0]["position"]={1,2}; reject(bad);
    bad=valid; bad["colliders"][0]["rotation"]={0,"NaN",0}; reject(bad);
    bad=valid; bad["playerSpawn"]["position"]={0,0,nullptr}; reject(bad);
    bad=valid; bad["stage"]["model"]="../outside.gltf"; reject(bad);
    bad=valid; bad["spawnPoints"][0]["id"]=bad["colliders"][0]["id"]; reject(bad);
    bad=valid; bad["spawnGroups"]=json::object(); reject(bad);
    bad=valid; bad["spawnGroups"]=json::array({{{"id",bad["spawnPoints"][0]["id"]}}}); reject(bad);
    bad=valid; bad["spawnGroups"]=json::array({{{"id",""}}}); reject(bad);
    bad=valid; bad["spawnGroups"]=json::array({{{"id","G"}},{{"id","G"}}}); reject(bad);
    auto grouped=valid; grouped.erase("spawnTriggers");
    grouped["spawnGroups"]=json::array({{{"time",3}, {"mode","Simultaneous"},
        {"enemies",json::array({{{"enemy","normal"},{"spawnPoint",valid["spawnPoints"][0]["id"]}}})}}});
    Save(grouped); assert(loader.Load("stage-loader-test.json"));
    EnemySpawnSystem groupedSpawns;
    EnemyDefinitions groupedDefinitions; assert(groupedDefinitions.Load("../../resources/Data/enemies.json"));
    assert(groupedSpawns.Load("stage-loader-test.json",groupedDefinitions) && groupedSpawns.Groups()[0].id=="SpawnGroup_1");
    assert(!loader.Load("absent-stage.json") && loader.id=="stage01");
    assert(loader.Load(path));
    EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
    EnemySpawnSystem spawns; assert(spawns.Load(path,definitions));
    WeaponSystem weapons; assert(weapons.Load("../../resources/Data/weapons.json",path));
    StageProgress goals; assert(goals.LoadGoals(path));
    assert(spawns.Points().size()==9 && weapons.Pickups().size()==3 && goals.Goals().size()==1);
    std::set<std::string> types; uint64_t next=0;
    const auto emit=[&](const EnemySpawnPoint& p,const std::string&) { types.insert(spawns.SelectEnemyId(p)); return next++; };
    spawns.Update(0,{3,0,4},emit,[](uint64_t){return true;});
    spawns.Update(2,{3,0,4},emit,[](uint64_t){return true;});
    assert(next==5 && types.size()==5);
    assert(goals.Update(0,{3,0,52}));
    assert(loader.Load(path)); // restart position comes from the same file
    assert(loader.playerPosition.z==-6);

    StageWorld world; world.colliders.push_back(Box({5,3,0}));
    StageHit hit;
    assert(world.Raycast({0,1,0},{2,0,0},100,hit) && Near(hit.distance,4));
    assert(!world.Raycast({0,1,0},{-1,0,0},100,hit));
    auto stopped=world.Move({0,0,0},{50,0,0}); assert(stopped.x<3.61f && stopped.x>3.5f);
    auto slide=world.Move({0,0,0},{10,0,5}); assert(slide.x<3.61f && Near(slide.z,5));
    auto leave=world.Move(stopped,{0,0,0}); assert(Near(leave.x,0));
    const auto& tank=*definitions.Find("tank"); const auto& fast=*definitions.Find("fast");
    const auto tankStop=world.Move({0,0,0},{10,0,0},tank.collisionRadius,tank.collisionHeight);
    const auto fastStop=world.Move({0,0,0},{10,0,0},fast.collisionRadius,fast.collisionHeight);
    assert(Near(tankStop.x,4-tank.collisionRadius-.002f));
    assert(Near(fastStop.x,4-fast.collisionRadius-.002f) && tankStop.x<fastStop.x);
    StageWorld overhead; overhead.colliders={Box({5,4,0},{1,1,10})};
    assert(Near(overhead.Move({0,0,0},{10,0,0},fast.collisionRadius,fast.collisionHeight).x,10));
    assert(overhead.Move({0,0,0},{10,0,0},tank.collisionRadius,tank.collisionHeight).x<4);
    StageWorld none; assert(Near(none.Move({0,0,0},{10,0,5}).x,10));
    world.colliders.push_back(Box({0,-.3f,0},{100,.3f,100}));
    assert(Near(world.Move({0,0,0},{0,0,5}).z,5)); // floor does not impede horizontal movement
    world.colliders.clear(); world.colliders.push_back(Box({5,3,0},{1,3,10},{0,.5f,0}));
    const auto& rotated=world.colliders[0];
    const auto origin=StagePoint({-4,0,0},rotated.world), end=StagePoint({0,0,0},rotated.world);
    assert(world.Raycast(origin,end-origin,20,hit) && Near(hit.distance,3));
    const auto normal=hit.normal; assert(Near(StageLength(normal),1));
    Vector3 feet=origin; feet.y=0; Vector3 destination=end; destination.y=0;
    const auto rotatedStop=world.Move(feet,destination);
    assert(StageLength(rotatedStop-destination)>1);

    world.colliders={Box({5,3,0})};
    const auto parts=MakeEnemyParts({{-.5f,0,-.5f},{.5f,2,.5f}});
    const auto enemy=Matrix4x4::MakeAffineMatrix({1,1,1},{},{10,0,0});
    EnemyPartHit enemyHit;
    assert(RaycastEnemyParts(parts,enemy,{0,1.2f,0},{1,0,0},30,enemyHit));
    assert(world.Raycast({0,1.2f,0},{1,0,0},30,hit));
    assert(!RaycastEnemyParts(parts,enemy,{0,1.2f,0},{1,0,0},hit.distance-.001f,enemyHit));
    auto bullet=MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{10,0,0});
    assert(StepStageProjectile(bullet,2,{10,0,0},world)==0 && !bullet.active && bullet.position.x<4);
    bullet=MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{2,0,0});
    assert(StepStageProjectile(bullet,2,{2,0,0},world)==8 && !bullet.active);
    auto bomb=MakeEnemyProjectile(*definitions.Find("bomber"),{0,1.2f,0},{10,0,0});
    assert(StepStageProjectile(bomb,1,{100,0,0},world)==0 && bomb.grounded && bomb.position.x<4);
    auto expired=bomb; expired.lifetime=.0000001f;
    assert(StepStageProjectile(expired,1,{100,0,0},world)==0 && !expired.active);
    const auto rest=bomb.position;
    assert(StepStageProjectile(bomb,2,rest,world)==definitions.Find("bomber")->explosionDamage && !bomb.active);
    assert(StepStageProjectile(bomb,2,rest,world)==0);
    world.colliders={Box({0,-.3f,0},{100,.3f,100})};
    bomb=MakeEnemyProjectile(*definitions.Find("bomber"),{0,1.2f,0},{10,0,0});
    StepStageProjectile(bomb,1.3f,{100,0,0},world);
    assert(bomb.grounded && Near(bomb.position.y,bomb.radius));
    // The exported mesh vertices already contain Blender's world transforms.
    // Compare glTF->engine bounds with JSON->engine collider bounds, including rotated wall.
    json gltf; std::ifstream("../../resources/levels/stage01/stage01.gltf")>>gltf;
    const auto nodeFor=[&](const std::string& name)->json {
        for (const auto& node : gltf["nodes"]) if (node.value("name",std::string{}).starts_with(name)) return node;
        return {};
    };
    for (const auto& c : loader.collision.colliders) {
        const auto node=nodeFor(c.id=="COL_Building" ? "Building_Custom" : c.id);
        assert(!node.empty());
        const auto mesh=gltf["meshes"][node["mesh"].get<size_t>()];
        const auto accessor=gltf["accessors"][mesh["primitives"][0]["attributes"]["POSITION"].get<size_t>()];
        const auto low=accessor["min"],high=accessor["max"];
        const auto box=TransformAABB(c.local,c.world);
        assert(Near(box.min.x,-high[0].get<float>()) && Near(box.max.x,-low[0].get<float>()));
        assert(Near(box.min.y,low[1].get<float>()) && Near(box.max.y,high[1].get<float>()));
        assert(Near(box.min.z,low[2].get<float>()) && Near(box.max.z,high[2].get<float>()));
    }
    std::cout<<"Stage loader tests passed: JSON/rollback, assets/coordinate agreement, spawn/weapon/goal compatibility, OBB rays, swept movement/slide, hitscan occlusion, bullets/bombs.\n";
}
