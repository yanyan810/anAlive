#pragma once
#include "EnemyPlacements.h"
#include <cctype>

// Optional exporter contract: glTF node extras.enemyPlacementEditor or sibling
// editor_metadata.json {floors:[...], nodes:{name:{floor,kind,room}}, spawns:{id:{floor,room}}}.
// Explicit metadata takes precedence over naming. Unknown objects stay visible.
struct EnemyPlacementMetadata {
    struct Tag { int floor=0; std::string kind="other",room; };
    std::vector<float> floors{0};
    nlohmann::json layout, metadata;
    std::map<std::string,Tag> tags;
    std::vector<std::string> warnings;
    static Tag FromName(const std::string& name) {
        Tag tag;
        for (size_t i=0;i+2<name.size();++i) if (name[i]=='F' && std::isdigit(static_cast<unsigned char>(name[i+1])) && name[i+2]=='_') {
            tag.floor=name[i+1]-'0'; break;
        }
        auto lower=name;
        for (auto& c:lower) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("roof")!=std::string::npos) tag.kind="roof";
        else if (lower.find("ceiling")!=std::string::npos) tag.kind="ceiling";
        else if (lower.find("wall")!=std::string::npos) tag.kind="wall";
        else if (lower.find("floor")!=std::string::npos) tag.kind="floor";
        const auto room=name.find("Room");
        if (room!=std::string::npos) {
            size_t end=room+4;
            while (end<name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
            if (end>room+4) tag.room=name.substr(room,end-room);
        }
        return tag;
    }
    static void Override(Tag& tag,const nlohmann::json& data) {
        tag.floor=data.value("floor",tag.floor); tag.kind=data.value("kind",tag.kind); tag.room=data.value("room",tag.room);
        if (tag.floor<0 || tag.floor>100) throw std::runtime_error("編集用階層番号が範囲外です");
    }
    void Load(const std::string& stagePath,const std::string& model) {
        *this=EnemyPlacementMetadata{};
        const auto read=[](const std::filesystem::path& path) {
            if (!std::filesystem::exists(path)) return nlohmann::json::object();
            std::ifstream file(path); return nlohmann::json::parse(file);
        };
        try {
            const auto path=std::filesystem::path(stagePath);
            layout=read(path.parent_path()/(path.stem().string()+"-layout.json"));
            metadata=read(path.parent_path()/"editor_metadata.json");
            const auto heights=metadata.value("floors",layout.value("floorHeights",nlohmann::json::array({0})));
            auto parsed=heights.get<std::vector<float>>();
            if (parsed.empty() || parsed.size()>100) throw std::runtime_error("階高情報が不正です");
            for (size_t i=0;i<parsed.size();++i) if (!std::isfinite(parsed[i]) || (i && parsed[i]<=parsed[i-1])) throw std::runtime_error("階高は有限値の昇順で指定してください");
            floors=parsed;
            const auto gltf=read(std::filesystem::path("resources")/model);
            for (const auto& node:gltf.value("nodes",nlohmann::json::array())) {
                const auto name=node.value("name",std::string{});
                auto tag=FromName(name);
                if (node.contains("extras") && node["extras"].contains("enemyPlacementEditor")) Override(tag,node["extras"]["enemyPlacementEditor"]);
                if (metadata.contains("nodes") && metadata["nodes"].contains(name)) Override(tag,metadata["nodes"][name]);
                tags[name]=tag;
            }
        } catch (const std::exception& e) { warnings.push_back(std::string("編集メタデータ: ")+e.what()); }
    }
    int FloorAt(float height) const {
        int result=1;
        for (size_t i=0;i<floors.size();++i) if (height>=floors[i]-.3f) result=static_cast<int>(i)+1;
        return result;
    }
    Tag Spawn(const std::string& id,const Vector3& p) const {
        auto tag=FromName(id);
        if (!tag.floor) tag.floor=FloorAt(p.y);
        if (tag.room.empty() && layout.contains("rooms")) for (const auto& room:layout["rooms"]) {
            const int number=room.at("number").get<int>();
            const auto b=room.at("bounds").get<std::vector<float>>();
            if (b.size()==4 && number/100==tag.floor && p.x>=b[0] && p.x<=b[1] && p.z>=b[2] && p.z<=b[3]) tag.room="Room"+std::to_string(number);
        }
        if (metadata.contains("spawns") && metadata["spawns"].contains(id)) Override(tag,metadata["spawns"][id]);
        if (tag.room.empty()) tag.room="部屋未指定";
        return tag;
    }
    Tag Node(const std::string& name) const {
        const auto it=tags.find(name); return it==tags.end() ? FromName(name) : it->second;
    }
};
