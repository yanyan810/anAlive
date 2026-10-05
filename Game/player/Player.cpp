#include "Player.h"
#include "Input.h"

void Player::Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera) {
    hp_ = 100.0f;
    movementEnabled_ = true;
    camera_ = camera;
    object_.Initialize(common, dx);
    object_.SetCamera(camera);
    object_.SetModel("cube/cube.obj");
    object_.SetScale({0.5f, 1.0f, 0.5f});
    object_.SetIsVisible(false);
}
void Player::Update(const Input& input, float dt) {
    const auto previous=transform_.translate;
    if (input.IsCameraControlEnabled() && input.HasFocus()) {
        const POINT mouse = input.GetMouseDelta();

        FPSMotion::Look(
            transform_,
            static_cast<float>(mouse.x),
            static_cast<float>(mouse.y),
            settings_,
            lookSensitivityMultiplier_);

        const float forward = static_cast<float>(input.IsKeyPressed(DIK_W)) - static_cast<float>(input.IsKeyPressed(DIK_S));
        const float right = static_cast<float>(input.IsKeyPressed(DIK_D)) - static_cast<float>(input.IsKeyPressed(DIK_A));
        if (movementEnabled_) FPSMotion::Move(transform_, right, forward, dt, settings_);
    }
    if (movementEnabled_ && stageWorld_) transform_.translate=stageWorld_->Move(previous,transform_.translate);
    SyncVisuals(dt);
}
int Player::UpdateShooting(const Input& input, float dt, bool allowFire, bool allowReload, bool cancelBurst) {
    if (!allowFire && cancelBurst) currentWeapon_.CancelBurst();
    return currentWeapon_.Step(dt,
        allowFire && input.IsLeftMouseTrigger(), allowFire && input.IsLeftMousePressed(),
        allowReload && input.IsKeyTrigger(DIK_R));
}
void Player::SyncVisuals(float dt) {
    object_.SetTranslate(transform_.translate + Vector3{0.0f, 1.0f, 0.0f});
    object_.SetRotate({0.0f, transform_.rotate.y, 0.0f});
    camera_->SetTranslate(transform_.translate + Vector3{0.0f, settings_.cameraHeight, 0.0f});
    camera_->SetRotate(transform_.rotate);
    camera_->Update();
    object_.Update(dt);
}
