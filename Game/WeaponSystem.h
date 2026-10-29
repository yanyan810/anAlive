#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

enum class WeaponFireMode {
    SemiAuto,
    FullAuto,
    Burst
};

inline const char* WeaponFireModeName(WeaponFireMode mode) {
    switch (mode) {
    case WeaponFireMode::SemiAuto:
        return "SemiAuto";

    case WeaponFireMode::FullAuto:
        return "FullAuto";
    case WeaponFireMode::Burst:
        return "Burst";
    }

    return "Unknown";
}

inline bool WeaponWantsFire(
    WeaponFireMode mode,
    bool trigger,
    bool held) {

    return mode == WeaponFireMode::FullAuto
        ? held
        : trigger;
}

enum class WeaponReloadMode { Magazine, PerRound };
enum class WeaponReloadState { None, Starting, InsertingRound, Finishing };
inline const char* WeaponReloadModeName(WeaponReloadMode mode) {
    return mode == WeaponReloadMode::Magazine ? "Magazine" : "PerRound";
}
inline const char* WeaponReloadStateName(WeaponReloadState state) {
    switch (state) {
    case WeaponReloadState::Starting: return "Starting";
    case WeaponReloadState::InsertingRound: return "InsertingRound";
    case WeaponReloadState::Finishing: return "Finishing";
    default: return "None";
    }
}

enum class WeaponSlot { Main, Sub };
inline const char* WeaponSlotName(WeaponSlot slot) {
    return slot == WeaponSlot::Main ? "Main" : "Sub";
}

struct WeaponDefinition {
    std::string id;
    std::string displayName;
    // Typeは種類、Slotは装備分類。レアリティと抽選重みも性能から独立させる。
    std::string type = "Unknown";
    WeaponSlot slot = WeaponSlot::Main;
    int rarity = 1;
    double spawnWeight = 1;

    WeaponFireMode fireMode = WeaponFireMode::SemiAuto;
    int burstCount = 3;
    float burstInterval = .07f;
    int ammoPerShot = 1;

    float damage = 0;
    float fireInterval = 0;
    float range = 0;
    float bulletSpeed = 120.0f;
    float bulletLifeTime = 3.0f;

    int magazineSize = 0;
    int reserveAmmo = 0;
    int maxReserveAmmo = 0;

    float reloadTime = 0;
    WeaponReloadMode reloadMode = WeaponReloadMode::Magazine;
    float reloadStartTime = .25f;
    float reloadPerRoundTime = .55f;
    float reloadEndTime = .30f;
    bool reloadCanInterrupt = true;

    int pelletCount = 1;

    // 通常射撃とADS射撃の拡散
    float hipSpreadDegrees = 0.0f;
    float adsSpreadDegrees = 0.0f;

    // ADS
    float adsFovDegrees = 60.0f;
    float adsTransitionTime = 0.15f;
    float adsSensitivityMultiplier = 1.0f;

    Vector3 pickupScale{ .3f, .15f, .15f };
    Vector3 pickupColor{ 1, 1, 1 };
};

