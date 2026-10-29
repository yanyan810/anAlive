#pragma once
#include "EnemyDefinition.h"
#include "Vector3.h"
#include <algorithm>
#include <vector>

enum class EnemyProjectileType { Bullet, Bomb };
struct EnemyProjectile {
    EnemyProjectileType type=EnemyProjectileType::Bullet;
    Vector3 position{}, velocity{};
    float radius=.2f, damage=0, lifetime=5;
    float fuseRemaining=0, explosionRadius=0, explosionDamage=0, gravity=9.8f;
    bool active=true, grounded=false;
};
inline float EnemyVectorLength(const Vector3& v) { return std::hypot(v.x,v.y,v.z); }
inline EnemyProjectile MakeEnemyProjectile(const EnemyDefinition& d, const Vector3& origin, const Vector3& playerFeet) {
    EnemyProjectile p;
    p.position=origin; p.radius=d.projectileRadius; p.damage=d.attackDamage; p.lifetime=d.projectileLifetime;
    const auto delta=playerFeet+Vector3{0,1,0}-origin;
    const float distance=EnemyVectorLength(delta);
    p.velocity=distance>1e-6f ? delta*(d.projectileSpeed/distance) : Vector3{0,0,d.projectileSpeed};
    if (d.type==EnemyType::Bomber) {
        p.type=EnemyProjectileType::Bomb; p.damage=0;
        p.fuseRemaining=d.fuseTime; p.explosionRadius=d.explosionRadius; p.explosionDamage=d.explosionDamage; p.gravity=d.bombGravity;
        // Choose a launch elevation which lands at the target's current XZ position.
        const float horizontal=std::hypot(delta.x,delta.z);
        const float flight=std::max(.2f,horizontal/d.projectileSpeed);
        p.velocity={delta.x/flight,(p.radius-origin.y)/flight+.5f*p.gravity*flight,delta.z/flight};
    }
    return p;
}
// Player body is a sphere at feet + (0,1,0). Sweep bullets to prevent tunnelling.
// Returns damage once; inactive projectiles never deal damage again.
inline float StepEnemyProjectile(EnemyProjectile& p, float dt, const Vector3& playerFeet) {
    if (!p.active || !std::isfinite(dt) || dt<=0) return 0;
    const float elapsed=std::min(dt,std::max(0.0f,p.lifetime));
    p.lifetime-=elapsed;
    float damage=0;
    if (p.type==EnemyProjectileType::Bullet) {
        const Vector3 travel=p.velocity*elapsed;
        const Vector3 offset=playerFeet+Vector3{0,1,0}-p.position;
        const float lengthSquared=travel.x*travel.x+travel.y*travel.y+travel.z*travel.z;
        const float fraction=lengthSquared>0 ? std::clamp((offset.x*travel.x+offset.y*travel.y+offset.z*travel.z)/lengthSquared,0.0f,1.0f) : 0;
        if (EnemyVectorLength(offset-travel*fraction)<=p.radius+.65f) { damage=p.damage; p.active=false; }
        p.position+=travel;
    } else {
        float groundedTime=elapsed;
        if (!p.grounded) {
            const float height=std::max(0.0f,p.position.y-p.radius);
            const float hitTime=(p.velocity.y+std::sqrt(p.velocity.y*p.velocity.y+2*p.gravity*height))/p.gravity;
            const float flight=std::min(elapsed,hitTime);
            p.position+=p.velocity*flight+Vector3{0,-.5f*p.gravity*flight*flight,0};
            p.velocity.y-=p.gravity*flight;
            groundedTime=elapsed-flight;
            if (hitTime<=elapsed) { p.position.y=p.radius; p.velocity={}; p.grounded=true; }
        }
        if (p.grounded) {
            p.fuseRemaining-=groundedTime;
            if (p.fuseRemaining<=0) {
                if (EnemyVectorLength(playerFeet-p.position)<=p.explosionRadius) damage=p.explosionDamage;
                p.active=false;
            }
        }
    }
    if (p.lifetime<=0) p.active=false;
    return damage;
}
class StageWorld;
class EnemyProjectileSystem {
public:
    const std::vector<EnemyProjectile>& Projectiles() const { return projectiles_; }
    bool Spawn(const EnemyProjectile& projectile) {
        const auto finiteVector=[](const Vector3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (!projectile.active || !finiteVector(projectile.position) || !finiteVector(projectile.velocity) ||
            !std::isfinite(projectile.radius) || projectile.radius <= 0 ||
            !std::isfinite(projectile.damage) || projectile.damage < 0 ||
            !std::isfinite(projectile.lifetime) || projectile.lifetime <= 0 ||
            (projectile.type != EnemyProjectileType::Bullet && projectile.type != EnemyProjectileType::Bomb)) return false;
        if (projectile.type == EnemyProjectileType::Bomb &&
            (!std::isfinite(projectile.gravity) || projectile.gravity <= 0 ||
             !std::isfinite(projectile.fuseRemaining) || projectile.fuseRemaining < 0 ||
             !std::isfinite(projectile.explosionRadius) || projectile.explosionRadius < 0 ||
             !std::isfinite(projectile.explosionDamage) || projectile.explosionDamage < 0)) return false;
        projectiles_.push_back(projectile);
        return true;
    }
    void Clear() { projectiles_.clear(); }
    float Update(float dt, const Vector3& player, const StageWorld& world, Vector3* impactDirection = nullptr);
    float Update(float dt,const Vector3& playerFeet) {
        float damage=0;
        for (auto& p : projectiles_) damage+=StepEnemyProjectile(p,dt,playerFeet);
        std::erase_if(projectiles_,[](const auto& p) { return !p.active; });
        return damage;
    }
private:
    std::vector<EnemyProjectile> projectiles_;
};
