#include "FadeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

void FadeManager::Initialize(SpriteCommon* sprites, DirectXCommon* dx) {
    overlay_.Initialize(sprites, dx, "resources/white1x1.png");
    const auto& texture = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    const float width = static_cast<float>(WinApp::kClientWidth);
    const float height = static_cast<float>(WinApp::kClientHeight);
    overlay_.SetPosition({0, 0});
    overlay_.SetScale({width / static_cast<float>(texture.width), height / static_cast<float>(texture.height), 1});
    overlay_.Update(Matrix4x4::MakeIdentity4x4(), Matrix4x4::MakeOrthographicMatrix(0, 0, width, height, 0, 100));
    initialized_ = true;
    Reset();
}
void FadeManager::Begin(float from, float to, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Fade duration must be finite and nonnegative");
    from_ = from; to_ = to; duration_ = seconds; elapsed_ = 0;
    alpha_ = seconds == 0 ? to_ : from_;
}
void FadeManager::FadeOut(float seconds) { Begin(alpha_, 1, seconds); }
void FadeManager::FadeIn(float seconds) { Begin(1, 0, seconds); }
void FadeManager::Reset() { alpha_ = from_ = to_ = elapsed_ = duration_ = 0; }
void FadeManager::Update(float dt) {
    if (IsFinished() || !std::isfinite(dt) || dt <= 0) return;
    elapsed_ = std::min(duration_, elapsed_ + dt);
    alpha_ = from_ + (to_ - from_) * (elapsed_ / duration_);
    if (IsFinished()) alpha_ = to_;
}
void FadeManager::Draw() {
    if (!initialized_ || alpha_ <= 0) return;
    overlay_.SetColor({0, 0, 0, alpha_});
    overlay_.Draw();
}
