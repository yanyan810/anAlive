// Behavior objects are stateless. EnemyAI keeps the value state used by pool
// resets, debug snapshots, and the game-over actor.
struct EnemyAI::Frame {
    Vector3& position;
    Vector3& rotation;
    const Vector3& target;
    Vector3 startPosition;
    const StageWorld* world;
    float dt, radius, height;
    int navigationBudget;
    float speedMultiplier, moveSpeed;
    bool dead;

    void Move(EnemyAI& ai,const Vector3& goal,float speed) const {
        ai.navigation.Move(position,rotation,goal,speed,dt,world,radius,height,navigationBudget);
    }

    bool ReachedLastSeen(const EnemyAI& ai) const {
        return EnemyAI::distanceTo(position,ai.lastSeen)<=ai.settings.senses.arrivalDistance &&
            std::abs(position.y-ai.lastSeen.y)<=.3f;
    }

    float RecordMovement(EnemyAI& ai,float damage) const {
        ai.currentSpeed=dt>0 ? EnemyAI::distanceTo(startPosition,position)/dt : 0;
        return damage;
    }

    float Finish(EnemyAI& ai) const {
        ai.cooldown=std::max(0.0f,ai.cooldown-dt);
        return RecordMovement(ai,0);
    }
};

class EnemyAI::Behavior {
public:
    virtual ~Behavior() = default;

    float Tick(EnemyAI& ai,const Frame& frame,bool recognized) const {
        if (frame.dead) return Update(ai,frame);

        // Recognition and alerts take priority over the current behavior.
        // Once acquired, pursue the live target even when sight is lost.
        if (recognized && !ai.pursuitOrdered) ai.OrderPursuit(frame.target);
        if (ai.pursuitOrdered) return BehaviorFor(EnemyState::Chase).Update(ai,frame);
        if (!ai.hasLastSeen) return BehaviorFor(EnemyState::Idle).Update(ai,frame);

        ai.lostTime+=frame.dt;
        return Update(ai,frame);
    }

    virtual float Update(EnemyAI& ai,const Frame& frame) const = 0;
};

class EnemyAI::IdleState final : public EnemyAI::Behavior {
    float Update(EnemyAI& ai,const Frame& frame) const override {
        if (ai.hasLastSeen) return frame.Finish(ai);
        ai.state=EnemyState::Idle;
        ai.cooldown=std::max(0.0f,ai.cooldown-frame.dt);
        return 0;
    }
};

class EnemyAI::PursuitState final : public EnemyAI::Behavior {
    float Update(EnemyAI& ai,const Frame& frame) const override {
        if (!ai.pursuitOrdered) {
            if (ai.lostTime>=ai.settings.senses.loseSightTime) {
                // Investigate on this frame, retaining the route and retry clock.
                ai.state=EnemyState::Investigate;
                return BehaviorFor(EnemyState::Investigate).Update(ai,frame);
            }
            ai.state=EnemyState::Chase;
            ai.moveTarget=ai.lastSeen;
            if (!frame.ReachedLastSeen(ai)) frame.Move(ai,ai.lastSeen,frame.moveSpeed);
            if (frame.ReachedLastSeen(ai)) ai.lastSeenReached=true;
            return frame.Finish(ai);
        }

        ai.lastSeen=frame.target;
        ai.hasLastSeen=true;
        ai.lostTime=0;
        ai.lastSeenReached=false;
        ai.investigateUnreachable=false;
        ai.investigateBlockedTime=0;
        ai.investigateFailure=EnemyPathState::None;
        ai.investigateFailureReason=EnemyPathFailure::None;
        ai.searchElapsed=0;
        ai.searchFinishing=false;
        ai.searchPointsVisited=0;
        if (ai.state!=EnemyState::Chase && ai.state!=EnemyState::Attack) ai.navigation={};
        ai.state=EnemyState::Chase;
        ai.moveTarget=ai.lastSeen;

        const auto& position=frame.position;
        const auto* world=frame.world;
        // A retained pursuit may navigate through the stage but may not attack
        // without line of sight and a clear body route.
        const bool clearRoute=!ai.occluded && std::abs(position.y-ai.lastSeen.y)<=.35f &&
            EnemyNavigation::Clear(world,position,ai.lastSeen,frame.radius,frame.height);
        if (!clearRoute) {
            ai.rangedRetreating=false;
            frame.Move(ai,ai.moveTarget,frame.moveSpeed);
            return frame.Finish(ai);
        }

        ai.navigation.CancelPlan();
        ai.navigation.path.clear();
        ai.navigation.next=0;
        ai.navigation.state=EnemyPathState::Direct;
        ai.navigation.failure=EnemyPathFailure::None;
        auto desired=position;
        const float damage=ai.Combat(desired,frame.rotation,ai.lastSeen,frame.dt,false,frame.speedMultiplier);
        // Validate the reverse segment too when a ranged enemy retreats.
        Vector3 projected;
        if (!EnemyNavigation::FollowSurface(world,position,desired,frame.radius,frame.height,projected)) {
            ai.navigation.state=EnemyPathState::NoPath;
            ai.navigation.failure=EnemyPathFailure::BlockedSegment;
            ai.rangedRetreating=false;
            ai.attacksThisUpdate=0;
            ai.state=EnemyState::Chase;
            return 0;
        }
        frame.position=projected;
        return frame.RecordMovement(ai,damage);
    }
};

