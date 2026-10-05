#pragma once
#include "Sprite.h"

// Independent of scene names: reusable full-screen black overlay.
class FadeManager {
public:
    void Initialize(SpriteCommon* sprites, DirectXCommon* dx);
    void FadeOut(float seconds);
    void FadeIn(float seconds);
    void Reset();
    void Update(float dt);
    void Draw(); // After post effects and HUD, in the active screen-sized target.
    bool IsFinished() const { return elapsed_ >= duration_; }
    float Alpha() const { return alpha_; }
private:
    void Begin(float from, float to, float seconds);
    Sprite overlay_;
    float alpha_ = 0, from_ = 0, to_ = 0, elapsed_ = 0, duration_ = 0;
    bool initialized_ = false;
};
