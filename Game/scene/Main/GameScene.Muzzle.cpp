#include "GameScene.h"
#include "ParticleManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "imgui.h"
#endif

namespace {
    constexpr auto kMuzzleLightsPath = "resources/Data/muzzle_lights.json";
}

Vector3 GameScene::ViewMuzzlePosition() const {
    // Camera-local effect anchor until the first-person model supplies a muzzle socket.
    const Vector3 hip{0.135f, -0.072f, 0.64f};
    const Vector3 ads{0.062f, -0.048f, 0.64f};
    return StagePoint(hip + (ads - hip) * adsBlend_, camera_.GetWorldMatrix());
}

void GameScene::LoadMuzzleLightSettings() {
    try {
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        if (!muzzleLightEditor_.Open(kMuzzleLightsPath)) throw std::runtime_error(muzzleLightEditor_.error);
        const auto& document = muzzleLightEditor_.document;
#else
        std::ifstream file(kMuzzleLightsPath);
        if (!file) return;
        const auto document = nlohmann::json::parse(file);
#endif
        const auto bounded = [](float value, float minimum, float maximum) {
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite muzzle setting");
            return std::clamp(value, minimum, maximum);
        };
        const auto light = document.value("light", nlohmann::json::object());
        MuzzleLightSettings next;
        const auto color = light.value("color", std::array{next.color.x, next.color.y, next.color.z});
        next.color = {bounded(color[0], 0, 1), bounded(color[1], 0, 1), bounded(color[2], 0, 1)};
        next.intensity = bounded(light.value("intensity", next.intensity), 0, 24);
        next.duration = bounded(light.value("duration", next.duration), 0.005f, 0.15f);
        next.range = bounded(light.value("range", next.range), 0.2f, 12);
        const auto weaponSettings = document.value("weapons", nlohmann::json::object());
        if (!weaponSettings.is_object()) throw std::runtime_error("Muzzle weapon settings must be an object");
        std::map<std::string, MuzzleWeaponScales> scales;
        for (const auto& [id, values] : weaponSettings.items()) {
            scales.emplace(id, MuzzleWeaponScales{
                bounded(values.value("lightScale", 1.0f), 0, 4),
                bounded(values.value("smokeScale", 1.0f), 0, 4)});
        }
        muzzleLightSettings_ = next;
        muzzleWeaponScales_ = std::move(scales);
    } catch (const std::exception& error) {
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        muzzleLightEditor_.error = error.what();
#endif
        OutputDebugStringA(("Muzzle light settings: " + std::string(error.what()) + "\n").c_str());
    }
}

void GameScene::ApplyMuzzleLight(float dt) {
    if (gameOver_.Active() || EscapePlaying()) muzzleLightPulse_.reset();
    if (muzzleLightPulse_) {
        auto& pulse = *muzzleLightPulse_;
        if (!std::exchange(pulse.firstFrame, false)) pulse.age += dt;
        if (pulse.age >= pulse.settings.duration) muzzleLightPulse_.reset();
    }

    float intensity = 0.0f;
    Vector3 position{};
    if (muzzleLightPulse_) {
        const auto& pulse = *muzzleLightPulse_;
        const float remaining = 1.0f - pulse.age / pulse.settings.duration;
        intensity = pulse.settings.intensity * remaining * remaining;
        position = ViewMuzzlePosition();
    }
    const auto apply = [&](Object3d& surface) {
        surface.SetPointLightIntensity(intensity);
        if (intensity <= 0.0f) return;
        const auto& settings = muzzleLightPulse_->settings;
        surface.SetPointLightPos(position);
        surface.SetPointLightColor({settings.color.x, settings.color.y, settings.color.z, 1.0f});
        surface.SetPointLightRadius(settings.range);
        surface.SetPointLightDecay(2.5f);
    };
    apply(ground_);
    for (auto& shutter : shutterVisuals_) apply(*shutter.model);
    for (auto& door : doorVisuals_) apply(*door);
}

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
void GameScene::DrawMuzzleEffectSettings() {
    constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
    ImGui::ColorEdit3("Light Color", &muzzleLightSettings_.color.x);
    ImGui::DragFloat("Light Intensity", &muzzleLightSettings_.intensity, 0.1f, 0.0f, 24.0f, "%.2f", clamp);
    ImGui::DragFloat("Light Duration (s)", &muzzleLightSettings_.duration, 0.001f, 0.005f, 0.15f, "%.3f", clamp);
    ImGui::DragFloat("Light Range", &muzzleLightSettings_.range, 0.05f, 0.2f, 12.0f, "%.2f", clamp);

    const auto& weapon = player_.CurrentWeapon().Definition();
    if (!weapon.id.empty()) {
        ImGui::TextUnformatted(weapon.displayName.c_str());
        bool customized = muzzleWeaponScales_.contains(weapon.id);
        if (ImGui::Checkbox("Use weapon-specific scales", &customized)) {
            if (customized) muzzleWeaponScales_.try_emplace(weapon.id);
            else muzzleWeaponScales_.erase(weapon.id);
        }
        if (const auto entry = muzzleWeaponScales_.find(weapon.id); entry != muzzleWeaponScales_.end()) {
            ImGui::DragFloat("Weapon Light Scale", &entry->second.light, 0.05f, 0.0f, 4.0f, "%.2f", clamp);
            ImGui::DragFloat("Weapon Smoke Count Scale", &entry->second.smoke, 0.05f, 0.0f, 4.0f, "%.2f", clamp);
        }
    }

    if (ImGui::Button("Save Light & Weapon Scales") && !muzzleLightEditor_.path.empty() && muzzleLightEditor_.document.is_object()) {
        const auto& light = muzzleLightSettings_;
        auto& document = muzzleLightEditor_.document;
        document["light"] = {{"color", {light.color.x, light.color.y, light.color.z}},
            {"intensity", light.intensity}, {"duration", light.duration}, {"range", light.range}};
        auto weapons = nlohmann::json::object();
        for (const auto& [id, scales] : muzzleWeaponScales_) {
            weapons[id] = {{"lightScale", scales.light}, {"smokeScale", scales.smoke}};
        }
        document["weapons"] = std::move(weapons);
        muzzleLightEditor_.Save([](const std::string&) { return std::string{}; });
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Light & Weapon Scales")) LoadMuzzleLightSettings();
    if (!muzzleLightEditor_.error.empty()) ImGui::TextWrapped("%s", muzzleLightEditor_.error.c_str());

    if (ImGui::TreeNode("Smoke Particles")) {
        ParticleManager::GetInstance()->DrawImGuiContents(kMuzzleFlashFile);
        ImGui::TreePop();
    }
}
#endif
