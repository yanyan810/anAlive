#include "EnemyAI.h"
#include "EnemyDefinition.h"
#include <iostream>
#include <stdexcept>

static void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
static StageCollider Box(Vector3 p,Vector3 half) {
    StageCollider c; c.position=p; c.scale=half; c.local={{-1,-1,-1},{1,1,1}}; c.UpdateMatrices(); return c;
}
static EnemyAI Ready() { EnemyAI ai; ai.settings.senses.detectionTime=0; ai.settings.senses.fieldOfView=360; return ai; }
static void EastHistory(EnemyAI& ai,Vector3& p,Vector3& r) {
    for (int i=0;i<3;++i) ai.Update(p,r,{static_cast<float>(i-2),0,6},.1f,false);
    Check(ai.memory.count>=2 && ai.memory.hasDirection && ai.memory.escapeDirection.x>.99f,"visible east history did not infer east");
}
int main() try {
    const Vector3 hidden{100,0,-100};
    {
        auto ai=Ready(); Vector3 p{},r{};
        ai.settings.senses.memoryRecordInterval=.5f; ai.settings.senses.memoryRecordDistance=2;
        ai.Update(p,r,{0,0,8},.1f,false); Check(ai.memory.count==1,"initial visible position not recorded");
        ai.Update(p,r,{.1f,0,8},.1f,false); Check(ai.memory.count==1,"recorded too frequently");
        ai.Update(p,r,{.2f,0,8},.4f,false); Check(ai.memory.count==2,"time interval did not record");
        ai.Update(p,r,{2.5f,0,8},.01f,false); Check(ai.memory.count==3,"distance threshold did not record");
        const auto count=ai.memory.count; const auto last=ai.memory.samples[count-1]; const auto direction=ai.memory.escapeDirection;
        ai.Update(p,r,hidden,1,false);
        Check(ai.memory.count==count && ai.memory.samples[count-1].time==last.time && StageLength(ai.memory.samples[count-1].position-last.position)==0,"hidden coordinates appended");
        Check(StageLength(ai.memory.escapeDirection-direction)==0 && ai.lastSeen.x==2.5f,"hidden movement changed memory/direction");
        const auto copy=ai;
        Check(copy.memory.count==count && copy.memory.clock==ai.memory.clock && StageLength(copy.memory.escapeDirection-direction)==0,"snapshot lost memory/direction");
        ai.settings.senses.memoryRetentionTime=.2f;
        ai.Update(p,r,hidden,.3f,false); Check(ai.memory.count==0,"expired samples retained");
        Check(StageLength(ai.memory.escapeDirection-direction)==0,"loss changed cached last-observed direction");
        ai.settings.senses.memoryRetentionTime=0;
        ai.Update(p,r,hidden,.1f,false); Check(!ai.memory.hasDirection && ai.memory.count==0,"disabling memory retained direction");
    }
    {
        auto ai=Ready(); Vector3 p{},r{};
        ai.settings.senses.memoryRetentionTime=60; ai.settings.senses.memoryRecordInterval=0;
        for (int i=0;i<300;++i) ai.Update(p,r,{static_cast<float>(i)*.01f,0,6},.01f,false);
        Check(ai.memory.count==EnemyObservationMemory::kCapacity && ai.memory.samples[0].position.x>2 && ai.memory.samples[ai.memory.count-1].position.x>2.98f,"history cap/oldest eviction broken");
    }
    {
        auto ai=Ready(); Vector3 p{},r{0,1.57079632679f,0};
        ai.settings.senses.fieldOfView=90;
        ai.Update(p,r,{8,0,0},1,false); Check(!ai.visible && ai.memory.count==0,"outside-FOV sample recorded");
        ai.settings.senses.detectionTime=1;
        ai.Update(p,r,{0,0,8},.1f,false); Check(ai.visible && ai.state==EnemyState::Idle && ai.memory.count==1,"raw visible observation missing before acquisition");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; EastHistory(ai,p,r);
        const auto previousSegment=ai.memory.samples[ai.memory.count-1].segment;
        ai.Update(p,r,hidden,.1f,false);
        ai.state=EnemyState::Search; ai.settings.senses.detectionTime=10;
        ai.Update(p,r,{-3,0,5},.01f,false);
        Check(ai.state==EnemyState::Chase && ai.lastSeen.x==-3,"Search rediscovery did not immediately chase");
        Check(!ai.memory.hasDirection && ai.memory.samples[ai.memory.count-1].segment!=previousSegment,"direction inferred across occlusion gap");
        ai.Update(p,r,{-2,0,5},.1f,false); Check(ai.memory.hasDirection && ai.memory.escapeDirection.x>.99f,"new visible sequence failed to update direction");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; EastHistory(ai,p,r);
        ai.Update(p,r,{0,0,7},.1f,false);
        ai.Update(p,r,{0,0,8},.1f,false);
        Check(ai.memory.hasDirection && ai.memory.escapeDirection.z>.99f && std::abs(ai.memory.escapeDirection.x)<.01f,"last visible turn was averaged into an old escape heading");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; EastHistory(ai,p,r); p=ai.lastSeen;
        ai.settings.senses.searchDirectionPriority=1; ai.BeginSearch(p,false);
        Check(ai.PickSearchGoal(p,nullptr,.4f,1.8f) && ai.moveTarget.x>p.x+.5f && std::abs(ai.moveTarget.z-p.z)<.01f,"clear Search did not continue east beyond observed trail");
        Check(ai.moveTarget.x>ai.memory.samples[ai.memory.count-1].position.x,"Search retraced old coordinates");
        StageWorld world; world.colliders.push_back(Box({1,3,6},{.2f,3,3}));
        ai.BeginSearch(p,false);
        Check(ai.PickSearchGoal(p,&world,.4f,1.8f),"blocked forward route did not choose reachable side alternative");
        Check(ai.moveTarget.x>-.1f && std::abs(ai.moveTarget.z-p.z)>.5f && EnemyNavigation::Clear(&world,ai.moveTarget,ai.moveTarget,.4f,1.8f),"blocked east search moved backwards or inside wall");
        Check(ai.searchCandidatesRejected>0 && ai.searchFailure==EnemyPathFailure::GoalOccupied,"invalid candidate rejection reason missing");
    }
    {
        auto ai=Ready(); Vector3 p{},r{};
        for (int i=-2;i<=0;++i) ai.Update(p,r,{0,0,static_cast<float>(i)},.1f,false);
        p=ai.lastSeen; ai.settings.senses.searchDirectionPriority=1; ai.BeginSearch(p,false);
        StageWorld world;
        world.colliders={Box({0,3,.7f},{3,3,.1f}),Box({.7f,3,-1},{.1f,3,5}),Box({-.7f,3,-1},{.1f,3,5})};
        bool planned=false;
        for (int attempt=0;attempt<4 && !planned;++attempt) planned=ai.PickSearchGoal(p,&world,.4f,1.8f);
        Check(planned && ai.moveTarget.z<-.5f && ai.searchCandidatesRejected>0,"failed forward candidates starved the reachable rear fallback");
    }
    {
        auto ai=Ready(); Vector3 p{},r{};
        ai.hasLastSeen=true; ai.lastSeen=p; ai.BeginSearch(p,false); ai.settings.senses.searchRadius=0;
        for (int i=0;i<10;++i) ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Search && StageLength(p)==0 && ai.navigation.failure==EnemyPathFailure::NoCandidate,"failed search immediately wandered or pushed obstacles");
        ai.settings.senses.searchDuration=0; ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Wander && StageLength(ai.wanderCenter-p)==0,"post-search Wander did not center on current position");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; ai.hasLastSeen=true; ai.lastSeen=p; ai.BeginSearch(p,false);
        StageWorld world; world.playerGravity=true; world.colliders.push_back(Box({0,-.25f,0},{2.5f,.25f,2.5f}));
        world.colliders.push_back(Box({1,3,0},{.2f,3,1}));
        for (int i=0;i<60 && ai.state==EnemyState::Search;++i) {
            ai.Update(p,r,hidden,.1f,false,&world);
            Check(EnemyNavigation::Clear(&world,p,p,.4f,1.8f),"memory search entered absent floor/obstacle");
            if (!ai.navigation.path.empty()) Check(EnemyNavigation::Clear(&world,ai.moveTarget,ai.moveTarget,.4f,1.8f),"invalid reachable-alternative goal");
        }
        Check(ai.state==EnemyState::Wander && ai.searchPointsVisited>0,"no-history fallback search failed");
    }
    {
        StageWorld world; EnemyNavigation nav;
        world.colliders.push_back(Box({0,3,0},{1,3,1}));
        Check(!nav.Plan(&world,{}, {0,0,6},.4f,1.8f) && nav.failure==EnemyPathFailure::StartOccupied,"start-occupied reason missing");
        world.colliders[0]=Box({0,3,6},{1,3,1});
        Check(!nav.Plan(&world,{}, {0,0,6},.4f,1.8f) && nav.failure==EnemyPathFailure::GoalOccupied,"goal-occupied reason missing");
        Check(!nav.Plan(&world,{}, {0,5,6},.4f,1.8f) && nav.failure==EnemyPathFailure::HeightDifference,"height reason missing");
        world={}; world.playerGravity=true;
        Check(!nav.Plan(&world,{}, {0,0,6},.4f,1.8f) && nav.failure==EnemyPathFailure::MissingFloor,"floor reason missing");
        world={}; world.colliders.push_back(Box({0,3,3},{100,3,.2f}));
        Check(!nav.Plan(&world,{}, {0,0,100},.4f,1.8f) && nav.failure==EnemyPathFailure::OutsideGrid,"grid-limit reason missing");
        Check(!nav.Plan(&world,{}, {0,0,6},.4f,1.8f) && nav.failure==EnemyPathFailure::NodeBudget,"node-budget reason missing");
        world={};
        world.colliders={Box({0,3,1},{2,3,.1f}),Box({0,3,-1},{2,3,.1f}),Box({1,3,0},{.1f,3,2}),Box({-1,3,0},{.1f,3,2})};
        Check(!nav.Plan(&world,{}, {0,0,6},.4f,1.8f) && nav.failure==EnemyPathFailure::Disconnected,"disconnected reason missing");
    }
    {
        EnemyDefinitions defs; nlohmann::json data; std::ifstream("../../resources/Data/enemies.json")>>data;
        for (auto& d:data["enemies"]) for (const char* key:{"memoryRetentionTime","memoryRecordInterval","memoryRecordDistance","searchDirectionPriority"}) d["senses"].erase(key);
        std::ofstream("memory-legacy.json")<<data; Check(defs.Load("memory-legacy.json"),"legacy JSON rejected");
        const auto& s=defs.Find("normal")->senses;
        Check(s.memoryRetentionTime==6 && s.memoryRecordInterval==.25f && s.memoryRecordDistance==.5f && s.searchDirectionPriority==.85f,"optional memory defaults wrong");
        data["enemies"][0]["senses"]["searchDirectionPriority"]=1.1f;
        std::ofstream("memory-invalid.json")<<data; Check(!defs.Load("memory-invalid.json"),"direction weight above one accepted");
    }
    std::cout<<"PASS: visible-only timed/distance history, bounded capacity/expiry/snapshot, no hidden updates, continuous-sighting direction, immediate reacquire, forward/side reachable Search, rejected-goal reasons, safe no-candidate retry, current-area Wander, detailed A* failures, legacy memory JSON.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
