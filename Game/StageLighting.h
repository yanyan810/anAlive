#pragma once
#include "Object3dLight.h"
#include <nlohmann/json.hpp>
#include <filesystem>

// Per-level configuration for the engine's shared scene lighting.
struct StageLighting {
    enum class Preset { Standard, Hotel, Outdoor, DarkRoom, Custom };
    Preset preset = Preset::Standard;
    bool ambientEnabled = true;
    bool directionalEnabled = true;
    Object3dLight::Environment environment{
        {1.0f, 1.0f, 1.0f}, 0.12f, {1.0f, 1.0f, 1.0f}, 0.88f,
        {0.3f, -1.0f, 0.5f}, Object3dLight::DiffuseMode::Lambert, 0.2f
    };

    struct Shadow {
        bool enabled = false;
        bool roomCastsShadows = false;
        bool followCamera = true;
        float strength = 0.65f;
        float depthBias = 0.0015f;
        Vector3 center{};
        float viewSize = 36.0f;
        float lightDistance = 36.0f;
        float nearClip = 0.1f;
        float farClip = 100.0f;
    } shadow;

    static std::filesystem::path PathFor(std::filesystem::path stagePath);
    static StageLighting FromPreset(Preset preset);
    static StageLighting FromJson(const nlohmann::json& data);
    static StageLighting Load(const std::filesystem::path& path);
    nlohmann::json ToJson() const;
    void Apply(Object3dLight& light) const;
};
