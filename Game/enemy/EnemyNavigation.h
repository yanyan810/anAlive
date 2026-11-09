#pragma once
#include "../StageWorld.h"
#include <queue>
#include <unordered_map>
#include <cstring>
#include <cstdint>

enum class EnemyPathState { None, Direct, Following, Arrived, NoPath, Limited, Unsupported };
enum class EnemyPathFailure { None, StartOccupied, GoalOccupied, HeightDifference, MissingFloor,
    OutsideGrid, NodeBudget, Disconnected, BlockedSegment, ExcessiveDetour, NoCandidate };
// Surface navigation keeps separate height layers at the same XZ grid cell.
struct EnemyNavigation {
    EnemyPathState state=EnemyPathState::None;
    EnemyPathFailure failure=EnemyPathFailure::None;
    std::vector<Vector3> path;
    size_t next=0;
    Vector3 goal{};
    float retry=0;
    unsigned int planAttempts=0;
private:
    struct Geometry {
        const StageWorld* world=nullptr;
        const StageCollider* data=nullptr;
        float margin=0;
        struct Cached { Matrix4x4 matrix{}; AABB local{},bounds{}; bool playerOnly=false; };
        std::vector<Cached> boxes;
        std::unordered_map<uint64_t,std::vector<size_t>> cells;
        std::vector<size_t> large;
        static uint64_t Key(int x,int z) { return (uint64_t{static_cast<uint32_t>(x)}<<32)|static_cast<uint32_t>(z); }
        static int Cell(float n) { return static_cast<int>(std::floor(n/2)); }
        void Refresh(const StageWorld* value,float radius) {
            bool rebuild=world!=value || data!=value->colliders.data() || boxes.size()!=value->colliders.size() || radius>margin;
            world=value; data=value->colliders.data(); margin=std::max(margin,radius);
            if (boxes.size()!=value->colliders.size()) boxes.clear();
            const bool fresh=boxes.empty(); boxes.resize(value->colliders.size());
            for (size_t i=0;i<boxes.size();++i) {
                const auto& c=value->colliders[i]; auto& b=boxes[i];
                if (fresh || rebuild || std::memcmp(&c.world,&b.matrix,sizeof(c.world)) || std::memcmp(&c.local,&b.local,sizeof(c.local)) || c.playerOnly!=b.playerOnly) {
                    const auto bounds=TransformAABB(c.local,c.world);
                    if (bounds.min.x!=b.bounds.min.x || bounds.max.x!=b.bounds.max.x || bounds.min.z!=b.bounds.min.z || bounds.max.z!=b.bounds.max.z || c.playerOnly!=b.playerOnly) rebuild=true;
                    b={c.world,c.local,bounds,c.playerOnly};
                }
            }
            if (!rebuild) return;
            cells.clear(); large.clear();
            for (size_t i=0;i<boxes.size();++i) {
                const auto& b=boxes[i]; if (b.playerOnly) continue;
                const int x0=Cell(b.bounds.min.x-margin),x1=Cell(b.bounds.max.x+margin),z0=Cell(b.bounds.min.z-margin),z1=Cell(b.bounds.max.z+margin);
                if (int64_t{x1-x0+1}*(z1-z0+1)>4096) { large.push_back(i); continue; }
                for (int z=z0;z<=z1;++z) for (int x=x0;x<=x1;++x) cells[Key(x,z)].push_back(i);
            }
        }
        template<class F> bool Each(const Vector3& a,const Vector3& b,F visit) const {
            const int x0=Cell(std::min(a.x,b.x)),x1=Cell(std::max(a.x,b.x)),z0=Cell(std::min(a.z,b.z)),z1=Cell(std::max(a.z,b.z));
            if (int64_t{x1-x0+1}*(z1-z0+1)>128) {
                for (size_t i=0;i<boxes.size();++i) if (!boxes[i].playerOnly && !visit(i)) return false;
                return true;
            }
            for (const auto i:large) if (!visit(i)) return false;
            for (int z=z0;z<=z1;++z) for (int x=x0;x<=x1;++x) {
                const auto found=cells.find(Key(x,z));
                if (found!=cells.end()) for (const auto i:found->second) if (!visit(i)) return false;
            }
            return true;
        }
    };
    static const Geometry& CachedGeometry(const StageWorld* world,float radius) {
        static thread_local Geometry cache;
        cache.Refresh(world,radius); return cache;
    }
    static bool Ground(const Geometry& g,const Vector3& sample,float reference,float& y) {
        constexpr float rise=.35f,drop=.5f;
        const Vector3 origin{sample.x,reference+rise,sample.z};
        bool found=false; y=-std::numeric_limits<float>::infinity();
        g.Each(sample,sample,[&](size_t i) {
            const auto& bounds=g.boxes[i].bounds;
            if (sample.x<bounds.min.x-.001f || sample.x>bounds.max.x+.001f || sample.z<bounds.min.z-.001f || sample.z>bounds.max.z+.001f || bounds.max.y<reference-drop || bounds.min.y>reference+rise) return true;
            const auto& c=g.world->colliders[i]; const auto local=StagePoint(origin,c.inverse);
            // An inside-origin ray is not a supporting top face. Reject it so
            // a tall solid block cannot become a series of upward teleports.
            if (local.x>c.local.min.x+1e-5f && local.x<c.local.max.x-1e-5f && local.y>c.local.min.y+1e-5f && local.y<c.local.max.y-1e-5f && local.z>c.local.min.z+1e-5f && local.z<c.local.max.z-1e-5f) return true;
            StageHit hit;
            if (StageWorld::CastBox(c,origin,{0,-1,0},rise+drop,{},hit) && hit.normal.y>=.7f) { y=std::max(y,origin.y-hit.distance); found=true; }
            return true;
        });
        return found;
    }
    static bool Sweep(const Geometry& g,const Vector3& a,const Vector3& b,float radius,float height) {
        const auto delta=b-a; const float length=StageLength(delta);
        const auto direction=length>1e-6f ? delta*(1/length) : Vector3{1,0,0};
        const float low=std::min(a.y,b.y),high=std::max(a.y,b.y)+height;
        return g.Each(a,b,[&](size_t i) {
            const auto& bounds=g.boxes[i].bounds;
            if (bounds.max.x<std::min(a.x,b.x)-radius || bounds.min.x>std::max(a.x,b.x)+radius || bounds.max.z<std::min(a.z,b.z)-radius || bounds.min.z>std::max(a.z,b.z)+radius || bounds.max.y<=low+.01f || bounds.min.y>=high-.01f) return true;
            const auto& c=g.world->colliders[i];
            // The sole follows walkable support; the upper body still sweeps
            // against that geometry, including overhead floors and ceilings.
            const float bottom=g.world->playerGravity && c.walkable ? std::min(height*.5f,.35f) : 0;
            const float body=height-bottom; float extent[3];
            // Float transforms can put a tangential body a few microns inside a wall.
            // Keep the navigation contact tolerance below the physical sweep stand-off.
            const float bodyRadius=std::max(0.0f,radius-.0001f);
            for (int j=0;j<3;++j) extent[j]=bodyRadius*std::hypot(c.inverse.m[0][j],c.inverse.m[2][j])+body*.5f*std::abs(c.inverse.m[1][j]);
            StageHit hit;
            return !StageWorld::CastBox(c,a+Vector3{0,bottom+body*.5f,0},direction,length,{extent[0],extent[1],extent[2]},hit);
        });
    }
    static bool Trace(const Geometry& g,const Vector3& start,const Vector3& desired,float radius,float height,Vector3& result) {
        if (!g.world->playerGravity) { result=desired; return Sweep(g,start,desired,radius,height); }
        float floor=0;
        if (!Ground(g,start,start.y,floor) || std::abs(floor-start.y)>.08f || !Sweep(g,start,start,radius,height)) return false;
        auto previous=start;
        const float horizontal=std::hypot(desired.x-start.x,desired.z-start.z);
        const int steps=std::max(1,static_cast<int>(std::ceil(horizontal/.18f)));
        for (int i=1;i<=steps;++i) {
            const float t=static_cast<float>(i)/steps;
            Vector3 p{start.x+(desired.x-start.x)*t,previous.y,start.z+(desired.z-start.z)*t};
            if (!Ground(g,p,previous.y,floor)) return false;
            p.y=floor; if (!Sweep(g,previous,p,radius,height)) return false;
            previous=p;
        }
        result=previous; return true;
    }
    static bool ClearWith(const Geometry& g,const Vector3& a,const Vector3& b,float radius,float height) {
        Vector3 projected;
        return Trace(g,a,b,radius,height,projected) && std::abs(projected.y-b.y)<=.25f;
    }
public:
    static bool Supported(const StageWorld* world,const Vector3& p) {
        if (!world || !world->playerGravity) return true;
        float y=0; return Ground(CachedGeometry(world,0),p,p.y,y) && std::abs(y-p.y)<=.25f;
    }
    static bool Clear(const StageWorld* world,const Vector3& a,const Vector3& b,float radius,float height) {
        if (!world) return true;
        return ClearWith(CachedGeometry(world,radius),a,b,radius,height);
    }
    static bool FollowSurface(const StageWorld* world,const Vector3& a,const Vector3& desired,float radius,float height,Vector3& result) {
        if (!world) { result=desired; return true; }
        return Trace(CachedGeometry(world,radius),a,desired,radius,height,result);
    }
    static Vector3 ConstrainMotion(const StageWorld& world,const Vector3& a,const Vector3& desired,float radius,float height) {
        if (!world.playerGravity) return world.Move(a,desired,radius,height);
        const auto clipped=world.Move(a,desired,radius,height,false,true);
        Vector3 projected;
        return FollowSurface(&world,a,clipped,radius,height,projected) ? projected : a;
    }
    bool Plan(const StageWorld* world,const Vector3& start,const Vector3& physicalTarget,float radius,float height) {
        ++planAttempts; failure=EnemyPathFailure::None;
        goal=physicalTarget; path.clear(); next=0; retry=1;
        auto target=physicalTarget;
        if (world && world->playerGravity) {
            float support=0;
            if (!Ground(CachedGeometry(world,radius),target,target.y,support) || std::abs(support-target.y)>.25f) {
                state=EnemyPathState::Unsupported; failure=EnemyPathFailure::MissingFloor; return false;
            }
            // Player support samples can lift feet slightly above the centre of
            // a ramp. Route the enemy onto the actual surface at that XZ.
            target.y=support;
        }
        if (std::abs(start.y-target.y)>.3f && (!world || !world->playerGravity)) { state=EnemyPathState::Unsupported; failure=EnemyPathFailure::HeightDifference; return false; }
        if (!Supported(world,start) || !Supported(world,target)) { state=EnemyPathState::Unsupported; failure=EnemyPathFailure::MissingFloor; return false; }
        if (Clear(world,start,target,radius,height)) { path.push_back(target); state=EnemyPathState::Direct; return true; }
        if (!Clear(world,start,start,radius,height)) { state=EnemyPathState::NoPath; failure=EnemyPathFailure::StartOccupied; return false; }
        if (!Clear(world,target,target,radius,height)) { state=EnemyPathState::NoPath; failure=EnemyPathFailure::GoalOccupied; return false; }
        const float cell=std::max(.75f,radius*2+.1f);
        if (PlanGrid(world,start,target,radius,height,cell,false)) return true;
        if (failure==EnemyPathFailure::Disconnected || failure==EnemyPathFailure::NodeBudget) return PlanGrid(world,start,target,radius,height,cell*.5f,true);
        return false;
    }
    bool PlanGrid(const StageWorld* world,const Vector3& start,const Vector3& target,float radius,float height,float cell,bool fine=false) {
        const bool terrain=world && world->playerGravity,levels=terrain && std::abs(start.y-target.y)>.3f;
        const int side=terrain && (levels || fine) ? (fine ? 193 : 129) : 65,center=side/2;
        const int budget=terrain && (levels || fine) ? (fine ? 24000 : 10000) : 1800;
        const int gx=static_cast<int>(std::round((target.x-start.x)/cell))+center,gz=static_cast<int>(std::round((target.z-start.z)/cell))+center;
        if (gx<0 || gz<0 || gx>=side || gz>=side) { state=EnemyPathState::Limited; failure=EnemyPathFailure::OutsideGrid; return false; }
        const Geometry* geometry=world ? &CachedGeometry(world,radius) : nullptr;
        struct Node { Vector3 point{}; float cost=std::numeric_limits<float>::infinity(); int cell=0,parent=-1; bool closed=false; };
        std::vector<Node> nodes; nodes.reserve(static_cast<size_t>(budget)*2);
        std::unordered_map<int64_t,int> index;
        const auto key=[&](int xy,float y) { return static_cast<int64_t>(std::floor((y+.01f)*2))*side*side+xy; };
        const int source=center*side+center;
        nodes.push_back({start,0,source,-1,false}); index[key(source,start.y)]=0;
        using Entry=std::pair<float,int>; std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
        open.push({StageLength(target-start),0}); int found=-1,visited=0;
        while (!open.empty() && visited<budget) {
            const int id=open.top().second; open.pop(); if (nodes[id].closed) continue;
            nodes[id].closed=true; ++visited;
            const auto p=nodes[id].point; const int xy=nodes[id].cell; const float baseCost=nodes[id].cost;
            if (StageLength(p-target)<=cell*1.5f && (!geometry || ClearWith(*geometry,p,target,radius,height))) { found=id; break; }
            for (int z=-1;z<=1;++z) for (int x=-1;x<=1;++x) {
                if (!x && !z) continue;
                const int nx=xy%side+x,nz=xy/side+z; if (nx<0 || nz<0 || nx>=side || nz>=side) continue;
                const int nextCell=nz*side+nx;
                const Vector3 desired{start.x+(nx-center)*cell,p.y,start.z+(nz-center)*cell}; Vector3 q=desired;
                if (geometry && !Trace(*geometry,p,desired,radius,height,q)) continue;
                const auto k=key(nextCell,q.y); auto existing=index.find(k); int neighbor=0;
                if (existing==index.end()) { neighbor=static_cast<int>(nodes.size()); index[k]=neighbor; nodes.push_back({q,std::numeric_limits<float>::infinity(),nextCell,-1,false}); }
                else neighbor=existing->second;
                if (nodes[neighbor].closed) continue;
                const float cost=baseCost+StageLength(q-p); if (cost>=nodes[neighbor].cost) continue;
                nodes[neighbor].cost=cost; nodes[neighbor].parent=id;
                open.push({cost+StageLength(q-target),neighbor});
            }
        }
        if (found<0) { state=visited>=budget ? EnemyPathState::Limited : EnemyPathState::NoPath; failure=visited>=budget ? EnemyPathFailure::NodeBudget : EnemyPathFailure::Disconnected; return false; }
        path.push_back(target);
        for (int id=found;id!=0;id=nodes[id].parent) path.push_back(nodes[id].point);
        std::reverse(path.begin(),path.end()); state=EnemyPathState::Following; failure=EnemyPathFailure::None; return true;
    }
    bool Move(Vector3& position,Vector3& rotation,const Vector3& target,float speed,float dt,const StageWorld* world,float radius,float height) {
        retry=std::max(0.0f,retry-dt); const bool changed=StageLength(target-goal)>.75f;
        if ((path.empty() || changed) && retry<=0) Plan(world,position,target,radius,height);
        if (next>=path.size()) return false;
        const Geometry* geometry=world ? &CachedGeometry(world,radius) : nullptr;
        float remaining=std::max(0.0f,speed)*dt; bool moved=false;
        while (next<path.size()) {
            if (geometry && !ClearWith(*geometry,position,path[next],radius,height)) { path.clear(); next=0; state=EnemyPathState::NoPath; failure=EnemyPathFailure::BlockedSegment; return moved; }
            const auto delta=path[next]-position; const float length=StageLength(delta);
            if (length<=1e-5f) { ++next; continue; } if (remaining<=0) break;
            rotation={0,std::atan2(delta.x,delta.z)+1.57079632679f,0}; const float travel=std::min(remaining,length);
            const auto desired=position+delta*(travel/length); Vector3 projected=desired;
            if (geometry && !Trace(*geometry,position,desired,radius,height,projected)) { path.clear(); next=0; state=EnemyPathState::NoPath; failure=EnemyPathFailure::BlockedSegment; return moved; }
            remaining-=std::max(travel,StageLength(projected-position)); position=projected; moved=true;
            if (travel>=length || StageLength(position-path[next])<1e-4f) ++next; else break;
        }
        if (next==path.size()) { path.clear(); next=0; retry=0; state=EnemyPathState::Arrived; }
        return moved;
    }
};