class EnemyAI::InvestigateState final : public EnemyAI::Behavior {
    float Update(EnemyAI& ai,const Frame& frame) const override {
        const auto& senses=ai.settings.senses;
        ai.moveTarget=ai.lastSeen;
        if (!frame.ReachedLastSeen(ai)) {
            frame.Move(ai,ai.lastSeen,frame.moveSpeed*senses.investigateSpeedMultiplier);
        }
        if (frame.ReachedLastSeen(ai)) {
            ai.BeginSearch(frame.position,false);
        } else {
            const bool failed=ai.navigation.state==EnemyPathState::NoPath ||
                ai.navigation.state==EnemyPathState::Limited || ai.navigation.state==EnemyPathState::Unsupported;
            ai.investigateBlockedTime=failed ? ai.investigateBlockedTime+frame.dt : 0;
            if (failed) {
                ai.investigateFailure=ai.navigation.state;
                ai.investigateFailureReason=ai.navigation.failure;
                if (ai.investigateBlockedTime>=senses.investigateUnreachableTime) ai.BeginSearch(frame.position,true);
            }
        }
        return frame.Finish(ai);
    }
};

class EnemyAI::SearchState final : public EnemyAI::Behavior {
    float Update(EnemyAI& ai,const Frame& frame) const override {
        const auto& senses=ai.settings.senses;
        const auto& position=frame.position;
        const float dt=frame.dt, moveSpeed=frame.moveSpeed;
        ai.searchElapsed+=dt; ai.searchRetry=std::max(0.0f,ai.searchRetry-dt);
        const float speed=moveSpeed*senses.searchSpeedMultiplier;
        if (ai.SearchRemaining()>0) ai.searchFinishing=false; // Live duration extension resumes exploration.
        if (!ai.navigation.path.empty() && !ai.searchFinishing &&
            EnemyAI::distanceTo(ai.moveTarget,ai.searchCenter)>senses.searchRadius+.001f) {
            ai.navigation.path.clear(); ai.navigation.next=0; ai.searchRetry=0; // Live radius reduction.
        }
        if (ai.SearchRemaining()<=0 && !ai.searchFinishing) {
            ai.searchFinishing=true;
            // Finish the current safe segment, rather than abruptly stopping
            // mid-stride or spending minutes completing a long detour.
            if (ai.navigation.next<ai.navigation.path.size()) {
                ai.navigation.path.resize(ai.navigation.next+1); ai.moveTarget=ai.navigation.path.back(); ai.navigation.goal=ai.moveTarget;
            }
        }
        if (ai.searchFinishing && (ai.navigation.path.empty() || speed<=0)) ai.BeginWander(position);
        else {
            if (ai.navigation.path.empty() && ai.searchRetry<=0) {
                if (!ai.PickSearchGoal(position,frame.world,frame.radius,frame.height,frame.moveSpeed)) ai.searchRetry=.5f;
            }
            if (!ai.navigation.path.empty()) {
                frame.Move(ai,ai.moveTarget,speed);
                if (ai.navigation.path.empty()) {
                    if (ai.navigation.state==EnemyPathState::Arrived) {
                        ++ai.searchPointsVisited;
                        if (ai.checkedSearchCount==ai.checkedSearchPoints.size()) {
                            for (size_t i=1;i<ai.checkedSearchCount;++i) ai.checkedSearchPoints[i-1]=ai.checkedSearchPoints[i];
                            --ai.checkedSearchCount;
                        }
                        ai.checkedSearchPoints[ai.checkedSearchCount++]=position;
                    }
                    else ai.searchRetry=.5f;
                    if (ai.searchFinishing) ai.BeginWander(position);
                }
            }
        }
        return frame.Finish(ai);
    }
};

