#include "GameScene.h"
#include "GameApp.h"
#include "ParticleManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>

#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
#include "imgui.h"
#include "ImGuiManagaer.h"
#endif

namespace {
    constexpr auto kMuzzleLightsPath = "resources/Data/muzzle_lights.json";
    constexpr std::array<std::string_view, 5> kModeIds{"none", "point", "spot", "area", "forward"};

    Matrix4x4 MuzzleLightBasis(const Matrix4x4& camera, LocalLighting::Mode mode, const Vector3& areaRotation) {
        if (mode != LocalLighting::Mode::Area) return camera;
        const auto rotation = Matrix4x4::MakeAffineMatrix({1.0f, 1.0f, 1.0f},
            areaRotation * (std::numbers::pi_v<float> / 180.0f), {});
        return Matrix4x4::Multiply(rotation, camera);
    }

    Vector3 Forward(const Matrix4x4& basis) {
        return {basis.m[2][0], basis.m[2][1], basis.m[2][2]};
    }

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

    struct VisibilitySource {
        Vector3 position{};
        float reach = 0.0f;
        Vector3 normal{};
        float surfaceOffset = 0.0f;
    };

    AABB StageOccluderBounds(const StageCollider& collider) {
        const auto center = StagePoint((collider.local.min + collider.local.max) * 0.5f, collider.world);
        const auto half = (collider.local.max - collider.local.min) * 0.5f;
        const auto& m = collider.world.m;
        const Vector3 extent{
            half.x * std::abs(m[0][0]) + half.y * std::abs(m[1][0]) + half.z * std::abs(m[2][0]),
            half.x * std::abs(m[0][1]) + half.y * std::abs(m[1][1]) + half.z * std::abs(m[2][1]),
            half.x * std::abs(m[0][2]) + half.y * std::abs(m[1][2]) + half.z * std::abs(m[2][2])};
        return {center - extent, center + extent};
    }

