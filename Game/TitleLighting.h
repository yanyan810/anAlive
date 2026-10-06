#pragma once
#include "Object3dLight.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <numbers>
#include <stdexcept>

// Title-only tuning data. Independent of Blender's exported layout.
struct TitleLighting {
    struct Flicker {
        bool enabled=false;
        float minInterval=2.5f, maxInterval=6.0f;
        float minOffTime=.04f, maxOffTime=.12f;
        int flashes=3;
        float offBrightness=0;
    };
    struct Spot {
        Vector3 position{}, direction{0,-1,0}, color{1,1,1};
        float intensity=1, distance=22, decay=1.5f;
        float outerAngle=40, innerAngle=25; // Cone half-angles, in degrees.
        float specularStrength=.12f; // Keep the dark walls from reflecting brighter than the targets.
        Flicker flicker;
    };
    Vector3 direction{.25f,-1,.5f}, color{1,1,1};
    float intensity=.22f;
    float ambientIntensity=.08f; // Minimum diffuse fill, independent of direct light and shadows.
    struct Shadow {
        bool enabled=true;
        float strength=.6f, bias=.0008f;
        bool roomCastsShadows=false;
        Vector3 center{0,2,8};
        float viewSize=40, lightDistance=40, nearClip=.1f, farClip=100;
    } shadow;
    std::array<Spot,3> spots{{
        {{-3.2f,9,6},{0,-2.9f,7.7f},{1,1,1},.9f,24,1.5f,46,32,.12f,{true}},
        {{-6.5f,4.8f,6.5f},{2.5f,-3.8f,4},{1,1,1},1.1f,20,1.5f,23,15},
        {{5,5.4f,1.5f},{0,-3.7f,9.1f},{1,1,1},2.2f,26,1.5f,32,25,.12f,{true}}
    }};
    inline static constexpr std::array<const char*,3> names{"UNALIVE","Enemy","GAME START"};

