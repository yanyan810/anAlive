#include "EnemyAI.h"
#include "EnemyDefinition.h"
#include "StageLoader.h"
#include <iostream>
#include <stdexcept>

static void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
static StageCollider Box(Vector3 p,Vector3 half,Vector3 rotation={}) {
    StageCollider c; c.position=p; c.scale=half; c.rotation=rotation;
    c.local={{-1,-1,-1},{1,1,1}}; c.UpdateMatrices(); return c;
}
int main() try {
    const Vector3 forward{0,1.57079632679f,0};
    {
        EnemyAI ai; Vector3 p{},r=forward;
        ai.Update(p,r,{0,0,10},.1f,false); Check(ai.visible && ai.state==EnemyState::Idle,"discovered before detection time");
        ai.Update(p,r,{0,0,-10},.3f,false); Check(!ai.inFov && !ai.visible && ai.detection==0 && !ai.hasLastSeen,"outside FOV acquired target");
        ai.Update(p,r,{0,0,50},1,false); Check(ai.state==EnemyState::Idle,"distance limit ignored");
        ai.Update(p,r,{0,0,10},.2f,false); Check(ai.state==EnemyState::Idle,"interrupted detection accumulated");
        ai.Update(p,r,{0,0,10},.16f,false); Check(ai.state==EnemyState::Chase && ai.hasLastSeen && p.z>0,"detection time did not start chase");
    }
    {
        StageWorld world; world.colliders.push_back(Box({0,3,3},{10,3,.25f}));
        EnemyAI ai; Vector3 p{},r=forward; ai.settings.senses.detectionTime=0;
        ai.Update(p,r,{0,0,6},2,false,&world);
        Check(ai.occluded && !ai.visible && !ai.hasLastSeen && p.z==0,"discovered through solid wall");
        world.colliders[0].playerOnly=true;
        ai.Update(p,r,{0,0,6},.1f,false,&world); Check(ai.visible,"playerOnly gate incorrectly blocked sight");
        ai={}; ai.settings.senses.detectionTime=0; p={}; r=forward;
        world.colliders[0].playerOnly=false; world.colliders[0].blocksSight=false;
        ai.Update(p,r,{0,0,6},.1f,false,&world); Check(ai.visible,"transparent sight policy ignored");
        // Physical attack remains blocked even with transparent sight.
        Check(ai.attacksThisUpdate==0,"transparent solid wall permitted attack");
        world.colliders[0]=Box({0,.55f,3},{10,.55f,.25f});
        ai={}; p={}; r=forward; ai.settings.senses.detectionTime=0;
        ai.Update(p,r,{0,0,6},.1f,false,&world);
        Check(ai.visible && ai.sightTarget.y>1.5f,"head/torso multi-sampling failed");
        world.colliders[0]=Box({0,3,3},{10,3,.25f},{0,.3f,0});
        ai={}; p={}; r=forward; ai.Update(p,r,{0,0,6},2,false,&world);
        Check(!ai.visible,"rotated collider failed to occlude");
    }
    {
        StageWorld world; EnemyAI ai; Vector3 p{},r=forward;
        ai.settings.senses.detectionTime=0; ai.settings.moveSpeed=0; ai.settings.senses.loseSightTime=.3f;
        ai.Update(p,r,{0,0,6},.1f,false,&world); Check(ai.lastSeen.z==6,"initial sight memory missing");
        world.colliders.push_back(Box({0,3,3},{10,3,.25f}));
        ai.Update(p,r,{4,0,9},.2f,false,&world);
        Check(ai.state==EnemyState::Chase && ai.moveTarget.x==0 && ai.moveTarget.z==6,"grace period leaked current target");
        ai.Update(p,r,{-4,0,11},.2f,false,&world);
        Check(ai.state==EnemyState::Investigate && ai.lastSeen.x==0 && ai.lastSeen.z==6,"lost-sight memory/state incorrect");
        p=ai.lastSeen;
        ai.Update(p,r,{0,0,-50},.1f,false,&world);
        Check(ai.state==EnemyState::Search && ai.searchElapsed==0 && ai.lastSeenReached,"arrival did not begin search");
        world.colliders.clear(); ai.settings.moveSpeed=2;
        ai.settings.senses.searchDuration=.5f;
        for (int i=0;i<30 && ai.state==EnemyState::Search;++i) ai.Update(p,r,{0,0,-50},.1f,false,&world);
        Check(ai.state==EnemyState::Wander,"search did not hand off to wander");
        const auto start=p;
        ai.Update(p,r,{0,0,-50},.2f,false,&world); Check(StageLength(p-start)==0,"wander wait skipped");
        for (int i=0;i<80;++i) ai.Update(p,r,{0,0,-50},.1f,false,&world);
        Check(ai.state==EnemyState::Wander && StageLength(p-start)>.1f,"wander did not move");
        Check(StageLength(ai.moveTarget-ai.wanderCenter)<=ai.settings.senses.wanderRadius+.01f,"wander centered on spawn");
        ai.settings.senses.fieldOfView=360;
        ai.Update(p,r,p+Vector3{0,0,5},.1f,false,&world); Check(ai.state==EnemyState::Chase,"wander reacquisition failed");
        ai.paused=true; const auto paused=p; const auto last=ai.lastSeen;
        ai.Update(p,r,{40,0,40},1,false,&world); Check(StageLength(p-paused)==0 && StageLength(ai.lastSeen-last)==0 && ai.attacksThisUpdate==0,"AI pause advanced");
        auto copy=ai; Check(StageLength(copy.lastSeen-last)==0 && copy.random==ai.random,"snapshot lost AI memory");
    }
    for (bool ranged:{false,true}) {
        StageWorld world; EnemyAI ai; Vector3 p{},r=forward;
        ai.settings.ranged=ranged; ai.settings.minRange=0; ai.settings.maxRange=3;
        ai.settings.senses.detectionTime=0; ai.settings.attackRange=2;
        ai.Update(p,r,{0,0,1},.1f,false,&world); Check(ai.attacksThisUpdate>0,"clear attack no longer works");
        world.colliders.push_back(Box({0,3,.5f},{4,3,.08f}));
        const auto before=p;
        Check(ai.Update(p,r,{0,0,1},2,false,&world)==0 && ai.attacksThisUpdate==0,"wall attack during lost-sight grace");
        Check(EnemyNavigation::Clear(&world,p,p,.4f,1.8f) && StageLength(p-before)<=ai.settings.moveSpeed*2+.01f,"pushed into close wall");
    }
    {
        StageWorld world; world.colliders.push_back(Box({0,3,-1},{4,3,.1f}));
        EnemyAI ranged; Vector3 p{},r=forward; ranged.settings.ranged=true;
        ranged.settings.senses.detectionTime=0;
        ranged.Update(p,r,{0,0,2},1,false,&world);
        Check(StageLength(p)==0 && ranged.navigation.state==EnemyPathState::NoPath,"ranged retreat crossed rear wall");
    }
    {
        StageWorld world; world.colliders.push_back(Box({0,3,3},{2,3,.3f}));
        EnemyNavigation nav; Vector3 p{},r=forward; const Vector3 goal{0,0,7};
        Check(nav.Plan(&world,p,goal,.4f,1.8f) && nav.path.size()>1,"bounded route did not detour");
        for (int i=0;i<300 && StageLength(p-goal)>.06f;++i) {
            const auto before=p; nav.Move(p,r,goal,3,.05f,&world,.4f,1.8f);
            Check(EnemyNavigation::Clear(&world,before,p,.4f,1.8f),"route crossed obstacle");
        }
        Check(StageLength(p-goal)<.06f,"detour never arrived");
        world.colliders.push_back(Box(goal,{1,3,1}));
        p={}; nav={}; Check(!nav.Plan(&world,p,goal,.4f,1.8f),"destination inside wall accepted");
        for (int i=0;i<30;++i) nav.Move(p,r,goal,3,.1f,&world,.4f,1.8f);
        Check(StageLength(p)==0 && nav.state==EnemyPathState::NoPath,"no-path should stop without vibration");
        nav={}; Check(!nav.Plan(&world,{}, {0,5,0},.4f,1.8f) && nav.state==EnemyPathState::Unsupported,"vertical limitation hidden");
        nav={}; Check(!nav.Plan(&world,{}, {0,0,100},.4f,1.8f) && nav.state==EnemyPathState::Limited,"search bounds not reported");
        StageWorld floor; floor.playerGravity=true;
        floor.colliders.push_back(Box({0,-.25f,0},{2,.25f,2}));
        Check(!EnemyNavigation::Clear(&floor,{}, {0,0,5},.4f,1.8f),"route crossed floor gap");
    }
    {
        EnemyDefinitions definitions; Check(definitions.Load("../../resources/Data/enemies.json"),"definitions failed");
        // The hotel doorway must admit the normal enemy's actual movement body.
        // A stale 3.22 m cylinder saw the player but could not follow the 2.11 m model outside.
        {
            StageLoader hotel;
            Check(hotel.Load("../../resources/levels/stage03/stage03.json"),"hotel stage failed");
            const auto& normal=*definitions.Find("normal");
            const Vector3 corridor{28,12,11};
            Vector3 p{28,12,19},r{0,-1.57079632679f,0};
            EnemyNavigation route;
            Check(route.Plan(&hotel.collision,p,corridor,normal.collisionRadius,normal.collisionHeight),
                "normal movement body cannot pass hotel room doorway");
            EnemyAI ai; ai.settings.senses=normal.senses; ai.settings.moveSpeed=normal.moveSpeed;
            for (int frame=0;frame<300;++frame)
                ai.Update(p,r,corridor,1.0f/60,false,&hotel.collision,normal.collisionRadius,normal.collisionHeight,1.6f);
            Check(ai.hasLastSeen && ai.visible && p.z<12.8f,
                "normal enemy did not spot and follow player into corridor");
            // Replanning from arbitrary positions must still find the narrow opening.
            for (float x:{27.4185f,29.336f}) {
                ai={}; ai.settings.senses=normal.senses;
                p={x,12,18.5f}; r={0,-1.57079632679f,0};
                ai.Update(p,r,{28,12,12},.4f,false,&hotel.collision,normal.collisionRadius,normal.collisionHeight,1.6f);
                for (int frame=0;frame<600;++frame) {
                    const auto before=p;
                    ai.Update(p,r,{29,12,11.5f},1.0f/60,false,&hotel.collision,normal.collisionRadius,normal.collisionHeight,1.6f);
                    p=hotel.collision.Move(before,p,normal.collisionRadius,normal.collisionHeight);
                }
                Check(p.z<12.8f && ai.visible,"doorway edge caused chase to stall during replanning");
            }
            // A physical wall slide must leave a valid starting point for navigation.
            p=hotel.collision.Move({28,12,13.5f},{27.5f,12,13},normal.collisionRadius,normal.collisionHeight);
            Check(route.Plan(&hotel.collision,p,{29,12,11.5f},normal.collisionRadius,normal.collisionHeight),
                "wall contact stranded enemy with occupied navigation start");
            p=hotel.collision.Move({28.45f,12,12.48f},{28.55f,12,12.5f},normal.collisionRadius,normal.collisionHeight);
            Check(route.Plan(&hotel.collision,p,{29,12,11.5f},normal.collisionRadius,normal.collisionHeight),
                "shallow wall slide stranded enemy with occupied navigation start");
        }
        nlohmann::json data; std::ifstream("../../resources/Data/enemies.json")>>data;
        for (auto& d:data["enemies"]) d.erase("senses");
        data["enemies"][0]["detectionRange"]=17;
        std::ofstream("senses-legacy.json")<<data;
        Check(definitions.Load("senses-legacy.json"),"legacy definitions rejected");
        Check(definitions.Find("normal_test")->senses.viewDistance==17 && definitions.Find("normal")->senses.fieldOfView==110,"legacy defaults broken");
        for (const char* key:{"viewDistance","fieldOfView","detectionTime","loseSightTime","arrivalDistance","wanderRadius","wanderSpeedMultiplier","wanderWaitTime","investigateSpeedMultiplier","searchDuration","searchRadius","searchSpeedMultiplier","investigateUnreachableTime","memoryRetentionTime","memoryRecordInterval","memoryRecordDistance","searchDirectionPriority"}) {
            auto bad=data; bad["enemies"][0]["senses"][key]=-1;
            std::ofstream("senses-invalid.json")<<bad;
            Check(!definitions.Load("senses-invalid.json") && definitions.Find("normal_test")->senses.viewDistance==17,"invalid senses changed loaded definitions");
        }
    }
    std::cout<<"PASS: detection/FOV/time, rotated walls and multi-sample LOS, memory/grace/investigate/wander/reacquire, no spawn return, melee/ranged wall gating, pause/snapshot, bounded detour/no-path/gaps/height, legacy JSON validation.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