    void AddStageOccluders(const StageWorld& stage, std::span<const VisibilitySource> sources,
        LocalLighting::Parameters& light) {
        struct Candidate {
            const StageCollider* collider;
            AABB worldBounds;
            float distanceSquared;
        };
        struct Selection {
            std::array<Candidate, LocalLighting::kMaxOccluders> candidates{};
            std::array<float, 8> omittedDistancesSquared;
            size_t count = 0;

            Selection() { omittedDistancesSquared.fill(std::numeric_limits<float>::infinity()); }

            void RecordOmitted(const Candidate& candidate, const Vector3& source) {
                const auto& bounds = candidate.worldBounds;
                // An AABB can span several octants. Mark each one conservatively.
                uint32_t octants = 0xff;
                if (bounds.min.x > source.x) octants &= 0xaa;
                if (bounds.max.x < source.x) octants &= 0x55;
                if (bounds.min.y > source.y) octants &= 0xcc;
                if (bounds.max.y < source.y) octants &= 0x33;
                if (bounds.min.z > source.z) octants &= 0xf0;
                if (bounds.max.z < source.z) octants &= 0x0f;
                for (size_t i = 0; i < omittedDistancesSquared.size(); ++i) {
                    if (octants & (1u << i))
                        omittedDistancesSquared[i] = std::min(omittedDistancesSquared[i], candidate.distanceSquared);
                }
            }
        };
        if (std::none_of(sources.begin(), sources.end(), [](const auto& source) { return source.reach > 0.0f; }))
            return;
        std::array<Selection, 1 + LocalLighting::kMaxBounceLights> selections{};
        for (const auto& collider : stage.colliders) {
            if (!collider.enabled || collider.playerOnly || !collider.blocksSight) continue;
            const auto bounds = StageOccluderBounds(collider);
            for (size_t i = 0; i < sources.size(); ++i) {
                const auto& source = sources[i];
                if (source.reach <= 0.0f) continue;
                const float distanceSquared = DistanceToBoxSquared(source.position, bounds);
                if (distanceSquared > source.reach * source.reach) continue;
                if (source.surfaceOffset > 0.0f) {
                    const auto& n = source.normal;
                    const Vector3 front{n.x >= 0.0f ? bounds.max.x : bounds.min.x,
                        n.y >= 0.0f ? bounds.max.y : bounds.min.y, n.z >= 0.0f ? bounds.max.z : bounds.min.z};
                    // The source and the unshifted receiver lie in the emitting hemisphere.
                    if (StageDot(front - source.position, n) + source.surfaceOffset <= 0.0001f) continue;
                }
                auto& selection = selections[i];
                auto& candidates = selection.candidates;
                auto& count = selection.count;
                const Candidate candidate{&collider, bounds, distanceSquared};
                if (count == candidates.size()) {
                    if (distanceSquared >= candidates.back().distanceSquared) {
                        selection.RecordOmitted(candidate, source.position);
                        continue;
                    }
                    selection.RecordOmitted(candidates.back(), source.position);
                }
                const auto end = candidates.begin() + std::min(count, candidates.size() - 1);
                const auto insert = std::upper_bound(candidates.begin(), end, distanceSquared,
                    [](float distance, const Candidate& entry) { return distance < entry.distanceSquared; });
                std::move_backward(insert, end, end + 1);
                *insert = candidate;
                count = std::min(count + 1, candidates.size());
            }
        }

        for (size_t slot = 0; slot < sources.size(); ++slot) {
            auto& visibility = light.visibility[slot];
            const auto& selection = selections[slot];
            const auto& candidates = selection.candidates;
            for (size_t i = 0; i < visibility.omittedRanges.size(); ++i) {
                const float distanceSquared = selection.omittedDistancesSquared[i];
                if (!std::isfinite(distanceSquared)) continue;
                // Fade before the first omitted box in this octant can intersect the segment.
                const float cutoff = std::max(0.001f, std::sqrt(distanceSquared) - 0.06f);
                visibility.omittedRanges[i] = cutoff;
                visibility.nearestOmittedRange = visibility.nearestOmittedRange > 0.0f ?
                    std::min(visibility.nearestOmittedRange, cutoff) : cutoff;
            }
            visibility.occluderCount = static_cast<uint32_t>(selection.count);
            uint32_t rotatedIndex = visibility.occluderCount;
            for (uint32_t i = 0; i < visibility.occluderCount; ++i) {
                const auto& candidate = candidates[i];
                const auto& collider = *candidate.collider;
                if (IsAxisAligned(collider.world)) {
                    const auto minimum = candidate.worldBounds.min - sources[slot].position;
                    const auto maximum = candidate.worldBounds.max - sources[slot].position;
                    auto& occluder = visibility.occluders[visibility.axisAlignedOccluderCount++];
                    occluder.minimum = {minimum.x, minimum.y, minimum.z, 0.0f};
                    occluder.maximum = {maximum.x, maximum.y, maximum.z, 0.0f};
                    continue;
                }
                const auto& inverse = collider.inverse;
                const auto localOrigin = StagePoint(sources[slot].position, inverse);
                const auto minimum = collider.local.min - localOrigin;
                const auto maximum = collider.local.max - localOrigin;
                visibility.occluders[--rotatedIndex] = {{inverse.m[0][0], inverse.m[1][0], inverse.m[2][0], 0.0f},
                    {inverse.m[0][1], inverse.m[1][1], inverse.m[2][1], 0.0f},
                    {inverse.m[0][2], inverse.m[1][2], inverse.m[2][2], 0.0f},
                    {minimum.x, minimum.y, minimum.z, 0.0f}, {maximum.x, maximum.y, maximum.z, 0.0f}};
            }
        }
    }

