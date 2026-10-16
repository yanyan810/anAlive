#pragma once
#include "StageShutter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <set>
#include <stdexcept>

class StageLoader {
public:
    std::string id, model;
    Vector3 playerPosition{3,0,-6}, playerRotation{};
    StageWorld collision;
    StageShutterSystem shutters;
    const std::string& Error() const { return error_; }
    bool Load(const std::string& path) {
        try {
            std::ifstream input(path);
            if (!input) throw std::runtime_error("Cannot open stage: "+path);
            const auto data=nlohmann::json::parse(input);
            if (data.at("version")!=1) throw std::runtime_error("Unsupported stage version");
            StageLoader next;
            next.id=data.at("stage").at("id").get<std::string>();
            next.model=data.at("stage").at("model").get<std::string>();
            next.collision.playerGravity=data.at("stage").value("playerGravity",false);
            const std::filesystem::path modelPath(next.model);
            if (next.id.empty() || next.model.empty() || modelPath.is_absolute() || modelPath.extension()!=".gltf")
                throw std::runtime_error("Invalid stage id/model");
            for (const auto& part : modelPath) if (part=="..") throw std::runtime_error("Model path must stay inside resources");
            const auto vector=[](const nlohmann::json& value) {
                if (!value.is_array() || value.size()!=3) throw std::runtime_error("Expected 3-vector");
                Vector3 p{value[0].get<float>(),value[1].get<float>(),value[2].get<float>()};
                for (float n : {p.x,p.y,p.z}) if (!std::isfinite(n) || std::abs(n)>1e6f) throw std::runtime_error("Invalid stage number");
                return p;
            };
            next.playerPosition=vector(data.at("playerSpawn").at("position"));
            next.playerRotation=vector(data.at("playerSpawn").at("rotation"));
            std::set<std::string> ids;
            for (const char* name : {"colliders","spawnPoints","spawnTriggers","weaponSpawnPoints","goalTriggers"}) {
                if (std::string(name)=="spawnTriggers" && !data.contains(name)) continue;
                if (!data.at(name).is_array()) throw std::runtime_error(std::string(name)+" must be an array");
                for (const auto& item : data.at(name)) {
                    const auto key=item.at("id").get<std::string>();
                    if (key.empty() || !ids.insert(key).second) throw std::runtime_error("Empty/duplicate stage ID: "+key);
                }
            }
            if (data.contains("spawnGroups")) {
                if (!data.at("spawnGroups").is_array()) throw std::runtime_error("spawnGroups must be an array");
                size_t index = 0;
                for (const auto& group : data.at("spawnGroups")) {
                    const auto key = group.value("id", "SpawnGroup_" + std::to_string(++index));
                    if (key.empty() || !ids.insert(key).second) throw std::runtime_error("Empty/duplicate stage ID: "+key);
                }
            }
            for (const auto& item : data.at("colliders")) {
                StageCollider c;
                c.id=item.at("id").get<std::string>();
                c.walkable=item.value("walkable",false);
                c.playerOnly=item.value("playerOnly",false);
                c.position=vector(item.at("position")); c.rotation=vector(item.at("rotation")); c.scale=vector(item.at("scale"));
                c.local={vector(item.at("localBounds").at("min")),vector(item.at("localBounds").at("max"))};
                if (c.scale.x<=1e-5f || c.scale.y<=1e-5f || c.scale.z<=1e-5f ||
                    c.local.min.x>=c.local.max.x || c.local.min.y>=c.local.max.y || c.local.min.z>=c.local.max.z)
                    throw std::runtime_error("Degenerate collider: "+c.id);
                c.world=Matrix4x4::MakeAffineMatrix(c.scale,c.rotation,c.position);
                c.inverse=Matrix4x4::Inverse(c.world);
                next.collision.colliders.push_back(c);
            }
            if (data.contains("shutters")) {
                if (!data.at("shutters").is_array()) throw std::runtime_error("shutters must be an array");
                for (const auto& item : data.at("shutters")) {
                    ShutterDefinition s;
                    s.id=item.at("id").get<std::string>();
                    if (s.id.empty() || !ids.insert(s.id).second) throw std::runtime_error("Duplicate/empty shutter ID");
                    s.model=item.at("model").get<std::string>();
                    const std::filesystem::path asset(s.model);
                    if (asset.is_absolute() || asset.extension()!=".gltf") throw std::runtime_error("Invalid shutter model");
                    for (const auto& part : asset) if (part=="..") throw std::runtime_error("Invalid shutter asset path");
                    s.closedPosition=vector(item.at("closedPosition"));
                    s.buttonPosition=vector(item.at("buttonPosition"));
                    s.openDuration=item.value("openDuration",30.0);
                    s.openDistance=item.value("openDistance",3.2f);
                    s.interactionDistance=item.value("interactionDistance",2.0f);
                    if (!std::isfinite(s.openDuration) || s.openDuration<=0 || s.openDuration>1e6 ||
                        !std::isfinite(s.openDistance) || s.openDistance<=0 || s.openDistance>1e6 ||
                        !std::isfinite(s.interactionDistance) || s.interactionDistance<=0 || s.interactionDistance>1e6)
                        throw std::runtime_error("Invalid shutter timing/distance: "+s.id);
                    const auto& box=item.at("collider");
                    s.collider.id=s.id;
                    s.collider.position=vector(box.at("position")); s.collider.rotation=vector(box.at("rotation"));
                    s.collider.scale=vector(box.at("scale"));
                    s.collider.local={vector(box.at("localBounds").at("min")),vector(box.at("localBounds").at("max"))};
                    if (s.collider.scale.x<=1e-5f || s.collider.scale.y<=1e-5f || s.collider.scale.z<=1e-5f ||
                        s.collider.local.min.x>=s.collider.local.max.x || s.collider.local.min.y>=s.collider.local.max.y ||
                        s.collider.local.min.z>=s.collider.local.max.z) throw std::runtime_error("Degenerate shutter collider");
                    const auto sounds=item.value("sounds",nlohmann::json::object());
                    s.switchSound=sounds.value("switch",""); s.startSound=sounds.value("start","");
                    s.motorSound=sounds.value("motor",""); s.completeSound=sounds.value("complete","");
                    const auto markers=item.value("progressEvents",nlohmann::json::array());
                    if (!markers.is_array()) throw std::runtime_error("Shutter progressEvents must be an array");
                    for (const auto& marker : markers) {
                        const float progress=marker.at("progress").get<float>();
                        const auto event=marker.at("event").get<std::string>();
                        if (!std::isfinite(progress) || progress<=0 || progress>=1 || event.empty())
                            throw std::runtime_error("Invalid shutter progress event");
                        s.progressEvents.push_back({progress,event});
                    }
                    std::stable_sort(s.progressEvents.begin(),s.progressEvents.end(),[](const auto& a,const auto& b){return a.progress<b.progress;});
                    s.colliderIndex=next.collision.colliders.size();
                    next.collision.colliders.push_back(s.collider);
                    next.shutters.shutters.push_back({std::move(s)});
                }
                next.shutters.Reset(next.collision);
            }
            *this=std::move(next); return true;
        } catch (const std::exception& e) { error_=e.what(); return false; }
    }
    // Check assets before Object3d's importer (which asserts on missing files).
    bool ValidateAssets(const std::string& resources="resources") {
        try {
            std::vector<std::string> models{model};
            for (const auto& shutter : shutters.shutters) models.push_back(shutter.definition.model);
            for (const auto& name : models) {
            const auto path=std::filesystem::path(resources)/name;
            std::ifstream input(path);
            if (!input) throw std::runtime_error("Missing stage model: "+path.string());
            const auto gltf=nlohmann::json::parse(input);
            if (!gltf.contains("meshes") || gltf.at("meshes").empty()) throw std::runtime_error("Stage has no meshes");
            for (const char* field : {"buffers","images"}) if (gltf.contains(field)) for (const auto& asset : gltf.at(field)) {
                if (!asset.contains("uri")) continue;
                const auto uri=asset.at("uri").get<std::string>();
                if (!uri.starts_with("data:") && !std::filesystem::is_regular_file(path.parent_path()/uri))
                    throw std::runtime_error("Missing stage dependency: "+uri);
            }
            }
            error_.clear(); return true;
        } catch (const std::exception& e) { error_=e.what(); return false; }
    }
private:
    std::string error_;
};
