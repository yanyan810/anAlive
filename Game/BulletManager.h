#pragma once
#include "Bullet.h"
#include "Enemy.h"

struct BulletEnemyImpact {
    size_t enemyIndex = 0;
    EnemyPartType part = EnemyPartType::None;
    EnemyBulletHitResult result;
    std::string partName;
};

class BulletManager {
public:
    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera);
    void Spawn(const WeaponDefinition& weapon, const Matrix4x4& cameraWorld, float adsBlend,
        std::mt19937& random, const StageWorld& world, const std::vector<Enemy*>& enemies,
        const BulletTrace& raycastTarget = {});
    void Update(float dt, const StageWorld& world, const std::vector<Enemy*>& enemies,
        const std::function<void(const BulletEnemyImpact&)>& onImpact,
        const BulletTrace& raycastTarget = {}, const BulletImpact& onTargetImpact = {});
    void Draw();
    void Clear() { simulation_.Clear(); }
    size_t Count() const { return simulation_.Bullets().size(); }
    const BulletSimulation& Capture() const { return simulation_; }
    void Restore(const BulletSimulation& state) { simulation_ = state; }
private:
    void EnsureVisuals(size_t count);
    BulletSimulation simulation_;
    // Reuse renderer slots instead of allocating/deleting GPU buffers on every shot.
    std::vector<std::unique_ptr<Object3d>> visuals_;
    Object3dCommon* common_ = nullptr;
    DirectXCommon* dx_ = nullptr;
    Camera* camera_ = nullptr;
};
