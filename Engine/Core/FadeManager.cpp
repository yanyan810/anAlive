#include "FadeManager.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

void FadeManager::Initialize(SpriteCommon* sprites, DirectXCommon* dx) {
    overlay_.Initialize(sprites, dx, "resources/white1x1.png");
    const auto& texture = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    overlay_.SetPosition({0, 0});
    // Unit quad spans the current viewport, regardless of render resolution or
    // how the game texture is resized inside the debug Scene window.
    overlay_.SetScale({1.0f / static_cast<float>(texture.width), 1.0f / static_cast<float>(texture.height), 1});
    overlay_.Update(Matrix4x4::MakeIdentity4x4(), Matrix4x4::MakeOrthographicMatrix(0, 0, 1, 1, 0, 100));
    initialized_ = true;
    Reset();
}
void FadeManager::SetState(TransitionState state) {
    if (state_ == state) return;
    ++ends_[static_cast<size_t>(state_)];
    state_ = state;
    ++starts_[static_cast<size_t>(state_)];
}
float FadeManager::Progress() const {
    return duration_ > 0 ? std::clamp(elapsed_ / duration_, 0.0f, 1.0f) :
        (state_ == TransitionState::Idle && from_ == to_ ? 0.0f : 1.0f);
}
void FadeManager::Evaluate() {
    float t = duration_ > 0 ? std::clamp(elapsed_ / duration_, 0.0f, 1.0f) : 1.0f;
    switch (settings_.easing) {
    case FadeEasing::EaseIn: t *= t; break;
    case FadeEasing::EaseOut: t = 1 - (1 - t) * (1 - t); break;
    case FadeEasing::EaseInOut: t = t < .5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); break;
    default: break;
    }
    alpha_ = std::clamp(from_ + (to_ - from_) * t, 0.0f, 1.0f);
    if (elapsed_ >= duration_) {
        alpha_ = to_;
        SetState(state_ == TransitionState::FadeOut ? TransitionState::Hold : TransitionState::Idle);
    }
}
void FadeManager::Begin(TransitionState state, float from, float to, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Fade duration must be finite and nonnegative");
    // A repeated standalone command is a new phase, even in the same state.
    if (state_ == state) { ++ends_[static_cast<size_t>(state_)]; ++starts_[static_cast<size_t>(state_)]; }
    else SetState(state);
    from_ = from; to_ = to; duration_ = seconds; elapsed_ = 0;
    Evaluate();
}
void FadeManager::FadeOut(float seconds) { Begin(TransitionState::FadeOut, alpha_, 1, seconds); }
void FadeManager::FadeIn(float seconds) { Begin(TransitionState::FadeIn, 1, 0, seconds); }
void FadeManager::Hold() { alpha_ = from_ = to_ = 1; elapsed_ = duration_ = 0; SetState(TransitionState::Hold); }
void FadeManager::Reset() { alpha_ = from_ = to_ = elapsed_ = duration_ = 0; SetState(TransitionState::Idle); }
void FadeManager::SetDuration(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Fade duration must be finite and nonnegative");
    settings_.duration = seconds;
    if (state_ == TransitionState::FadeOut || state_ == TransitionState::FadeIn) {
        duration_ = seconds;
        elapsed_ = std::min(elapsed_, duration_);
        Evaluate();
    }
}
void FadeManager::SetColor(const Vector3& color) {
    if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
        throw std::invalid_argument("Fade color must be finite");
    settings_.color = {std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f), std::clamp(color.z, 0.0f, 1.0f)};
}
void FadeManager::SetEasing(FadeEasing easing) {
    if (easing < FadeEasing::Linear || easing > FadeEasing::EaseInOut) throw std::invalid_argument("Unknown fade easing");
    settings_.easing = easing;
    if (state_ == TransitionState::FadeOut || state_ == TransitionState::FadeIn) Evaluate();
}
void FadeManager::Update(float dt) {
    if (IsFinished() || !std::isfinite(dt) || dt <= 0) return;
    elapsed_ = std::min(duration_, elapsed_ + dt);
    Evaluate();
}
void FadeManager::Draw() {
    if (!initialized_ || alpha_ <= 0) return;
    const auto& color = settings_.color;
    overlay_.SetColor({color.x, color.y, color.z, alpha_});
    overlay_.Draw();
}
