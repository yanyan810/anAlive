#pragma once
#include "../StageWorld.h"
#include <queue>
#include <unordered_map>
#include <cstring>
#include <cstdint>
#include <optional>

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

    EnemyNavigation()=default;
    // Debug history copies the AI each frame. Keep the route and restart any
    // unfinished search on restore instead of storing its open/closed nodes.
    EnemyNavigation(const EnemyNavigation& other) { *this=other; }
    EnemyNavigation& operator=(const EnemyNavigation& other) {
        if (this==&other) return *this;
        state=other.state; failure=other.failure; path=other.path; next=other.next;
        goal=other.goal; retry=other.search_ ? 0 : other.retry; planAttempts=other.planAttempts;
        search_.reset(); directCheck_=0;
        return *this;
    }
    EnemyNavigation(EnemyNavigation&&)=default;
    EnemyNavigation& operator=(EnemyNavigation&&)=default;

private:
    struct GridSearch {
        struct Node { Vector3 point{}; float cost=0; int cell=0,parent=-1; bool closed=false; };
        using Entry=std::pair<float,int>;
        Vector3 start{},target{};
        float cell=0;
        int side=0,limit=0,visited=0;
        bool fine=false;
        std::vector<Node> nodes;
        std::unordered_map<int64_t,int> index;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;

        int64_t Key(int xy,float y) const { return static_cast<int64_t>(std::floor((y+.01f)*2))*side*side+xy; }
    };
    std::optional<GridSearch> search_;
    float directCheck_=0;

    struct Geometry {
        const StageWorld* world=nullptr;
        const StageCollider* data=nullptr;
        float margin=0;
        struct Cached {
            Matrix4x4 matrix{};
            AABB local{},bounds{};
            Vector3 horizontalProjection{},verticalProjection{};
            bool playerOnly=false;
            int firstX=0,firstZ=0;
        };
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
                    b.matrix=c.world; b.local=c.local; b.bounds=bounds; b.playerOnly=c.playerOnly;
                    const auto& inverse=c.inverse.m;
                    b.horizontalProjection={std::hypot(inverse[0][0],inverse[2][0]),
                        std::hypot(inverse[0][1],inverse[2][1]),std::hypot(inverse[0][2],inverse[2][2])};
                    b.verticalProjection={std::abs(inverse[1][0]),std::abs(inverse[1][1]),std::abs(inverse[1][2])};
                }
            }
            if (!rebuild) return;
            cells.clear(); large.clear();
            for (size_t i=0;i<boxes.size();++i) {
                auto& b=boxes[i]; if (b.playerOnly) continue;
                const int x0=Cell(b.bounds.min.x-margin),x1=Cell(b.bounds.max.x+margin),z0=Cell(b.bounds.min.z-margin),z1=Cell(b.bounds.max.z+margin);
                b.firstX=x0; b.firstZ=z0;
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
                if (found==cells.end()) continue;
                for (const auto i:found->second) {
                    // Visit the collider at the first cell it shares with this query.
                    const auto& box=boxes[i];
                    if (x==std::max(x0,box.firstX) && z==std::max(z0,box.firstZ) && !visit(i)) return false;
                }
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
            const auto& c=g.world->colliders[i];
            if (!c.enabled) return true;
            const auto local=StagePoint(origin,c.inverse);
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
        const Vector3 low{std::min(a.x,b.x)-radius,std::min(a.y,b.y)+.01f,std::min(a.z,b.z)-radius};
        const Vector3 high{std::max(a.x,b.x)+radius,std::max(a.y,b.y)+height-.01f,std::max(a.z,b.z)+radius};
        const float bodyRadius=std::max(0.0f,radius-.0001f);
        return g.Each(a,b,[&](size_t i) {
            const auto& box=g.boxes[i];
            const auto& bounds=box.bounds;
            if (bounds.max.x<low.x || bounds.min.x>high.x || bounds.max.z<low.z || bounds.min.z>high.z ||
                bounds.max.y<=low.y || bounds.min.y>=high.y) return true;
            const auto& c=g.world->colliders[i];
            if (!c.enabled) return true;
            // The sole follows walkable support; the upper body still sweeps
            // against that geometry, including overhead floors and ceilings.
            const float bottom=g.world->playerGravity && c.walkable ? std::min(height*.5f,.35f) : 0;
            const float body=height-bottom;
            // Float transforms can put a tangential body a few microns inside a wall.
            // Keep the navigation contact tolerance below the physical sweep stand-off.
            const auto extent=box.horizontalProjection*bodyRadius+box.verticalProjection*(body*.5f);
            return !StageWorld::IntersectsBox(c,a+Vector3{0,bottom+body*.5f,0},direction,length,extent);
        });
    }
    static bool CanStartTrace(const Geometry& g,const Vector3& start,float radius,float height,float& floor) {
        return !g.world->playerGravity ||
            (Ground(g,start,start.y,floor) && std::abs(floor-start.y)<=.08f && Sweep(g,start,start,radius,height));
    }
    static bool TraceFromStart(const Geometry& g,const Vector3& start,const Vector3& desired,
        float radius,float height,float floor,Vector3& result) {
        if (!g.world->playerGravity) { result=desired; return Sweep(g,start,desired,radius,height); }
        auto previous=start;
        const float horizontal=std::hypot(desired.x-start.x,desired.z-start.z);
        if (horizontal==0 && floor==start.y) { result=start; return true; }
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
    static bool Trace(const Geometry& g,const Vector3& start,const Vector3& desired,float radius,float height,Vector3& result) {
        float floor=0;
        return CanStartTrace(g,start,radius,height,floor) && TraceFromStart(g,start,desired,radius,height,floor,result);
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
    void CancelPlan() { search_.reset(); }
private:
    void BeginPlan(const StageWorld* world,const Vector3& start,const Vector3& physicalTarget,float radius,float height) {
        CancelPlan();
        ++planAttempts; state=EnemyPathState::None; failure=EnemyPathFailure::None;
        goal=physicalTarget; path.clear(); next=0; retry=1;
        auto target=physicalTarget;
        if (world && world->playerGravity) {
            float support=0;
            if (!Ground(CachedGeometry(world,radius),target,target.y,support) || std::abs(support-target.y)>.25f) {
                state=EnemyPathState::Unsupported; failure=EnemyPathFailure::MissingFloor; return;
            }
            // Player support samples can lift feet slightly above the centre of
            // a ramp. Route the enemy onto the actual surface at that XZ.
            target.y=support;
        }
        if (std::abs(start.y-target.y)>.3f && (!world || !world->playerGravity)) { state=EnemyPathState::Unsupported; failure=EnemyPathFailure::HeightDifference; return; }
        if (!Supported(world,start) || !Supported(world,target)) { state=EnemyPathState::Unsupported; failure=EnemyPathFailure::MissingFloor; return; }
        if (Clear(world,start,target,radius,height)) { path.push_back(target); state=EnemyPathState::Direct; return; }
        if (!Clear(world,start,start,radius,height)) { state=EnemyPathState::NoPath; failure=EnemyPathFailure::StartOccupied; return; }
        if (!Clear(world,target,target,radius,height)) { state=EnemyPathState::NoPath; failure=EnemyPathFailure::GoalOccupied; return; }
        BeginGrid(world,start,target,std::max(.75f,radius*2+.1f),false);
    }
    void BeginGrid(const StageWorld* world,const Vector3& start,const Vector3& target,float cell,bool fine) {
        const bool terrain=world && world->playerGravity,levels=terrain && std::abs(start.y-target.y)>.3f;
        const int side=terrain && (levels || fine) ? (fine ? 193 : 129) : 65,center=side/2;
        const int gx=static_cast<int>(std::round((target.x-start.x)/cell))+center,gz=static_cast<int>(std::round((target.z-start.z)/cell))+center;
        if (gx<0 || gz<0 || gx>=side || gz>=side) { state=EnemyPathState::Limited; failure=EnemyPathFailure::OutsideGrid; return; }
        state=EnemyPathState::None; failure=EnemyPathFailure::None;
        auto& search=search_.emplace();
        search.start=start; search.target=target; search.cell=cell; search.side=side; search.fine=fine;
        search.limit=terrain && (levels || fine) ? (fine ? 24000 : 10000) : 1800;
        const int source=center*side+center;
        search.nodes.push_back({start,0,source});
        search.index[search.Key(source,start.y)]=0;
        search.open.push({StageLength(target-start),0});
    }
    void ContinuePlan(const StageWorld* world,float radius,float height,int budget) {
        while (search_ && budget>0) {
            auto& search=*search_;
            AdvanceGrid(world,radius,height,search,budget);
            if (!search.open.empty() && search.visited<search.limit && path.empty()) return;
            const bool refine=path.empty() && !search.fine;
            const auto start=search.start,target=search.target;
            const float fineCell=search.cell*.5f;
            CancelPlan();
            if (refine) BeginGrid(world,start,target,fineCell,true);
            if (!search_) retry=1;
        }
    }
    void AdvanceGrid(const StageWorld* world,float radius,float height,GridSearch& search,int& budget) {
        const Geometry* geometry=world ? &CachedGeometry(world,radius) : nullptr;
        const auto start=search.start,target=search.target;
        const int side=search.side,center=side/2;
        const float cell=search.cell;
        auto& nodes=search.nodes;
        auto& index=search.index;
        auto& open=search.open;
        int found=-1;
        while (!open.empty() && search.visited<search.limit && budget>0) {
            const int id=open.top().second; open.pop(); if (nodes[id].closed) continue;
            nodes[id].closed=true; ++search.visited; --budget;
            const auto p=nodes[id].point; const int xy=nodes[id].cell; const float baseCost=nodes[id].cost;
            float floor=0;
            if (geometry && !CanStartTrace(*geometry,p,radius,height,floor)) continue;
            if (StageLength(p-target)<=cell*1.5f) {
                Vector3 projected;
                if (!geometry || (TraceFromStart(*geometry,p,target,radius,height,floor,projected) &&
                    std::abs(projected.y-target.y)<=.25f)) { found=id; break; }
            }
            for (int z=-1;z<=1;++z) for (int x=-1;x<=1;++x) {
                if (!x && !z) continue;
                const int nx=xy%side+x,nz=xy/side+z; if (nx<0 || nz<0 || nx>=side || nz>=side) continue;
                const int nextCell=nz*side+nx;
                const Vector3 desired{start.x+(nx-center)*cell,p.y,start.z+(nz-center)*cell}; Vector3 q=desired;
                if (geometry && !TraceFromStart(*geometry,p,desired,radius,height,floor,q)) continue;
                const auto k=search.Key(nextCell,q.y); auto existing=index.find(k); int neighbor=0;
                if (existing==index.end()) { neighbor=static_cast<int>(nodes.size()); index[k]=neighbor; nodes.push_back({q,std::numeric_limits<float>::infinity(),nextCell,-1,false}); }
                else neighbor=existing->second;
                if (nodes[neighbor].closed) continue;
                const float cost=baseCost+StageLength(q-p); if (cost>=nodes[neighbor].cost) continue;
                nodes[neighbor].cost=cost; nodes[neighbor].parent=id;
                open.push({cost+StageLength(q-target),neighbor});
            }
        }
        if (found<0) {
            if (open.empty() || search.visited>=search.limit) {
                state=search.visited>=search.limit ? EnemyPathState::Limited : EnemyPathState::NoPath;
                failure=search.visited>=search.limit ? EnemyPathFailure::NodeBudget : EnemyPathFailure::Disconnected;
            }
            return;
        }
        path.push_back(target);
        for (int id=found;id!=0;id=nodes[id].parent) path.push_back(nodes[id].point);
        std::reverse(path.begin(),path.end()); state=EnemyPathState::Following; failure=EnemyPathFailure::None;
    }
public:
    bool Plan(const StageWorld* world,const Vector3& start,const Vector3& target,float radius,float height) {
        BeginPlan(world,start,target,radius,height);
        ContinuePlan(world,radius,height,std::numeric_limits<int>::max());
        return !path.empty();
    }
    bool Move(Vector3& position,Vector3& rotation,const Vector3& target,float speed,float dt,const StageWorld* world,float radius,float height,
        int nodeBudget=std::numeric_limits<int>::max()) {
        retry=std::max(0.0f,retry-dt); const bool changed=StageLength(target-goal)>.75f;
        // Finish the current detour even if its target moves. Restarting every
        // time a player moves would starve a search with a small node budget.
        if (search_ && StageLength(position-search_->start)>.75f) {
            CancelPlan();
            retry=0;
        }
        if (search_) {
            // A door may open while a longer detour is still being searched.
            directCheck_-=dt;
            if (directCheck_<=0) {
                directCheck_=.2f;
                Vector3 projected;
                if ((std::abs(position.y-target.y)<=.3f || (world && world->playerGravity)) &&
                    FollowSurface(world,position,target,radius,height,projected) && std::abs(projected.y-target.y)<=.25f) {
                    CancelPlan();
                    path={projected}; next=0; goal=target;
                    state=EnemyPathState::Direct; failure=EnemyPathFailure::None;
                }
            }
        } else if ((path.empty() || changed) && retry<=0) {
            BeginPlan(world,position,target,radius,height);
            directCheck_=.2f;
        }
        ContinuePlan(world,radius,height,nodeBudget);
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
