#pragma once
#include "StageLoader.h"
#include "StageProgress.h"
#include "EnemySpawnSystem.h"
#include "WeaponSystem.h"
#include "FPSMotion.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace HotelStageChecks {
inline void Check(bool ok,const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
inline bool Near(float a,float b,float tolerance=.12f) { return std::abs(a-b)<tolerance; }
inline const std::vector<Vector3> route={
    {9,12,4},{9,12,10},{15,12,10},{15,12,4},{15,12,10},
    {21,12,10},{21,12,4},{21,12,10},{8,12,10.8f},{4.4f,12,10.8f},
    {4.4f,9,1.1f},{1.6f,9,1.1f},{1.6f,6,10.8f},{9,6,10.8f},
    {9,6,26},{16,6,26},{29,6,26},{35,6,26},{36.4f,6,10.8f},
    {36.4f,3,1.1f},{33.6f,3,1.1f},{33.6f,0,14},{35,0,18},
    {30,0,18},{10,0,18},{-2,0,18},{-16,0,18}};
inline void Walk(const StageWorld& world,Vector3& position,const Vector3& target,
    int fps,StageWorld::PlayerMotion& motion) {
    const float dt=1.0f/static_cast<float>(fps);
    for (int frame=0;frame<fps*45;++frame) {
        const auto difference=Vector3{target.x-position.x,0,target.z-position.z};
        const float distance=StageLength(difference);
        if (distance<.006f) {
            for (int settle=0;settle<fps/5;++settle) position=world.MovePlayer(position,position,dt,motion);
            Check(Near(position.y,target.y),"Route height: expected "+std::to_string(target.y)+" got "+std::to_string(position.y));
            return;
        }
        Transform transform{{1,1,1},{0,std::atan2(difference.x,difference.z),0},position};
        FPSMotion::Settings settings;
        FPSMotion::Move(transform,0,1,std::min(dt,distance/settings.moveSpeed),settings);
        position=world.MovePlayer(position,transform.translate,dt,motion);
    }
    throw std::runtime_error("Route blocked toward ("+std::to_string(target.x)+","+std::to_string(target.z)+
        ") at ("+std::to_string(position.x)+","+std::to_string(position.y)+","+std::to_string(position.z)+")");
}
inline void Validate(const std::string& resources) {
    const auto path=resources+"/levels/stage03/stage03.json";
    StageLoader level;
    Check(level.Load(path) && level.ValidateAssets(resources),"Hotel assets: "+level.Error());
    Check(level.id=="stage03" && level.collision.playerGravity && Near(level.playerPosition.y,12),"Hotel spawn / gravity");
    nlohmann::json gltf;
    std::ifstream(resources+"/"+level.model)>>gltf;
    for (const auto& collider : level.collision.colliders) {
        if (collider.id.starts_with("COL_")) continue; // Independent invisible proxies.
        const nlohmann::json* node=nullptr;
        for (const auto& entry : gltf.at("nodes"))
            if (entry.value("name",std::string{}).starts_with(collider.id)) { node=&entry; break; }
        Check(node!=nullptr,"Missing visible hotel object: "+collider.id);
        const auto& mesh=gltf.at("meshes").at(node->at("mesh").get<size_t>());
        const auto& accessor=gltf.at("accessors").at(mesh.at("primitives").at(0).at("attributes").at("POSITION").get<size_t>());
        const auto low=accessor.at("min"), high=accessor.at("max");
        const auto box=TransformAABB(collider.local,collider.world);
        Check(Near(box.min.x,-high[0].get<float>(),.002f) && Near(box.max.x,-low[0].get<float>(),.002f) &&
            Near(box.min.y,low[1].get<float>(),.002f) && Near(box.max.y,high[1].get<float>(),.002f) &&
            Near(box.min.z,low[2].get<float>(),.002f) && Near(box.max.z,high[2].get<float>(),.002f),
            "Mesh/collider alignment: "+collider.id);
    }
    EnemyDefinitions definitions;
    Check(definitions.Load(resources+"/Data/enemies.json"),"Enemy definitions");
    EnemySpawnSystem spawns;
    WeaponSystem weapons;
    StageProgress progress;
    Check(spawns.Load(path,definitions) && weapons.Load(resources+"/Data/weapons.json",path) && progress.LoadGoals(path),"Existing systems cannot load hotel");
    Check(spawns.Points().size()==6 && spawns.Triggers().empty() && spawns.Groups().empty(),"Six reserved spawns, no active schedule");
    int emitted=0;
    spawns.Update(300,{22.5f,0,18},[&](const EnemySpawnPoint&,const std::string&){++emitted; return uint64_t{1};},[](uint64_t){return false;});
    Check(emitted==0,"Placeholder spawns activated");
    Check(progress.Goals().size()==1,"Outdoor goal missing");
    for (int fps : {30,60,144}) {
        StageWorld::PlayerMotion motion;
        auto position=level.playerPosition;
        for (const auto& target : route) Walk(level.collision,position,target,fps,motion);
        Check(progress.Update(0,position),"Outdoor goal not reached");
        // Reverse verifies uphill movement and both floor/ramp transitions.
        for (auto target=route.rbegin();target!=route.rend();++target) Walk(level.collision,position,*target,fps,motion);
        Check(Near(position.y,12),"Reverse route did not return to 3F");
        progress.Reset();
    }
    // The second ballroom entrance is a usable alternate route.
    StageWorld::PlayerMotion alternateMotion;
    Vector3 alternate{9,6,13};
    Walk(level.collision,alternate,{9,6,10},60,alternateMotion);
    Walk(level.collision,alternate,{16,6,10},60,alternateMotion);
    // Every accessible support space has a real doorway, rather than a named cavity.
    const std::vector<Vector3> supportRoute={{22.5f,6,10},{22.5f,6,3},{22.5f,6,10},
        {22.5f,6,26},{22.5f,6,33},{22.5f,6,26},{9,6,26},{6,6,26},{6,6,33},{6,6,26}};
    for (const auto& target : supportRoute) Walk(level.collision,alternate,target,60,alternateMotion);
    alternate={8,0,18}; alternateMotion={};
    for (const auto& target : std::vector<Vector3>{{8,0,10},{8,0,18},{8,0,26},{8,0,18}})
        Walk(level.collision,alternate,target,60,alternateMotion);
    for (const auto& point : spawns.Points()) {
        const bool upper=point.id.find("_Upper_")!=std::string::npos;
        const float doorway=upper ? 6.0f : 30.0f;
        const float sign=upper ? -1.0f : 1.0f;
        Vector3 start{point.position.x,0,doorway-sign*2};
        StageWorld::PlayerMotion motion;
        const auto stopped=level.collision.MovePlayer(start,point.position,1,motion);
        Check(sign*(stopped.z-doorway)<-.4f,"Player entered "+point.id);
        const auto enemy=level.collision.Move(point.position,{point.position.x,0,doorway-sign*2});
        Check(Near(enemy.z,doorway-sign*2),"Enemy exit blocked: "+point.id);
    }
    StageWorld::PlayerMotion fall;
    Vector3 feet{22.5f,4,18};
    for (int i=0;i<120;++i) feet=level.collision.MovePlayer(feet,feet,1.0f/60,fall);
    Check(Near(feet.y,0,.003f) && fall.grounded,"Gravity did not land on floor");
    const auto stopped=level.collision.MovePlayer({16,0,12},{-100,0,12},1,fall);
    Check(stopped.x>12.5f,"Long frame tunnelled through ballroom wall");
    // Upper-floor slab underside is the real ceiling; no detached ceiling layer.
    StageHit ceiling;
    for (float y : {2.0f,8.0f}) {
        Check(level.collision.Raycast({22.5f,y,18},{0,1,0},10,ceiling),"Shared slab ceiling missing");
        Check(Near(ceiling.distance,3.65f,.003f) && ceiling.normal.y<-.99f,"Floor/ceiling height mismatch");
    }
    Check(level.collision.Raycast({9,13,4},{0,1,0},10,ceiling) && Near(ceiling.distance,2,.003f),"Guest room 3 m ceiling");
    for (const auto& guard : std::vector<Vector3>{{1.6f,12,10.8f},{33.6f,6,10.8f}}) {
        StageWorld::PlayerMotion guardMotion;
        const auto guarded=level.collision.MovePlayer(guard,{guard.x,guard.y,8},1,guardMotion);
        Check(guarded.z>10.5f && Near(guarded.y,guard.y),"Upper landing allowed fall into other flight");
    }
    std::cout<<"Hotel PASS: assets, 6 dormant spawns, 3 guest rooms, down/up full route at 30/60/144 FPS, "
        "alternate entrance, player-only gates/enemy exits, gravity, walls, shared slab ceilings, outdoor goal.\n";
}
}
