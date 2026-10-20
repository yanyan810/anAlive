#pragma once
// Included after the production spawn scheduler harness in EnemySpawnTests.cpp.
inline void RunEnemyPlacementTests() {
    Harness h;
    auto data=IndependentConfiguration();
    data["spawnPoints"][0]["activation"]="StageStart";
    data["spawnPoints"][0]["initialDelay"]=1;
    data["spawnPoints"][0]["maxAlive"]=1;
    data["spawnPoints"][0]["mode"]="Sequential";
    data["spawnPoints"][0]["spawnInterval"]=.5;
    data["spawnPoints"][1]["activation"]="OnTrigger";
    data["spawnPoints"][2]["activation"]="Manual";
    h.Load(data);
    EnemyPlacements placements; placements.stage="fixture";
    for (const auto* id:{"A","B","C"}) {
        auto& g=placements.groups[id];
        g.enemies={{placements.NewId(),"normal",{1,0,2},.25f,true},
            {placements.NewId(),"tank",{-2,0,1},-.5f,false},
            {placements.NewId(),"fast",{3,0,4},1.f,true}};
    }
    h.system.SetPlacements(placements);
    assert(h.system.FindPoint("A")->spawnCount==2 && h.system.FindPoint("A")->maxAlive==1);
    h.Tick(.5f,{1000,0,0}); assert(h.events.empty());
    h.Tick(.5f,{1000,0,0}); assert(h.events.size()==1 && h.events[0].enemy=="normal");
    const auto* point=h.system.FindPoint("A");
    const auto expected=EnemyPlacements::WorldPosition(placements.groups["A"].enemies[0],point->position,point->rotation);
    assert(StageLength(expected-h.events[0].position)<.0001f && std::abs(h.events[0].rotation.y-1.25f)<.0001f);
    h.Tick(4,{1000,0,0}); assert(h.events.size()==1); // maxAlive and no queued burst
    h.alive.clear(); h.Tick(0,{1000,0,0}); assert(h.events.size()==2 && h.events.back().enemy=="fast");
    h.Tick(0,{}); assert(h.events.size()==3); // OnTrigger retains sequential interval
    h.Tick(.5f,{1000,0,0}); assert(h.events.size()==4);
    assert(h.system.ActivateSpawn("C"));
    h.Tick(0,{1000,0,0}); assert(h.events.size()==6 && h.events.back().enemy=="fast");
    h.system.Reset(); h.alive.clear(); h.events.clear(); h.Tick(1,{1000,0,0});
    assert(h.events.size()==1 && StageLength(h.events[0].position-expected)<.0001f);
    // Full anchor rotation basis round trip, not a raw translation offset.
    EnemyPlacement p{"stable","normal",{2,3,4},0,true};
    const Vector3 origin{10,12,9},rotation{.2f,1.57079632679f,-.3f};
    const auto world=EnemyPlacements::WorldPosition(p,origin,rotation);
    assert(StageLength(EnemyPlacements::LocalPosition(world,origin,rotation)-p.offset)<.0001f);
    const auto moved=EnemyPlacements::WorldPosition(p,origin+Vector3{5,0,0},rotation);
    assert(StageLength(moved-world-Vector3{5,0,0})<.0001f);
    // Empty configured group deliberately disables its wave; missing group preserves original counts.
    placements.groups["A"].enemies.clear(); placements.groups.erase("B");
    placements.groups["removed-spawn"].enemies.push_back(p);
    h.system.SetPlacements(placements);
    assert(h.system.FindPoint("A")->spawnCount==0 && h.system.FindPoint("B")->spawnCount==5);
    placements.groups["A"].overrideEnabled=false; h.system.SetPlacements(placements);
    assert(h.system.FindPoint("A")->spawnCount==3); // Per-spawn opt-out preserves original composition.
    placements.groups["A"].overrideEnabled=true; h.system.SetPlacements(placements);
    assert(!h.system.PlacementWarnings().empty());
    h.events.clear(); h.Tick(10,{1000,0,0}); assert(h.events.empty());
    // Validate, round trip, stable IDs, safe backup and external edit conflict.
    const auto path=std::string("placement-test-fixture.json");
    std::error_code ec; std::filesystem::remove(path,ec); std::filesystem::remove(path+".bak",ec);
    assert(placements.Save(path,h.definitions,{}));
    EnemyPlacements loaded; assert(loaded.Load(path,h.definitions,"fixture"));
    assert(loaded.Json()==placements.Json());
    const auto original=EnemyPlacements::ReadBytes(path);
    loaded.groups["C"].enemies[0].offset.x+=10;
    assert(loaded.Save(path,h.definitions,original));
    assert(EnemyPlacements::ReadBytes(path+".bak")==original);
    assert(!placements.Save(path,h.definitions,original)); // cannot overwrite another editor's change
    const auto good=loaded.Json(); auto invalid=good;
    invalid["spawns"][1]["enemies"][0]["id"]=invalid["spawns"][1]["enemies"][1]["id"];
    assert(!loaded.Parse(invalid,h.definitions,"fixture") && loaded.Json()==good);
    invalid=good; invalid["spawns"][1]["enemies"][0]["enemy"]="missing";
    assert(!loaded.Parse(invalid,h.definitions,"fixture") && loaded.Json()==good);
    assert(!loaded.Parse(good,h.definitions,"another-stage"));
    // Sidecar is consumed by production Load. Exported stage stays unchanged.
    std::filesystem::create_directories("placement-fixture");
    data["stage"]={{"id","fixture"}};
    const auto exported=data.dump();
    { std::ofstream file("placement-fixture/stage.json"); file<<exported; }
    const auto sidecar="placement-fixture/enemy_placements.json";
    std::filesystem::remove(sidecar,ec);
    assert(placements.Save(sidecar,h.definitions,{}));
    assert(h.system.Load("placement-fixture/stage.json",h.definitions));
    assert(h.system.FindPoint("A")->spawnCount==0);
    { std::ofstream file("placement-fixture/stage.json"); file<<data.dump(2); } // simulated Blender re-export
    assert(h.system.Load("placement-fixture/stage.json",h.definitions));
    assert(h.system.FindPoint("A")->spawnCount==0);
    { std::ofstream file(sidecar); file<<"broken json"; }
    assert(h.system.Load("placement-fixture/stage.json",h.definitions));
    assert(h.system.FindPoint("A")->spawnCount==3 && !h.system.PlacementWarnings().empty());
    std::filesystem::remove(sidecar,ec);
    assert(h.system.Load("placement-fixture/stage.json",h.definitions) && h.system.FindPoint("A")->spawnCount==3);
    // Floor support and conservative collision diagnostics reuse the gameplay world.
    StageWorld geometry; geometry.playerGravity=true;
    StageCollider floor; floor.local={{-10,-1,-10},{10,0,10}}; floor.UpdateMatrices(); geometry.colliders.push_back(floor);
    EnemyDefinition definition; definition.collisionRadius=.4f; definition.collisionHeight=1.8f;
    Vector3 at{0,.4f,0}; assert(EnemyPlacements::SnapFloor(at,geometry) && std::abs(at.y)<.001f);
    assert(EnemyPlacements::InvalidReason(at,definition,geometry).empty());
    StageCollider wall; wall.local={{-.2f,0,-2},{.2f,3,2}}; wall.UpdateMatrices(); geometry.colliders.push_back(wall);
    assert(!EnemyPlacements::InvalidReason(at,definition,geometry).empty());
    assert(!EnemyPlacements::InvalidReason({20,0,0},definition,geometry).empty());
    std::cout<<"Placement tests passed: all activations, enabled counts, timing/capacity, relative transforms, stable IDs, transactional JSON, backup/conflict, re-export, fallback, geometry warnings.\n";
}
