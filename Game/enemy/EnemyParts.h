#pragma once
#include "Raycast.h"
#include <array>
#include <algorithm>
#include <limits>
#include <vector>
#include <string>
#include <memory>

enum class EnemyPartType { None, Head, Body, LeftArm, RightArm, LeftLeg, RightLeg };
inline const char* EnemyPartName(EnemyPartType type) {
    switch(type) {
    case EnemyPartType::Head: return "Head";
    case EnemyPartType::Body: return "Body";
    case EnemyPartType::LeftArm: return "LeftArm";
    case EnemyPartType::RightArm: return "RightArm";
    case EnemyPartType::LeftLeg: return "LeftLeg";
    case EnemyPartType::RightLeg: return "RightLeg";
    default: return "None";
    }
}
// Legacy tags remain adapters for old assets/tools; runtime hit identity is partIndex.
enum class EnemyPartRole { Generic, Head, Body, Arm, Leg, Core, Armor };
inline const char* EnemyPartRoleName(EnemyPartRole role) {
    switch(role) {
    case EnemyPartRole::Head: return "Head"; case EnemyPartRole::Body: return "Body";
    case EnemyPartRole::Arm: return "Arm"; case EnemyPartRole::Leg: return "Leg";
    case EnemyPartRole::Core: return "Core"; case EnemyPartRole::Armor: return "Armor";
    default: return "Generic";
    }
}
inline EnemyPartRole LegacyPartRole(EnemyPartType type) {
    switch(type) {
    case EnemyPartType::Head: return EnemyPartRole::Head;
    case EnemyPartType::Body: return EnemyPartRole::Body;
    case EnemyPartType::LeftArm: case EnemyPartType::RightArm: return EnemyPartRole::Arm;
    case EnemyPartType::LeftLeg: case EnemyPartType::RightLeg: return EnemyPartRole::Leg;
    default: return EnemyPartRole::Generic;
    }
}
inline EnemyPartType LegacyRoleType(EnemyPartRole role) {
    switch(role) {
    case EnemyPartRole::Head: return EnemyPartType::Head;
    case EnemyPartRole::Body: return EnemyPartType::Body;
    case EnemyPartRole::Arm: return EnemyPartType::LeftArm;
    case EnemyPartRole::Leg: return EnemyPartType::LeftLeg;
    default: return EnemyPartType::None;
    }
}
inline constexpr size_t kNoEnemyPart=std::numeric_limits<size_t>::max();
struct EnemyPartVertex { Vector3 position{},normal{0,1,0}; Vector2 uv{}; };
struct EnemyPartGeometry {
    std::vector<std::array<EnemyPartVertex,3>> triangles;
    std::vector<std::array<Vector3,3>> faces; // shared with Face Shatter; never copied per enemy
};
inline auto PartitionEnemyChunks(const EnemyPartGeometry& geometry,const AABB& bounds) {
    std::array<std::vector<std::array<EnemyPartVertex,3>>,8> buckets;
    const auto center=(bounds.min+bounds.max)*.5f;
    for (const auto& tri:geometry.triangles) {
        const auto p=(tri[0].position+tri[1].position+tri[2].position)*(1.0f/3);
        const size_t bucket=(p.x>=center.x?1u:0u)|(p.y>=center.y?2u:0u)|(p.z>=center.z?4u:0u);
        buckets[bucket].push_back(tri);
    }
    return buckets;
}
struct EnemyHpGroup { std::string id; float maxHp=1,hp=1; bool deathOnZero=true; };
enum class EnemyPartDamageState { Normal, LightDamage, HeavyDamage, Critical, Destroyed };
inline const char* EnemyPartDamageStateName(EnemyPartDamageState state) {
    switch (state) {
    case EnemyPartDamageState::Normal: return "Normal";
    case EnemyPartDamageState::LightDamage: return "LightDamage";
    case EnemyPartDamageState::HeavyDamage: return "HeavyDamage";
    case EnemyPartDamageState::Critical: return "Critical";
    default: return "Destroyed";
    }
}
inline float EnemyPartMaxHp(EnemyPartType type) {
    switch (type) {
    case EnemyPartType::Head: return 50.0f;
    case EnemyPartType::Body: return 100.0f;
    case EnemyPartType::LeftArm: case EnemyPartType::RightArm: return 60.0f;
    case EnemyPartType::LeftLeg: case EnemyPartType::RightLeg: return 70.0f;
    default: return 0.0f;
    }
}
struct EnemyPart {
    EnemyPartType type = EnemyPartType::None;
    AABB bounds{};
    float flashRemaining = 0.0f;
    float maxHp = EnemyPartMaxHp(type);
    float hp = maxHp;
    std::string name=EnemyPartName(type);
    EnemyPartRole role=LegacyPartRole(type);
    bool usesLocalHp=true,breakable=true;
    bool deathOnZero=(role==EnemyPartRole::Head || role==EnemyPartRole::Body);
    size_t sharedGroup=kNoEnemyPart;
    float sharedDamageRate=1;
    std::shared_ptr<const EnemyPartGeometry> geometry;
    bool detachedOnDeath=false;
    bool Destroyed() const { return detachedOnDeath || (usesLocalHp && breakable && hp<=0); }
    float DamageRate() const { if (!usesLocalHp) return 0; return maxHp > 0 ? std::clamp(1.0f - hp / maxHp, 0.0f, 1.0f) : 1.0f; }
    EnemyPartDamageState DamageState() const {
        if (Destroyed()) return EnemyPartDamageState::Destroyed;
        if (!usesLocalHp) return EnemyPartDamageState::Normal;
        const float rate = DamageRate();
        if (rate >= .75f) return EnemyPartDamageState::Critical;
        if (rate >= .50f) return EnemyPartDamageState::HeavyDamage;
        if (rate >= .25f) return EnemyPartDamageState::LightDamage;
        return EnemyPartDamageState::Normal;
    }
};
// Vector-compatible state container also owns this instance's shared HP.
struct EnemyParts : std::vector<EnemyPart> {
    using std::vector<EnemyPart>::vector;
    std::vector<EnemyHpGroup> hpGroups;
    bool deathProcessed=false;
};
// Latch death independently of HP rules (including invincible/nonbreakable parts).
// The state is copied with debug snapshots and fresh assets start alive.
inline bool BeginEnemyDeath(EnemyParts& parts) {
    if (parts.deathProcessed) return false;
    parts.deathProcessed=true;
    for (auto& part:parts) part.detachedOnDeath=true;
    return true;
}
inline void ApplyEnemyHpMultiplier(EnemyParts& parts, float multiplier) {
    for (auto& part : parts) part.hp = part.maxHp = EnemyPartMaxHp(part.type)*multiplier;
}
inline float DamageEnemyPart(EnemyParts& parts,size_t index,float damage) {
    if (index>=parts.size() || !std::isfinite(damage) || damage<=0) return 0;
    auto& part=parts[index];
    if (part.Destroyed()) return 0;
    float localLost=0,sharedLost=0;
    if (part.usesLocalHp) {
        const float before=part.hp;
        part.hp=std::max(0.0f,before-damage); localLost=before-part.hp;
    }
    if (part.sharedGroup<parts.hpGroups.size()) {
        auto& group=parts.hpGroups[part.sharedGroup];
        const float before=group.hp;
        group.hp=std::max(0.0f,before-damage*part.sharedDamageRate); sharedLost=before-group.hp;
    }
    // Feedback counts the greater loss, not double the same impact in LocalAndShared.
    return std::max(localLost,sharedLost);
}
// Return actual HP lost (overkill and already-destroyed hits are clamped).
inline float DamageEnemyPart(EnemyParts& parts, EnemyPartType type, float damage) {
    if (!std::isfinite(damage) || damage <= 0 || type == EnemyPartType::None) return 0;
    for (size_t i=0;i<parts.size();++i)
        if (parts[i].type==type) return DamageEnemyPart(parts,i,damage);
    return 0;
}
struct EnemyPartHit {
    bool hit = false;
    float distance = 0.0f;
    Vector3 position{};
    EnemyPartType part = EnemyPartType::None;
    size_t partIndex=kNoEnemyPart;
};
inline Vector3 EnemyPartTransformPoint(const Vector3& p, const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
inline EnemyParts MakeEnemyParts(const AABB& model) {
    // Boss bind pose: local Y is up, Z is arm span, X is depth.
    // Left/right are the enemy's own sides (+Z = left), not camera-relative.
    const auto box = [&](float y0,float y1,float z0,float z1) {
        const Vector3 size=model.max-model.min;
        return AABB{{model.min.x,model.min.y+size.y*y0,model.min.z+size.z*z0},
            {model.max.x,model.min.y+size.y*y1,model.min.z+size.z*z1}};
    };
    return {{EnemyPartType::Head,box(.82f,1,.38f,.62f)},
        {EnemyPartType::Body,box(.45f,.82f,.38f,.62f)},
        {EnemyPartType::LeftArm,box(.70f,.86f,.62f,1)},
        {EnemyPartType::RightArm,box(.70f,.86f,0,.38f)},
        {EnemyPartType::LeftLeg,box(0,.45f,.50f,.64f)},
        {EnemyPartType::RightLeg,box(0,.45f,.36f,.50f)}};
}
// The provider returns each part's current render matrix, or nullptr when hidden.
template<class WorldForPart>
inline bool RaycastEnemyPartsTransformed(const EnemyParts& parts, WorldForPart worldForPart,
    const Vector3& origin,const Vector3& direction,float range,EnemyPartHit& hit) {
    hit = {};
    const float length=std::hypot(direction.x,direction.y,direction.z);
    if (!std::isfinite(length)||length<=0||!std::isfinite(range)||range<0) return false;
    const Vector3 unit=direction*(1.0f/length);
    float closest=range;
    for (size_t index=0;index<parts.size();++index) {
        const auto& part=parts[index];
        if (part.Destroyed()) continue;
        const auto* currentWorld=worldForPart(index);
        if (!currentWorld) continue;
        const auto& world=*currentWorld;
        // Reject singular transforms before inversion (including collapsed scale axes).
        const float determinant=world.m[0][0]*(world.m[1][1]*world.m[2][2]-world.m[1][2]*world.m[2][1])
            -world.m[0][1]*(world.m[1][0]*world.m[2][2]-world.m[1][2]*world.m[2][0])
            +world.m[0][2]*(world.m[1][0]*world.m[2][1]-world.m[1][1]*world.m[2][0]);
        if (!std::isfinite(determinant)||std::abs(determinant)<1e-8f) continue;
        const auto inverse=Matrix4x4::Inverse(world);
        const Vector3 localOrigin=EnemyPartTransformPoint(origin,inverse);
        const Vector3 localDirection{unit.x*inverse.m[0][0]+unit.y*inverse.m[1][0]+unit.z*inverse.m[2][0],
            unit.x*inverse.m[0][1]+unit.y*inverse.m[1][1]+unit.z*inverse.m[2][1],
            unit.x*inverse.m[0][2]+unit.y*inverse.m[1][2]+unit.z*inverse.m[2][2]};
        const float factor=std::hypot(localDirection.x,localDirection.y,localDirection.z);
        if (!std::isfinite(factor)||factor<=0) continue;
        float localDistance;
        if (!RaycastAABB(localOrigin,localDirection,part.bounds,closest*factor,localDistance)) continue;
        if (part.geometry) {
            // Two-sided Moller-Trumbore in model space; t remains world distance.
            bool found=false;
            float nearest=closest;
            const auto cross=[](Vector3 a,Vector3 b) { return Vector3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; };
            const auto dot=[](Vector3 a,Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; };
            for (const auto& triangle:part.geometry->faces) {
                const auto edge1=triangle[1]-triangle[0],edge2=triangle[2]-triangle[0];
                const auto p=cross(localDirection,edge2);
                const float det=dot(edge1,p);
                if (std::abs(det)<1e-9f) continue;
                const auto delta=localOrigin-triangle[0];
                const float u=dot(delta,p)/det;
                if (u<0 || u>1) continue;
                const auto q=cross(delta,edge1);
                const float v=dot(localDirection,q)/det;
                if (v<0 || u+v>1) continue;
                const float t=dot(edge2,q)/det;
                if (t>=0 && t<=nearest) { nearest=t; found=true; }
            }
            if (!found) continue;
            localDistance=nearest*factor;
        }
        const float distance=localDistance/factor;
        if (hit.hit && distance>=closest) continue;
        closest=distance;
        hit={true,distance,origin+unit*distance,part.type,index};
    }
    return hit.hit;
}
// Bind-pose/shared-transform adapter for tools and existing callers.
inline bool RaycastEnemyParts(const EnemyParts& parts, const Matrix4x4& world,
    const Vector3& origin,const Vector3& direction,float range,EnemyPartHit& hit) {
    return RaycastEnemyPartsTransformed(parts,[&world](size_t) { return &world; },origin,direction,range,hit);
}
