#include "GameScene.h"
#include "GameApp.h"
#include "ParticleManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <string_view>
#include <utility>

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "imgui.h"
#include "ImGuiManagaer.h"
#endif

namespace {
    constexpr auto kMuzzleLightsPath = "resources/Data/muzzle_lights.json";
    constexpr std::array<std::string_view, 4> kModeIds{"none", "point", "spot", "area"};

    float DistanceToBoxSquared(const Vector3& position, const AABB& box) {
        const Vector3 nearest{std::clamp(position.x, box.min.x, box.max.x),
            std::clamp(position.y, box.min.y, box.max.y), std::clamp(position.z, box.min.z, box.max.z)};
        const auto delta = position - nearest;
        return StageDot(delta, delta);
    }

    bool IsAxisAligned(const Matrix4x4& world) {
        for (int row = 0; row < 3; ++row) {
            const float x = std::abs(world.m[row][0]);
            const float y = std::abs(world.m[row][1]);
            const float z = std::abs(world.m[row][2]);
            const float largest = std::max({x, y, z});
            if (x + y + z - largest > largest * 1e-5f) return false;
        }
        return true;
    }

    void AddStageOccluders(const StageWorld& stage, LocalLighting::Parameters& light, float reach) {
        struct Candidate {
            const StageCollider* collider;
            float distanceSquared;
        };
        std::array<Candidate, LocalLighting::kMaxOccluders + 1> candidates{};
        size_t count = 0;
        for (const auto& collider : stage.colliders) {
            if (!collider.enabled || collider.playerOnly || !collider.blocksSight) continue;
            const float distanceSquared = DistanceToBoxSquared(light.position, TransformAABB(collider.local, collider.world));
            if (distanceSquared > reach * reach) continue;
            if (count == candidates.size() && distanceSquared >= candidates.back().distanceSquared) continue;
            const auto end = candidates.begin() + std::min(count, candidates.size() - 1);
            const auto insert = std::upper_bound(candidates.begin(), end, distanceSquared,
                [](float distance, const Candidate& entry) { return distance < entry.distanceSquared; });
            std::move_backward(insert, end, end + 1);
            *insert = {&collider, distanceSquared};
            count = std::min(count + 1, candidates.size());
        }

        if (count > LocalLighting::kMaxOccluders) {
            // Fade before the first omitted box can intersect the light.
            light.visibilityRange = std::max(0.001f, std::sqrt(candidates.back().distanceSquared) - 0.06f);
        }
        light.occluderCount = static_cast<uint32_t>(std::min(count, LocalLighting::kMaxOccluders));
        uint32_t rotatedIndex = light.occluderCount;
        for (uint32_t i = 0; i < light.occluderCount; ++i) {
            const auto& collider = *candidates[i].collider;
            if (IsAxisAligned(collider.world)) {
                const auto bounds = TransformAABB(collider.local, collider.world);
                const auto minimum = bounds.min - light.position;
                const auto maximum = bounds.max - light.position;
                auto& occluder = light.occluders[light.axisAlignedOccluderCount++];
                occluder.minimum = {minimum.x, minimum.y, minimum.z, 0.0f};
                occluder.maximum = {maximum.x, maximum.y, maximum.z, 0.0f};
                continue;
            }
            const auto& inverse = collider.inverse;
            const auto localOrigin = StagePoint(light.position, inverse);
            const auto minimum = collider.local.min - localOrigin;
            const auto maximum = collider.local.max - localOrigin;
            light.occluders[--rotatedIndex] = {{inverse.m[0][0], inverse.m[1][0], inverse.m[2][0], 0.0f},
                {inverse.m[0][1], inverse.m[1][1], inverse.m[2][1], 0.0f},
                {inverse.m[0][2], inverse.m[1][2], inverse.m[2][2], 0.0f},
                {minimum.x, minimum.y, minimum.z, 0.0f}, {maximum.x, maximum.y, maximum.z, 0.0f}};
        }
    }
}

GameScene::MuzzleAnchor GameScene::ResolveMuzzleAnchor(const Vector3& hipOffset) const {
    const Vector3 adsDelta{-0.073f, 0.024f, 0.0f};
    const auto intended = StagePoint(hipOffset + adsDelta * adsBlend_, camera_.GetWorldMatrix());
    const auto eye = camera_.GetTranslate();
    const auto displacement = intended - eye;
    const float distance = StageLength(displacement);
    StageHit hit{};
    if (distance > 1e-5f && level_.collision.Raycast(eye, displacement, distance, hit, 0.025f)) {
        const float safeDistance = std::max(0.0f, hit.distance - 0.05f);
        return {eye + displacement * (safeDistance / distance), true};
    }
    return {intended, false};
}

