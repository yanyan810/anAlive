#include "GameHUD.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <string>

void GameHUD::Initialize(SpriteCommon* common, DirectXCommon* dx,bool hasDoors) {
    font_.Initialize(common, dx, hasDoors ? 24 : 16);
    weaponHUD_.Initialize(common, dx);
    for (Sprite* sprite : {&hpBackground_, &hpBar_}) {
        sprite->Initialize(common, dx, "resources/white1x1.png");
        sprite->SetAnchorPoint({0, 0});
    }
    const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    whiteSize_ = {static_cast<float>(white.width), static_cast<float>(white.height)};
    hpBackground_.SetColor({.75f, .75f, .75f, 1});
    hpBar_.SetColor({0, 0, 0, 1});
}

void GameHUD::Update(const Player& player, const WeaponSystem& weapons, float width, float height,
    bool showPickup,std::string_view doorPrompt) {
    constexpr float maxHP = 100.0f;
    hp_ = std::isfinite(player.GetHP()) ? std::clamp(player.GetHP(), 0.0f, maxHP) : 0.0f;
    // Match the weapon panel's bottom margin, scaling both panels on smaller viewports.
    const float scale = std::min({1.0f, width / 1280.0f, height / 720.0f});
    const float x = 28 * scale;
    const float barY = height - 93 * scale;
    hpBackground_.SetPosition({x, barY});
    hpBar_.SetPosition({x, barY});
    hpBackground_.SetScale({280 * scale / whiteSize_.x, 20 * scale / whiteSize_.y, 1});
    hpBar_.SetScale({280 * scale * (hp_ / maxHP) / whiteSize_.x, 20 * scale / whiteSize_.y, 1});
    font_.Reset();
    font_.DrawText("HP", x, height - 136 * scale, 20 * scale, {0, 0, 0, 1});
    font_.DrawText(std::to_string(static_cast<int>(std::ceil(hp_))) + " / 100",
        x, height - 60 * scale, 20 * scale, {0, 0, 0, 1});
    if (!doorPrompt.empty()) {
        const float left=(width-static_cast<float>(doorPrompt.size())*18*scale)*.5f;
        const float top=height*.5f+44*scale;
        font_.DrawText(doorPrompt,left+1,top+1,18*scale,{0,0,0,1});
        font_.DrawText(doorPrompt,left,top,18*scale,{1,1,1,1});
    }
    const auto nearest = weapons.Nearest(player.GetTransform().translate);
    const auto* nearby = showPickup && nearest ? weapons.PickupDefinition(weapons.Pickups()[*nearest]) : nullptr;
    weaponHUD_.Update(player.CurrentWeapon(), nearby, width, height);
}

void GameHUD::Draw(const Matrix4x4& view, const Matrix4x4& projection) {
    hpBackground_.Update(view, projection);
    hpBackground_.Draw();
    // Avoid a singular transform (zero width) when the player is dead.
    if (hp_ > 0) {
        hpBar_.Update(view, projection);
        hpBar_.Draw();
    }
    font_.Draw(view, projection);
    weaponHUD_.Draw(view, projection);
}
