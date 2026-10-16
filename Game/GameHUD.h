#pragma once
#include "UIFont.h"
#include "WeaponHUD.h"

class Player;

class GameHUD {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx);
    void Update(const Player& player, const WeaponSystem& weapons, float width, float height,bool showPickup=true);
    void Draw(const Matrix4x4& view, const Matrix4x4& projection);
private:
    UIFont font_;
    WeaponHUD weaponHUD_;
    Sprite hpBackground_;
    Sprite hpBar_;
    Vector2 whiteSize_{};
    float hp_ = 100.0f;
};
