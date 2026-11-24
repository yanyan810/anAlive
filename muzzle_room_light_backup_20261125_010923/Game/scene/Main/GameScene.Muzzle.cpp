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

GameScene::MuzzleAnchor GameScene::ResolveMuzzleAnchor() const {
    const Vector3 hip{ 0.135f, -0.072f, 0.64f };
    const Vector3 ads{ 0.062f, -0.048f, 0.64f };

    const Vector3 intended =
        StagePoint(hip + (ads - hip) * adsBlend_,
            camera_.GetWorldMatrix());

    const Vector3 eye = camera_.GetTranslate();
    const Vector3 displacement = intended - eye;
    const float distance = StageLength(displacement);

    StageHit hit{};

    if (distance > 1e-5f &&
        level_.collision.Raycast(
            eye, displacement, distance, hit, 0.025f)) {

        const float safeDistance =
            std::max(0.0f, hit.distance - 0.05f);

        return {
            eye + displacement * (safeDistance / distance),
            true
        };
    }

    return { intended, false };
}

void GameScene::EmitMuzzleSmoke(const Vector3& position, const Vector3& direction,
    float smokeScale, float verticalSmokeScale) {
    ParticleManager::GetInstance()->EmitDirectedFromFile(kMuzzleFlashFile, position,
        direction + Vector3{0.0f, 0.20f, 0.0f}, camera_, smokeScale);
    if (verticalSmokeScale > 0.0f) {
        // DrawRender consumes accumulated time from before this shot as well.
        const float delay = std::max(0.060f, muzzleLightSettings_.duration) + particleDeltaTime_;
        pendingVerticalSmoke_.push_back({position + direction * 0.022f, direction, verticalSmokeScale, delay});
    }
}

void GameScene::AdvanceMuzzleSmoke(float dt) {
    if (gameOver_.Active() || EscapePlaying()) pendingVerticalSmoke_.clear();
    if (pendingVerticalSmoke_.empty()) return;

    auto* particles = ParticleManager::GetInstance();
    for (auto& smoke : pendingVerticalSmoke_) {
        smoke.remaining -= dt;
        if (smoke.remaining <= 0.0f) {
            particles->EmitDirectedFromFile(kMuzzleVerticalSmokeFile, smoke.position, smoke.direction,
                camera_, smoke.countScale);
        }
    }
    std::erase_if(pendingVerticalSmoke_, [](const PendingMuzzleSmoke& smoke) { return smoke.remaining <= 0.0f; });
}

