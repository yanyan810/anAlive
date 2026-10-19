#pragma once
#include <nlohmann/json.hpp>
#include "Vector3.h"
#include "EnemyAsset.h"
#include "EnemySenses.h"
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <stdexcept>

enum class EnemyType { Normal, Ranged, Fast, Tank, Bomber };
inline const char* EnemyTypeName(EnemyType type) {
    switch (type) {
    case EnemyType::Normal: return "Normal";
    case EnemyType::Ranged: return "Ranged";
    case EnemyType::Fast: return "Fast";
    case EnemyType::Tank: return "Tank";
    default: return "Bomber";
    }
}
struct EnemyTypeMarker {
    bool enabled=false;
    Vector3 offset{0,2.8f,0}; // Enemy model-local coordinates, before its effective scale/rotation.
    Vector3 scale{.15f,.15f,.15f};
    Vector3 color{1,1,1};
};
struct EnemyDefinition {
    std::string id = "normal", displayName = "Normal";
    EnemyType type = EnemyType::Normal;
    std::shared_ptr<const EnemyAsset> partAsset;
    float detectionRange=20, moveSpeed=2.5f, attackRange=1.5f, attackDamage=10, attackInterval=1;
    EnemySenses senses{};
    float hpMultiplier=1, preferredRange=12, minRange=7, maxRange=16;
    float projectileSpeed=12, projectileRadius=.2f, projectileLifetime=5;
    float explosionRadius=4, explosionDamage=25, fuseTime=1.2f;
    float bombGravity=9.8f;
    // World-space movement cylinder dimensions; independent of visualScale and part hitboxes.
    float collisionRadius=.55f;
    float collisionHeight=4.96f;
    Vector3 visualScaleMultiplier{1,1,1};
    EnemyTypeMarker typeMarker{};
    Vector3 VisualScale(const Vector3& base) const {
        return {base.x*visualScaleMultiplier.x,base.y*visualScaleMultiplier.y,base.z*visualScaleMultiplier.z};
    }
    bool IsRanged() const { return type==EnemyType::Ranged; }
};
class EnemyDefinitions {
public:
    bool Load(const std::string& path) {
        try {
            std::ifstream file(path);
            if (!file) throw std::runtime_error("Cannot open " + path);
            nlohmann::json data; file >> data;
            if (!data.at("enemies").is_array()) throw std::runtime_error("enemies must be an array");
            std::map<std::string,EnemyDefinition> next;
            std::map<std::string,std::shared_ptr<const EnemyAsset>> assets;
            for (const auto& item : data.at("enemies")) {
                EnemyDefinition d;
                d.id=item.at("id").get<std::string>();
                d.displayName=item.value("displayName",d.id);
                const auto reference=item.value("partAsset",std::string{});
                const auto assetPath=reference.empty()?std::string{}:ResolveEnemyResource(reference,path);
                if (!assetPath.empty()) {
                    auto& asset=assets[assetPath];
                    if (!asset) asset=EnemyAsset::Load(assetPath);
                    d.partAsset=asset;
                }
                const auto type=item.at("type").get<std::string>();
                bool known=false;
                for (auto t : {EnemyType::Normal,EnemyType::Ranged,EnemyType::Fast,EnemyType::Tank,EnemyType::Bomber})
                    if (type==EnemyTypeName(t)) { d.type=t; known=true; }
                if (!known) throw std::runtime_error("Unknown enemy type: " + type);
                const auto number=[&](const char* key,float& value,bool positive=false) {
                    value=item.value(key,value);
                    if (!std::isfinite(value) || (positive ? value<=0 : value<0))
                        throw std::runtime_error(d.id + ": invalid " + key);
                };
                number("collisionRadius",d.collisionRadius,true); number("collisionHeight",d.collisionHeight,true);
                number("detectionRange",d.detectionRange); number("moveSpeed",d.moveSpeed);
                d.senses.viewDistance=d.detectionRange; // Legacy key remains a fallback.
                if (item.contains("senses")) {
                    const auto& senses=item.at("senses");
                    if (!senses.is_object()) throw std::runtime_error("senses must be an object");
                    const auto senseNumber=[&](const char* key,float& value,float maximum) {
                        value=senses.value(key,value);
                        if (!std::isfinite(value) || value<0 || value>maximum)
                            throw std::runtime_error(d.id+": invalid senses."+key);
                    };
                    senseNumber("viewDistance",d.senses.viewDistance,1000);
                    senseNumber("fieldOfView",d.senses.fieldOfView,360);
                    senseNumber("detectionTime",d.senses.detectionTime,60);
                    senseNumber("loseSightTime",d.senses.loseSightTime,60);
                    senseNumber("investigateSpeedMultiplier",d.senses.investigateSpeedMultiplier,5);
                    senseNumber("searchDuration",d.senses.searchDuration,60);
                    senseNumber("searchRadius",d.senses.searchRadius,100);
                    senseNumber("searchSpeedMultiplier",d.senses.searchSpeedMultiplier,5);
                    senseNumber("investigateUnreachableTime",d.senses.investigateUnreachableTime,60);
                    senseNumber("memoryRetentionTime",d.senses.memoryRetentionTime,60);
                    senseNumber("memoryRecordInterval",d.senses.memoryRecordInterval,10);
                    senseNumber("memoryRecordDistance",d.senses.memoryRecordDistance,20);
                    senseNumber("searchDirectionPriority",d.senses.searchDirectionPriority,1);
                    senseNumber("arrivalDistance",d.senses.arrivalDistance,10);
                    senseNumber("wanderRadius",d.senses.wanderRadius,100);
                    senseNumber("wanderSpeedMultiplier",d.senses.wanderSpeedMultiplier,5);
                    senseNumber("wanderWaitTime",d.senses.wanderWaitTime,60);
                }
                number("attackRange",d.attackRange); number("attackDamage",d.attackDamage);
                number("attackInterval",d.attackInterval,true); number("hpMultiplier",d.hpMultiplier,true);
                number("minRange",d.minRange); number("preferredRange",d.preferredRange); number("maxRange",d.maxRange);
                number("projectileSpeed",d.projectileSpeed,true); number("projectileRadius",d.projectileRadius,true);
                number("projectileLifetime",d.projectileLifetime,true); number("explosionRadius",d.explosionRadius,true);
                number("explosionDamage",d.explosionDamage); number("fuseTime",d.fuseTime); number("bombGravity",d.bombGravity,true);
                const auto vector=[](const nlohmann::json& value,const char* key,bool positive,bool color=false) {
                    if (!value.is_array() || value.size()!=3) throw std::runtime_error(std::string(key)+": expected 3 components");
                    Vector3 result{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
                    for (float component : {result.x,result.y,result.z})
                        if (!std::isfinite(component) || (positive && component<=0) || (color && (component<0 || component>1)))
                            throw std::runtime_error(std::string(key)+": invalid component");
                    return result;
                };
                if (item.contains("visualScale")) d.visualScaleMultiplier=vector(item.at("visualScale"),"visualScale",true);
                if (item.contains("typeMarker")) {
                    const auto& marker=item.at("typeMarker");
                    if (!marker.is_object()) throw std::runtime_error("typeMarker must be an object");
                    d.typeMarker.enabled=marker.value("enabled",false);
                    if (marker.contains("offset")) d.typeMarker.offset=vector(marker.at("offset"),"typeMarker.offset",false);
                    if (marker.contains("scale")) d.typeMarker.scale=vector(marker.at("scale"),"typeMarker.scale",true);
                    if (marker.contains("color")) d.typeMarker.color=vector(marker.at("color"),"typeMarker.color",false,true);
                }
                if (d.minRange>d.maxRange) throw std::runtime_error("minRange exceeds maxRange: " + d.id);
                if (d.id.empty() || !next.emplace(d.id,d).second) throw std::runtime_error("Empty/duplicate enemy id: " + d.id);
            }
            if (!next.count("normal")) throw std::runtime_error("Missing normal fallback");
            definitions_=std::move(next); error_.clear(); return true;
        } catch (const std::exception& e) { error_=e.what(); return false; }
    }
    const EnemyDefinition* Find(const std::string& id) const {
        const auto it=definitions_.find(id); return it==definitions_.end() ? nullptr : &it->second;
    }
    const std::map<std::string,EnemyDefinition>& All() const { return definitions_; }
    const std::string& Error() const { return error_; }
private:
    std::map<std::string,EnemyDefinition> definitions_{{"normal",EnemyDefinition{}}};
    std::string error_;
};
