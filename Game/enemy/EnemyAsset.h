#pragma once
#include "EnemyModelSurface.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>

// Resource references are game-root relative. Walking source ancestors also supports
// tools/tests opening the same asset from a generated working directory.
inline std::string ResolveEnemyResource(const std::string& reference,const std::filesystem::path& source) {
    const auto requested=EnemyUtf8Path(reference);
    if (std::filesystem::is_regular_file(requested)) return EnemyPathText(std::filesystem::relative(requested));
    auto base=std::filesystem::absolute(source).parent_path();
    while (!base.empty()) {
        const auto candidate=base/requested;
        if (std::filesystem::is_regular_file(candidate)) return EnemyPathText(std::filesystem::relative(candidate));
        const auto parent=base.parent_path();
        if (parent==base) break;
        base=parent;
    }
    throw std::runtime_error("Missing enemy resource: "+reference);
}
// Immutable CPU asset; instances copy only HP/configuration and share triangle storage.
struct EnemyAsset {
    using ModelLoader=std::shared_ptr<const EnemyModelSurface>(*)(const std::string&);
    std::string path,texture="resources/white1x1.png";
    std::string modelPath;
    std::shared_ptr<const EnemyModelSurface> model;
    float modelScale=1;
    EnemyParts defaults;
    EnemyParts Instantiate(float multiplier=1) const {
        auto result=defaults;
        for (auto& part:result) part.hp=part.maxHp*=multiplier;
        for (auto& group:result.hpGroups) group.hp=group.maxHp*=multiplier;
        return result;
    }
    static std::shared_ptr<const EnemyAsset> Load(const std::filesystem::path& path,ModelLoader loader=nullptr) {
        const auto pathText=EnemyPathText(path);
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open EnemyAsset: "+pathText);
        nlohmann::json data; input>>data;
        std::shared_ptr<const EnemyModelSurface> model;
        if (data.at("version")==2) {
            if (!loader) throw std::runtime_error("EnemyAsset version 2 requires a model importer");
            model=loader(data.at("model").at("path").get<std::string>());
        }
        return Parse(data,path,std::move(model));
    }
    static std::shared_ptr<EnemyAsset> Parse(const nlohmann::json& data,const std::filesystem::path& path,
        std::shared_ptr<const EnemyModelSurface> model={}) {
        auto asset=std::make_shared<EnemyAsset>(); asset->path=EnemyPathText(path);
        const bool version2=data.at("version")==2;
        if ((!version2 && data.at("version")!=1) || data.at("coordinateSystem")!="yanengine")
            throw std::runtime_error("Unsupported EnemyAsset format: "+asset->path);
        if (!version2) asset->texture=ResolveEnemyResource(data.value("texture",asset->texture),path);
        const auto number=[](const nlohmann::json& value,bool positive) {
            if (!value.is_number()) throw std::runtime_error("Expected numeric EnemyAsset value");
            const float result=value.get<float>();
            if (!std::isfinite(result) || result<0 || (positive && result==0) || result>1e8f)
                throw std::runtime_error("Invalid EnemyAsset number");
            return result;
        };
        std::vector<bool> modelAssigned;
        if (version2) {
            const auto& reference=data.at("model");
            if (!model || reference.at("geometrySignature")!=model->signature || reference.at("faceCount")!=model->faces.size())
                throw std::runtime_error("Model geometry changed. Reopen the source model and assign its faces again.");
            asset->modelPath=reference.at("path").get<std::string>();
            asset->modelScale=number(reference.value("scale",nlohmann::json(1)),true);
            if (asset->modelScale<.01f || asset->modelScale>100.f) throw std::runtime_error("Model scale must be in [0.01, 100]");
            asset->model=std::move(model);
            modelAssigned.resize(asset->model->faces.size());
        }
        const auto vector=[](const nlohmann::json& value) {
            if (!value.is_array() || value.size()!=3) throw std::runtime_error("Expected EnemyAsset vec3");
            Vector3 result{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
            for (float v:{result.x,result.y,result.z}) if (!std::isfinite(v) || std::abs(v)>1e6f) throw std::runtime_error("Invalid EnemyAsset vector");
            return result;
        };
        std::map<std::string,size_t> groups;
        if (!data.at("hpGroups").is_array() || data.at("hpGroups").size()>4096) throw std::runtime_error("Invalid HP groups");
        for (const auto& item:data.at("hpGroups")) {
            EnemyHpGroup group;
            group.id=item.at("id").get<std::string>(); group.hp=group.maxHp=number(item.at("maxHp"),true);
            group.deathOnZero=item.value("deathOnZero",true);
            if (group.id.empty() || !groups.emplace(group.id,groups.size()).second) throw std::runtime_error("Duplicate/empty HP group");
            asset->defaults.hpGroups.push_back(group);
        }
        std::set<std::string> names;
        std::set<int> assignedFaces;
        if (!data.at("parts").is_array() || (!version2 && data.at("parts").empty()) || data.at("parts").size()>4096)
            throw std::runtime_error("EnemyAsset requires 1..4096 parts");
        size_t triangleCount=0;
        for (const auto& item:data.at("parts")) {
            EnemyPart part;
            part.name=item.at("name").get<std::string>();
            if (part.name.empty() || !names.insert(part.name).second) throw std::runtime_error("Duplicate/empty part name");
            if (version2 && part.name=="__Unassigned") throw std::runtime_error("__Unassigned is a reserved part name");
            const auto role=item.value("role",std::string("Generic"));
            bool known=false;
            for (auto r:kEnemyPartRoles)
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
            if (!item.at("faces").is_array()) throw std::runtime_error("Part faces must be an array");
            auto geometry=std::make_shared<EnemyPartGeometry>();
            if (version2) {
                for (const auto& face:item.at("faces")) {
                    const auto id=ReadModelFaceId(face);
                    const auto index=asset->model->FindFace(id);
                    if (index==kNoEnemyPart) throw std::runtime_error("Unknown model face in "+part.name);
                    if (modelAssigned[index]) throw std::runtime_error("Model face belongs to multiple parts");
                    modelAssigned[index]=true;
                    AppendModelFace(*geometry,part.bounds,*asset->model,index,asset->modelScale);
                }
                if (geometry->faces.empty()) throw std::runtime_error("Assign at least one face to "+part.name);
                part.geometry=std::move(geometry);
                asset->defaults.push_back(std::move(part));
                continue;
            }
            std::set<int> partFaces;
            for (const auto& face:item.at("faces")) {
                if (!face.is_number_integer()) throw std::runtime_error("Face ID must be integer");
                const int id=face.get<int>();
                if (id<0 || !assignedFaces.insert(id).second) throw std::runtime_error("Face belongs to multiple parts");
                partFaces.insert(id);
            }
            std::set<int> usedFaces;
            for (const auto& triangle:item.at("triangles")) {
                if (++triangleCount>1000000) throw std::runtime_error("EnemyAsset triangle limit");
                const int face=triangle.at("face").get<int>();
                if (!partFaces.contains(face)) throw std::runtime_error("Triangle has unassigned face ID");
                usedFaces.insert(face);
                const auto& vertices=triangle.at("vertices");
                if (vertices.size()!=3) throw std::runtime_error("EnemyAsset expects triangles");
                std::array<EnemyPartVertex,3> tri;
                for (size_t i=0;i<3;++i) {
                    tri[i].position=vector(vertices[i].at("p"));
                    tri[i].normal=vector(vertices[i].at("n"));
                    const auto& uv=vertices[i].at("uv");
                    if (!uv.is_array() || uv.size()!=2) throw std::runtime_error("Invalid UV");
                    tri[i].uv={uv[0].get<float>(),uv[1].get<float>()};
                    if (!std::isfinite(tri[i].uv.x) || !std::isfinite(tri[i].uv.y)) throw std::runtime_error("Invalid UV");
                }
                AppendEnemyTriangle(*geometry,part.bounds,tri);
            }
            if (geometry->faces.empty() || usedFaces!=partFaces) throw std::runtime_error("Empty part / face without triangles: "+part.name);
            part.geometry=std::move(geometry); asset->defaults.push_back(std::move(part));
        }
        if (version2) {
            EnemyPart other;
            other.name="__Unassigned";
            other.usesLocalHp=false;
            other.hp=other.maxHp=0;
            other.breakable=false;
            auto geometry=std::make_shared<EnemyPartGeometry>();
            for (size_t i=0;i<modelAssigned.size();++i)
                if (!modelAssigned[i]) AppendModelFace(*geometry,other.bounds,*asset->model,i,asset->modelScale);
            if (!geometry->faces.empty()) {
                other.geometry=std::move(geometry);
                asset->defaults.push_back(std::move(other));
            }
        }
        return asset;
    }
    static EnemyModelFaceId ReadModelFaceId(const nlohmann::json& face) {
        if (!face.is_array() || face.size()!=4) throw std::runtime_error("Expected [instance, mesh, material, triangle]");
        for (const auto& index:face) if (!index.is_number_integer() || index.get<double>()<0 || index.get<double>()>UINT32_MAX)
            throw std::runtime_error("Invalid model face index");
        return {face[0].get<uint32_t>(),face[1].get<uint32_t>(),face[2].get<uint32_t>(),face[3].get<uint32_t>()};
    }
    static void AppendModelFace(EnemyPartGeometry& geometry,AABB& bounds,const EnemyModelSurface& model,size_t index,float scale) {
        auto triangle=model.faces[index].vertices;
        for (auto& vertex:triangle) vertex.position=model.Fit(vertex.position,scale);
        AppendEnemyTriangle(geometry,bounds,triangle,model.faces[index].id.material);
    }
};
