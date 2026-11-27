#include "GameScene.h"
#include "GameApp.h"
#include "Object3dCommon.h"
#include "SrvManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "imgui.h"
#endif

namespace {
    constexpr std::array<std::string_view, 5> kPresetIds{"standard", "hotel", "outdoor", "dark_room", "custom"};
    constexpr std::array<std::string_view, 2> kDiffuseIds{"lambert", "half_lambert"};
    using Json = nlohmann::json;

    template<size_t N>
    size_t ReadChoice(const Json& value, const std::array<std::string_view, N>& choices, const char* name) {
        const auto& key = value.get_ref<const std::string&>();
        const auto entry = std::find(choices.begin(), choices.end(), key);
        if (entry == choices.end()) throw std::runtime_error(std::string("Unknown ") + name + ": " + key);
        return static_cast<size_t>(entry - choices.begin());
    }

    float ReadNumber(const Json& value, float min, float max) {
        if (!value.is_number()) throw std::runtime_error("Expected a lighting number");
        const auto number = value.get<float>();
        if (!std::isfinite(number) || number < min || number > max)
            throw std::runtime_error("Lighting number outside valid range");
        return number;
    }

    float Number(const Json& block, const char* key, float fallback, float min, float max) {
        return block.contains(key) ? ReadNumber(block.at(key), min, max) : fallback;
    }

    Vector3 Vector(const Json& block, const char* key, const Vector3& fallback, float min, float max) {
        if (!block.contains(key)) return fallback;
        const auto& value = block.at(key);
        if (!value.is_array() || value.size() != 3) throw std::runtime_error("Expected a lighting 3-vector");
        return {ReadNumber(value[0], min, max), ReadNumber(value[1], min, max), ReadNumber(value[2], min, max)};
    }

    Json Block(const Json& data, const char* key) {
        auto value = data.value(key, Json::object());
        if (!value.is_object()) throw std::runtime_error(std::string("Expected lighting object: ") + key);
        return value;
    }
}

std::filesystem::path StageLighting::PathFor(std::filesystem::path stagePath) {
    return stagePath.replace_extension(".lighting.json");
}

StageLighting StageLighting::FromPreset(Preset preset) {
    StageLighting settings;
    settings.preset = preset;
    auto& light = settings.environment;
    switch (preset) {
    case Preset::Hotel:
        light.ambientColor = {0.90f, 0.94f, 1.0f};
        light.ambientIntensity = 0.16f;
        light.directionalColor = {1.0f, 0.97f, 0.92f};
        light.directionalIntensity = 0.78f;
        light.specularStrength = 0.15f;
        break;
    case Preset::Outdoor:
        light.ambientColor = {0.87f, 0.94f, 1.0f};
        light.ambientIntensity = 0.18f;
        light.directionalColor = {1.0f, 0.97f, 0.91f};
        light.directionalIntensity = 0.94f;
        light.direction = {0.4f, -1.0f, 0.25f};
        break;
    case Preset::DarkRoom:
        light.ambientColor = {0.79f, 0.85f, 1.0f};
        light.ambientIntensity = 0.045f;
        light.directionalIntensity = 0.28f;
        light.specularStrength = 0.10f;
        break;
    case Preset::Standard:
    case Preset::Custom:
        break;
    }
    return settings;
}

