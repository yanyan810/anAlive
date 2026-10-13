#pragma once
#include "Sprite.h"
#include <array>
#include <cstdint>

enum class TransitionState { Idle, FadeOut, Hold, FadeIn };
enum class FadeEasing { Linear, EaseIn, EaseOut, EaseInOut };

struct FadeSettings {
    float duration = .75f;
    Vector3 color{0, 0, 0};
    FadeEasing easing = FadeEasing::Linear;
};

// ScreenTransition implementation. Retains the engine's existing name/API.
// Owns presentation only; scene destinations and loading belong to SceneManager.
class FadeManager {
public:
    void Initialize(SpriteCommon* sprites, DirectXCommon* dx);
    void FadeOut(float seconds);
    void FadeIn(float seconds);
    void FadeOut() { FadeOut(settings_.duration); }
    void FadeIn() { FadeIn(settings_.duration); }
    void Hold(); // Opaque until FadeIn or Reset is explicitly requested.
    void Reset();
    void Update(float unscaledDt);
    void Draw(); // After post effects and HUD, before debug ImGui.
    void SetDuration(float seconds); // Also retimes an active fade.
    void SetColor(const Vector3& color);
    void SetEasing(FadeEasing easing);
    const FadeSettings& Settings() const { return settings_; }
    TransitionState State() const { return state_; }
    bool IsActive() const { return state_ != TransitionState::Idle; }
    bool IsFinished() const { return state_ == TransitionState::Idle || state_ == TransitionState::Hold; }
    float Alpha() const { return alpha_; }
    float Elapsed() const { return elapsed_; }
    float Duration() const { return duration_; }
    float Progress() const;
    // Monotonic notifications are safe to poll even across zero-duration states.
    // Store a count and compare on the next update; no scene-owned callbacks.
    uint64_t StateStartCount(TransitionState state) const { return starts_[static_cast<size_t>(state)]; }
    uint64_t StateEndCount(TransitionState state) const { return ends_[static_cast<size_t>(state)]; }
private:
    void Begin(TransitionState state, float from, float to, float seconds);
    void SetState(TransitionState state);
    void Evaluate();
    Sprite overlay_;
    FadeSettings settings_;
    TransitionState state_ = TransitionState::Idle;
    std::array<uint64_t, 4> starts_{1, 0, 0, 0}, ends_{};
    float alpha_ = 0, from_ = 0, to_ = 0, elapsed_ = 0, duration_ = 0;
    bool initialized_ = false;
};