    Vector3 BounceRayDirection(int index, int count) {
        static const std::array<Vector3, 6> axes{{{0, -1, 0}, {0, 1, 0},
            {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
        if (index < static_cast<int>(axes.size())) return axes[index];
        // Fibonacci samples fill the sphere between the six guaranteed directions.
        const int axisCount = static_cast<int>(axes.size());
        const float sample = static_cast<float>(index - axisCount);
        const float y = 1.0f - 2.0f * (sample + 0.5f) / static_cast<float>(count - axisCount);
        const float radius = std::sqrt(1.0f - y * y);
        const float azimuth = (sample + 0.5f) * 2.39996323f;
        return {radius * std::cos(azimuth), y, radius * std::sin(azimuth)};
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
        next.forwardBias = bounded(light.value("forwardBias", next.forwardBias), 0.0f, 0.9f);
        next.indirectIntensity = bounded(light.value("indirectIntensity", next.indirectIntensity), 0.0f, 4.0f);
        next.indirectRange = bounded(light.value("indirectRange", next.indirectRange), 0.2f, 12.0f);
        const auto bounce = document.value("bounce", nlohmann::json::object());
        next.bounce.enabled = bounce.value("enabled", next.bounce.enabled);
        next.bounce.occlusionEnabled = bounce.value("occlusionEnabled", next.bounce.occlusionEnabled);
        next.bounce.intensity = bounded(bounce.value("intensity", next.bounce.intensity), 0.0f, 1.0f);
        next.bounce.range = bounded(bounce.value("range", next.bounce.range), 0.2f, 12.0f);
        next.bounce.rayCount = std::clamp(bounce.value("rayCount", next.bounce.rayCount), 6, kMaxBounceRays);
        next.bounce.rayDistance = bounded(bounce.value("rayDistance", next.bounce.rayDistance), 0.2f, 12.0f);
        next.bounce.maxLights = std::clamp(bounce.value("maxLights", next.bounce.maxLights),
            1, static_cast<int>(LocalLighting::kMaxBounceLights));
        next.bounce.surfaceOffset = bounded(bounce.value("surfaceOffset", next.bounce.surfaceOffset), 0.005f, 0.10f);
        next.bounce.decay = bounded(bounce.value("decay", next.bounce.decay), 0.5f, 6.0f);
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
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        heldMuzzleBounce_.reset();
#endif
        ApplyMuzzleSmokeToggles();
    } catch (const std::exception& error) {
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        muzzleLightEditor_.error = error.what();
#endif
        OutputDebugStringA(("Muzzle light settings: " + std::string(error.what()) + "\n").c_str());
    }
}

GameScene::MuzzleBounceSample GameScene::FindMuzzleBouncePoints() const {
    MuzzleBounceSample sample;
    const auto& settings = muzzleLightSettings_.bounce;
    if (!settings.enabled) return sample;

    const auto start = std::chrono::steady_clock::now();
    const auto anchor = ResolveMuzzleAnchor(muzzleLightSettings_.offset);
    sample.origin = anchor.position;
    sample.emissionScale = anchor.obstructed ? 0.45f : 1.0f;
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    sample.rayCount = settings.rayCount;
#endif
    std::array<MuzzleBouncePoint, kMaxBounceRays> candidates{};
    size_t candidateCount = 0;
    const auto& stage = level_.collision;
    for (int i = 0; i < settings.rayCount; ++i) {
        const auto direction = BounceRayDirection(i, settings.rayCount);
        StageHit hit;
        const bool found = stage.Raycast(anchor.position, direction, settings.rayDistance, hit, 0.0f, true);
        const auto end = anchor.position + direction * (found ? hit.distance : settings.rayDistance);
        const float incidence = -StageDot(direction, hit.normal);
        const float normalizedDistance = hit.distance / 2.5f;
        const float weight = found && hit.distance >= 0.001f ?
            std::max(0.0f, incidence) / (1.0f + normalizedDistance * normalizedDistance) : 0.0f;
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
        sample.rays[i] = {end, found, hit.normal, weight, hit.colliderIndex};
#endif
        if (weight <= 0.0f) continue;
        auto& candidate = candidates[candidateCount++];
        candidate.position = end;
        candidate.normal = hit.normal;
        candidate.weight = weight;
        candidate.colliderIndex = hit.colliderIndex;
        candidate.colliderWorld = stage.colliders[hit.colliderIndex].world;
    }

    std::sort(candidates.begin(), candidates.begin() + candidateCount, [](const auto& a, const auto& b) {
        return a.weight > b.weight;
    });
    const size_t maxLights = static_cast<size_t>(settings.maxLights);
    for (size_t i = 0; i < candidateCount && sample.count < maxLights; ++i) {
        const auto& candidate = candidates[i];
        const auto end = sample.points.begin() + sample.count;
        // Keep the strongest hit on each plane, including walls made of adjoining boxes.
        const bool sameSurface = std::any_of(sample.points.begin(), end, [&](const auto& accepted) {
            return StageDot(candidate.normal, accepted.normal) > 0.98f &&
                std::abs(StageDot(candidate.position - accepted.position, accepted.normal)) < 0.12f;
        });
        if (!sameSurface) sample.points[sample.count++] = candidate;
    }
    sample.calculationMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return sample;
}

uint32_t GameScene::PrepareMuzzleBounceLights(const MuzzleBounceSample& sample, float envelope,
    LocalLighting::Parameters& light) const {
    const auto& settings = muzzleLightSettings_.bounce;
    const float strength = muzzleLightSettings_.intensity * settings.intensity * envelope * sample.emissionScale;
    if (!settings.enabled || strength <= 0.0f) return 0;

    const size_t count = std::min(sample.count, static_cast<size_t>(settings.maxLights));
    float totalWeight = 0.0f;
    for (size_t i = 0; i < count; ++i) totalWeight += sample.points[i].weight;
    const float budget = strength / std::max(1.0f, totalWeight);
    uint32_t active = 0;
    const auto& colliders = level_.collision.colliders;
    for (size_t i = 0; i < count; ++i) {
        const auto& point = sample.points[i];
        if (point.colliderIndex >= colliders.size()) continue;
        const auto& collider = colliders[point.colliderIndex];
        // Drop a cached sample if its reflecting door, shutter or removable surface moved.
        if (!collider.enabled || std::memcmp(collider.world.m, point.colliderWorld.m, sizeof(collider.world.m)) != 0)
            continue;
        // A door can close across the original incident ray while a flash is held.
        // Revalidate only movable blockers on this known segment, keeping the sample fixed.
        const auto incident = point.position - sample.origin;
        const float distance = StageLength(incident);
        const auto unit = incident * (1.0f / std::max(distance, 0.002f));
        const auto blocksIncident = [&](size_t index) {
            if (index == point.colliderIndex || index >= colliders.size() || distance <= 0.002f) return false;
            const auto& blocker = colliders[index];
            return blocker.blocksSight && !blocker.playerOnly &&
                StageWorld::IntersectsBox(blocker, sample.origin, unit, distance - 0.002f, {});
        };
        const auto& doors = level_.doors.doors;
        const auto& shutters = level_.shutters.shutters;
        if (std::any_of(doors.begin(), doors.end(), [&](const auto& door) { return blocksIncident(door.colliderIndex); }) ||
            std::any_of(shutters.begin(), shutters.end(), [&](const auto& shutter) {
                return blocksIncident(shutter.definition.colliderIndex);
            })) continue;
        auto& bounce = light.bounces[active++];
        bounce.position = point.position + point.normal * settings.surfaceOffset;
        bounce.normal = point.normal;
        bounce.surfaceOffset = settings.surfaceOffset;
        bounce.range = settings.range;
        bounce.decay = settings.decay;
        const auto& color = muzzleLightSettings_.color;
        bounce.radiance = Vector3{color.x * point.reflectance.x, color.y * point.reflectance.y,
            color.z * point.reflectance.z} * (budget * point.weight);
    }
    return active;
}

void GameScene::StartMuzzleLight(float weaponScale) {
    muzzleLightPulse_ = MuzzleLightPulse{std::chrono::steady_clock::now(), weaponScale};
    if (weaponScale > 0.0f) muzzleLightPulse_->bounce = FindMuzzleBouncePoints();
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    if (!heldMuzzleLightScale_) muzzleBounceDebug_ = muzzleLightPulse_->bounce;
#endif
}

void GameScene::ClearMuzzleLight() {
    muzzleLightPulse_.reset();
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    heldMuzzleLightScale_.reset();
    heldMuzzleBounce_.reset();
    muzzleBounceDebug_ = {};
    muzzleShotPoseDebug_.reset();
    activeMuzzleBounceLights_ = 0;
#endif
}

D3D12_GPU_VIRTUAL_ADDRESS GameScene::PrepareMuzzleLight() {
    float envelope = 0.0f;
    const MuzzleBounceSample* bounceSample = nullptr;
    if (muzzleLightPulse_) {
        auto& pulse = *muzzleLightPulse_;
        const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - pulse.started).count();
        if (std::exchange(pulse.firstFrame, false)) {
            // Always draw one peak frame, even if a long simulation step crossed the whole flash.
            envelope = pulse.weaponScale;
        } else if (age < muzzleLightSettings_.duration) {
            const float remaining = 1.0f - age / muzzleLightSettings_.duration;
            envelope = pulse.weaponScale * remaining * remaining;
        } else {
            muzzleLightPulse_.reset();
        }
        if (muzzleLightPulse_) bounceSample = &muzzleLightPulse_->bounce;
    }
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    activeMuzzleBounceLights_ = 0;
    if (heldMuzzleLightScale_) {
        envelope = *heldMuzzleLightScale_;
        if (!heldMuzzleBounce_) {
            heldMuzzleBounce_ = FindMuzzleBouncePoints();
            muzzleBounceDebug_ = *heldMuzzleBounce_;
        }
        bounceSample = &*heldMuzzleBounce_;
    }
#endif
    if (envelope <= 0.0f) return 0;

    const auto light = BuildMuzzleLightParameters(envelope, bounceSample);
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    activeMuzzleBounceLights_ = light.bounceCount;
#endif
    if (light.intensity <= 0.0f && light.indirectIntensity <= 0.0f && light.bounceCount == 0) return 0;
    muzzleLightBuffer_.Update(light);
    return muzzleLightBuffer_.Address();
}

LocalLighting::Parameters GameScene::BuildMuzzleLightParameters(float envelope, const MuzzleBounceSample* bounceSample) const {
    const auto& settings = muzzleLightSettings_;
    LocalLighting::Parameters light;
    light.mode = settings.directEnabled ? settings.mode : LocalLighting::Mode::None;
    light.intensity = light.mode != LocalLighting::Mode::None ? settings.intensity * envelope : 0.0f;
    light.indirectIntensity = settings.indirectEnabled ? settings.indirectIntensity * envelope : 0.0f;
    if (bounceSample) light.bounceCount = PrepareMuzzleBounceLights(*bounceSample, envelope, light);
    const bool hasMuzzleSource = light.intensity > 0.0f || light.indirectIntensity > 0.0f;
    if (!hasMuzzleSource && light.bounceCount == 0) return light;

    const auto anchor = hasMuzzleSource ? ResolveMuzzleAnchor(settings.offset) : MuzzleAnchor{};
    light.position = anchor.position;
    light.color = settings.color;
    light.range = settings.range;
    light.indirectRange = settings.indirectRange;
    light.decay = settings.decay;
    light.specularStrength = settings.specularStrength;
    light.forwardBias = settings.forwardBias;
    if (anchor.obstructed) {
        light.intensity *= 0.45f;
        light.indirectIntensity *= 0.65f;
    }

    const auto basis = MuzzleLightBasis(camera_.GetWorldMatrix(), light.mode, settings.areaRotationDegrees);
    light.direction = Forward(basis);
    float areaReach = 0.0f;
    if (light.mode == LocalLighting::Mode::Area && light.intensity > 0.0f) {
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
    std::array<VisibilitySource, 1 + LocalLighting::kMaxBounceLights> sources{};
    sources[0] = {light.position, reach};
    if (settings.bounce.occlusionEnabled) {
        for (uint32_t i = 0; i < light.bounceCount; ++i) {
            const auto& bounce = light.bounces[i];
            sources[i + 1] = {bounce.position, bounce.range + 0.04f, bounce.normal, bounce.surfaceOffset};
        }
    }
    AddStageOccluders(level_.collision, std::span(sources).first(1 + light.bounceCount), light);
    return light;
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
    if (ImGui::Combo("Light Mode", &mode, "None\0Point Light\0Spot Light (Legacy)\0Area Light\0Forward Light\0"))
        light.mode = static_cast<LocalLighting::Mode>(mode);
    if (light.mode == LocalLighting::Mode::Forward)
        ImGui::SliderFloat("Forward Bias", &light.forwardBias, 0.0f, 0.9f, "%.2f", clamp);
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
    bool resample = ImGui::DragFloat3("Muzzle Local XYZ (m)", &light.offset.x, 0.005f, -2.0f, 2.0f, "%.3f", clamp);

    const auto scaleIt = muzzleWeaponScales_.find(weapon.id);
    const float previewScale = scaleIt == muzzleWeaponScales_.end() ? 1.0f : scaleIt->second.light;
    const bool testFlash = ImGui::Button("Test Flash");
    ImGui::SameLine();
    bool holdLight = heldMuzzleLightScale_.has_value();
    if (ImGui::Checkbox("Hold Light", &holdLight)) {
        if (holdLight) heldMuzzleLightScale_ = previewScale;
        else heldMuzzleLightScale_.reset();
        heldMuzzleBounce_.reset();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show Muzzle Anchor", &showMuzzleAnchor_);

    if (ImGui::TreeNode("Light Direction Debug")) {
        if (ImGui::Checkbox("Show Light Directions", &showMuzzleDirections_) && !showMuzzleDirections_)
            muzzleShotPoseDebug_.reset();
        if (showMuzzleDirections_) {
            const auto& world = camera_.GetWorldMatrix();
            const auto cameraAxis = Forward(world);
            const auto source = ResolveMuzzleAnchor(light.offset);
            const auto lightAxis = Forward(MuzzleLightBasis(world, light.mode, light.areaRotationDegrees));
            ImGui::Text("Muzzle world: %.2f, %.2f, %.2f%s", source.position.x, source.position.y, source.position.z,
                source.obstructed ? " (clipped)" : "");
            ImGui::Text("Camera forward: %.3f, %.3f, %.3f", cameraAxis.x, cameraAxis.y, cameraAxis.z);
            ImGui::Text("Light reference axis: %.3f, %.3f, %.3f", lightAxis.x, lightAxis.y, lightAxis.z);
            if (muzzleShotPoseDebug_) {
                const auto& direction = muzzleShotPoseDebug_->direction;
                ImGui::Text("Last shot before spread: %.3f, %.3f, %.3f", direction.x, direction.y, direction.z);
            }
            StageHit wall;
            if (level_.collision.Raycast(camera_.GetTranslate(), cameraAxis, 25.0f, wall, 0.0f, true)) {
                const auto& collider = level_.collision.colliders[wall.colliderIndex];
                ImGui::TextWrapped("Aim wall: %s (%.2f m from camera)", collider.id.c_str(), wall.distance);
                ImGui::Text("Wall normal: %.2f, %.2f, %.2f", wall.normal.x, wall.normal.y, wall.normal.z);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::CollapsingHeader("Bounce Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& bounce = light.bounce;
        resample |= ImGui::Checkbox("Bounce Light Enable", &bounce.enabled);
        ImGui::DragFloat("Bounce Intensity", &bounce.intensity, 0.01f, 0.0f, 1.0f, "%.2f", clamp);
        ImGui::DragFloat("Bounce Range (m)", &bounce.range, 0.05f, 0.2f, 12.0f, "%.2f", clamp);
        resample |= ImGui::SliderInt("Ray Count", &bounce.rayCount, 6, kMaxBounceRays, "%d", clamp);
        resample |= ImGui::DragFloat("Ray Distance (m)", &bounce.rayDistance, 0.05f, 0.2f, 12.0f, "%.2f", clamp);
        resample |= ImGui::SliderInt("Max Bounce Lights", &bounce.maxLights, 1,
            static_cast<int>(LocalLighting::kMaxBounceLights), "%d", clamp);
        ImGui::DragFloat("Surface Offset (m)", &bounce.surfaceOffset, 0.001f, 0.005f, 0.10f, "%.3f", clamp);
        ImGui::DragFloat("Bounce Decay", &bounce.decay, 0.05f, 0.5f, 6.0f, "%.2f", clamp);
        ImGui::Checkbox("Occlusion Enable", &bounce.occlusionEnabled);
        ImGui::Checkbox("Show Bounce Rays", &showBounceRays_);
        ImGui::Checkbox("Show Bounce Points", &showBouncePoints_);
        ImGui::Checkbox("Show Bounce Normals", &showBounceNormals_);
        ImGui::Text("Active Bounce Lights: %u", activeMuzzleBounceLights_);
        ImGui::Text("Bounce Calculation Time: %.3f ms", muzzleBounceDebug_.calculationMs);
        ImGui::BeginDisabled(!holdLight || !bounce.enabled);
        resample |= ImGui::Button("Recalculate Bounce Points");
        ImGui::EndDisabled();
    }
    if (resample) heldMuzzleBounce_.reset();
    if (testFlash) {
        StartMuzzleLight(previewScale);
        if (holdLight) {
            heldMuzzleBounce_ = muzzleLightPulse_->bounce;
            muzzleBounceDebug_ = *heldMuzzleBounce_;
        }
    }

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
            {"forwardBias", light.forwardBias},
            {"areaRotationDegrees", {light.areaRotationDegrees.x, light.areaRotationDegrees.y, light.areaRotationDegrees.z}},
            {"indirectIntensity", light.indirectIntensity}, {"indirectRange", light.indirectRange},
            {"offset", {light.offset.x, light.offset.y, light.offset.z}}});
        auto& bounceDocument = document["bounce"];
        if (!bounceDocument.is_object()) bounceDocument = nlohmann::json::object();
        const auto& bounce = light.bounce;
        bounceDocument.update({{"enabled", bounce.enabled}, {"intensity", bounce.intensity}, {"range", bounce.range},
            {"rayCount", bounce.rayCount}, {"rayDistance", bounce.rayDistance}, {"maxLights", bounce.maxLights},
            {"surfaceOffset", bounce.surfaceOffset}, {"decay", bounce.decay}, {"occlusionEnabled", bounce.occlusionEnabled}});
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

void GameScene::DrawMuzzleLightingDebug(GameApp& app) const {
    const bool showBounce = muzzleLightSettings_.bounce.enabled && (showBounceRays_ || showBouncePoints_ || showBounceNormals_);
    RECT rect{};
    if ((!showMuzzleAnchor_ && !showMuzzleDirections_ && !showBounce) || !app.ImGui()->GetSceneImageRect(rect)) return;
    const auto& vp = camera_.GetViewProjectionMatrix();
    const ImVec2 lo{static_cast<float>(rect.left), static_cast<float>(rect.top)};
    const ImVec2 hi{static_cast<float>(rect.right), static_cast<float>(rect.bottom)};
    const auto toClip = [&](const Vector3& p) {
        const auto xyz = StagePoint(p, vp);
        const float w = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
        return Vector4{xyz.x, xyz.y, xyz.z, w};
    };
    const auto planes = [](const Vector4& p) {
        return std::array{p.w + p.x, p.w - p.x, p.w + p.y, p.w - p.y, p.z, p.w - p.z};
    };
    const auto toPixel = [&](const Vector4& p) {
        return ImVec2{lo.x + (1.0f + p.x / p.w) * 0.5f * (hi.x - lo.x),
            lo.y + (1.0f - p.y / p.w) * 0.5f * (hi.y - lo.y)};
    };
    const auto visible = [&](const Vector4& p) {
        const auto distances = planes(p);
        return p.w > 1e-5f && std::all_of(distances.begin(), distances.end(), [](float d) { return d >= 0.0f; });
    };
    auto* draw = ImGui::GetForegroundDrawList();
    const auto drawLine = [&](const Vector3& start, const Vector3& end, ImU32 color) {
        const auto a = planes(toClip(start));
        const auto b = planes(toClip(end));
        float first = 0.0f, last = 1.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] < 0.0f && b[i] < 0.0f) return;
            if (a[i] < 0.0f) first = std::max(first, a[i] / (a[i] - b[i]));
            if (b[i] < 0.0f) last = std::min(last, a[i] / (a[i] - b[i]));
        }
        if (first > last) return;
        const auto direction = end - start;
        const auto from = toClip(start + direction * first);
        const auto to = toClip(start + direction * last);
        if (from.w > 1e-5f && to.w > 1e-5f) draw->AddLine(toPixel(from), toPixel(to), color, 1.5f);
    };

    draw->PushClipRect(lo, hi, true);
    if (showMuzzleAnchor_) {
        const auto anchor = ResolveMuzzleAnchor(muzzleLightSettings_.offset);
        const auto clip = toClip(anchor.position);
        if (visible(clip)) {
            const auto pixel = toPixel(clip);
            const ImU32 color = anchor.obstructed ? IM_COL32(255, 165, 40, 255) : IM_COL32(80, 230, 255, 255);
            draw->AddCircle(pixel, 7.0f, color, 16, 2.0f);
            draw->AddText({pixel.x + 10.0f, pixel.y - 8.0f}, color,
                anchor.obstructed ? "Muzzle (clipped)" : "Muzzle");
        }
    }
    if (showMuzzleDirections_) {
        const auto& settings = muzzleLightSettings_;
        const auto& world = camera_.GetWorldMatrix();
        const auto origin = ResolveMuzzleAnchor(settings.offset).position;
        const auto cameraAxis = Forward(world);
        const auto lightAxis = Forward(MuzzleLightBasis(world, settings.mode, settings.areaRotationDegrees));
        constexpr ImU32 lightColor = IM_COL32(255, 195, 80, 255);
        constexpr ImU32 shotColor = IM_COL32(224, 130, 255, 255);
        constexpr ImU32 aimColor = IM_COL32(80, 230, 255, 255);
        drawLine(origin, origin + lightAxis * std::min(3.0f, settings.range), lightColor);
        if (muzzleShotPoseDebug_) {
            const auto& shot = *muzzleShotPoseDebug_;
            drawLine(shot.muzzle, shot.muzzle + shot.direction * 2.5f, shotColor);
        }
        const auto eye = camera_.GetTranslate();
        StageHit wall;
        const bool hit = level_.collision.Raycast(eye, cameraAxis, 25.0f, wall, 0.0f, true);
        const auto aim = eye + cameraAxis * (hit ? wall.distance : 3.0f);
        const auto clip = toClip(aim);
        if (visible(clip)) draw->AddCircle(toPixel(clip), 5.0f, aimColor, 12, 1.5f);
        if (hit) drawLine(aim, aim + wall.normal * 0.45f, aimColor);
    }
    if (showBounce) {
        const auto& sample = muzzleBounceDebug_;
        if (showBounceRays_) {
            for (size_t i = 0; i < sample.rayCount; ++i) {
                const auto& ray = sample.rays[i];
                drawLine(sample.origin, ray.end, ray.hit ? IM_COL32(90, 185, 255, 220) : IM_COL32(150, 150, 150, 155));
            }
        }
        for (size_t i = 0; i < sample.count; ++i) {
            const auto& point = sample.points[i];
            if (showBouncePoints_) {
                const auto clip = toClip(point.position);
                if (visible(clip)) draw->AddCircleFilled(toPixel(clip), 4.0f, IM_COL32(255, 195, 80, 255));
            }
            if (showBounceNormals_)
                drawLine(point.position, point.position + point.normal * 0.4f, IM_COL32(85, 245, 140, 255));
        }
    }
    draw->PopClipRect();
}
#endif