StageLighting StageLighting::FromJson(const Json& data) {
    if (!data.is_object() || !data.contains("version") || !data.at("version").is_number_integer() || data.at("version") != 1)
        throw std::runtime_error("Unsupported stage lighting version");
    const auto preset = static_cast<Preset>(ReadChoice(data.value("preset", Json("standard")), kPresetIds, "lighting preset"));
    auto result = FromPreset(preset);
    auto& light = result.environment;
    const auto ambient = Block(data, "ambient");
    result.ambientEnabled = ambient.value("enabled", result.ambientEnabled);
    light.ambientColor = Vector(ambient, "color", light.ambientColor, 0, 1);
    light.ambientIntensity = Number(ambient, "intensity", light.ambientIntensity, 0, 1);

    const auto directional = Block(data, "directional");
    result.directionalEnabled = directional.value("enabled", result.directionalEnabled);
    light.directionalColor = Vector(directional, "color", light.directionalColor, 0, 1);
    light.directionalIntensity = Number(directional, "intensity", light.directionalIntensity, 0, 10);
    light.direction = Vector(directional, "direction", light.direction, -10, 10);
    if (StageLength(light.direction) < 1e-5f) throw std::runtime_error("Directional light direction must be nonzero");

    const auto material = Block(data, "material");
    if (material.contains("diffuseMode"))
        light.diffuseMode = static_cast<Object3dLight::DiffuseMode>(1 + ReadChoice(material.at("diffuseMode"), kDiffuseIds, "diffuse mode"));
    light.specularStrength = Number(material, "specularStrength", light.specularStrength, 0, 1);

    const auto shadow = Block(data, "shadow");
    auto& map = result.shadow;
    map.enabled = shadow.value("enabled", map.enabled);
    map.roomCastsShadows = shadow.value("roomCastsShadows", map.roomCastsShadows);
    map.followCamera = shadow.value("followCamera", map.followCamera);
    map.strength = Number(shadow, "strength", map.strength, 0, 1);
    map.depthBias = Number(shadow, "depthBias", map.depthBias, 0, 0.02f);
    map.center = Vector(shadow, "center", map.center, -1e6f, 1e6f);
    map.viewSize = Number(shadow, "viewSize", map.viewSize, 1, 200);
    map.lightDistance = Number(shadow, "lightDistance", map.lightDistance, 1, 200);
    map.nearClip = Number(shadow, "nearClip", map.nearClip, 0.01f, 999.0f);
    map.farClip = Number(shadow, "farClip", map.farClip, 0.02f, 1000);
    if (map.farClip < map.nearClip + 0.01f) throw std::runtime_error("Invalid shadow clip range");
    return result;
}

StageLighting StageLighting::Load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open stage lighting: " + path.string());
    return FromJson(Json::parse(file));
}

Json StageLighting::ToJson() const {
    const auto vector = [](const Vector3& value) { return Json::array({value.x, value.y, value.z}); };
    return {{"version", 1}, {"preset", kPresetIds[static_cast<size_t>(preset)]},
        {"ambient", {{"enabled", ambientEnabled}, {"color", vector(environment.ambientColor)}, {"intensity", environment.ambientIntensity}}},
        {"directional", {{"enabled", directionalEnabled}, {"color", vector(environment.directionalColor)},
            {"intensity", environment.directionalIntensity}, {"direction", vector(environment.direction)}}},
        {"material", {{"diffuseMode", environment.diffuseMode == Object3dLight::DiffuseMode::HalfLambert ? kDiffuseIds[1] : kDiffuseIds[0]},
            {"specularStrength", environment.specularStrength}}},
        {"shadow", {{"enabled", shadow.enabled}, {"strength", shadow.strength}, {"depthBias", shadow.depthBias},
            {"followCamera", shadow.followCamera}, {"center", vector(shadow.center)}, {"viewSize", shadow.viewSize},
            {"lightDistance", shadow.lightDistance}, {"nearClip", shadow.nearClip}, {"farClip", shadow.farClip},
            {"roomCastsShadows", shadow.roomCastsShadows}}}};
}

void StageLighting::Apply(Object3dLight& light) const {
    auto active = environment;
    if (!ambientEnabled) active.ambientIntensity = 0.0f;
    if (!directionalEnabled) active.directionalIntensity = 0.0f;
    light.SetEnvironment(active);
}

void GameScene::LoadStageLighting() {
    try {
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        DebugJsonEditor reader;
        if (!reader.OpenOrCreate(stageLightingPath_.string(), StageLighting{}.ToJson()))
            throw std::runtime_error(reader.error);
        auto next = StageLighting::FromJson(reader.document);
        stageLightingEditor_ = std::move(reader);
        stageLighting_ = next;
#else
        stageLighting_ = StageLighting::Load(stageLightingPath_);
#endif
        stageLightingError_.clear();
    } catch (const std::exception& error) {
        stageLightingError_ = error.what();
        OutputDebugStringA(("Stage lighting: " + stageLightingError_ + "\n").c_str());
    }
}

