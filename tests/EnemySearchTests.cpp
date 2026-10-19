#include "EnemyAI.h"
#include "EnemyDefinition.h"
#include <iostream>
#include <stdexcept>

static void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
static bool Near(float a,float b) { return std::abs(a-b)<.002f; }
static StageCollider Box(Vector3 p,Vector3 half) {
    StageCollider c; c.position=p; c.scale=half; c.local={{-1,-1,-1},{1,1,1}}; c.UpdateMatrices(); return c;
}
static EnemyAI Ready() { EnemyAI ai; ai.settings.senses.detectionTime=0; ai.settings.senses.fieldOfView=360; return ai; }
int main() try {
    const Vector3 hidden{100,0,100};
    {
        auto ai=Ready(); Vector3 p{},r{};
        ai.Update(p,r,{0,0,1},.1f,false); p=ai.lastSeen;
        ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Chase && ai.lastSeenReached && ai.searchElapsed==0,"nearby memory arrival skipped grace");
        for (int i=0;i<30 && ai.state!=EnemyState::Search;++i) ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Search && ai.lostTime>=2 && ai.searchElapsed==0,"arrival during grace did not begin fresh search after investigate");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; const Vector3 last{0,0,12};
        ai.Update(p,r,last,.1f,false);
        const auto before=p;
        ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Chase && Near(p.z-before.z,.25f) && Near(ai.currentSpeed,2.5f),"lost sight stopped/slow chase");
        Check(ai.lastSeen.z==12 && ai.moveTarget.z==12 && ai.moveTarget.x==0,"hidden target leaked into chase");
        unsigned int plans=0;
        for (int i=0;i<22;++i) {
            plans=ai.navigation.planAttempts;
            ai.Update(p,r,hidden+Vector3{static_cast<float>(i),0,0},.1f,false);
            Check(ai.searchElapsed==0 && !ai.lastSeenReached,"search clock ran before arrival");
            Check(Near(ai.currentSpeed,2.5f),"Investigate changed pursuit speed");
            Check(ai.navigation.planAttempts>=plans,"grace transition discarded route");
        }
        Check(ai.state==EnemyState::Investigate && ai.lastSeen.z==12 && ai.lostTime>2,"grace/default investigate transition failed");
        ai.settings.senses.investigateSpeedMultiplier=1.2f;
        ai.Update(p,r,hidden,.1f,false); Check(Near(ai.currentSpeed,3),"live investigate speed multiplier ignored");
        ai.settings.senses.investigateSpeedMultiplier=1;
        for (int i=0;i<100 && ai.state!=EnemyState::Search;++i) ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Search && ai.lastSeenReached && !ai.investigateUnreachable && ai.searchElapsed==0,"arrival failed to start fresh search");
        const auto memory=ai.lastSeen;
        for (int i=0;i<100 && ai.state==EnemyState::Search;++i) {
            ai.Update(p,r,hidden,.1f,false);
            if (ai.state==EnemyState::Search && !ai.navigation.path.empty()) {
                Check(EnemyAI::distanceTo(ai.moveTarget,memory)<=ai.settings.senses.searchRadius+.01f,"search goal outside radius");
                Check(ai.currentSpeed>0 && ai.currentSpeed<=2.126f,"search speed wrong");
            }
            Check(ai.attacksThisUpdate==0 && ai.lastSeen.z==memory.z,"search attacked hidden target or changed memory");
        }
        Check(ai.state==EnemyState::Wander && ai.searchPointsVisited>=2,"search did not visit multiple points then wander");
        for (int i=0;i<100;++i) ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Wander && EnemyAI::distanceTo(ai.moveTarget,ai.wanderCenter)<=6.01f,"wander left its current-area center");
    }
    {
        // Keep the existing A* algorithm and consume the full frame movement budget across nodes.
        EnemyNavigation nav; Vector3 p{},r{};
        nav.path={{0,0,.1f},{0,0,.2f},{0,0,1}}; nav.goal={0,0,1}; nav.state=EnemyPathState::Following;
        nav.Move(p,r,{0,0,1},2.5f,.1f,nullptr,.4f,1.8f);
        Check(Near(p.z,.25f),"A* waypoint boundaries lowered speed");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; StageWorld world;
        ai.settings.senses.loseSightTime=.2f; ai.settings.senses.investigateUnreachableTime=2.5f;
        ai.Update(p,r,{0,0,6},.1f,false,&world);
        world.colliders.push_back(Box({0,3,6},{1,3,1})); const auto stopped=p;
        unsigned int attempts=0;
        for (int i=0;i<40 && ai.state!=EnemyState::Search;++i) {
            ai.Update(p,r,hidden,.1f,false,&world);
            attempts=std::max(attempts,ai.navigation.planAttempts);
            Check(StageLength(p-stopped)<.001f && ai.searchElapsed==0,"unreachable investigation pushed wall or started search timer early");
        }
        Check(attempts>=3 && ai.state==EnemyState::Search && ai.investigateUnreachable && !ai.lastSeenReached,"unreachable route did not retry/fall back");
        Check(ai.investigateFailure==EnemyPathState::NoPath && StageLength(ai.searchCenter-stopped)<.01f && ai.lastSeen.z==6,"fallback lost reason/last seen position");
        for (int i=0;i<80 && ai.state==EnemyState::Search;++i) ai.Update(p,r,hidden,.1f,false,&world);
        Check(ai.state==EnemyState::Wander && ai.searchPointsVisited>0,"fallback did not explore reachable local area");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; StageWorld world;
        ai.settings.senses.loseSightTime=.2f;
        ai.Update(p,r,{0,0,6},.1f,false,&world);
        world.colliders.push_back(Box({0,3,6},{1,3,1}));
        for (int i=0;i<15;++i) ai.Update(p,r,hidden,.1f,false,&world);
        Check(ai.state==EnemyState::Investigate,"temporary obstruction abandoned too early");
        world.colliders.clear();
        for (int i=0;i<60 && ai.state!=EnemyState::Search;++i) ai.Update(p,r,hidden,.1f,false,&world);
        Check(ai.state==EnemyState::Search && ai.lastSeenReached && !ai.investigateUnreachable,"reopened route did not reach remembered target");
    }
    {
        auto ai=Ready(); Vector3 p{},r{}; StageWorld world; world.playerGravity=true;
        world.colliders.push_back(Box({0,-.25f,0},{2.5f,.25f,2.5f}));
        world.colliders.push_back(Box({1,3,0},{.2f,3,1}));
        ai.hasLastSeen=true; ai.lastSeen=p; ai.BeginSearch(p,false);
        for (int i=0;i<80 && ai.state==EnemyState::Search;++i) {
            ai.Update(p,r,hidden,.1f,false,&world);
            Check(EnemyNavigation::Clear(&world,p,p,.4f,1.8f),"search entered wall or absent floor");
            if (!ai.navigation.path.empty()) Check(EnemyNavigation::Clear(&world,ai.moveTarget,ai.moveTarget,.4f,1.8f),"search selected invalid destination");
        }
        Check(ai.state==EnemyState::Wander && ai.searchPointsVisited>0,"search with walls/floor did not finish");
    }
    {
        auto ai=Ready(); Vector3 p{},r{};
        for (int i=0;i<100;++i) ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Idle && StageLength(p)==0 && ai.searchElapsed==0,"undiscovered enemy began searching");
        ai.hasLastSeen=true; ai.lastSeen={}; ai.BeginSearch(p,false);
        ai.navigation.Plan(nullptr,p,{2,0,0},.4f,1.8f); ai.moveTarget={2,0,0};
        ai.settings.senses.searchRadius=.5f;
        ai.Update(p,r,hidden,.1f,false);
        Check(EnemyAI::distanceTo(ai.moveTarget,ai.searchCenter)<=.501f,"live search radius reduction ignored");
    }
    {
        auto ai=Ready(); Vector3 p{},r{};
        ai.hasLastSeen=true; ai.lastSeen={}; ai.BeginSearch(p,false); ai.settings.senses.searchDuration=.1f;
        Check(ai.navigation.Plan(nullptr,p,{2,0,0},.4f,1.8f),"search segment fixture"); ai.moveTarget={2,0,0};
        ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Search && ai.searchFinishing && p.x>0 && Near(ai.currentSpeed,2.125f),"search expiry stopped mid-segment");
        for (int i=0;i<20 && ai.state==EnemyState::Search;++i) ai.Update(p,r,hidden,.1f,false);
        Check(ai.state==EnemyState::Wander && Near(p.x,2),"expired search failed to finish its safe segment");
    }
    for (auto state:{EnemyState::Chase,EnemyState::Investigate,EnemyState::Search,EnemyState::Wander}) {
        auto ai=Ready(); Vector3 p{},r{}; ai.state=state; ai.hasLastSeen=true; ai.lastSeen={0,0,15};
        ai.searchElapsed=3; ai.searchPointsVisited=3; ai.investigateUnreachable=true;
        ai.Update(p,r,{0,0,5},.1f,false);
        Check(ai.state==EnemyState::Chase && ai.lastSeen.z==5 && ai.lostTime==0 && !ai.investigateUnreachable && ai.searchElapsed==0,"reacquire failed to clear search/fallback state");
    }
    for (bool ranged:{false,true}) {
        auto ai=Ready(); Vector3 p{},r{}; StageWorld world;
        ai.settings.ranged=ranged; ai.settings.minRange=0; ai.settings.maxRange=3; ai.settings.attackRange=3;
        ai.Update(p,r,{0,0,2},.1f,false,&world);
        world.colliders.push_back(Box({0,3,1},{4,3,.1f}));
        ai.BeginSearch(p,false);
        for (int i=0;i<10;++i) {
            Check(ai.Update(p,r,{0,0,2},.1f,false,&world)==0 && ai.attacksThisUpdate==0,"search attacked through wall");
            Check(!ai.visible && ai.lastSeen.z==2,"search xray memory");
        }
    }
    {
        EnemyDefinitions defs; nlohmann::json data; std::ifstream("../../resources/Data/enemies.json")>>data;
        for (auto& d:data["enemies"]) for (const char* key:{"loseSightTime","investigateSpeedMultiplier","searchDuration","searchRadius","searchSpeedMultiplier","investigateUnreachableTime"}) d["senses"].erase(key);
        std::ofstream("search-legacy.json")<<data;
        Check(defs.Load("search-legacy.json"),"legacy senses rejected");
        const auto& s=defs.Find("normal")->senses;
        Check(s.loseSightTime==2 && s.investigateSpeedMultiplier==1 && s.searchDuration==4 && s.searchRadius==5 && s.searchSpeedMultiplier==.85f,"new defaults not applied");
    }
    std::cout<<"PASS: full-speed blind chase/investigate without xray, preserved A* route and waypoint speed, arrival-only Search timer, multiple reachable search goals, smooth expiry, Wander/reacquire/no spawn return, unreachable stop/retry/local fallback, reopening routes, melee/ranged search occlusion, optional JSON defaults.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
