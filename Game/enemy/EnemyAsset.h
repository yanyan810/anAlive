#pragma once
#include "EnemyParts.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>

// Resource references are game-root relative. Walking source ancestors also supports
// tools/tests opening the same asset from a generated working directory.
inline std::string ResolveEnemyResource(const std::string& reference,const std::filesystem::path& source) {
    const std::filesystem::path requested(reference);
    if (std::filesystem::is_regular_file(requested)) return std::filesystem::relative(std::filesystem::weakly_canonical(requested)).generic_string();
    auto base=std::filesystem::absolute(source).parent_path();
    while (!base.empty()) {
        const auto candidate=base/requested;
        if (std::filesystem::is_regular_file(candidate)) return std::filesystem::relative(std::filesystem::weakly_canonical(candidate)).generic_string();
        const auto parent=base.parent_path();
        if (parent==base) break;
        base=parent;
    }
    throw std::runtime_error("Missing enemy resource: "+reference);
}
// Immutable CPU asset; instances copy only HP/configuration and share triangle storage.
struct EnemyAsset {
    std::string path,texture="resources/white1x1.png";
    EnemyParts defaults;
    EnemyParts Instantiate(float multiplier=1) const {
        auto result=defaults;
        for (auto& part:result) part.hp=part.maxHp*=multiplier;
        for (auto& group:result.hpGroups) group.hp=group.maxHp*=multiplier;
        return result;
    }
    static std::shared_ptr<const EnemyAsset> Load(const std::filesystem::path& path) {
        const auto utf8Path=path.generic_u8string();
        const std::string pathText(utf8Path.begin(),utf8Path.end());
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open EnemyAsset: "+pathText);
        nlohmann::json data; input>>data;
        auto asset=std::make_shared<EnemyAsset>(); asset->path=pathText;
        if (data.at("version")!=1 || data.at("coordinateSystem")!="yanengine")
            throw std::runtime_error("Unsupported EnemyAsset format: "+pathText);
        asset->texture=ResolveEnemyResource(data.value("texture",asset->texture),path);
        if (!std::filesystem::is_regular_file(asset->texture)) throw std::runtime_error("Missing EnemyAsset texture: "+asset->texture);
        const auto number=[](const nlohmann::json& value,bool positive) {
            if (!value.is_number()) throw std::runtime_error("Expected numeric EnemyAsset value");
            const float result=value.get<float>();
            if (!std::isfinite(result) || result<0 || (positive && result==0) || result>1e8f)
                throw std::runtime_error("Invalid EnemyAsset number");
            return result;
        };
        const auto vector=[](const nlohmann::json& value) {
            if (!value.is_array() || value.size()!=3) throw std::runtime_error("Expected EnemyAsset vec3");
            Vector3 result{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
            for (float v:{result.x,result.y,result.z}) if (!std::isfinite(v) || std::abs(v)>1e6f) throw std::runtime_error("Invalid EnemyAsset vector");
            return result;
        };
        std::map<std::string,size_t> groups;
        for (const auto& item:data.at("hpGroups")) {
            EnemyHpGroup group;
            group.id=item.at("id").get<std::string>(); group.hp=group.maxHp=number(item.at("maxHp"),true);
            group.deathOnZero=item.value("deathOnZero",true);
            if (group.id.empty() || !groups.emplace(group.id,groups.size()).second) throw std::runtime_error("Duplicate/empty HP group");
            asset->defaults.hpGroups.push_back(group);
        }
        std::set<std::string> names;
        std::set<int> assignedFaces;
        if (!data.at("parts").is_array() || data.at("parts").empty() || data.at("parts").size()>4096)
            throw std::runtime_error("EnemyAsset requires 1..4096 parts");
        size_t triangleCount=0;
        for (const auto& item:data.at("parts")) {
            EnemyPart part;
            part.name=item.at("name").get<std::string>();
            if (part.name.empty() || !names.insert(part.name).second) throw std::runtime_error("Duplicate/empty part name");
            const auto role=item.value("role",std::string("Generic"));
            bool known=false;
            for (auto r:{EnemyPartRole::Generic,EnemyPartRole::Head,EnemyPartRole::Body,EnemyPartRole::Arm,EnemyPartRole::Leg,EnemyPartRole::Core,EnemyPartRole::Armor})
                if (role==EnemyPartRoleName(r)) { part.role=r; known=true; }
            if (!known) throw std::runtime_error("Unknown part role: "+role);
            part.type=LegacyRoleType(part.role);
            // Preserve legacy debug labels/physics without using them as identity.
            for (auto t:{EnemyPartType::Head,EnemyPartType::Body,EnemyPartType::LeftArm,EnemyPartType::RightArm,EnemyPartType::LeftLeg,EnemyPartType::RightLeg})
                if (part.name==EnemyPartName(t) && part.role==LegacyPartRole(t)) part.type=t;
            part.usesLocalHp=item.contains("localHp") && !item.at("localHp").is_null();
            part.hp=part.maxHp=part.usesLocalHp ? number(item.at("localHp"),true) : 0;
            part.breakable=item.value("breakable",true);
            part.deathOnZero=item.value("deathOnZero",false);
            if (part.deathOnZero && !part.usesLocalHp) throw std::runtime_error("deathOnZero requires localHp");
            part.sharedDamageRate=number(item.value("sharedDamageRate",nlohmann::json(1)),false);
            const auto group=item.value("sharedHpGroup",std::string{});
            if (!group.empty()) {
                const auto found=groups.find(group);
                if (found==groups.end()) throw std::runtime_error("Unknown sharedHpGroup: "+group);
                part.sharedGroup=found->second;
            }
            std::set<int> partFaces;
            for (const auto& face:item.at("faces")) {
                if (!face.is_number_integer()) throw std::runtime_error("Face ID must be integer");
                const int id=face.get<int>();
                if (id<0 || !assignedFaces.insert(id).second) throw std::runtime_error("Face belongs to multiple parts");
                partFaces.insert(id);
            }
            auto geometry=std::make_shared<EnemyPartGeometry>();
            std::set<int> usedFaces;
            for (const auto& triangle:item.at("triangles")) {
                if (++triangleCount>1000000) throw std::runtime_error("EnemyAsset triangle limit");
                const int face=triangle.at("face").get<int>();
                if (!partFaces.contains(face)) throw std::runtime_error("Triangle has unassigned face ID");
                usedFaces.insert(face);
                const auto& vertices=triangle.at("vertices");
                if (vertices.size()!=3) throw std::runtime_error("EnemyAsset expects triangles");
                std::array<EnemyPartVertex,3> tri;
                std::array<Vector3,3> positions;
                for (size_t i=0;i<3;++i) {
                    tri[i].position=positions[i]=vector(vertices[i].at("p"));
                    tri[i].normal=vector(vertices[i].at("n"));
                    const auto& uv=vertices[i].at("uv");
                    if (!uv.is_array() || uv.size()!=2) throw std::runtime_error("Invalid UV");
                    tri[i].uv={uv[0].get<float>(),uv[1].get<float>()};
                    if (!std::isfinite(tri[i].uv.x) || !std::isfinite(tri[i].uv.y)) throw std::runtime_error("Invalid UV");
                    const auto p=positions[i];
                    if (geometry->triangles.empty() && i==0) part.bounds={p,p};
                    part.bounds.min={std::min(part.bounds.min.x,p.x),std::min(part.bounds.min.y,p.y),std::min(part.bounds.min.z,p.z)};
                    part.bounds.max={std::max(part.bounds.max.x,p.x),std::max(part.bounds.max.y,p.y),std::max(part.bounds.max.z,p.z)};
                }
                geometry->triangles.push_back(tri); geometry->faces.push_back(positions);
            }
            if (geometry->faces.empty() || usedFaces!=partFaces) throw std::runtime_error("Empty part / face without triangles: "+part.name);
            part.geometry=std::move(geometry); asset->defaults.push_back(std::move(part));
        }
        return asset;
    }
};
