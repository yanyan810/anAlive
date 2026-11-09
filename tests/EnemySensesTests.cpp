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
        EnemyAI observer; observer.settings.senses.viewDistance=6;
        StageWorld world;
        Check(observer.CanShareAlert({},1.8f,{3,0,0},1.8f,&world),"near unobstructed ally missed alert");
        Check(observer.CanShareAlert({},1.8f,{6,0,0},1.8f,&world),"alert range boundary excluded");
        Check(!observer.CanShareAlert({},1.8f,{6.1f,0,0},1.8f,&world),"alert escaped observer radius");
        world.colliders.push_back(Box({2,2,0},{.2f,2,4}));
        Check(!observer.CanShareAlert({},1.8f,{4,0,0},1.8f,&world),"alert crossed wall");
        Check(observer.CanShareAlert({},1.8f,{1,0,0},1.8f,&world),"same-side ally blocked by distant wall");
        world.colliders={Box({0,3,0},{5,.2f,5})};
        Check(!observer.CanShareAlert({},1.8f,{0,6,0},1.8f,&world),"alert crossed ceiling into another floor");
    }
    {
        StageWorld world;
        EnemyAI chasing; Vector3 p{},r=forward; chasing.settings.senses.detectionTime=0;
        chasing.Update(p,r,{0,0,6},.05f,false,&world);
        Check(chasing.pursuitOrdered,"direct recognition did not latch pursuit without a scene order");
        world.colliders.push_back(Box({0,2,3},{1,2,.2f}));
        for (int frame=0;frame<200;++frame) {
            const auto damage=chasing.Update(p,r,{0,0,6},.05f,false,&world);
            Check(EnemyNavigation::Clear(&world,p,p,.4f,1.8f),"persistent pursuit entered a wall");
            Check(!chasing.occluded || (damage==0 && chasing.attacksThisUpdate==0),"persistent pursuit attacked across wall");
        }
        Check(p.z>3.4f && chasing.pursuitOrdered,"persistent pursuit failed to route around wall");
    }

    {
        EnemyAI ordered; Vector3 p{},r=forward; StageWorld wall;
        wall.colliders.push_back(Box({0,3,3},{10,3,.25f}));
        ordered.OrderPursuit({0,0,6});
        const float damage=ordered.Update(p,r,{1,0,7},.1f,false,&wall);
        Check(ordered.pursuitOrdered && ordered.hasLastSeen && ordered.lastSeen.x==1 && ordered.lastSeen.z==7,"ordered pursuit lost hidden target");
        Check(ordered.occluded && damage==0 && ordered.attacksThisUpdate==0 && ordered.memory.count==0,"pursuit order granted wall attacks or fake visual memory");
        EnemyAI ranged=ordered; ranged.settings.ranged=true; ranged.settings.attackRange=20; ranged.settings.maxRange=20;
        Vector3 rp{},rr=forward; ranged.Update(rp,rr,{0,0,6},.1f,false,&wall);
        Check(ranged.attacksThisUpdate==0,"ordered ranged enemy fired through wall");
        ordered.settings.senses.viewDistance=0;
        p={}; ordered.navigation={}; ordered.Update(p,r,{0,0,6},.2f,false);
        Check(!ordered.visible && p.z>0 && ordered.state==EnemyState::Chase,"ordered enemy waited for its own sight");
        const auto stopped=p,goal=ordered.lastSeen; ordered.paused=true;
        ordered.Update(p,r,{0,0,10},1,false);
        Check(StageLength(p-stopped)==0 && StageLength(ordered.lastSeen-goal)==0,"paused pursuit advanced");
    }

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
        ai.Update(p,r,{0,0,6},.1f,false,&world);
        Check(ai.pursuitOrdered && ai.lastSeen.z==6,"initial sight did not latch discovery");
        world.colliders.push_back(Box({0,3,3},{10,3,.25f}));
        for (int frame=0;frame<600;++frame) {
            const Vector3 hidden{frame%2 ? -4.f : 4.f,0,11};
            Check(ai.Update(p,r,hidden,.1f,false,&world)==0 && ai.attacksThisUpdate==0,"retained discovery attacked through wall");
            Check(!ai.visible && ai.pursuitOrdered && ai.state==EnemyState::Chase && ai.lostTime==0 &&
                StageLength(ai.lastSeen-hidden)==0 && StageLength(ai.moveTarget-hidden)==0,"discovery timed out or retained only old position");
        }
        Check(ai.memory.count==0,"hidden pursuit fabricated visible observation history");
        world.colliders.clear(); ai.settings.moveSpeed=2;
        const auto start=p;
        ai.Update(p,r,{0,0,-50},.2f,false,&world);
        Check(!ai.visible && ai.state==EnemyState::Chase && p.z<start.z && ai.lastSeen.z==-50,"range/FOV loss stopped pursuit");
        ai.paused=true; const auto paused=p; const auto last=ai.lastSeen;
        ai.Update(p,r,{40,0,40},1,false,&world); Check(StageLength(p-paused)==0 && StageLength(ai.lastSeen-last)==0 && ai.attacksThisUpdate==0,"AI pause advanced");
        auto copy=ai; Check(copy.pursuitOrdered && StageLength(copy.lastSeen-last)==0 && copy.random==ai.random,"snapshot lost persistent discovery");
        ai.paused=false; const auto beforeDeath=p;
        ai.Update(p,r,{40,0,40},1,true,&world);
        Check(ai.state==EnemyState::Dead && StageLength(p-beforeDeath)==0 && ai.attacksThisUpdate==0,"dead enemy continued pursuit");
        ai={}; p={}; r=forward;
        ai.Update(p,r,{0,0,-50},1,false,&world);
        Check(!ai.pursuitOrdered && !ai.hasLastSeen && ai.state==EnemyState::Idle,"fresh AI retained discovery");
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
        floor.colliders.push_back(Box({0,5.75f,0},{2,.25f,2}));
        Check(!nav.Plan(&floor,{}, {0,6,0},.4f,1.8f),"route teleported between floors without stairs");
        StageWorld tallStep; tallStep.playerGravity=true;
        tallStep.colliders={Box({0,-.25f,0},{5,.25f,5}),Box({0,.4f,2},{1,.4f,1})};
        tallStep.colliders.back().walkable=true;
        Check(!EnemyNavigation::Clear(&tallStep,{}, {0,.8f,2},.4f,1.8f),"inside-origin support ray climbed a tall vertical block");
        tallStep.colliders.back()=Box({0,.2f,2},{1,.2f,1}); tallStep.colliders.back().walkable=true;
        Check(!EnemyNavigation::Clear(&tallStep,{}, {0,.4f,2},.4f,1.8f),"navigation clipped through a step above its maximum rise");

    }
    {
        EnemyDefinitions definitions; Check(definitions.Load("../../resources/Data/enemies.json"),"definitions failed");
        // The hotel doorway must admit the normal enemy's actual movement body.
        // A stale 3.22 m cylinder saw the player but could not follow the 2.11 m model outside.
        {
            StageLoader hotel;
            Check(hotel.Load("../../resources/levels/stage03/stage03.json"),"hotel stage failed");
            const auto& normal=*definitions.Find("normal");
            {
                const std::vector<std::pair<Vector3,Vector3>> cases={
                    {{4.4f,12,10.8f},{4.4f,10.5f,6}},
                    {{4.4f,10.5f,6},{4.4f,12,10.8f}},
                    {{4.4f,12,10.8f},{9,6,10.8f}},
                    {{9,6,10.8f},{4.4f,12,10.8f}},
                    {{36.4f,6,10.8f},{35,0,18}},
                    {{35,0,14},{36.4f,6,10.8f}},
                    {{28,12,11.5f},{9,6,10.8f}},
                    {{28.1f,12,19},{35,0,18}}
                };
                std::ofstream trace("enemy-stairs-trace.txt");
                for (size_t scenario=0;scenario<cases.size();++scenario) {
                    const auto [start,target]=cases[scenario]; EnemyNavigation route; Vector3 feet=start,rotation{};
                    const bool planned=route.Plan(&hotel.collision,feet,target,normal.collisionRadius,normal.collisionHeight);
                    trace<<scenario<<" planned="<<planned<<" state="<<static_cast<int>(route.state)<<" failure="<<static_cast<int>(route.failure)<<" points="<<route.path.size()<<std::endl;
                    Check(planned,"hotel stairs route failed to plan; see enemy-stairs-trace.txt");
                    for (int frame=0;frame<5400 && StageLength(feet-target)>.06f;++frame) {
                        const auto before=feet;
                        route.Move(feet,rotation,target,normal.moveSpeed,1.f/60,&hotel.collision,normal.collisionRadius,normal.collisionHeight);
                        feet=EnemyNavigation::ConstrainMotion(hotel.collision,before,feet,normal.collisionRadius,normal.collisionHeight);
                        Check(EnemyNavigation::Clear(&hotel.collision,feet,feet,normal.collisionRadius,normal.collisionHeight),"stair navigation intersected geometry");
                        if (frame%120==0) trace<<"  "<<frame<<" "<<feet.x<<","<<feet.y<<","<<feet.z<<" state="<<static_cast<int>(route.state)<<" failure="<<static_cast<int>(route.failure)<<std::endl;
                    }
                    Check(StageLength(feet-target)<.06f,"hotel stairs pursuit stalled; see enemy-stairs-trace.txt");
                }
            }

            for (int fps:{30,144}) {
                Vector3 feet{4.4f,12,10.8f},rotation{}; EnemyNavigation route;
                const Vector3 target{4.4f,10.62f,6};
                for (int frame=0;frame<fps*8 && (std::hypot(feet.x-target.x,feet.z-target.z)>.06f || std::abs(feet.y-target.y)>.25f);++frame)
                    route.Move(feet,rotation,target,normal.moveSpeed,1.f/fps,&hotel.collision,normal.collisionRadius,normal.collisionHeight);
                Check(std::hypot(feet.x-target.x,feet.z-target.z)<.06f && std::abs(feet.y-target.y)<.25f,"ramp pursuit depends on FPS or rejected lifted player feet");
            }
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
    std::cout<<"PASS: detection/FOV/time, permanent discovery after direct sight without scene orders, 60s hidden moving target, range/FOV loss, visible-only history, wall detour and melee/ranged occlusion, pause/death/reset/snapshot, stairs/gaps/height, legacy JSON validation.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