// Each player owns mutable ammo/timers; definitions are copied only on equip.
class WeaponRuntime {
public:
    bool Equip(const WeaponDefinition& definition) {
        // Definitions are public data. Validate again at the runtime boundary because
        // editor/tests can construct them without going through the JSON loader.
        const auto duration = [](float value) { return std::isfinite(value) && value >= 0; };
        if (definition.id.empty() || definition.magazineSize <= 0 || definition.reserveAmmo < 0 ||
            definition.maxReserveAmmo < 0 || definition.ammoPerShot <= 0 || definition.burstCount <= 0 || definition.burstCount > 64 ||
            !duration(definition.fireInterval) || !duration(definition.burstInterval) ||
            !duration(definition.reloadTime) || !duration(definition.reloadStartTime) ||
            !duration(definition.reloadPerRoundTime) || !duration(definition.reloadEndTime) ||
            definition.fireMode < WeaponFireMode::SemiAuto || definition.fireMode > WeaponFireMode::Burst ||
            definition.reloadMode < WeaponReloadMode::Magazine || definition.reloadMode > WeaponReloadMode::PerRound)
            return false;
        definition_ = definition;
        magazine_ = definition.magazineSize;
        reserve_ = std::min(definition.reserveAmmo, definition.maxReserveAmmo);
        cooldown_ = reloadRemaining_ = 0;
        reloadState_ = WeaponReloadState::None;
        burstShotsRemaining_ = 0;
        burstTimer_ = 0;
        infiniteAmmo_ = false;
        return true;
    }
    const WeaponDefinition& Definition() const { return definition_; }
    bool InfiniteAmmo() const { return infiniteAmmo_; }
    void SetInfiniteAmmo(bool enabled) {
        infiniteAmmo_ = enabled;
        if (enabled) {
            magazine_ = definition_.magazineSize;
            reloadState_ = WeaponReloadState::None;
            reloadRemaining_ = 0;
        }
    }
    int Magazine() const { return magazine_; }
    int Reserve() const { return reserve_; }
    bool Reloading() const { return reloadState_ != WeaponReloadState::None; }
    WeaponReloadState ReloadState() const { return reloadState_; }
    int BurstRemaining() const { return burstShotsRemaining_; }
    double BurstTimer() const { return burstTimer_; }
    double ReloadRemaining() const { return reloadRemaining_; }
    double Cooldown() const { return cooldown_; }
    // Returns completed burst shots. Caller must produce one set of pellets per shot.
    int Update(float dt) {
        const double elapsed = std::isfinite(dt) ? std::max(0.0,static_cast<double>(dt)) : 0;
        cooldown_ = std::max(0.0,cooldown_-elapsed);
        UpdateReload(elapsed);
        int shots = 0;
        if (burstShotsRemaining_ > 0) {
            burstTimer_ -= elapsed;
            while (burstShotsRemaining_ > 0 && burstTimer_ <= 1e-7) {
                if (HasAmmo()) {
                    if (!infiniteAmmo_) magazine_ -= definition_.ammoPerShot;
                    ++shots;
                    --burstShotsRemaining_;
                }
                if (burstShotsRemaining_ == 0 || !HasAmmo()) {
                    burstShotsRemaining_ = 0;
                    // Preserve frame overshoot: cooldown starts at the final shot's time.
                    cooldown_ = std::max(0.0,definition_.fireInterval+burstTimer_);
                    burstTimer_ = 0;
                } else burstTimer_ += definition_.burstInterval;
            }
        }
        return shots;
    }
    int Step(float dt, bool trigger, bool held, bool reloadRequested) {
        int shots = Update(dt);
        if (reloadRequested) StartReload();
        if (WeaponWantsFire(definition_.fireMode,trigger,held) && TryFire()) {
            ++shots;
            shots += Update(0); // permits burstInterval=0 without delaying the extra shots
        }
        return shots;
    }
    bool TryFire() {
        if (definition_.id.empty() || burstShotsRemaining_ > 0 || cooldown_ > 1e-7 || !HasAmmo()) return false;
        if (Reloading()) {
            if (definition_.reloadMode != WeaponReloadMode::PerRound || !definition_.reloadCanInterrupt) return false;
            reloadState_ = WeaponReloadState::None;
            reloadRemaining_ = 0;
        }
        if (!infiniteAmmo_) magazine_ -= definition_.ammoPerShot;
        cooldown_ = definition_.fireInterval;
        if (definition_.fireMode == WeaponFireMode::Burst && definition_.burstCount > 1 && HasAmmo()) {
            burstShotsRemaining_ = definition_.burstCount-1;
            burstTimer_ = definition_.burstInterval;
            cooldown_ = 0;
        }
        return true;
    }
    bool StartReload() {
        if (infiniteAmmo_ || definition_.id.empty() || Reloading() || magazine_ >= definition_.magazineSize || reserve_ <= 0) return false;
        CancelBurst();
        reloadState_ = WeaponReloadState::Starting;
        reloadRemaining_ = definition_.reloadMode == WeaponReloadMode::Magazine ? definition_.reloadTime : definition_.reloadStartTime;
        UpdateReload(0);
        return true;
    }
    void CancelBurst() {
        if (burstShotsRemaining_ > 0) cooldown_ = definition_.fireInterval;
        burstShotsRemaining_ = 0;
        burstTimer_ = 0;
    }
private:
    bool HasAmmo() const { return infiniteAmmo_ || magazine_ >= definition_.ammoPerShot; }
    void UpdateReload(double elapsed) {
        if (!Reloading()) return;
        reloadRemaining_ -= elapsed;
        // Every iteration inserts one round or exits a phase; zero timings are bounded by ammo.
        while (Reloading() && reloadRemaining_ <= 1e-7) {
            if (definition_.reloadMode == WeaponReloadMode::Magazine) {
                const int amount = std::min(definition_.magazineSize-magazine_,reserve_);
                magazine_ += amount;
                reserve_ -= amount;
                reloadState_ = WeaponReloadState::None;
            } else if (reloadState_ == WeaponReloadState::Finishing) {
                reloadState_ = WeaponReloadState::None;
            } else {
                if (reserve_ > 0 && magazine_ < definition_.magazineSize) { ++magazine_; --reserve_; }
                if (magazine_ == definition_.magazineSize || reserve_ == 0) {
                    reloadState_ = WeaponReloadState::Finishing;
                    reloadRemaining_ += definition_.reloadEndTime;
                } else {
                    reloadState_ = WeaponReloadState::InsertingRound;
                    reloadRemaining_ += definition_.reloadPerRoundTime;
                }
            }
        }
        if (!Reloading()) reloadRemaining_ = 0;
    }
    WeaponDefinition definition_;
    int magazine_ = 0;
    int reserve_ = 0;
    bool infiniteAmmo_ = false;
    double cooldown_ = 0;
    double reloadRemaining_ = 0;
    WeaponReloadState reloadState_ = WeaponReloadState::None;
    int burstShotsRemaining_ = 0;
    double burstTimer_ = 0;
};

