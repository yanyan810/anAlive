#pragma once
#include "StageShutter.h"
#include "StageDoor.h"
#include "CameraSequence.h"
#include "CameraSequenceOverrides.h"
#include "PlayerVisualSettings.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <set>
#include <stdexcept>

class StageLoader {
public:
    struct RemovableMesh { size_t colliderIndex; std::string meshNode; bool breakOnBullet; };
    struct Transfer {
        Vector3 position, size, arrivalPosition, arrivalRotation;
        std::string spawnGroup;
        bool Contains(const Vector3& feet) const {
            const auto d=feet-position;
            return std::abs(d.x)<=size.x*.5f && std::abs(d.y)<=size.y*.5f && std::abs(d.z)<=size.z*.5f;
        }
    };
    std::string id, model;
    Vector3 playerPosition{3,0,-6}, playerRotation{};
    StageWorld collision;
    StageShutterSystem shutters;
    StageDoorSystem doors;
    std::vector<RemovableMesh> removableMeshes;
    std::optional<Transfer> transfer;
    std::optional<float> fallDeathY;
    std::vector<CameraSequenceDefinition> cameraSequences;
    std::vector<CameraSequenceDefinition> blenderCameraSequences;
    CameraSequenceOverrides cameraOverrides;
    std::string cameraOverrideWarning;
    std::string cameraWarning;
    PlayerVisualSettings playerVisual;
    std::string playerVisualWarning;
    const std::string& Error() const { return error_; }
    bool Load(const std::string& path, const nlohmann::json* draft = nullptr) {
        try {
            nlohmann::json data;
            if (draft) data=*draft;
            else {
                std::ifstream input(path);
                if (!input) throw std::runtime_error("Cannot open stage: "+path);
                data=nlohmann::json::parse(input);
            }
            if (data.at("version")!=1) throw std::runtime_error("Unsupported stage version");
            StageLoader next;
            try { next.playerVisual=PlayerVisualSettings::FromStage(data); }
            catch (const std::exception& error) { next.playerVisualWarning=error.what(); }
            // Optional presentation errors must not invalidate playable stage data.
            try { next.cameraSequences=CameraSequence::Parse(data); }
            catch (const std::exception& error) { next.cameraWarning=error.what(); }
            next.id=data.at("stage").at("id").get<std::string>();
            next.blenderCameraSequences=next.cameraSequences;
            if (next.cameraOverrides.Load(CameraSequenceOverrides::PathFor(path),next.id)) {
                auto merged=CameraSequenceOverrides::Merge(next.cameraSequences,next.cameraOverrides.sequences);
                if (merged.size()<=128) next.cameraSequences=std::move(merged);
                else next.cameraOverrideWarning="Camera overrides exceed the 128 Sequence ID limit";
            } else next.cameraOverrideWarning=next.cameraOverrides.error;
            next.model=data.at("stage").at("model").get<std::string>();
            next.collision.playerGravity=data.at("stage").value("playerGravity",false);
            next.collision.fallDeathDistance=data.at("stage").value("fallDeathDistance",0.0f);
            if (!std::isfinite(next.collision.fallDeathDistance) || next.collision.fallDeathDistance<0 ||
                next.collision.fallDeathDistance>1e6f)
                throw std::runtime_error("Invalid fallDeathDistance");
            if (data.at("stage").contains("fallDeathY")) {
                next.fallDeathY=data.at("stage").at("fallDeathY").get<float>();
                if (!std::isfinite(*next.fallDeathY) || std::abs(*next.fallDeathY)>1e6f)
                    throw std::runtime_error("Invalid fallDeathY");
            }
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
            const auto hasManualGroup=[&](const std::string& group) {
                return !group.empty() && std::any_of(data.at("spawnPoints").begin(),data.at("spawnPoints").end(),[&](const auto& point) {
                    return point.value("activation",std::string{})=="Manual" && point.value("groupId",std::string{})==group;
                });
            };
            if (data.contains("transfer")) {
                const auto& item=data.at("transfer");
                const auto key=item.at("id").get<std::string>();
                if (key.empty() || !ids.insert(key).second) throw std::runtime_error("Empty/duplicate transfer ID");
                Transfer t{vector(item.at("position")),vector(item.at("size")),
                    vector(item.at("arrivalPosition")),vector(item.at("arrivalRotation")),item.value("spawnGroup",std::string{})};
                if (t.size.x<=0 || t.size.y<=0 || t.size.z<=0 || t.Contains(t.arrivalPosition) ||
                    (!t.spawnGroup.empty() && !hasManualGroup(t.spawnGroup)))
                    throw std::runtime_error("Invalid stage transfer: "+key);
                next.transfer=std::move(t);
            }
            const auto readCollider=[&](const nlohmann::json& item) {
                StageCollider c;
                c.id=item.at("id").get<std::string>();
                c.walkable=item.value("walkable",false);
                c.playerOnly=item.value("playerOnly",false);
                c.blocksSight=item.value("blocksSight",true);
                c.position=vector(item.at("position")); c.rotation=vector(item.at("rotation")); c.scale=vector(item.at("scale"));
                c.local={vector(item.at("localBounds").at("min")),vector(item.at("localBounds").at("max"))};
                if (c.scale.x<=1e-5f || c.scale.y<=1e-5f || c.scale.z<=1e-5f ||
                    c.local.min.x>=c.local.max.x || c.local.min.y>=c.local.max.y || c.local.min.z>=c.local.max.z)
                    throw std::runtime_error("Degenerate collider: "+c.id);
                c.UpdateMatrices();
                return c;
            };
            std::set<std::string> removableNodes;
            for (const auto& item : data.at("colliders")) {
                auto collider=readCollider(item);
                const bool bullet=item.contains("breakableMesh");
                if (bullet || item.contains("eventMesh")) {
                    auto node=item.at(bullet ? "breakableMesh" : "eventMesh").get<std::string>();
                    if (node.empty() || !removableNodes.insert(node).second || collider.walkable || collider.playerOnly ||
                        (bullet && item.contains("eventMesh")))
                        throw std::runtime_error("Invalid removable mesh: "+collider.id);
                    next.removableMeshes.push_back({next.collision.colliders.size(),std::move(node),bullet});
                }
                next.collision.colliders.push_back(std::move(collider));
            }
            if (data.contains("doors")) {
                if (!data.at("doors").is_array()) throw std::runtime_error("doors must be an array");
                for (const auto& item : data.at("doors")) {
                    StageDoor door;
                    door.closed=readCollider(item);
                    const auto& key=door.closed.id;
                    if (key.empty() || !ids.insert(key).second) throw std::runtime_error("Empty/duplicate door ID: "+key);
                    door.openAngle=item.at("openAngle").get<float>();
                    door.initiallyOpen=item.value("initiallyOpen",false);
                    if (!std::isfinite(door.openAngle) || std::abs(door.openAngle)<.17453f || std::abs(door.openAngle)>2.09440f ||
                        door.closed.rotation.x!=0 || door.closed.rotation.z!=0 || door.closed.walkable || door.closed.playerOnly)
                        throw std::runtime_error("Invalid hinged door: "+key);
                    door.colliderIndex=next.collision.colliders.size();
                    next.collision.colliders.push_back(door.closed);
                    next.doors.doors.push_back(std::move(door));
                }
                next.doors.Reset(next.collision);
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
                    s.defendBeforeOpening=item.value("defendBeforeOpening",false);
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
                    if (item.contains("enemyWaves")) {
                        const auto& waves=item.at("enemyWaves");
                        s.enemyWaveGroup=waves.at("groupId").get<std::string>();
                        s.enemyWaveInterval=waves.at("interval").get<double>();
                        if (s.enemyWaveGroup.find_first_not_of(" \t\r\n")==std::string::npos ||
                            !std::isfinite(s.enemyWaveInterval) || s.enemyWaveInterval<=0 ||
                            s.openDuration/s.enemyWaveInterval>1000)
                            throw std::runtime_error("Invalid shutter enemy wave group/interval");
                        if (!hasManualGroup(s.enemyWaveGroup)) throw std::runtime_error("Shutter enemy wave group has no Manual spawn points");
                    }
                    const auto markers=item.value("progressEvents",nlohmann::json::array());
                    if (!markers.is_array()) throw std::runtime_error("Shutter progressEvents must be an array");
                    for (const auto& marker : markers) {
                        const float progress=marker.at("progress").get<float>();
                        const auto event=marker.at("event").get<std::string>();
                        if (!std::isfinite(progress) || progress<=0 || progress>=1 || event.empty())
                            throw std::runtime_error("Invalid shutter progress event");
                        ShutterProgressEvent command{progress,event,marker.value("spawnGroup",std::string{}),{}};
                        if (!command.spawnGroup.empty() && !hasManualGroup(command.spawnGroup))
                            throw std::runtime_error("Shutter event references missing Manual spawn group: "+command.spawnGroup);
                        if (marker.contains("openCollider")) {
                            const auto target=marker.at("openCollider").get<std::string>();
                            const auto panel=std::find_if(next.removableMeshes.begin(),next.removableMeshes.end(),[&](const auto& mesh) {
                                return !mesh.breakOnBullet && next.collision.colliders[mesh.colliderIndex].id==target;
                            });
                            if (panel==next.removableMeshes.end()) throw std::runtime_error("Shutter event references missing event mesh: "+target);
                            command.openCollider=panel->colliderIndex;
                        }
                        s.progressEvents.push_back(std::move(command));
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
                if (name==model && !removableMeshes.empty()) {
                    const auto& nodes=gltf.at("nodes");
                    for (const auto& mesh : removableMeshes) {
                        if (std::count_if(nodes.begin(),nodes.end(),[&](const auto& node) {
                            return node.contains("mesh") && node.value("name",std::string{})==mesh.meshNode;
                        })!=1) throw std::runtime_error("Missing/ambiguous removable mesh: "+mesh.meshNode);
                    }
                }
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
