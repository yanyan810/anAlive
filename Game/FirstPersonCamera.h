#pragma once
#include "StageWorld.h"

// Conservative sphere vs. transformed stage boxes, including player-only gates.
// The radius encloses the near plane even when the first-person camera rolls.
namespace FirstPersonCamera {
inline Vector3 Padding(const StageCollider& c,float radius) {
    return {radius*std::hypot(c.inverse.m[0][0],c.inverse.m[1][0],c.inverse.m[2][0]),
        radius*std::hypot(c.inverse.m[0][1],c.inverse.m[1][1],c.inverse.m[2][1]),
        radius*std::hypot(c.inverse.m[0][2],c.inverse.m[1][2],c.inverse.m[2][2])};
}
inline Vector3 ResolveOverlap(const StageWorld& world,Vector3 position,float radius) {
    for (int iteration=0;iteration<12;++iteration) {
        bool moved=false;
        for (const auto& c:world.colliders) {
            const auto p=StagePoint(position,c.inverse), pad=Padding(c,radius);
            const auto low=c.local.min-pad, high=c.local.max+pad;
            if (p.x<=low.x || p.x>=high.x || p.y<=low.y || p.y>=high.y || p.z<=low.z || p.z>=high.z) continue;
            const float a[]={p.x,p.y,p.z}, l[]={low.x,low.y,low.z}, h[]={high.x,high.y,high.z};
            float nearest=std::numeric_limits<float>::infinity(); Vector3 push{};
            for (int axis=0;axis<3;++axis) for (int side:{-1,1}) {
                const Vector3 n{c.inverse.m[0][axis]*side,c.inverse.m[1][axis]*side,c.inverse.m[2][axis]*side};
                const float length=StageLength(n);
                if (length<1e-8f) continue;
                const float gap=(side<0 ? a[axis]-l[axis] : h[axis]-a[axis])/length;
                if (gap<nearest) { nearest=gap; push=n*((gap+.002f)/length); }
            }
            position+=push; moved=true;
        }
        if (!moved) break;
    }
    return position;
}
inline Vector3 Move(const StageWorld& world,const Vector3& start,const Vector3& wanted,float radius) {
    auto position=ResolveOverlap(world,start,radius);
    auto remaining=wanted-position;
    for (int iteration=0;iteration<8;++iteration) {
        const float length=StageLength(remaining);
        if (length<1e-6f) break;
        const auto direction=remaining*(1/length);
        StageHit nearest{length,{}}; bool blocked=false;
        for (const auto& c:world.colliders) {
            StageHit hit;
            if (StageWorld::CastBox(c,position,direction,nearest.distance,Padding(c,radius),hit) &&
                StageDot(direction,hit.normal)<-1e-6f) { nearest=hit; blocked=true; }
        }
        if (!blocked) { position+=remaining; break; }
        const float travel=std::max(0.0f,nearest.distance-.002f);
        position+=direction*travel; remaining-=direction*travel;
        const float inward=StageDot(remaining,nearest.normal);
        if (inward<0) remaining-=nearest.normal*inward;
    }
    return position;
}
inline bool NearBounds(const Vector3& eye,const AABB& bounds,float clearance) {
    const Vector3 closest{std::clamp(eye.x,bounds.min.x,bounds.max.x),
        std::clamp(eye.y,bounds.min.y,bounds.max.y),std::clamp(eye.z,bounds.min.z,bounds.max.z)};
    return StageLength(eye-closest)<=clearance;
}
}