void GameScene::ClearMuzzleSmoke() {
    pendingVerticalSmoke_.clear();
    auto* particles = ParticleManager::GetInstance();
    particles->ClearParticlesFromFile(kMuzzleFlashFile);
    particles->ClearParticlesFromFile(kMuzzleVerticalSmokeFile);
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
                bounded(values.value("smokeScale", 1.0f), 0, 4),
                bounded(values.value("verticalSmokeScale", 0.0f), 0, 4)});
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
    Vector3 forward{0.0f, 0.0f, 1.0f};
    if (muzzleLightPulse_) {
        const auto& pulse = *muzzleLightPulse_;
        const float remaining = 1.0f - pulse.age / pulse.settings.duration;
        const auto anchor = ResolveMuzzleAnchor();
        position = anchor.position;

        // Point lights illuminate every direction, often making a nearby wall
        // look like the flash itself. Aim a small spotlight down the barrel.
        const auto& cameraWorld = camera_.GetWorldMatrix();
        const Vector3 cameraOrigin = StagePoint({}, cameraWorld);
        forward = StagePoint({0.0f, 0.0f, 1.0f}, cameraWorld) - cameraOrigin;
        const float forwardLength = StageLength(forward);
        if (forwardLength > 1e-5f) forward = forward * (1.0f / forwardLength);
        else forward = {0.0f, 0.0f, 1.0f};

        intensity = anchor.obstructed
            ? 0.0f
            : pulse.settings.intensity * remaining * remaining;

        // A real muzzle flash can illuminate a wall, but when the wall is
        // extremely close, reduce its intensity instead of painting it bright.
        if (intensity > 0.0f) {
            StageHit wallHit{};
            if (level_.collision.Raycast(position, forward, pulse.settings.range, wallHit)) {
                constexpr float kFadeStartDistance = 0.12f;
                constexpr float kFullIntensityDistance = 0.67f;
                const float wallFactor = std::clamp(
                    (wallHit.distance - kFadeStartDistance) /
                        (kFullIntensityDistance - kFadeStartDistance), 0.0f, 1.0f);
                intensity *= wallFactor;
            }
        }
    }
    const auto apply = [&](Object3d& surface) {
        // The affected objects have no persistent spotlight; keep both light
        // types explicit so no point-light pulse is left over from earlier frames.
        surface.SetPointLightIntensity(0.0f);
        surface.SetSpotLightIntensity(intensity);
        if (intensity <= 0.0f) return;
        const auto& settings = muzzleLightPulse_->settings;
        surface.SetSpotLightPos(position);
        surface.SetSpotLightDirection(forward);
        surface.SetSpotLightColor({settings.color.x, settings.color.y, settings.color.z, 1.0f});
        surface.SetSpotLightDistance(settings.range);
        surface.SetSpotLightDecay(2.5f);
        surface.SetSpotLightCosAngle(0.8660254f);         // 30-degree outer half-angle
        surface.SetSpotLightCosFalloffStart(0.9659258f); // 15-degree inner half-angle
    };
    apply(ground_);
    for (auto& shutter : shutterVisuals_) apply(*shutter.model);
    for (auto& door : doorVisuals_) apply(*door);
}

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
void GameScene::DrawMuzzleEffectSettings(const WeaponDefinition& weapon) {
    constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
    ImGui::SeparatorText("Shared Muzzle Spotlight Settings");
    ImGui::ColorEdit3("Light Color", &muzzleLightSettings_.color.x);
    ImGui::DragFloat("Light Intensity", &muzzleLightSettings_.intensity, 0.1f, 0.0f, 24.0f, "%.2f", clamp);
    ImGui::DragFloat("Light Duration (s)", &muzzleLightSettings_.duration, 0.001f, 0.005f, 0.15f, "%.3f", clamp);
    ImGui::DragFloat("Spot Distance", &muzzleLightSettings_.range, 0.05f, 0.2f, 12.0f, "%.2f", clamp);

    if (!weapon.id.empty()) {
        ImGui::SeparatorText("Weapon Scales");
        ImGui::TextUnformatted(weapon.displayName.c_str());
        bool customized = muzzleWeaponScales_.contains(weapon.id);
        if (ImGui::Checkbox("Use weapon-specific scales", &customized)) {
            if (customized) muzzleWeaponScales_.try_emplace(weapon.id);
            else muzzleWeaponScales_.erase(weapon.id);
        }
        float lightScale = 1.0f;
        if (auto entry = muzzleWeaponScales_.find(weapon.id); entry != muzzleWeaponScales_.end()) {
            ImGui::DragFloat("Light Scale", &entry->second.light, 0.05f, 0.0f, 4.0f, "%.2f", clamp);
            ImGui::DragFloat("Short Smoke Count Scale", &entry->second.smoke, 0.05f, 0.0f, 4.0f, "%.2f", clamp);
            ImGui::DragFloat("Vertical Smoke Count Scale", &entry->second.verticalSmoke, 0.05f, 0.0f, 4.0f, "%.2f", clamp);
            lightScale = entry->second.light;
        }
        ImGui::Text("Effective Light Intensity: %.2f", muzzleLightSettings_.intensity * lightScale);
    }

    if (ImGui::Button("Save Light & Weapon Scales") && !muzzleLightEditor_.path.empty() && muzzleLightEditor_.document.is_object()) {
        const auto& light = muzzleLightSettings_;
        auto& document = muzzleLightEditor_.document;
        document["light"] = {{"color", {light.color.x, light.color.y, light.color.z}},
            {"intensity", light.intensity}, {"duration", light.duration}, {"range", light.range}};
        auto weapons = nlohmann::json::object();
        for (const auto& [id, scales] : muzzleWeaponScales_) {
            weapons[id] = {{"lightScale", scales.light}, {"smokeScale", scales.smoke}};
            if (scales.verticalSmoke > 0.0f) weapons[id]["verticalSmokeScale"] = scales.verticalSmoke;
        }
        document["weapons"] = std::move(weapons);
        muzzleLightEditor_.Save([](const std::string&) { return std::string{}; });
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Light & Weapon Scales")) LoadMuzzleLightSettings();
    if (!muzzleLightEditor_.error.empty()) ImGui::TextWrapped("%s", muzzleLightEditor_.error.c_str());

    ImGui::SeparatorText("Shared Smoke Presets");
    if (ImGui::TreeNode("Smoke Particles")) {
        ParticleManager::GetInstance()->DrawImGuiContents(kMuzzleFlashFile);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Vertical Smoke Particles")) {
        ParticleManager::GetInstance()->DrawImGuiContents(kMuzzleVerticalSmokeFile);
        ImGui::TreePop();
    }
}
#endif
