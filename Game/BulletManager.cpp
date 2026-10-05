#include "BulletManager.h"

namespace {
    BulletTrace MakeTrace(const StageWorld& world, const std::vector<Enemy*>& enemies,
        const BulletTrace& raycastTarget) {
        return [&world, &enemies, &raycastTarget](const Vector3& origin, const Vector3& direction, float distance) {
            return TraceBulletPath(origin, direction, distance, world, enemies.size(),
                [&enemies](size_t i, const Vector3& start, const Vector3& unit, float range, EnemyPartHit& hit) {
                    return enemies[i]->Raycast(start, unit, range, hit);
                }, raycastTarget);
        };
    }
}

void BulletManager::Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera) {
    common_ = common; dx_ = dx; camera_ = camera;
    Clear();
    EnsureVisuals(32); // Prepare common firing loads before the first shot.
}

void BulletManager::Spawn(const WeaponDefinition& weapon, const Matrix4x4& cameraWorld,
    float adsBlend, std::mt19937& random, const StageWorld& world,
    const std::vector<Enemy*>& enemies, const BulletTrace& raycastTarget) {
    simulation_.SpawnShot(weapon, cameraWorld, adsBlend, random, MakeTrace(world, enemies, raycastTarget));
}

void BulletManager::Update(float dt, const StageWorld& world,
    const std::vector<Enemy*>& enemies,
    const std::function<void(const BulletEnemyImpact&)>& onImpact,
    const BulletTrace& raycastTarget, const BulletImpact& onTargetImpact) {
    simulation_.Update(dt, MakeTrace(world, enemies, raycastTarget), [&](const Bullet& bullet, const BulletHit& hit) {
        if (hit.targetIndex != std::numeric_limits<size_t>::max()) {
            if (onTargetImpact) onTargetImpact(bullet, hit);
            return;
        }
        if (hit.wall) return;
        auto& enemy = *enemies[hit.enemyIndex];
        auto result = enemy.ApplyBulletDamage(hit.partIndex, bullet.damage, bullet.direction);
        enemy.ShowHitFeedback(hit.partIndex);
        if (onImpact) onImpact({hit.enemyIndex, hit.part, result, enemy.PartName(hit.partIndex)});
    });
}

void BulletManager::EnsureVisuals(size_t count) {
    // 描画オブジェクトは不足分だけ追加し、弾が消えた後も保持して連射時のGPUリソース生成を減らす。
    while (visuals_.size() < count) {
        auto visual = std::make_unique<Object3d>();
        visual->Initialize(common_, dx_);
        visual->SetCamera(camera_);
        visual->SetModel("cube/cube.obj");
        visual->SetTexture("resources/white1x1.png");
        visual->SetEnableLighting(0);
        visual->SetMaterialColor({1,1,0,1});
        visuals_.push_back(std::move(visual));
    }
}

void BulletManager::Draw() {
    const auto& bullets = simulation_.Bullets();
    EnsureVisuals(bullets.size());
    for (size_t i=0; i<bullets.size(); ++i) {
        const auto& bullet = bullets[i];
        auto& visual = *visuals_[i];
        visual.SetTranslate(bullet.position);
        visual.SetRotate({-std::asin(std::clamp(bullet.direction.y,-1.0f,1.0f)),
            std::atan2(bullet.direction.x,bullet.direction.z),0});
        visual.SetScale({.035f,.035f,.12f});
        visual.Update(0);
        visual.Draw();
    }
}