void GameScene::EmitMuzzleSmoke(const Vector3& position, const Vector3& direction,
    float smokeScale, float verticalSmokeScale) {
    if (muzzleLightSettings_.normalSmokeEnabled) {
        ParticleManager::GetInstance()->EmitDirectedFromFile(kMuzzleFlashFile, position,
            direction + Vector3{0.0f, 0.20f, 0.0f}, camera_, smokeScale);
    }
    if (muzzleLightSettings_.verticalSmokeEnabled && verticalSmokeScale > 0.0f) {
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

void GameScene::ApplyMuzzleSmokeToggles() {
    auto* particles = ParticleManager::GetInstance();
    if (!muzzleLightSettings_.normalSmokeEnabled) particles->ClearParticlesFromFile(kMuzzleFlashFile);
    if (!muzzleLightSettings_.verticalSmokeEnabled) {
        pendingVerticalSmoke_.clear();
        particles->ClearParticlesFromFile(kMuzzleVerticalSmokeFile);
    }
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
        const auto mode = light.value("mode", std::string(kModeIds[static_cast<size_t>(next.mode)]));
        const auto modeIt = std::find(kModeIds.begin(), kModeIds.end(), mode);
        if (modeIt == kModeIds.end()) throw std::runtime_error("Unknown muzzle light mode: " + mode);
        next.mode = static_cast<LocalLighting::Mode>(modeIt - kModeIds.begin());
        const auto vectorSetting = [&](const char* name, const Vector3& fallback, float minimum, float maximum) {
            if (const auto entry = light.find(name); entry != light.end()) {
                if (!entry->is_array() || entry->size() != 3) throw std::runtime_error(std::string("Expected muzzle 3-vector: ") + name);
                return Vector3{bounded((*entry)[0].get<float>(), minimum, maximum),
                    bounded((*entry)[1].get<float>(), minimum, maximum), bounded((*entry)[2].get<float>(), minimum, maximum)};
            }
            return fallback;
        };
        next.color = vectorSetting("color", next.color, 0.0f, 1.0f);
        next.offset = vectorSetting("offset", next.offset, -2.0f, 2.0f);
        next.areaRotationDegrees = vectorSetting("areaRotationDegrees", next.areaRotationDegrees, -180.0f, 180.0f);
        next.directEnabled = light.value("directEnabled", next.directEnabled);
        next.indirectEnabled = light.value("indirectEnabled", next.indirectEnabled);
        const auto smoke = document.value("smoke", nlohmann::json::object());
        next.normalSmokeEnabled = smoke.value("normalEnabled", next.normalSmokeEnabled);
        next.verticalSmokeEnabled = smoke.value("verticalEnabled", next.verticalSmokeEnabled);
        next.intensity = bounded(light.value("intensity", next.intensity), 0, 24);
        next.duration = bounded(light.value("duration", next.duration), 0.005f, 0.15f);
        next.range = bounded(light.value("range", next.range), 0.2f, 12);
        next.decay = bounded(light.value("decay", next.decay), 0.5f, 6.0f);
        next.specularStrength = bounded(light.value("specularStrength", next.specularStrength), 0.0f, 1.0f);
        next.areaWidth = bounded(light.value("areaWidth", next.areaWidth), 0.01f, 2.0f);
        next.areaHeight = bounded(light.value("areaHeight", next.areaHeight), 0.01f, 2.0f);
        next.indirectIntensity = bounded(light.value("indirectIntensity", next.indirectIntensity), 0.0f, 4.0f);
        next.indirectRange = bounded(light.value("indirectRange", next.indirectRange), 0.2f, 12.0f);
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
        ApplyMuzzleSmokeToggles();
    } catch (const std::exception& error) {
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        muzzleLightEditor_.error = error.what();
#endif
        OutputDebugStringA(("Muzzle light settings: " + std::string(error.what()) + "\n").c_str());
    }
}

void GameScene::StartMuzzleLight(float weaponScale) {
    muzzleLightPulse_ = MuzzleLightPulse{std::chrono::steady_clock::now(), weaponScale};
}

void GameScene::ClearMuzzleLight() {
    muzzleLightPulse_.reset();
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    heldMuzzleLightScale_.reset();
#endif
}

D3D12_GPU_VIRTUAL_ADDRESS GameScene::PrepareMuzzleLight() {
    const auto& settings = muzzleLightSettings_;
    float envelope = 0.0f;
    if (muzzleLightPulse_) {
        auto& pulse = *muzzleLightPulse_;
        const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - pulse.started).count();
        if (std::exchange(pulse.firstFrame, false)) {
            // Always draw one peak frame, even if a long simulation step crossed the whole flash.
            envelope = pulse.weaponScale;
        } else if (age < settings.duration) {
            const float remaining = 1.0f - age / settings.duration;
            envelope = pulse.weaponScale * remaining * remaining;
        } else {
            muzzleLightPulse_.reset();
        }
    }
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    if (heldMuzzleLightScale_) envelope = *heldMuzzleLightScale_;
#endif
    if (envelope <= 0.0f) return 0;

    LocalLighting::Parameters light;
    light.mode = settings.directEnabled ? settings.mode : LocalLighting::Mode::None;
    light.intensity = light.mode != LocalLighting::Mode::None ? settings.intensity * envelope : 0.0f;
    light.indirectIntensity = settings.indirectEnabled ? settings.indirectIntensity * envelope : 0.0f;
    if (light.intensity <= 0.0f && light.indirectIntensity <= 0.0f) return 0;

    const auto anchor = ResolveMuzzleAnchor(settings.offset);
    light.position = anchor.position;
    light.color = settings.color;
    light.range = settings.range;
    light.indirectRange = settings.indirectRange;
    light.decay = settings.decay;
    light.specularStrength = settings.specularStrength;
    if (anchor.obstructed) {
        light.intensity *= 0.45f;
        light.indirectIntensity *= 0.65f;
    }

    const auto& cameraWorld = camera_.GetWorldMatrix();
    light.direction = {cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2]};
    float areaReach = 0.0f;
    if (light.mode == LocalLighting::Mode::Area && light.intensity > 0.0f) {
        const auto rotation = Matrix4x4::MakeAffineMatrix({1.0f, 1.0f, 1.0f},
            settings.areaRotationDegrees * (std::numbers::pi_v<float> / 180.0f), {});
        const auto basis = Matrix4x4::Multiply(rotation, cameraWorld);
        light.direction = {basis.m[2][0], basis.m[2][1], basis.m[2][2]};
        const auto right = Vector3{basis.m[0][0], basis.m[0][1], basis.m[0][2]} * settings.areaWidth;
        const auto up = Vector3{basis.m[1][0], basis.m[1][1], basis.m[1][2]} * settings.areaHeight;

        // Pull the four sample positions toward the center together when the rectangle meets a wall.
        float fit = 1.0f;
        for (float x : {-0.25f, 0.25f}) {
            for (float y : {-0.25f, 0.25f}) {
                const auto sample = right * x + up * y;
                const float distance = StageLength(sample);
                StageHit hit{};
                if (level_.collision.Raycast(anchor.position, sample, distance, hit, 0.01f, true))
                    fit = std::min(fit, std::max(0.0f, hit.distance - 0.02f) / distance);
            }
        }
        light.areaRight = right * fit;
        light.areaUp = up * fit;
        areaReach = 0.25f * (settings.areaWidth + settings.areaHeight) * fit;
    }
    const float reach = std::max(light.intensity > 0.0f ? light.range + areaReach : 0.0f,
        light.indirectIntensity > 0.0f ? light.indirectRange : 0.0f);
    AddStageOccluders(level_.collision, light, reach);
    muzzleLightBuffer_.Update(light);
    return muzzleLightBuffer_.Address();
}

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
void GameScene::DrawMuzzleEffectSettings(const WeaponDefinition& weapon) {
    constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
    auto& light = muzzleLightSettings_;
    ImGui::SeparatorText("Room Lighting");
    ImGui::Checkbox("Direct Light Enable", &light.directEnabled);
    ImGui::Checkbox("Indirect Light Enable", &light.indirectEnabled);
    const bool normalSmokeChanged = ImGui::Checkbox("Normal Smoke Enable", &light.normalSmokeEnabled);
    const bool verticalSmokeChanged = ImGui::Checkbox("Vertical Smoke Enable", &light.verticalSmokeEnabled);
    if (normalSmokeChanged || verticalSmokeChanged) ApplyMuzzleSmokeToggles();

    int mode = static_cast<int>(light.mode);
    if (ImGui::Combo("Light Mode", &mode, "None\0Point Light\0Spot Light (Legacy)\0Area Light\0"))
        light.mode = static_cast<LocalLighting::Mode>(mode);
    ImGui::ColorEdit3("Light Color", &light.color.x);
    ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 24.0f, "%.2f", clamp);
    ImGui::DragFloat("Range (m)", &light.range, 0.05f, 0.2f, 12.0f, "%.2f", clamp);
    ImGui::DragFloat("Duration (s)", &light.duration, 0.001f, 0.005f, 0.15f, "%.3f", clamp);
    ImGui::DragFloat("Decay", &light.decay, 0.05f, 0.5f, 6.0f, "%.2f", clamp);
    ImGui::DragFloat("Specular Strength", &light.specularStrength, 0.01f, 0.0f, 1.0f, "%.2f", clamp);
    ImGui::DragFloat("Area Width (m)", &light.areaWidth, 0.01f, 0.01f, 2.0f, "%.2f", clamp);
    ImGui::DragFloat("Area Height (m)", &light.areaHeight, 0.01f, 0.01f, 2.0f, "%.2f", clamp);
    ImGui::DragFloat3("Area Pitch / Yaw / Roll (deg)", &light.areaRotationDegrees.x, 1.0f, -180.0f, 180.0f, "%.0f", clamp);
    ImGui::DragFloat("Indirect Intensity", &light.indirectIntensity, 0.01f, 0.0f, 4.0f, "%.2f", clamp);
    ImGui::DragFloat("Indirect Range (m)", &light.indirectRange, 0.05f, 0.2f, 12.0f, "%.2f", clamp);
    ImGui::DragFloat3("Muzzle Local XYZ (m)", &light.offset.x, 0.005f, -2.0f, 2.0f, "%.3f", clamp);

    const auto scaleIt = muzzleWeaponScales_.find(weapon.id);
    const float previewScale = scaleIt == muzzleWeaponScales_.end() ? 1.0f : scaleIt->second.light;
    if (ImGui::Button("Test Flash")) StartMuzzleLight(previewScale);
    ImGui::SameLine();
    bool holdLight = heldMuzzleLightScale_.has_value();
    if (ImGui::Checkbox("Hold Light", &holdLight)) {
        if (holdLight) heldMuzzleLightScale_ = previewScale;
        else heldMuzzleLightScale_.reset();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show Muzzle Anchor", &showMuzzleAnchor_);

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
        auto& document = muzzleLightEditor_.document;
        auto& lightDocument = document["light"];
        if (!lightDocument.is_object()) lightDocument = nlohmann::json::object();
        lightDocument.update({{"mode", kModeIds[static_cast<size_t>(light.mode)]},
            {"directEnabled", light.directEnabled}, {"indirectEnabled", light.indirectEnabled},
            {"color", {light.color.x, light.color.y, light.color.z}}, {"intensity", light.intensity},
            {"range", light.range}, {"duration", light.duration}, {"decay", light.decay},
            {"specularStrength", light.specularStrength}, {"areaWidth", light.areaWidth}, {"areaHeight", light.areaHeight},
            {"areaRotationDegrees", {light.areaRotationDegrees.x, light.areaRotationDegrees.y, light.areaRotationDegrees.z}},
            {"indirectIntensity", light.indirectIntensity}, {"indirectRange", light.indirectRange},
            {"offset", {light.offset.x, light.offset.y, light.offset.z}}});
        auto& smokeDocument = document["smoke"];
        if (!smokeDocument.is_object()) smokeDocument = nlohmann::json::object();
        smokeDocument.update({{"normalEnabled", light.normalSmokeEnabled}, {"verticalEnabled", light.verticalSmokeEnabled}});
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
    if (heldMuzzleLightScale_) {
        const auto entry = muzzleWeaponScales_.find(weapon.id);
        heldMuzzleLightScale_ = entry == muzzleWeaponScales_.end() ? 1.0f : entry->second.light;
    }
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

void GameScene::DrawMuzzleAnchorDebug(GameApp& app) const {
    RECT rect{};
    if (!showMuzzleAnchor_ || !app.ImGui()->GetSceneImageRect(rect)) return;
    const auto anchor = ResolveMuzzleAnchor(muzzleLightSettings_.offset);
    const auto& p = anchor.position;
    const auto& vp = camera_.GetViewProjectionMatrix();
    const float w = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
    const auto clip = StagePoint(p, vp);
    if (w <= 1e-5f || clip.z < 0.0f || clip.z > w || std::abs(clip.x) > w || std::abs(clip.y) > w) return;

    const ImVec2 lo{static_cast<float>(rect.left), static_cast<float>(rect.top)};
    const ImVec2 hi{static_cast<float>(rect.right), static_cast<float>(rect.bottom)};
    const ImVec2 pixel{lo.x + (1.0f + clip.x / w) * 0.5f * (hi.x - lo.x),
        lo.y + (1.0f - clip.y / w) * 0.5f * (hi.y - lo.y)};
    const ImU32 color = anchor.obstructed ? IM_COL32(255, 165, 40, 255) : IM_COL32(80, 230, 255, 255);
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(lo, hi, true);
    draw->AddCircle(pixel, 7.0f, color, 16, 2.0f);
    draw->AddText({pixel.x + 10.0f, pixel.y - 8.0f}, color,
        anchor.obstructed ? "Muzzle (clipped)" : "Muzzle");
    draw->PopClipRect();
}
#endif
