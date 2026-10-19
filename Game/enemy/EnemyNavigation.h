#pragma once
#include "../StageWorld.h"
#include <queue>

enum class EnemyPathState { None, Direct, Following, Arrived, NoPath, Limited, Unsupported };
enum class EnemyPathFailure { None, StartOccupied, GoalOccupied, HeightDifference, MissingFloor,
    OutsideGrid, NodeBudget, Disconnected, BlockedSegment, ExcessiveDetour, NoCandidate };
// Bounded, flat XZ navigation. No floor changes, stairs, jumps or navmesh baking.
// Every segment uses the same conservative cylinder/OBB sweep as StageWorld::Move.
struct EnemyNavigation {
    EnemyPathState state=EnemyPathState::None;
    EnemyPathFailure failure=EnemyPathFailure::None;
    std::vector<Vector3> path;
    size_t next=0;
    Vector3 goal{};
    float retry=0;
    unsigned int planAttempts=0;
    static bool Supported(const StageWorld* world,const Vector3& p) {
        if (!world || !world->playerGravity) return true;
        StageHit hit;
        return world->Raycast(p+Vector3{0,.2f,0},{0,-1,0},.45f,hit) && hit.normal.y>.7f;
    }
    static bool Clear(const StageWorld* world,const Vector3& a,const Vector3& b,float radius,float height) {
        if (!world) return true;
        const auto delta=b-a; const float length=StageLength(delta);
        const auto direction=length>1e-6f ? delta*(1/length) : Vector3{1,0,0};
        for (const auto& c:world->colliders) {
            if (c.playerOnly) continue;
            const auto bounds=TransformAABB(c.local,c.world);
            if (bounds.max.y<=a.y+.01f || bounds.min.y>=a.y+height-.01f) continue;
            float extent[3];
            for (int i=0;i<3;++i) extent[i]=(radius+.015f)*std::hypot(c.inverse.m[0][i],c.inverse.m[2][i])+
                height*.5f*std::abs(c.inverse.m[1][i]);
            StageHit hit;
            if (StageWorld::CastBox(c,a+Vector3{0,height*.5f,0},direction,length,{extent[0],extent[1],extent[2]},hit)) return false;
        }
        const int samples=std::max(1,static_cast<int>(std::ceil(length/.35f)));
        for (int i=0;i<=samples;++i) if (!Supported(world,a+delta*(static_cast<float>(i)/samples))) return false;
        return true;
    }
    bool Plan(const StageWorld* world,const Vector3& start,const Vector3& target,float radius,float height) {
        ++planAttempts;
        failure=EnemyPathFailure::None;
        goal=target; path.clear(); next=0; retry=1;
        if (std::abs(start.y-target.y)>.3f) {
            state=EnemyPathState::Unsupported; failure=EnemyPathFailure::HeightDifference; return false;
        }
        if (!Supported(world,start) || !Supported(world,target)) {
            state=EnemyPathState::Unsupported; failure=EnemyPathFailure::MissingFloor; return false;
        }
        if (Clear(world,start,target,radius,height)) { path.push_back(target); state=EnemyPathState::Direct; return true; }
        if (!Clear(world,start,start,radius,height)) {
            state=EnemyPathState::NoPath; failure=EnemyPathFailure::StartOccupied; return false;
        }
        if (!Clear(world,target,target,radius,height)) {
            state=EnemyPathState::NoPath; failure=EnemyPathFailure::GoalOccupied; return false;
        }
        constexpr int side=65, center=side/2, total=side*side, budget=1800;
        const float cell=std::max(.75f,radius*2+.1f);
        const int gx=static_cast<int>(std::round((target.x-start.x)/cell))+center;
        const int gz=static_cast<int>(std::round((target.z-start.z)/cell))+center;
        if (gx<1 || gz<1 || gx>=side-1 || gz>=side-1) { state=EnemyPathState::Limited; failure=EnemyPathFailure::OutsideGrid; return false; }
        const auto point=[&](int id) { return start+Vector3{static_cast<float>(id%side-center)*cell,0,static_cast<float>(id/side-center)*cell}; };
        struct Node { float cost=std::numeric_limits<float>::infinity(); int parent=-1; bool closed=false; };
        std::vector<Node> nodes(total);
        using Entry=std::pair<float,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
        const int source=center*side+center;
        nodes[source].cost=0; open.push({0.0f,source}); int found=-1,visited=0;
        while (!open.empty() && visited<budget) {
            const int id=open.top().second; open.pop();
            if (nodes[id].closed) continue;
            nodes[id].closed=true; ++visited;
            const auto p=point(id);
            if (StageLength(p-target)<=cell*1.5f && Clear(world,p,target,radius,height)) { found=id; break; }
            for (int z=-1;z<=1;++z) for (int x=-1;x<=1;++x) {
                if (!x && !z) continue;
                const int nx=id%side+x,nz=id/side+z;
                if (nx<0 || nz<0 || nx>=side || nz>=side) continue;
                const int neighbor=nz*side+nx;
                if (nodes[neighbor].closed) continue;
                const auto q=point(neighbor);
                const float cost=nodes[id].cost+StageLength(q-p);
                if (cost>=nodes[neighbor].cost || !Clear(world,p,q,radius,height)) continue;
                nodes[neighbor].cost=cost; nodes[neighbor].parent=id;
                open.push({cost+StageLength(q-target),neighbor});
            }
        }
        if (found<0) {
            state=visited>=budget ? EnemyPathState::Limited : EnemyPathState::NoPath;
            failure=visited>=budget ? EnemyPathFailure::NodeBudget : EnemyPathFailure::Disconnected; return false;
        }
        path.push_back(target);
        for (int id=found;id!=source;id=nodes[id].parent) path.push_back(point(id));
        std::reverse(path.begin(),path.end()); state=EnemyPathState::Following; return true;
    }
    bool Move(Vector3& position,Vector3& rotation,const Vector3& target,float speed,float dt,
        const StageWorld* world,float radius,float height) {
        retry=std::max(0.0f,retry-dt);
        const bool changed=StageLength(target-goal)>.75f;
        if ((path.empty() || changed) && retry<=0) Plan(world,position,target,radius,height);
        if (next>=path.size()) return false;
        float remaining=std::max(0.0f,speed)*dt; bool moved=false;
        // Carry unused frame movement over to the next waypoint. Dense A* paths
        // must not slow investigation compared with a direct chase.
        while (next<path.size()) {
            // A shutter may have closed since the route was planned.
            if (!Clear(world,position,path[next],radius,height)) {
                path.clear(); next=0; state=EnemyPathState::NoPath; failure=EnemyPathFailure::BlockedSegment; return moved;
            }
            const auto delta=path[next]-position; const float length=StageLength(delta);
            if (length<=1e-6f) { ++next; continue; }
            if (remaining<=0) break;
            rotation={0,std::atan2(delta.x,delta.z)+1.57079632679f,0};
            const float travel=std::min(remaining,length);
            position+=delta*(travel/length); remaining-=travel; moved=true;
            if (travel>=length) ++next; else break;
        }
        if (next==path.size()) { path.clear(); next=0; retry=0; state=EnemyPathState::Arrived; }
        return moved;
    }
};