void GameScene::DrawShadow(GameApp& app) {
    stageShadowReady_ = false;
    const auto& map = stageLighting_.shadow;
    if (!map.enabled || map.strength <= 0 || !stageLighting_.directionalEnabled ||
        stageLighting_.environment.directionalIntensity <= 0 || escapeMode_ == EscapeMode::Results) return;
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    if (showroom_ && showEnemyEditor_) return;
#endif

    if (!stageShadowMap_) {
        auto shadow = std::make_unique<DirectionalShadowMap>();
        shadow->Initialize(app.Dx(), app.Srv());
        stageShadowMap_ = std::move(shadow);
    }
    auto& shadow = *stageShadowMap_;
    const auto center = map.followCamera ? camera_.GetTranslate() + map.center : map.center;
    shadow.Begin(stageLighting_.environment.direction, center, map.viewSize, map.lightDistance, map.nearClip, map.farClip);
    if (map.roomCastsShadows) ground_.DrawDirectionalShadow(shadow);
    for (auto& shutter : shutterVisuals_) shutter.model->DrawDirectionalShadow(shadow);
    for (auto& door : doorVisuals_) door->DrawDirectionalShadow(shadow);
    if (EscapePlaying()) {
        if (escapeCar_) escapeCar_->DrawDirectionalShadow(shadow);
        for (const auto& wheel : escapeCarWheels_) wheel.visual->DrawDirectionalShadow(shadow);
        for (const auto& entry : escapeEnemies_) entry.enemy->DrawDirectionalShadow(shadow);
    } else {
        if (gameOver_.EnemiesVisible()) {
            for (auto* enemy : enemies_) enemy->DrawDirectionalShadow(shadow);
        }
        for (size_t i = 0; i < weapons_.Pickups().size() && i < weaponVisuals_.size(); ++i) {
            if (weapons_.Pickups()[i].visible) weaponVisuals_[i]->DrawDirectionalShadow(shadow);
        }
    }
    shadow.End();
    stageShadowReady_ = true;
}

void GameScene::PrepareStageLighting() {
    stageLighting_.Apply(stageLight_);
    if (std::exchange(stageShadowReady_, false)) {
        const auto& shadow = *stageShadowMap_;
        stageLight_.SetDirectionalShadow(shadow.ViewProjection(), {
            stageLighting_.shadow.strength, stageLighting_.shadow.depthBias,
            1.0f / DirectionalShadowMap::kResolution, 0.0f}, shadow.Srv());
    } else {
        stageLight_.SetDirectionalShadow(Matrix4x4::MakeIdentity4x4(), {}, {});
    }
}

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
bool GameScene::SaveStageLighting() {
    stageLightingEditor_.document.update(stageLighting_.ToJson());
    const bool saved = stageLightingEditor_.Save([](const std::string& path) {
        try { StageLighting::Load(path); return std::string{}; }
        catch (const std::exception& error) { return std::string(error.what()); }
    });
    stageLightingError_ = stageLightingEditor_.error;
    return saved;
}

void GameScene::DrawLightingEditorButton() {
    if (ImGui::Button("Lighting Editor")) {
        showLightingEditor_ = true;
        focusLightingEditor_ = true;
    }
}