class EnemyAI::WanderState final : public EnemyAI::Behavior {
    float Update(EnemyAI& ai,const Frame& frame) const override {
        const auto& senses=ai.settings.senses;
        const auto& position=frame.position;
        const float dt=frame.dt;
        if (ai.wanderWait>0) ai.wanderWait=std::max(0.0f,ai.wanderWait-dt);
        else if (ai.navigation.path.empty()) {
            bool planned=false;
            for (int attempt=0;attempt<8 && !planned;++attempt) {
                const float angle=ai.RandomUnit()*6.28318530718f;
                const float range=std::sqrt(ai.RandomUnit())*senses.wanderRadius;
                const auto center=ai.wanderCenter;
                const auto candidate=center+Vector3{std::sin(angle)*range,0,std::cos(angle)*range};
                if (EnemyAI::distanceTo(position,candidate)<=senses.arrivalDistance) continue;
                planned=ai.navigation.Plan(frame.world,position,candidate,frame.radius,frame.height);
                if (planned) ai.moveTarget=candidate;
            }
            if (!planned) ai.wanderWait=std::max(.5f,senses.wanderWaitTime);
        } else {
            frame.Move(ai,ai.moveTarget,frame.moveSpeed*senses.wanderSpeedMultiplier);
            if (ai.navigation.path.empty()) ai.wanderWait=std::max(.1f,senses.wanderWaitTime);
        }
        return frame.Finish(ai);
    }
};

class EnemyAI::DeadState final : public EnemyAI::Behavior {
    float Update(EnemyAI& ai,const Frame& frame) const override {
        if (!frame.dead) return frame.Finish(ai);
        ai.navigation.CancelPlan();
        ai.state=EnemyState::Dead;
        return 0;
    }
};

inline const EnemyAI::Behavior& EnemyAI::BehaviorFor(EnemyState phase) {
    static const IdleState idle;
    static const PursuitState pursuit;
    static const InvestigateState investigate;
    static const SearchState search;
    static const WanderState wander;
    static const DeadState dead;
    switch (phase) {
    case EnemyState::Chase:
    case EnemyState::Attack: return pursuit; // Combat reports Attack while staying in pursuit.
    case EnemyState::Investigate: return investigate;
    case EnemyState::Search: return search;
    case EnemyState::Wander: return wander;
    case EnemyState::Dead: return dead;
    case EnemyState::Idle: return idle;
    }
    return idle;
}

inline bool EnemyAI::Observe(const Frame& frame,float playerEyeHeight) {
    const auto& senses=settings.senses;
    const auto& position=frame.position;
    const auto& rotation=frame.rotation;
    const auto& target=frame.target;
    const auto* world=frame.world;
    const float dt=frame.dt, height=frame.height;
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
    return visible && (hasLastSeen || detection>=senses.detectionTime);
}

inline float EnemyAI::Update(Vector3& position,Vector3& rotation,const Vector3& target,float dt,bool dead,
    const StageWorld* world,float radius,float height,float playerEyeHeight,int navigationBudget,float speedMultiplier) {
    attacksThisUpdate=0;
    currentSpeed=0;
    if (paused && !dead) return 0;

    dt=std::isfinite(dt) ? std::max(0.0f,dt) : 0;
    const Frame frame{position,rotation,target,position,world,dt,radius,height,navigationBudget,
        speedMultiplier,settings.moveSpeed*speedMultiplier,dead};
    const bool recognized=!dead && Observe(frame,playerEyeHeight);
    return BehaviorFor(dead ? EnemyState::Dead : state).Tick(*this,frame,recognized);
}
