#pragma once
#include "StageWorld.h"
#include "EnemyProjectile.h"

inline float StepStageProjectile(EnemyProjectile& p,float dt,const Vector3& player,const StageWorld& world) {
    if (!p.active || !std::isfinite(dt) || dt<=0) return 0;
    if (p.type==EnemyProjectileType::Bullet) {
        const float elapsed=std::min(dt,std::max(0.0f,p.lifetime));
        const float speed=StageLength(p.velocity);
        StageHit hit;
        if (speed>0 && world.Raycast(p.position,p.velocity,speed*elapsed,hit,p.radius)) {
            const float damage=hit.distance>0 ? StepEnemyProjectile(p,hit.distance/speed,player) : 0;
            p.active=false; return damage;
        }
        return StepEnemyProjectile(p,dt,player);
    }
    // Subdivide the curved trajectory, sweep each chord; collision stops the bomb and starts its fuse.
    float remaining=std::min(dt,std::max(0.0f,p.lifetime));
    while (remaining>1e-6f && p.active) {
        float step=std::min(remaining,1.0f/120);
        float groundedTime=p.grounded ? step : 0;
        if (!p.grounded) {
            const auto travel=p.velocity*step+Vector3{0,-.5f*p.gravity*step*step,0};
            const float distance=StageLength(travel);
            StageHit hit;
            if (distance>0 && world.Raycast(p.position,travel,distance,hit,p.radius)) {
                const float fraction=std::clamp(hit.distance/distance,0.0f,1.0f);
                p.position+=travel*fraction; p.velocity={}; p.grounded=true;
                groundedTime=step*(1-fraction);
            } else { p.position+=travel; p.velocity.y-=p.gravity*step; }
        }
        p.lifetime-=step; remaining-=step;
        if (p.grounded) {
            p.fuseRemaining-=groundedTime;
            if (p.fuseRemaining<=0) {
                p.active=false;
                return StageLength(player-p.position)<=p.explosionRadius ? p.explosionDamage : 0;
            }
        }
        if (p.lifetime<=1e-6f) p.active=false;
    }
    if (p.lifetime<=1e-6f) p.active=false;
    return 0;
}
inline float EnemyProjectileSystem::Update(float dt,const Vector3& player,const StageWorld& world,Vector3* impactDirection) {
    float damage=0;
    for (auto& p : projectiles_) {
        const float hit=StepStageProjectile(p,dt,player,world);
        damage+=hit;
        if (hit>0 && impactDirection) *impactDirection=p.type==EnemyProjectileType::Bullet ? p.velocity : player-p.position;
    }
    std::erase_if(projectiles_,[](const auto& p) { return !p.active; });
    return damage;
}
inline float UpdateStageProjectiles(EnemyProjectileSystem& system,float dt,const Vector3& player,const StageWorld& world,Vector3* impactDirection=nullptr) {
    return system.Update(dt,player,world,impactDirection);
}
