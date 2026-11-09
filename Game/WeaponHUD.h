#pragma once
#include "UIFont.h"
#include "WeaponSystem.h"
#include <algorithm>
#include <string_view>

// Weapon and pickup presentation, owned by GameHUD. Gameplay remains in WeaponSystem.
class WeaponHUD {
public:
    void Initialize(SpriteCommon* common, DirectXCommon* dx) {
        panel_.Initialize(common, dx, "resources/white1x1.png");
        panel_.SetColor({.015f, .025f, .045f, .8f});
        const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
        whiteSize_ = {static_cast<float>(white.width), static_cast<float>(white.height)};
        font_.Initialize(common, dx);
    }
    void Update(const WeaponRuntime& weapon, const WeaponDefinition* nearby, float width, float height) {
        font_.Reset();
        const float scale = std::min({1.0f, width / 1280.0f, height / 720.0f});
        const float left = width - 496 * scale;
        const float x = left + 12 * scale;
        panel_.SetPosition({left, height - 146 * scale});
        panel_.SetScale({480 * scale / whiteSize_.x, 130 * scale / whiteSize_.y, 1});
        const std::string_view name = weapon.Definition().displayName.empty() ?
            std::string_view{"NO WEAPON"} : std::string_view{weapon.Definition().displayName};
        font_.DrawText(name, x, height - 136 * scale,
            std::min(20.0f, 440.0f / static_cast<float>(name.size())) * scale, {1, 1, 1, 1});
        if (!weapon.Definition().id.empty()) {
            font_.DrawText(std::to_string(weapon.Magazine()) + " / " + (weapon.InfiniteReserveAmmo() ? std::to_string(weapon.Definition().magazineSize)+"  RES INF" : std::to_string(weapon.Reserve())),
                x, height - 99 * scale, 22 * scale, {1, .85f, .35f, 1});
            font_.DrawText(weapon.Reloading() ? "RELOADING" : "R : RELOAD",
                x, height - 60 * scale, 16 * scale, {.75f, .85f, 1, 1});
        }
        if (nearby) {
            const std::string_view prompt = "PRESS E TO PICK UP";
            font_.DrawText(prompt, width * .5f - static_cast<float>(prompt.size()) * 8 * scale,
                height * .70f, 16 * scale, {0, 0, 0, 1});
            const float advance = std::min(20.0f, 800.0f /
                static_cast<float>(std::max(size_t{1}, nearby->displayName.size()))) * scale;
            font_.DrawText(nearby->displayName,
                width * .5f - static_cast<float>(nearby->displayName.size()) * advance * .5f,
                height * .70f + 30 * scale, advance, {0, 0, 0, 1});
        }
    }
    void Draw(const Matrix4x4& view, const Matrix4x4& projection) {
        panel_.Update(view, projection);
        panel_.Draw();
        font_.Draw(view, projection);
    }
private:
    Sprite panel_;
    UIFont font_;
    Vector2 whiteSize_{};
};