    static Vector3 Unit(const Vector3& v) {
        const float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
        return length>1e-5f ? v*(1/length) : Vector3{0,-1,0};
    }
    void Apply(Object3dLight& light) const {
        light.SetDirectionalLightColor({color.x,color.y,color.z,1});
        light.SetDirectionalLightDirection(Unit(direction));
        light.SetDirectionalLightIntensity(intensity);
        light.SetAmbientFillIntensity(ambientIntensity);
        light.SetPointLightIntensity(0);
        Object3dLight::SpotLights gpu{};
        for (size_t i=0;i<spots.size();++i) {
            const auto& s=spots[i];
            gpu[i]={ {s.color.x,s.color.y,s.color.z,1}, s.position, s.intensity,
                Unit(s.direction), s.distance, s.decay,
                std::cos(s.outerAngle*std::numbers::pi_v<float>/180),
                std::cos(s.innerAngle*std::numbers::pi_v<float>/180), s.specularStrength };
        }
        light.SetSpotLights(gpu);
    }
    nlohmann::json ToJson() const {
        const auto vec=[](const Vector3& v) { return nlohmann::json::array({v.x,v.y,v.z}); };
        nlohmann::json data{{"version",1},{"directional",{
            {"direction",vec(direction)},{"color",vec(color)},{"intensity",intensity},{"ambientIntensity",ambientIntensity}}}};
        data["shadow"]={{"enabled",shadow.enabled},{"strength",shadow.strength},{"depthBias",shadow.bias},
            {"center",vec(shadow.center)},{"viewSize",shadow.viewSize},{"lightDistance",shadow.lightDistance},
            {"nearClip",shadow.nearClip},{"farClip",shadow.farClip},{"roomCastsShadows",shadow.roomCastsShadows}};
        for (size_t i=0;i<spots.size();++i) {
            const auto& s=spots[i];
            data["spots"][names[i]]={{"position",vec(s.position)},{"direction",vec(s.direction)},
                {"color",vec(s.color)},{"intensity",s.intensity},{"distance",s.distance},
                {"decay",s.decay},{"outerAngleDegrees",s.outerAngle},{"innerAngleDegrees",s.innerAngle},
                {"specularStrength",s.specularStrength}};
            const auto& f=s.flicker;
            data["spots"][names[i]]["flicker"]={{"enabled",f.enabled},{"minInterval",f.minInterval},{"maxInterval",f.maxInterval},
                {"minOffTime",f.minOffTime},{"maxOffTime",f.maxOffTime},{"flashes",f.flashes},{"offBrightness",f.offBrightness}};
        }
        return data;
    }
    static TitleLighting FromJson(const nlohmann::json& data) {
        if (data.at("version")!=1) throw std::runtime_error("Unsupported title lighting version");
        const auto number=[](const nlohmann::json& value,float lo,float hi) {
            if (!value.is_number()) throw std::runtime_error("Light value must be a number");
            const float n=value.get<float>();
            if (!std::isfinite(n) || n<lo || n>hi) throw std::runtime_error("Light value outside valid range");
            return n;
        };
        const auto vec=[&](const nlohmann::json& value,float lo,float hi) {
            if (!value.is_array() || value.size()!=3) throw std::runtime_error("Light requires a 3-vector");
            return Vector3{number(value[0],lo,hi),number(value[1],lo,hi),number(value[2],lo,hi)};
        };
        const auto axis=[&](const nlohmann::json& value) {
            auto v=vec(value,-1e6f,1e6f);
            if (v.x*v.x+v.y*v.y+v.z*v.z<1e-10f) throw std::runtime_error("Light direction must be nonzero");
            return v;
        };
        TitleLighting result;
        const auto& dir=data.at("directional");
        result.direction=axis(dir.at("direction"));
        result.color=vec(dir.at("color"),0,1);
        result.intensity=number(dir.at("intensity"),0,10);
        if (dir.contains("ambientIntensity")) result.ambientIntensity=number(dir.at("ambientIntensity"),0,1);
        if (data.contains("shadow")) {
            const auto& json=data.at("shadow");
            auto& s=result.shadow;
            s.enabled=json.at("enabled").get<bool>();
            if (json.contains("roomCastsShadows")) s.roomCastsShadows=json.at("roomCastsShadows").get<bool>();
            s.strength=number(json.at("strength"),0,1);
            s.bias=number(json.at("depthBias"),0,.02f);
            s.center=vec(json.at("center"),-1e6f,1e6f);
            s.viewSize=number(json.at("viewSize"),1,200);
            s.lightDistance=number(json.at("lightDistance"),1,200);
            s.nearClip=number(json.at("nearClip"),.01f,999.9f);
            s.farClip=number(json.at("farClip"),s.nearClip+.1f,1000);
        }
        const auto& spots=data.at("spots");
        if (!spots.is_object() || spots.size()!=3) throw std::runtime_error("Title requires three named spot lights");
        for (size_t i=0;i<result.spots.size();++i) {
            const auto& json=spots.at(names[i]);
            auto& s=result.spots[i];
            s.position=vec(json.at("position"),-1e6f,1e6f);
            s.direction=axis(json.at("direction"));
            s.color=vec(json.at("color"),0,1);
            s.intensity=number(json.at("intensity"),0,10);
            s.distance=number(json.at("distance"),.1f,1000);
            s.decay=number(json.at("decay"),.1f,8);
            s.outerAngle=number(json.at("outerAngleDegrees"),1,89);
            s.innerAngle=number(json.at("innerAngleDegrees"),0,s.outerAngle-.1f);
            if (json.contains("specularStrength")) s.specularStrength=number(json.at("specularStrength"),0,1);
            s.flicker=Flicker{}; // Old tuning files keep their original steady lighting.
            if (json.contains("flicker")) {
                const auto& flicker=json.at("flicker");
                auto& f=s.flicker;
                f.enabled=flicker.at("enabled").get<bool>();
                f.minInterval=number(flicker.at("minInterval"),.2f,20);
                f.maxInterval=number(flicker.at("maxInterval"),f.minInterval,20);
                f.minOffTime=number(flicker.at("minOffTime"),.01f,.4f);
                f.maxOffTime=number(flicker.at("maxOffTime"),f.minOffTime,.4f);
                if (!flicker.at("flashes").is_number_integer()) throw std::runtime_error("Flicker flashes must be an integer");
                f.flashes=flicker.at("flashes").get<int>();
                if (f.flashes<1 || f.flashes>6) throw std::runtime_error("Flicker flashes outside valid range");
                f.offBrightness=number(flicker.at("offBrightness"),0,1);
            }
        }
        return result;
    }
    static TitleLighting Load(const std::string& path) {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Cannot open title lighting: "+path);
        return FromJson(nlohmann::json::parse(file));
    }
};
