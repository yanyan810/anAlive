#include "Bullet.h"

namespace {
    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
    }
    Vector3 Unit(const Vector3& value, const Vector3& fallback) {
        const float length = StageLength(value);
        return length > 1e-6f && std::isfinite(length) ? value*(1/length) : fallback;
    }
}

std::optional<BulletHit> TraceBulletPath(const Vector3& origin, const Vector3& direction,
    float distance, const StageWorld& world, size_t enemyCount, const BulletEnemyRaycast& raycastEnemy,
    const BulletTrace& raycastTarget) {
    std::optional<BulletHit> closest;
    // 壁までに探索範囲を絞り、壁と同距離の敵より壁を優先して遮蔽物越しの命中を防ぐ。
    StageHit wall;
    if (world.Raycast(origin, direction, distance, wall)) {
        distance = wall.distance;
        closest = BulletHit{distance, origin+direction*distance, EnemyPartType::None, 0, true};
    }
    for (size_t i=0; i<enemyCount; ++i) {
        EnemyPartHit part;
        if (raycastEnemy(i, origin, direction, distance, part) &&
            (!closest || part.distance < distance)) {
            distance = part.distance;
            closest = BulletHit{distance, origin+direction*distance, part.part, i, false, part.partIndex};
        }
    }
    if (raycastTarget) {
        const auto target = raycastTarget(origin, direction, distance);
        if (target && (!closest || target->distance < distance)) closest = target;
    }
    return closest;
}

bool BulletSimulation::Spawn(const Bullet& source) {
    if (bullets_.size() >= kMaxBullets || !source.active ||
        !std::isfinite(source.speed) || source.speed <= 0 ||
        !std::isfinite(source.remainingLife) || source.remainingLife <= 0 ||
        !std::isfinite(source.remainingRange) || source.remainingRange <= 0 ||
        !std::isfinite(source.damage) || source.damage < 0 ||
        !std::isfinite(StageLength(source.position))) return false;
    const float length = StageLength(source.direction);
    if (!std::isfinite(length) || length <= 1e-6f) return false;
    Bullet bullet = source;
    // 方向ベクトルの長さが弾速や射程に影響しないよう、生成時に単位化する。
    bullet.direction *= 1/length;
    bullet.previousPosition = bullet.position;
    bullets_.push_back(bullet);
    return true;
}

size_t BulletSimulation::SpawnShot(const WeaponDefinition& weapon, const Matrix4x4& cameraWorld,
    float adsBlend, std::mt19937& random, const BulletTrace& trace) {
    // 上限付近で散弾の一部だけが生成されないよう、1発分の空きを先に確認する。
    if (weapon.pelletCount <= 0 || weapon.pelletCount > 64 ||
        bullets_.size()+static_cast<size_t>(weapon.pelletCount) > kMaxBullets) return 0;
    const Vector3 eye{cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2]};
    const Vector3 forward{cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2]};
    const Vector3 right{cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2]};
    const Vector3 up{cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2]};
    // Temporary muzzle until a first-person weapon model supplies a socket transform.
    constexpr float muzzleRight=.20f, muzzleDown=.15f, muzzleForward=.50f;
    Vector3 muzzle = eye+right*muzzleRight-up*muzzleDown+forward*muzzleForward;
    const auto offset = muzzle-eye;
    const float offsetLength = StageLength(offset);
    const auto offsetDirection = offset*(1/offsetLength);
    // Never spawn through a thin wall or an enemy touching the muzzle.
    if (const auto blocked = trace(eye, offsetDirection, offsetLength))
        muzzle = eye+offsetDirection*std::max(0.0f, blocked->distance-.001f);
    // 視点と銃口の位置の差を補正し、銃口から照準先へ飛ばす。ここでは照準だけを求め、ダメージは移動時に判定する。
    const auto aimedHit = trace(eye, forward, weapon.range);
    const auto aim = eye+forward*(aimedHit ? aimedHit->distance : weapon.range);
    const auto aimForward = Unit(aim-muzzle, forward);
    const auto aimRight = Unit(Cross(up, aimForward), right);
    const auto aimUp = Unit(Cross(aimForward, aimRight), up);
    const float spread = weapon.hipSpreadDegrees+
        (weapon.adsSpreadDegrees-weapon.hipSpreadDegrees)*std::clamp(adsBlend, 0.0f, 1.0f);
    size_t spawned = 0;
    for (int pellet=0; pellet<weapon.pelletCount; ++pellet) {
        Bullet bullet;
        bullet.position = muzzle;
        bullet.direction = WeaponPelletDirection(aimForward, aimRight, aimUp, spread, random);
        bullet.speed = weapon.bulletSpeed;
        bullet.remainingLife = weapon.bulletLifeTime;
        bullet.remainingRange = weapon.range;
        bullet.damage = weapon.damage;
        spawned += Spawn(bullet) ? 1 : 0;
    }
    return spawned;
}

void BulletSimulation::Update(float dt, const BulletTrace& trace, const BulletImpact& impact) {
    if (!std::isfinite(dt) || dt <= 0) return;
    for (auto& bullet : bullets_) {
        if (!bullet.active) continue;
        // 寿命・射程を超えて進まないよう、最終フレームでは移動区間を短くする。
        const float elapsed = std::min(dt, bullet.remainingLife);
        const float distance = std::min(bullet.speed*elapsed, bullet.remainingRange);
        bullet.previousPosition = bullet.position;
        bullet.position += bullet.direction*distance;
        bullet.remainingLife = std::max(0.0f, bullet.remainingLife-elapsed);
        bullet.remainingRange = std::max(0.0f, bullet.remainingRange-distance);
        // 移動先の点だけでなく区間全体を調べ、高速な弾が薄い壁や敵をすり抜けるのを防ぐ。
        // Trace even the final partial interval before expiring the bullet.
        if (const auto hit = trace(bullet.previousPosition, bullet.direction, distance)) {
            bullet.position = hit->position;
            bullet.active = false;
            impact(bullet, *hit);
        } else if (bullet.remainingLife <= 0 || bullet.remainingRange <= 0) bullet.active = false;
    }
    std::erase_if(bullets_, [](const Bullet& bullet) { return !bullet.active; });
}