void GameScene::DrawLightingEditor() {
    if (!showLightingEditor_) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + std::max(0.0f, viewport->WorkSize.x - 379.0f),
        viewport->WorkPos.y + 14.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({365.0f, 550.0f}, ImGuiCond_FirstUseEver);
    if (std::exchange(focusLightingEditor_, false)) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("Lighting Editor", &showLightingEditor_)) {
        ImGui::TextUnformatted(level_.id.empty() ? SceneName() : level_.id.c_str());
        constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
        auto& settings = stageLighting_;
        auto& light = settings.environment;
        auto& shadow = settings.shadow;
        int preset = static_cast<int>(settings.preset);
        if (ImGui::Combo("Preset", &preset, "Standard\0Hotel\0Outdoor\0Dark Room\0Custom\0")) {
            const auto selected = static_cast<StageLighting::Preset>(preset);
            if (selected == StageLighting::Preset::Custom) settings.preset = selected;
            else settings = StageLighting::FromPreset(selected);
            stageLightingEditor_.dirty = true;
        }

        bool changed = false;
        if (ImGui::CollapsingHeader("Ambient Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::Checkbox("Enable##ambient", &settings.ambientEnabled);
            changed |= ImGui::ColorEdit3("Color##ambient", &light.ambientColor.x);
            changed |= ImGui::DragFloat("Intensity##ambient", &light.ambientIntensity, 0.005f, 0.0f, 1.0f, "%.3f", clamp);
        }
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::Checkbox("Enable##directional", &settings.directionalEnabled);
            changed |= ImGui::ColorEdit3("Color##directional", &light.directionalColor.x);
            changed |= ImGui::DragFloat("Intensity##directional", &light.directionalIntensity, 0.01f, 0.0f, 10.0f, "%.2f", clamp);
            const auto previous = light.direction;
            if (ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -10.0f, 10.0f, "%.2f", clamp)) {
                if (StageLength(light.direction) < 1e-5f) light.direction = previous;
                else changed = true;
            }
        }
        if (ImGui::CollapsingHeader("Material Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            int mode = light.diffuseMode == Object3dLight::DiffuseMode::HalfLambert ? 1 : 0;
            if (ImGui::Combo("Diffuse Mode", &mode, "Lambert\0Half-Lambert\0")) {
                light.diffuseMode = mode == 0 ? Object3dLight::DiffuseMode::Lambert : Object3dLight::DiffuseMode::HalfLambert;
                changed = true;
            }
            changed |= ImGui::DragFloat("Specular Strength", &light.specularStrength, 0.01f, 0.0f, 1.0f, "%.2f", clamp);
        }
        if (ImGui::CollapsingHeader("Shadow")) {
            changed |= ImGui::Checkbox("Enable##shadow", &shadow.enabled);
            changed |= ImGui::DragFloat("Strength", &shadow.strength, 0.01f, 0.0f, 1.0f, "%.2f", clamp);
            changed |= ImGui::Checkbox("Room Casts Shadows", &shadow.roomCastsShadows);
            changed |= ImGui::Checkbox("Follow Camera", &shadow.followCamera);
            changed |= ImGui::DragFloat3(shadow.followCamera ? "Center Offset" : "Center",
                &shadow.center.x, 0.1f, -1e6f, 1e6f, "%.2f", clamp);
            changed |= ImGui::DragFloat("View Size", &shadow.viewSize, 0.1f, 1.0f, 200.0f, "%.2f", clamp);
            changed |= ImGui::DragFloat("Light Distance", &shadow.lightDistance, 0.1f, 1.0f, 200.0f, "%.2f", clamp);
            changed |= ImGui::DragFloat("Depth Bias", &shadow.depthBias, 0.00001f, 0.0f, 0.02f, "%.5f", clamp);
            changed |= ImGui::DragFloat("Near Clip", &shadow.nearClip, 0.1f, 0.01f, 999.0f, "%.2f", clamp);
            shadow.farClip = std::max(shadow.farClip, shadow.nearClip + 0.01f);
            changed |= ImGui::DragFloat("Far Clip", &shadow.farClip, 0.1f, shadow.nearClip + 0.01f, 1000.0f, "%.2f", clamp);
        }
        if (changed) {
            settings.preset = StageLighting::Preset::Custom;
            stageLightingEditor_.dirty = true;
        }

        ImGui::Separator();
        ImGui::BeginDisabled(stageLightingEditor_.path.empty());
        if (ImGui::Button("Save")) SaveStageLighting();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reload")) LoadStageLighting();
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            settings = StageLighting{};
            stageLightingEditor_.dirty = true;
        }
        ImGui::TextWrapped("%s%s", stageLightingPath_.string().c_str(), stageLightingEditor_.dirty ? " (unsaved)" : "");
        if (!stageLightingError_.empty()) ImGui::TextWrapped("%s", stageLightingError_.c_str());
    }
    ImGui::End();
}
#endif
