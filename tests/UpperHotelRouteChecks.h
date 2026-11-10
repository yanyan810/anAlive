#pragma once
#include "StageLoader.h"
#include "StageProgress.h"
#include "EnemyNavigation.h"

namespace UpperHotelChecks {
inline constexpr float Upper=15.2f, Lower=11.4f;
inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Open, class Visit, class Interact, class Advance>
void WalkRoute(StageLoader& level, StageProgress& progress, Open open, Visit visit,
    Interact interact, Advance advance) {
    Require(level.id=="stage04" && level.collision.playerGravity && level.doors.doors.size()==30,
        "Upper hotel stage / door count");
    const auto upperWindows=std::count_if(level.removableMeshes.begin(),level.removableMeshes.end(),
        [](const auto& window) { return window.meshNode.starts_with("F5_"); });
    Require(upperWindows==16 && level.removableMeshes.size()==33 && level.transfer.has_value() &&
        level.collision.fallDeathDistance==3,
        "Window, breach, transfer and fall settings");
    auto& world=level.collision;
    auto feet=level.playerPosition;
    StageWorld::PlayerMotion motion;
    const auto walk=[&](const Vector3& destination,const char* name) {
        constexpr float tick=1.0f/60.0f;
        const float distance=std::hypot(destination.x-feet.x,destination.z-feet.z);
        const auto steps=std::max(1,static_cast<int>(std::ceil(distance/.075f)));
        const auto stride=Vector3{destination.x-feet.x,0,destination.z-feet.z}*(1.0f/static_cast<float>(steps));
        for (int i=0;i<steps;++i) feet=world.MovePlayer(feet,feet+stride,tick,motion);
        Require(StageLength(feet-destination)<.21f && motion.grounded && !motion.fatalFall,
            std::string(name)+": reached "+std::to_string(feet.x)+", "+std::to_string(feet.y)+", "+std::to_string(feet.z));
    };
    walk({6.35f,Upper,36.5f},"start room");
    Require(!EnemyNavigation::Clear(&world,feet,{9.5f,Upper,36.5f},.4f,1.8f),"closed start door leaked");
    open(feet,"F5_Room515_Door");
    walk({9.25f,Upper,36.5f},"opened start door");
    walk({13.5f,Upper,36.5f},"open lounge and weapon pickup");
    visit("item",feet);
    walk({13.5f,Upper,29.5f},"walk the length of the lounge");
    walk({9.7f,Upper,29.5f},"return to corridor");
    walk({9.7f,Upper,9.5f},"first corridor");
    walk({1.1f,Upper,9.5f},"5F west corridor");
    walk({9.7f,Upper,9.5f},"return to the 5F corner");
    walk({11.9f,Upper,9.5f},"first corner");
    visit("corner",feet);
    const Vector3 upperElevator{14.25f,Upper,9.5f};
    Require(world.Move(upperElevator,{14.25f,Upper,13.3f}).z<10.7f,
        "5F elevator must remain closed");
    const Vector3 beforeWall{27,Upper,9.5f}, afterWall{32,Upper,9.5f};
    Require(world.Move(beforeWall,afterWall,.4f,1.8f,true).x<29.6f &&
        !EnemyNavigation::Clear(&world,beforeWall,afterWall,.42f,1.8f),"5F barrier must block the direct corridor");
    EnemyNavigation detour;
    Require(detour.Plan(&world,beforeWall,afterWall,.42f,1.8f) &&
        std::any_of(detour.path.begin(),detour.path.end(),[](const auto& point) {return point.z<7.5f;}),
        "enemy planner could not take the 5F room detour");
    walk({25,Upper,9.5f},"Room503 threshold");
    walk({25,Upper,4},"Room503 interior");
    visit("detour",feet);
    walk({32,Upper,4},"opening between Rooms503 and 504");
    walk(afterWall,"Room504 exit");
    walk({50.5f,Upper,9.5f},"upper stair landing");
    walk({50.5f,13.3f,5.1f},"stair slope");
    walk({50.5f,Lower,.95f},"lower stair landing");
    walk({53.4f,Lower,.95f},"stair return turn");
    walk({53.4f,Lower,9.5f},"stair return aisle");
    walk({50.5f,Lower,9.5f},"4F approach to collapsed flight");
    const Vector3 belowCollapse{50.5f,11,7.8f};
    Require(world.Move(feet,belowCollapse,.4f,1.8f,true).z>8.95f &&
        !EnemyNavigation::Clear(&world,feet,belowCollapse,.42f,1.8f),"rubble must block the 4F to 3F flight");
    visit("stair-collapse",feet);
    walk({47.5f,Lower,9.5f},"exit stairs on 4F");
    visit("arrival",feet);

    const Vector3 east{41,Lower,9.5f}, west{32,Lower,9.5f};
    const auto blocked=world.Move(east,west,.4f,1.8f,true);
    Require(blocked.x>39,"player crossed the fire blocker");
    Require(!EnemyNavigation::Clear(&world,east,west,.42f,1.8f),"enemy path crossed fire");
    const auto enemy=EnemyNavigation::ConstrainMotion(world,east,west,.42f,1.8f);
    Require(enemy.x>39,"enemy motion crossed fire");

    walk({46,Lower,9.5f},"first room threshold");
    open(feet,"F4_Room406_Door");
    walk({46,Lower,3.2f},"detour room");
    walk({46,Lower,-.83f},"exit through first window");
    visit("ledge",feet);
    const auto checkWindow=[&](float x,const char* id) {
        walk({x,Lower,-.83f},"walk along facade to breakable pane");
        const Vector3 interior{x,Lower,1.4f};
        Require(world.Move(feet,interior).z<0 &&
            !EnemyNavigation::Clear(&world,feet,interior,.42f,1.8f),"intact glass must block movement");
        visit(id,feet);
        Require(EnemyNavigation::Clear(&world,feet,interior,.42f,1.8f),"broken glass must clear enemy navigation");
    };
    checkWindow(39,"window405");
    walk({32,Lower,-.83f},"original 404 exit");
    walk({32,Lower,4.5f},"reenter through second window");
    walk({32,Lower,9.5f},"return west of fire");
    EnemyNavigation nav;
    Require(nav.Plan(&world,east,west,.42f,1.8f) &&
        std::any_of(nav.path.begin(),nav.path.end(),[](const auto& point) {return point.z<0;}),
        "enemy planner could not find the open ledge detour");
    walk({32,Lower,-.83f},"return to the facade");
    checkWindow(25,"window403");
    checkWindow(18,"window402");
    walk({18,Lower,1.4f},"reenter Room402");
    walk({18,Lower,-.83f},"return from Room402");
    checkWindow(11,"window401");
    walk({11,Lower,1.4f},"reenter Room401");
    walk({11,Lower,-.83f},"return from Room401");
    visit("ledge-extension",feet);
    auto fall=feet;
    auto falling=motion;
    for (int i=0;i<75;++i) fall=world.MovePlayer(fall,fall+Vector3{0,0,-.075f},1.f/60,falling);
    Require(fall.y<Lower-3 && falling.fatalFall,"4F ledge drop must be fatal");
    StageWorld::PlayerMotion fromUpper;
    const Vector3 aboveLedge{11,Upper,-.83f};
    const auto landed=world.MovePlayer(aboveLedge,aboveLedge,1,fromUpper);
    Require(std::abs(landed.y-Lower)<.05f && fromUpper.grounded && fromUpper.fatalFall,
        "a 5F drop onto the 4F ledge must be fatal even across a long frame");
    walk({25,Lower,-.83f},"return to Room403");
    walk({25,Lower,9.5f},"reenter and cross Room403");
    walk({1.1f,Lower,9.5f},"4F west corridor");
    Require(!progress.Update(0,feet),"old elevator site still triggers the goal");
    walk({14.25f,Lower,9.5f},"4F elevator approach");
    visit("elevator",feet);
    walk({14.25f,Lower,13.3f},"4F elevator");
    Require(level.transfer->Contains(feet) && !progress.Update(0,feet),"4F elevator must lead to 1F");
    visit("transfer",feet);
    feet=level.transfer->arrivalPosition;
    motion={};
    constexpr float lobby=.08f;
    const float arrivalYaw=level.transfer->arrivalRotation.y;
    StageHit shutterView;
    Require(level.shutters.shutters.size()==1 &&
        world.Raycast(feet+Vector3{0,1.65f,0},{std::sin(arrivalYaw),0,std::cos(arrivalYaw)},45,shutterView) &&
        shutterView.colliderIndex==level.shutters.shutters.front().definition.colliderIndex,
        "1F elevator arrival faces the shutter through the open doorway");
    walk({21,lobby,7.67f},"1F elevator exit");
    walk({21,lobby,9.5f},"1F lobby north aisle");
    walk({21,lobby,16.5f},"1F lobby center");
    visit("lobby",feet);
    const auto lobbyFeet=feet;
    const auto lobbyMotion=motion;
    // Enter below the collapse in the fixture to check every lower flight.
    feet={50.5f,7.6f+3.8f*5/6.8f,6.7f};
    motion={};
    walk({50.5f,7.6f,.95f},"4F flight reaches the 3F landing");
    for (const float y : {7.6f,3.8f}) {
        walk({53.4f,y,.95f},"lower stair return turn");
        walk({53.4f,y,9.5f},"lower stair return aisle");
        walk({50.5f,y,9.5f},"next flight threshold");
        walk({50.5f,y>4 ? 3.8f : lobby,.95f},"descend to the next landing");
    }
    visit("stairwell",feet);
    walk({47,lobby,.95f},"1F stairwell exit");
    walk({47,lobby,4.1f},"1F stair connector turn");
    walk({33,lobby,4.1f},"stair connector into the north aisle");
    walk({33,lobby,7.5f},"north aisle into the lobby");
    feet=lobbyFeet;
    motion=lobbyMotion;
    walk({43.45f,lobby,16.5f},"exit approach");
    walk({43.45f,lobby,15.15f},"exit switch");

    const Vector3 beforeShutter{43.45f,lobby,16.5f}, afterShutter{48.3f,lobby,16.5f};
    const auto closed=[&]() { return world.Move(beforeShutter,afterShutter,.4f,1.8f,true).x<45; };
    const auto clear=[&](const Vector3& a,const Vector3& b) { return EnemyNavigation::Clear(&world,a,b,.42f,1.8f); };
    const Vector3 sourceA{23,lobby,3.9f}, roomA{23,lobby,7.6f};
    const Vector3 sourceB{42,lobby,9.5f}, roomB{37,lobby,9.5f};
    const Vector3 sourceC{19.7f,lobby,30}, roomC{19.7f,lobby,24.8f};
    Require(level.shutters.shutters.size()==1 && closed(),"exit starts closed");
    Require(!clear(sourceA,roomA) && !clear(sourceB,roomB) && !clear(sourceC,roomC),"breaches start sealed");
    interact(feet);
    Require(level.shutters.shutters.front().state==ShutterState::Opening,"E starts defense");
    const auto tick=[&](float dt) {
        level.shutters.Update(dt,world);
        advance();
    };
    tick(2.1f);
    const Vector3 stairSource{50.5f,lobby+(3.8f-lobby)*1.3f/6.8f,3};
    EnemyNavigation stairIngress;
    Require(stairIngress.Plan(&world,stairSource,beforeShutter,.42f,1.8f) &&
        clear({8,lobby,21.5f},{12,lobby,21.5f}) && clear({32,lobby,30},{32,lobby,25.5f}),
        "all three regular ingress routes must be walkable");
    visit("regular",feet);
    tick(10.1f); // 12.2 s
    Require(clear(sourceA,roomA) && !clear(sourceB,roomB) && !clear(sourceC,roomC),"only A opens at 12s");
    visit("breachA",feet);
    tick(8); // 20.2 s
    Require(clear(sourceA,roomA) && clear(sourceB,roomB) && !clear(sourceC,roomC),"B opens at 20s");
    visit("breachB",feet);
    tick(6); // 26.2 s
    Require(clear(sourceA,roomA) && clear(sourceB,roomB) && clear(sourceC,roomC),"C opens at 26s");
    visit("breachC",feet);
    tick(3.65f); // 29.85 s, visually raised, but still locked.
    Require(closed() && level.shutters.shutters.front().state==ShutterState::Opening,"exit must hold until 30s");
    visit("lastWave",feet);
    tick(.16f);
    Require(!closed() && level.shutters.shutters.front().state==ShutterState::Open,"shutter opens at 30s");
    visit("open",beforeShutter);
    walk(beforeShutter,"move to the opened exit");
    walk(afterShutter,"walk straight to the final goal");
    Require(progress.Goals().size()==1 && progress.Goals()[0].Contains(feet),"goal immediately beyond shutter");
    visit("goal",feet);
}
}
