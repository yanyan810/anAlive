#pragma once
#include "Object3d.h"
#include "FPSMotion.h"
#include "WeaponSystem.h"
#include "StageWorld.h"
class Input;

class Player {
public:
    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera);
    void Update(const Input& input, float dt);
    // Prepare camera/model transforms without consuming input or moving.
    void RefreshVisuals() { SyncVisuals(0); }
    void SetMovementEnabled(bool enabled) { movementEnabled_ = enabled; }
    bool IsMovementEnabled() const { return movementEnabled_; }
    int UpdateShooting(const Input& input, float dt, bool allowFire, bool allowReload,
        bool cancelBurst = true);
    void SetStage(const StageWorld* world,const Vector3& position,const Vector3& rotation) {
        stageWorld_=world; transform_.translate=position; transform_.rotate=rotation;
        motion_={};
    }
    const Transform& GetTransform() const { return transform_; }
    Vector3 EyePosition() const { return transform_.translate+Vector3{0,settings_.cameraHeight,0}; }
    const FPSMotion::Settings& Settings() const { return settings_; }
    bool ConfigureMovement(const FPSMotion::Settings& settings) {
        if (!std::isfinite(settings.cameraHeight) || settings.cameraHeight <= 0 ||
            !std::isfinite(settings.mouseSensitivity) || settings.mouseSensitivity < 0 ||
            !std::isfinite(settings.moveSpeed) || settings.moveSpeed < 0) return false;
        settings_ = settings;
        return true;
    }
    const WeaponRuntime& CurrentWeapon() const { return currentWeapon_; }
    bool EquipWeapon(const WeaponDefinition& definition) { return currentWeapon_.Equip(definition); }
    void UnequipWeapon() { currentWeapon_ = WeaponRuntime{}; }
    void CancelWeaponBurst() { currentWeapon_.CancelBurst(); }
    void SetInfiniteAmmo(bool enabled) { currentWeapon_.SetInfiniteAmmo(enabled); }
    bool TryPickupWeapon(WeaponSystem& weapons) { return weapons.TryPickup(transform_.translate, currentWeapon_); }
    // Editor/test snapshots are copies of an already valid runtime, including its timers.
    void RestoreWeaponForDebug(const WeaponRuntime& weapon) { currentWeapon_ = weapon; }
    void ApplyDamage(float damage,const Vector3& direction={}) {
        if (hp_>0 && std::isfinite(damage) && damage>0) {
            hp_=std::max(0.0f,hp_-damage); lastImpactDirection_=direction;
        }
    }
    const Vector3& LastImpactDirection() const { return lastImpactDirection_; }
    void ResetHPForDebug() { hp_ = 100.0f; }
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    struct DebugState {
        Transform transform;
        FPSMotion::Settings settings;
        WeaponRuntime weapon;
        float hp=100, sensitivity=1;
        StageWorld::PlayerMotion motion;
        Vector3 impactDirection{};
    };
    void RefreshDebug() { RefreshVisuals(); }
    DebugState CaptureDebug() const { return {transform_,settings_,currentWeapon_,hp_,lookSensitivityMultiplier_,motion_,lastImpactDirection_}; }
    void RestoreDebug(const DebugState& state) {
        transform_=state.transform; settings_=state.settings; currentWeapon_=state.weapon;
        hp_=state.hp; lookSensitivityMultiplier_=state.sensitivity; SyncVisuals(0);
        motion_=state.motion;
        lastImpactDirection_=state.impactDirection;
    }
    void SetPositionForDebug(const Vector3& position) { transform_.translate = position; motion_={}; }
#endif
    float GetHP() const { return hp_; }
    bool IsDead() const { return hp_ <= 0; }

    void SetLookSensitivityMultiplier(float multiplier) {
        if (!std::isfinite(multiplier)) return;
        lookSensitivityMultiplier_ =
            std::clamp(multiplier, 0.05f, 2.0f);
    }

private:
    void SyncVisuals(float dt);
    WeaponRuntime currentWeapon_;
    const StageWorld* stageWorld_ = nullptr;
    StageWorld::PlayerMotion motion_;
    float hp_ = 100.0f;
    Vector3 lastImpactDirection_{};
    Transform transform_{{1.0f, 1.0f, 1.0f}, {}, {3.0f, 0.0f, -6.0f}};
    FPSMotion::Settings settings_;
    Camera* camera_ = nullptr;
    Object3d object_; 

    float lookSensitivityMultiplier_ = 1.0f;
    bool movementEnabled_ = true;

};