struct WeaponPoolEntry { std::string id; double weight = 1; };
struct WeaponSpawnFilter {
    std::optional<WeaponSlot> slot; // 未指定なら両枠を許可する。
    int minRarity = 1, maxRarity = 5;
    std::vector<std::string> types; // 空なら全Type。複数指定時はそのいずれか。
    bool Matches(const WeaponDefinition& weapon) const {
        return (!slot || weapon.slot == *slot) &&
            weapon.rarity >= minRarity && weapon.rarity <= maxRarity &&
            (types.empty() || std::find(types.begin(), types.end(), weapon.type) != types.end());
    }
};
struct WeaponSpawnPoint {
    std::string id;
    Vector3 position{};
    Vector3 rotation{};
    WeaponSpawnFilter filter;
    std::vector<WeaponPoolEntry> weaponPool; // 検証済み・条件で絞り込んだ抽選候補。
};
struct WeaponPickup {
    std::string spawnPointId;
    std::string weaponId;
    Vector3 position{};
    Vector3 rotation{};
    bool visible = true;
    bool pickedUp = false;
};
struct WeaponRandomSettings { bool useFixedSeed = false; uint32_t seed = 12345; };

class WeaponSystem {
public:
    // All definition/point validation and lottery happen once at stage initialization.
    bool Load(const std::string& definitionsPath, const std::string& levelPath,
        std::optional<WeaponRandomSettings> settingsOverride = std::nullopt);
    // Editor reload is transactional and does not reroll or respawn existing pickups.
    bool ReloadForEditor(const std::string& definitionsPath, const std::string& levelPath);
    const WeaponDefinition* Find(const std::string& id) const {
        for (const auto& definition : definitions_) if (definition.id == id) return &definition;
        return nullptr;
    }
    const WeaponDefinition* InitialWeapon() const { return Find(initialWeaponId_); }
    const std::vector<WeaponDefinition>& Definitions() const { return definitions_; }
    const std::vector<WeaponSpawnPoint>& Points() const { return points_; }
    const std::vector<WeaponPickup>& Pickups() const { return pickups_; }
    const std::string& Error() const { return error_; }
    WeaponRandomSettings Settings() const { return settings_; }
    uint32_t ActualSeed() const { return actualSeed_; }
    static constexpr float kPickupRadius = 2.2f;
    std::optional<size_t> Nearest(const Vector3& player) const {
        std::optional<size_t> nearest;
        float best = kPickupRadius*kPickupRadius;
        for (size_t i=0; i<pickups_.size(); ++i) {
            const auto& pickup = pickups_[i];
            if (!pickup.visible || pickup.pickedUp) continue;
            const auto delta = player-pickup.position;
            const float distance = delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
            if (distance <= best && (!nearest || distance < best)) { nearest = i; best = distance; }
        }
        return nearest; // ties use JSON order
    }
    void ResetPickups() { for (auto& pickup : pickups_) { pickup.pickedUp=false; pickup.visible=true; } }
    bool TryPickup(const Vector3& player, WeaponRuntime& weapon) {
        const auto index = Nearest(player);
        if (!index) return false;
        auto& pickup = pickups_[*index];
        const auto* definition = Find(pickup.weaponId);
        if (!definition) return false;
        if (!weapon.Equip(*definition)) return false;
        pickup.pickedUp = true;
        pickup.visible = false;
        return true;
    }
private:
    std::vector<WeaponDefinition> definitions_;
    std::vector<WeaponSpawnPoint> points_;
    std::vector<WeaponPickup> pickups_;
    std::string initialWeaponId_;
    std::string error_;
    WeaponRandomSettings settings_;
    uint32_t actualSeed_ = 0;
};

// Camera basis avoids world-up singularities when aiming vertically.
inline Vector3 WeaponPelletDirection(const Vector3& forward, const Vector3& right, const Vector3& up,
    float spreadDegrees, std::mt19937& random) {
    if (spreadDegrees <= 0) return forward;
    std::uniform_real_distribution<float> unit(0,1);
    const float radius = std::sqrt(unit(random))*std::tan(spreadDegrees*.01745329252f);
    const float angle = unit(random)*6.28318530718f;
    const auto direction = forward + right*(radius*std::cos(angle)) + up*(radius*std::sin(angle));
    const float length = std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    return direction*(1.0f/length);
}
