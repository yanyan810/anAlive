#pragma once
#include "EnemyParts.h"
#include "EnemySenses.h"
#include "EnemyNavigation.h"
#include "EnemyObservationMemory.h"

enum class EnemyState { Idle, Chase, Investigate, Search, Wander, Attack, Dead };
inline const char* EnemyStateName(EnemyState state) {
    switch (state) {
    case EnemyState::Idle: return "Idle";
    case EnemyState::Chase: return "Chase";
    case EnemyState::Investigate: return "Investigate";
    case EnemyState::Search: return "Search";
    case EnemyState::Wander: return "Wander";
    case EnemyState::Attack: return "Attack";
    default: return "Dead";
    }
}
inline bool EnemyPartsDead(const EnemyParts& parts) {
    if (parts.deathProcessed) return true;
    for (const auto& group:parts.hpGroups) if (group.deathOnZero && group.hp<=0) return true;
    for (const auto& part : parts)
        if (part.deathOnZero && part.usesLocalHp && part.hp<=0) return true;
    return false;
}
// Symmetric correction per enemy; radius sum defines minimum XZ separation.
inline Vector3 EnemySeparationOffset(const Vector3& a,float radiusA,const Vector3& b,float radiusB,float dt) {
    const Vector3 delta{a.x-b.x,0,a.z-b.z};
    const float distance=std::hypot(delta.x,delta.z);
    const float minimum=radiusA+radiusB;
    if (distance>=minimum || !std::isfinite(dt) || dt<=0) return {};
    const auto direction=distance>1e-5f ? delta*(1/distance) : Vector3{1,0,0};
    return direction*std::min((minimum-distance)*.5f,dt*.6f);
}
struct EnemyAISettings {
    float detectionRange = 20;
    float attackRange = 1.5f;
    float moveSpeed = 2.5f;
    float attackDamage = 10;
    float attackInterval = 1;
    bool ranged = false;
    float minRange = 7, maxRange = 16;
    EnemySenses senses{};
};
struct EnemyAI {
    EnemyState state = EnemyState::Idle;
    EnemyAISettings settings{};
    float distance = 0;
    float cooldown = 0;
    unsigned int attacksThisUpdate = 0;
    bool paused=false, hasLastSeen=false, inFov=false, occluded=false, visible=false;
    float detection=0, lostTime=0, wanderWait=0;
    float searchElapsed=0, searchRetry=0, investigateBlockedTime=0, currentSpeed=0;
    bool lastSeenReached=false, investigateUnreachable=false, searchFinishing=false;
    EnemyPathState investigateFailure=EnemyPathState::None;
    EnemyPathFailure investigateFailureReason=EnemyPathFailure::None, searchFailure=EnemyPathFailure::None;
    unsigned int searchCandidatesRejected=0;
    unsigned int searchPointsVisited=0;
    Vector3 searchCenter{}, wanderCenter{}, rejectedSearchGoal{};
    std::array<Vector3,8> checkedSearchPoints{};
    size_t checkedSearchCount=0;
    size_t searchCandidateOffset=0;
    EnemyObservationMemory memory;
    Vector3 lastSeen{}, moveTarget{}, eye{}, sightTarget{};
    EnemyNavigation navigation;
    uint32_t random=0x9e3779b9u;
    float RandomUnit() { random=random*1664525u+1013904223u; return static_cast<float>(random>>8)/16777216.0f; }
    float SearchRemaining() const { return state==EnemyState::Search ? std::max(0.0f,settings.senses.searchDuration-searchElapsed) : 0; }
    void BeginSearch(const Vector3& position,bool unreachable) {
        state=EnemyState::Search; searchElapsed=0; searchRetry=0; searchPointsVisited=0; searchFinishing=false;
        lastSeenReached=!unreachable; investigateUnreachable=unreachable;
        searchCenter=unreachable ? position : lastSeen; moveTarget=searchCenter; navigation={};
        searchFailure=EnemyPathFailure::None; searchCandidatesRejected=0;
        checkedSearchCount=1; checkedSearchPoints[0]=position;
        searchCandidateOffset=0;
    }
    void BeginWander(const Vector3& position) {
        wanderCenter=position;
        state=EnemyState::Wander; wanderWait=settings.senses.wanderWaitTime; navigation={};
    }
    bool PickSearchGoal(const Vector3& position,const StageWorld* world,float radius,float height) {
        const auto& s=settings.senses;
        // Short successive legs let a zombie inspect several places within four
        // seconds, while the complete search stays inside the configured circle.
        const float leg=std::min(s.searchRadius,std::max(.5f,settings.moveSpeed*s.searchSpeedMultiplier*.75f));
        struct Candidate { Vector3 point{}; float score=0; };
        std::array<Candidate,16> candidates{}; size_t count=0;
        const float priority=memory.hasDirection ? std::clamp(s.searchDirectionPriority,0.0f,1.0f) : 0;
        const float yaw=std::atan2(memory.escapeDirection.x,memory.escapeDirection.z);
        constexpr float angles[]={0,.5235988f,-.5235988f,1.0471976f,-1.0471976f,1.5707963f,-1.5707963f,3.14159265f};
        for (int attempt=0;attempt<16;++attempt) {
            const float angle=priority>0 ? yaw+angles[attempt%8] : RandomUnit()*6.28318530718f;
            const float range=leg*(priority>0 ? (attempt<8 ? 1.0f : .6f) : .6f+.4f*RandomUnit());
            const auto candidate=position+Vector3{std::sin(angle)*range,0,std::cos(angle)*range};
            if (distanceTo(candidate,searchCenter)>s.searchRadius || distanceTo(position,candidate)<.1f) continue;
            if (!EnemyNavigation::Clear(world,candidate,candidate,radius,height)) {
                ++searchCandidatesRejected; rejectedSearchGoal=candidate;
                searchFailure=EnemyNavigation::Supported(world,candidate) ? EnemyPathFailure::GoalOccupied : EnemyPathFailure::MissingFloor;
                continue;
            }
            const auto delta=candidate-position;
            const float alignment=range>0 ? (delta.x*memory.escapeDirection.x+delta.z*memory.escapeDirection.z)/range : 0;
            const auto fromMemory=candidate-lastSeen;
            const float progress=(fromMemory.x*memory.escapeDirection.x+fromMemory.z*memory.escapeDirection.z)/std::max(1.0f,s.searchRadius);
            float score=priority*(4*alignment+progress)+(1-priority)*RandomUnit()*2;
            for (size_t i=0;i<checkedSearchCount;++i) {
                const float visitedDistance=distanceTo(candidate,checkedSearchPoints[i]);
                if (visitedDistance<.7f) score-=(1-visitedDistance/.7f)*3;
            }
            candidates[count++]={candidate,score};
        }
        std::sort(candidates.begin(),candidates.begin()+count,[](const Candidate& a,const Candidate& b) { return a.score>b.score; });
        // Bound repeated A* work; try forward and nearby alternatives before stopping.
        const size_t first=count>0 ? searchCandidateOffset%count : 0;
        const size_t attempts=std::min(count,size_t{6});
        for (size_t attempt=0;attempt<attempts;++attempt) {
            const auto candidate=candidates[(first+attempt)%count].point;
            if (navigation.Plan(world,position,candidate,radius,height)) {
                float length=0; auto previous=position;
                for (const auto& point:navigation.path) { length+=StageLength(point-previous); previous=point; }
                if (length<=std::max(leg*5,s.searchRadius*2)) { moveTarget=candidate; searchCandidateOffset=0; return true; }
                navigation.path.clear(); navigation.next=0; navigation.state=EnemyPathState::NoPath;
                navigation.failure=EnemyPathFailure::ExcessiveDetour;
            }
            ++searchCandidatesRejected; rejectedSearchGoal=candidate; searchFailure=navigation.failure;
        }
        // Continue with other directions on the next retry. A low-ranked but
        // reachable fallback must not be starved by the same six failed goals.
        if (count>0) searchCandidateOffset=(first+attempts)%count;
        if (navigation.state==EnemyPathState::None || navigation.state==EnemyPathState::Direct ||
            navigation.state==EnemyPathState::Following || navigation.state==EnemyPathState::Arrived) navigation.state=EnemyPathState::NoPath;
        navigation.failure=searchFailure==EnemyPathFailure::None ? EnemyPathFailure::NoCandidate : searchFailure;
        return false;
    }
    // Reads the physical target only here. Hidden target coordinates never reach movement or memory.
    float Update(Vector3& position,Vector3& rotation,const Vector3& target,float dt,bool dead,
        const StageWorld* world=nullptr,float radius=.4f,float height=1.8f,float playerEyeHeight=1.65f) {
        attacksThisUpdate=0;
        currentSpeed=0;
        if (dead) { state=EnemyState::Dead; return 0; }
        if (paused) return 0;
        dt=std::isfinite(dt) ? std::max(0.0f,dt) : 0;
        const auto startPosition=position;
        const auto finish=[&](float damage) { currentSpeed=dt>0 ? distanceTo(startPosition,position)/dt : 0; return damage; };
        const auto& senses=settings.senses;
        const Vector3 delta{target.x-position.x,0,target.z-position.z};
        distance=StageLength(delta);
        const float facing=rotation.y-1.57079632679f;
        inFov=distance<1e-5f || (std::sin(facing)*delta.x+std::cos(facing)*delta.z)/distance >=
            std::cos(std::clamp(senses.fieldOfView,0.0f,360.0f)*.00872664626f);
        eye=position+Vector3{0,height*.85f,0};
        visible=false; occluded=true;
        const Vector3 samples[]={target+Vector3{0,playerEyeHeight,0},target+Vector3{0,playerEyeHeight*.6f,0}};
        sightTarget=samples[0];
        for (const auto& sample:samples) {
            StageHit hit;
            const bool blocked=world && world->Raycast(eye,sample-eye,StageLength(sample-eye),hit,0,true);
            if (!blocked) { occluded=false; sightTarget=sample; }
            if (!blocked && inFov && StageLength(sample-eye)<=senses.viewDistance) { visible=true; sightTarget=sample; break; }
        }
        memory.Update(dt,visible,target,senses);
        detection=visible ? std::min(senses.detectionTime,detection+dt) : 0;
        const bool recognized=visible && (hasLastSeen || detection>=senses.detectionTime);
        if (recognized) {
            lastSeen=target; hasLastSeen=true; lostTime=0;
            lastSeenReached=false; investigateUnreachable=false; investigateBlockedTime=0; investigateFailure=EnemyPathState::None;
            investigateFailureReason=EnemyPathFailure::None;
            searchElapsed=0; searchFinishing=false; searchPointsVisited=0;
            if (state!=EnemyState::Chase && state!=EnemyState::Attack) navigation={};
            state=EnemyState::Chase;
        } else if (hasLastSeen) {
            lostTime+=dt;
            if (state==EnemyState::Chase || state==EnemyState::Attack) {
                state=lostTime>=senses.loseSightTime ? EnemyState::Investigate : EnemyState::Chase;
                // Preserve the route and retry clock across the grace-period boundary.
            }
        }
        if (!hasLastSeen) { state=EnemyState::Idle; cooldown=std::max(0.0f,cooldown-dt); return 0; }
        if (recognized) {
            moveTarget=lastSeen;
            // Keep the original attack cadence/ranges. Movement/attacks require a clear body route;
            // LOS additionally gates projectile emission, including while within the lost-sight grace period.
            if (EnemyNavigation::Clear(world,position,lastSeen,radius,height)) {
                navigation.path.clear(); navigation.next=0; navigation.state=EnemyPathState::Direct;
                navigation.failure=EnemyPathFailure::None;
                auto desired=position;
                const float damage=Combat(desired,rotation,lastSeen,dt,false);
                // Ranged retreat goes away from the observed target. Check that segment too,
                // so backing into a wall or off the floor cannot bypass navigation.
                if (!EnemyNavigation::Clear(world,position,desired,radius,height)) {
                    navigation.state=EnemyPathState::NoPath; navigation.failure=EnemyPathFailure::BlockedSegment;
                    attacksThisUpdate=0; state=EnemyState::Chase; return 0;
                }
                position=desired; return finish(damage);
            }
            navigation.Move(position,rotation,moveTarget,settings.moveSpeed,dt,world,radius,height);
        } else if (state==EnemyState::Chase || state==EnemyState::Investigate) {
            moveTarget=lastSeen;
            const auto arrived=[&]() { return distanceTo(position,lastSeen)<=senses.arrivalDistance && std::abs(position.y-lastSeen.y)<=.3f; };
            if (arrived()) {
                lastSeenReached=true;
                if (state==EnemyState::Investigate) BeginSearch(position,false);
            }
            else {
                const float multiplier=state==EnemyState::Investigate ? senses.investigateSpeedMultiplier : 1.0f;
                navigation.Move(position,rotation,lastSeen,settings.moveSpeed*multiplier,dt,world,radius,height);
                if (arrived()) {
                    lastSeenReached=true;
                    if (state==EnemyState::Investigate) BeginSearch(position,false);
                }
                else if (state==EnemyState::Investigate) {
                    const bool failed=navigation.state==EnemyPathState::NoPath || navigation.state==EnemyPathState::Limited || navigation.state==EnemyPathState::Unsupported;
                    investigateBlockedTime=failed ? investigateBlockedTime+dt : 0;
                    if (failed) { investigateFailure=navigation.state; investigateFailureReason=navigation.failure; }
                    if (failed && investigateBlockedTime>=senses.investigateUnreachableTime) BeginSearch(position,true);
                }
            }
        } else if (state==EnemyState::Search) {
            searchElapsed+=dt; searchRetry=std::max(0.0f,searchRetry-dt);
            const float speed=settings.moveSpeed*senses.searchSpeedMultiplier;
            if (SearchRemaining()>0) searchFinishing=false; // Live duration extension resumes exploration.
            if (!navigation.path.empty() && !searchFinishing && distanceTo(moveTarget,searchCenter)>senses.searchRadius+.001f) {
                navigation.path.clear(); navigation.next=0; searchRetry=0; // Live radius reduction.
            }
            if (SearchRemaining()<=0 && !searchFinishing) {
                searchFinishing=true;
                // Finish the current safe segment, rather than abruptly stopping
                // mid-stride or spending minutes completing a long detour.
                if (navigation.next<navigation.path.size()) {
                    navigation.path.resize(navigation.next+1); moveTarget=navigation.path.back(); navigation.goal=moveTarget;
                }
            }
            if (searchFinishing && (navigation.path.empty() || speed<=0)) BeginWander(position);
            else {
                if (navigation.path.empty() && searchRetry<=0) {
                    if (!PickSearchGoal(position,world,radius,height)) searchRetry=.5f;
                }
                if (!navigation.path.empty()) {
                    navigation.Move(position,rotation,moveTarget,speed,dt,world,radius,height);
                    if (navigation.path.empty()) {
                        if (navigation.state==EnemyPathState::Arrived) {
                            ++searchPointsVisited;
                            if (checkedSearchCount==checkedSearchPoints.size()) {
                                for (size_t i=1;i<checkedSearchCount;++i) checkedSearchPoints[i-1]=checkedSearchPoints[i];
                                --checkedSearchCount;
                            }
                            checkedSearchPoints[checkedSearchCount++]=position;
                        }
                        else searchRetry=.5f;
                        if (searchFinishing) BeginWander(position);
                    }
                }
            }
        } else if (state==EnemyState::Wander) {
            if (wanderWait>0) wanderWait=std::max(0.0f,wanderWait-dt);
            else if (navigation.path.empty()) {
                bool planned=false;
                for (int attempt=0;attempt<8 && !planned;++attempt) {
                    const float angle=RandomUnit()*6.28318530718f;
                    const float range=std::sqrt(RandomUnit())*senses.wanderRadius;
                    const auto center=wanderCenter;
                    const auto candidate=center+Vector3{std::sin(angle)*range,0,std::cos(angle)*range};
                    if (distanceTo(position,candidate)<=senses.arrivalDistance) continue;
                    planned=navigation.Plan(world,position,candidate,radius,height);
                    if (planned) moveTarget=candidate;
                }
                if (!planned) wanderWait=std::max(.5f,senses.wanderWaitTime);
            } else {
                navigation.Move(position,rotation,moveTarget,settings.moveSpeed*senses.wanderSpeedMultiplier,dt,world,radius,height);
                if (navigation.path.empty()) wanderWait=std::max(.1f,senses.wanderWaitTime);
            }
        }
        cooldown=std::max(0.0f,cooldown-dt); return finish(0);
    }
    static float distanceTo(const Vector3& a,const Vector3& b) { return std::hypot(a.x-b.x,a.z-b.z); }
    // Returns pending damage. Scene applies it after shooting, so lethal shots win this frame.
    float Combat(Vector3& position, Vector3& rotation, const Vector3& target, float dt, bool dead) {
        attacksThisUpdate = 0;
        const Vector3 delta{target.x-position.x,0,target.z-position.z};
        distance = std::hypot(delta.x,delta.z);
        if (dead) { state=EnemyState::Dead; return 0; }
        dt = std::isfinite(dt) ? std::max(dt,0.0f) : 0;
        const float attackRange=std::max(settings.attackRange,0.0f);
        float reachTime=0;
        if (distance > 1e-6f) {
            // Boss glTF front +X becomes engine -X during import.
            rotation={0,std::atan2(delta.x,delta.z)+1.57079632679f,0};
        }
        if (settings.ranged) {
            const float minimum=std::max(0.0f,settings.minRange);
            const float maximum=std::max(minimum,settings.maxRange);
            if (distance<minimum || distance>maximum) {
                const float desired=distance<minimum ? minimum : maximum;
                const float travel=std::min(std::max(settings.moveSpeed,0.0f)*dt,std::abs(distance-desired));
                const Vector3 direction=distance>1e-6f ? delta*(1.0f/distance) : Vector3{0,0,1};
                position+=direction*(distance<minimum ? -travel : travel);
                distance=std::hypot(target.x-position.x,target.z-position.z);
                state=EnemyState::Chase; cooldown=std::max(0.0f,cooldown-dt); return 0;
            }
            state=EnemyState::Attack;
            cooldown=std::max(0.0f,cooldown-dt);
            if (dt>0 && cooldown<=0 && distance<=attackRange) {
                attacksThisUpdate=1; cooldown=std::max(.05f,settings.attackInterval);
            }
            return 0; // Scene emits a projectile after resolving player shots.
        }
        if (distance > attackRange) {
            const float speed=std::max(settings.moveSpeed,0.0f);
            const float travel=std::min(speed*dt,distance-attackRange);
            position=position+delta*(travel/distance);
            reachTime = speed > 0 ? (distance-attackRange)/speed : std::numeric_limits<float>::infinity();
            distance-=travel;
            if (reachTime > dt) {
                state=EnemyState::Chase; cooldown=std::max(0.0f,cooldown-dt); return 0;
            }
        }
        state=EnemyState::Attack;
        const float first=std::max(reachTime,cooldown);
        if (dt<=0 || first>dt) { cooldown=std::max(0.0f,cooldown-dt); return 0; }
        const float interval=std::max(.05f,settings.attackInterval);
        const float remaining=dt-first;
        const float attacks=1+std::floor((remaining+1e-6f)/interval);
        cooldown=std::max(0.0f,first+attacks*interval-dt);
        attacksThisUpdate = static_cast<unsigned int>(attacks);
        return attacks*std::max(0.0f,settings.attackDamage);
    }
};
