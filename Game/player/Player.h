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
    }
    const Transform& GetTransform() const { return transform_; }
    FPSMotion::Settings& Settings() { return settings_; }
    WeaponRuntime& CurrentWeapon() { return currentWeapon_; }
    const WeaponRuntime& CurrentWeapon() const { return currentWeapon_; }
    void ApplyDamage(float damage) { if (std::isfinite(damage) && damage > 0) hp_ = std::max(0.0f, hp_-damage); }
    void ResetHPForDebug() { hp_ = 100.0f; }
#ifdef _DEBUG
    struct DebugState {
        Transform transform;
        FPSMotion::Settings settings;
        WeaponRuntime weapon;
        float hp=100, sensitivity=1;
    };
    void RefreshDebug() { RefreshVisuals(); }
    DebugState CaptureDebug() const { return {transform_,settings_,currentWeapon_,hp_,lookSensitivityMultiplier_}; }
    void RestoreDebug(const DebugState& state) {
        transform_=state.transform; settings_=state.settings; currentWeapon_=state.weapon;
        hp_=state.hp; lookSensitivityMultiplier_=state.sensitivity; SyncVisuals(0);
    }
    void SetPositionForDebug(const Vector3& position) { transform_.translate = position; }
#endif
    float GetHP() const { return hp_; }
    bool IsDead() const { return hp_ <= 0; }

    void SetLookSensitivityMultiplier(float multiplier) {
        lookSensitivityMultiplier_ =
            std::clamp(multiplier, 0.05f, 2.0f);
    }

private:
    void SyncVisuals(float dt);
    WeaponRuntime currentWeapon_;
    const StageWorld* stageWorld_ = nullptr;
    float hp_ = 100.0f;
    Transform transform_{{1.0f, 1.0f, 1.0f}, {}, {3.0f, 0.0f, -6.0f}};
    FPSMotion::Settings settings_;
    Camera* camera_ = nullptr;
    Object3d object_; 

    float lookSensitivityMultiplier_ = 1.0f;
    bool movementEnabled_ = true;

};
