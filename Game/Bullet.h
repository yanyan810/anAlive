#pragma once
#include "WeaponSystem.h"
#include "StageWorld.h"
#include "EnemyParts.h"
#include <functional>

class Bullet {
public:
    Vector3 position{}, previousPosition{}, direction{0,0,1};
    float speed = 120, damage = 0, remainingLife = 3, remainingRange = 100;
    bool active = true;
};

struct BulletHit {
    float distance = 0;
    Vector3 position{};
    EnemyPartType part = EnemyPartType::None;
    size_t enemyIndex = 0;
    bool wall = true;
    size_t partIndex=kNoEnemyPart;
    size_t targetIndex=std::numeric_limits<size_t>::max(); // Optional scene-owned shootable.
};
using BulletTrace = std::function<std::optional<BulletHit>(const Vector3&, const Vector3&, float)>;
using BulletImpact = std::function<void(const Bullet&, const BulletHit&)>;
using BulletEnemyRaycast = std::function<bool(size_t, const Vector3&, const Vector3&, float, EnemyPartHit&)>;

// Shared by aiming and movement sweeps. Walls win equal-distance ties.
std::optional<BulletHit> TraceBulletPath(const Vector3& origin, const Vector3& direction,
    float distance, const StageWorld& world, size_t enemyCount, const BulletEnemyRaycast& raycastEnemy,
    const BulletTrace& raycastTarget = {});

// CPU-only simulation; rendering and game-specific impact reactions live in BulletManager.
// Copying this state also supports Debug timeline rewind without copying GPU resources.
class BulletSimulation {
public:
    static constexpr size_t kMaxBullets = 1024;
    bool Spawn(const Bullet& bullet);
    size_t SpawnShot(const WeaponDefinition& weapon, const Matrix4x4& cameraWorld,
        float adsBlend, std::mt19937& random, const BulletTrace& trace);
    void Update(float dt, const BulletTrace& trace, const BulletImpact& impact);
    void Clear() { bullets_.clear(); }
    const std::vector<Bullet>& Bullets() const { return bullets_; }
private:
    std::vector<Bullet> bullets_;
};
