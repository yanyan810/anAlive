#pragma once
#include "EnemyDefinition.h"
#include "EnemyNavigation.h"
#include <filesystem>
#include <set>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// Sidecar only. Blender's exported stage JSON remains the source of activation/timing.
struct EnemyPlacement {
    std::string id, enemy;
    Vector3 offset{}; // metres in the spawn's rotated coordinate frame (no scale)
    float yaw = 0;    // radians relative to the spawn's engine Y rotation
    bool enabled = true;
};
struct EnemyPlacementGroup {
    bool overrideEnabled = true;
    bool automatic = false;
    float radius = 3, initialYaw = 0;
    bool randomYaw = false;
    std::vector<EnemyPlacement> enemies;
};
class EnemyPlacements {
public:
    std::string stage;
    uint64_t nextId = 1;
    std::map<std::string,EnemyPlacementGroup> groups;
    std::string error;
    static std::string PathFor(const std::string& stagePath) {
        return (std::filesystem::path(stagePath).parent_path()/"enemy_placements.json").string();
    }
    static Vector3 WorldPosition(const EnemyPlacement& p,const Vector3& origin,const Vector3& rotation) {
        return StagePoint(p.offset,Matrix4x4::MakeAffineMatrix({1,1,1},rotation,origin));
    }
    static Vector3 LocalPosition(const Vector3& world,const Vector3& origin,const Vector3& rotation) {
        return StagePoint(world,Matrix4x4::Inverse(Matrix4x4::MakeAffineMatrix({1,1,1},rotation,origin)));
    }
    std::string NewId() {
        for (;;) {
            const auto id="placement-"+std::to_string(nextId++);
            bool used=false;
            for (const auto& [_,g]:groups) for (const auto& p:g.enemies) used=used || p.id==id;
            if (!used) return id;
        }
    }
    nlohmann::json Json() const {
        nlohmann::json list=nlohmann::json::array();
        for (const auto& [id,g]:groups) {
            nlohmann::json entries=nlohmann::json::array();
            for (const auto& p:g.enemies) entries.push_back({{"id",p.id},{"enemy",p.enemy},
                {"offset",{p.offset.x,p.offset.y,p.offset.z}},{"yaw",p.yaw},{"enabled",p.enabled}});
            list.push_back({{"spawnId",id},{"mode",g.automatic ? "Automatic" : "Individual"},
                {"overrideEnabled",g.overrideEnabled},{"radius",g.radius},{"initialYaw",g.initialYaw},{"randomYaw",g.randomYaw},{"enemies",entries}});
        }
        return {{"version",1},{"stage",stage},{"coordinates","engine-spawn-local-radians"},{"nextId",nextId},{"spawns",list}};
    }
    bool Parse(const nlohmann::json& data,const EnemyDefinitions& definitions,const std::string& expectedStage) {
        try {
            EnemyPlacements next;
            if (data.at("version")!=1 || data.at("coordinates")!="engine-spawn-local-radians")
                throw std::runtime_error("未対応の配置形式です");
            next.stage=data.at("stage").get<std::string>();
            if (next.stage!=expectedStage) throw std::runtime_error("配置データのステージIDが一致しません");
            next.nextId=data.value("nextId",uint64_t{1});
            if (next.nextId==0 || next.nextId>1000000000000ull) throw std::runtime_error("不正な配置IDカウンターです");
            const auto finite=[](float n) {
                if (!std::isfinite(n) || std::abs(n)>1e6f) throw std::runtime_error("配置数値が範囲外です");
                return n;
            };
            std::set<std::string> ids;
            if (!data.at("spawns").is_array() || data.at("spawns").size()>10000) throw std::runtime_error("配置一覧が不正です");
            for (const auto& row:data.at("spawns")) {
                const auto id=row.at("spawnId").get<std::string>();
                if (id.empty() || next.groups.contains(id)) throw std::runtime_error("EnemySpawn IDが空または重複しています: "+id);
                EnemyPlacementGroup group;
                group.overrideEnabled=row.value("overrideEnabled",true);
                const auto mode=row.at("mode").get<std::string>();
                if (mode!="Automatic" && mode!="Individual") throw std::runtime_error("配置方式が不正です");
                group.automatic=mode=="Automatic";
                group.radius=finite(row.value("radius",3.f));
                if (group.radius<0 || group.radius>1000) throw std::runtime_error("配置半径が範囲外です");
                group.initialYaw=finite(row.value("initialYaw",0.f)); group.randomYaw=row.value("randomYaw",false);
                if (!row.at("enemies").is_array() || row.at("enemies").size()>10000) throw std::runtime_error("敵配置は10000体以下です");
                for (const auto& entry:row.at("enemies")) {
                    EnemyPlacement p;
                    p.id=entry.at("id").get<std::string>(); p.enemy=entry.at("enemy").get<std::string>();
                    if (p.id.empty() || !ids.insert(p.id).second) throw std::runtime_error("個別配置IDが空または重複しています: "+p.id);
                    if (!definitions.Find(p.enemy)) throw std::runtime_error("存在しない敵定義: "+p.enemy);
                    const auto& v=entry.at("offset");
                    if (!v.is_array() || v.size()!=3) throw std::runtime_error("相対座標が不正です");
                    p.offset={finite(v[0].get<float>()),finite(v[1].get<float>()),finite(v[2].get<float>())};
                    p.yaw=finite(entry.at("yaw").get<float>()); p.enabled=entry.at("enabled").get<bool>();
                    group.enemies.push_back(p);
                }
                next.groups.emplace(id,std::move(group));
            }
            *this=std::move(next); return true;
        } catch (const std::exception& e) { error=e.what(); return false; }
    }
    bool Load(const std::string& path,const EnemyDefinitions& definitions,const std::string& expectedStage) {
        try {
            if (!std::filesystem::exists(path)) { *this=EnemyPlacements{}; stage=expectedStage; return true; }
            std::ifstream file(path); return Parse(nlohmann::json::parse(file),definitions,expectedStage);
        } catch (const std::exception& e) { error=e.what(); return false; }
    }
    static std::string ReadBytes(const std::string& path) {
        if (!std::filesystem::exists(path)) return {};
        std::ifstream file(path,std::ios::binary);
        if (!file) throw std::runtime_error("配置ファイルを読み取れません: "+path);
        return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    }
    bool Save(const std::string& path,const EnemyDefinitions& definitions,const std::string& originalBytes) {
        const auto temporary=path+".tmp";
        try {
            EnemyPlacements checked;
            if (!checked.Parse(Json(),definitions,stage)) throw std::runtime_error(checked.error);
            if (ReadBytes(path)!=originalBytes) throw std::runtime_error("配置ファイルが外部で変更されました。再読み込みしてください");
            const auto bytes=Json().dump(2)+'\n';
            { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
                if (!file || !(file<<bytes) || !file.flush()) throw std::runtime_error("一時ファイルの書き込みに失敗しました"); }
            if (!checked.Load(temporary,definitions,stage)) throw std::runtime_error(checked.error);
            if (ReadBytes(path)!=originalBytes) throw std::runtime_error("保存中に配置ファイルが外部で変更されました");
            if (std::filesystem::exists(path)) std::filesystem::copy_file(path,path+".bak",std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
            if (!MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("配置ファイルの置換に失敗しました: "+std::to_string(GetLastError()));
#else
            std::filesystem::rename(temporary,path);
#endif
            error.clear(); return true;
        } catch (const std::exception& e) {
            error=e.what(); std::error_code ignored; std::filesystem::remove(temporary,ignored); return false;
        }
    }
    static bool SnapFloor(Vector3& position,const StageWorld& world,float up=1,float down=3) {
        StageHit hit;
        if (!world.Raycast(position+Vector3{0,up,0},{0,-1,0},up+down,hit) || hit.normal.y<.7f) return false;
        position.y+=up-hit.distance; return true;
    }
    static std::string InvalidReason(const Vector3& position,const EnemyDefinition& definition,const StageWorld& world) {
        // Require a floor even in old stages with gravity disabled.
        StageHit floor;
        if (!world.Raycast(position+Vector3{0,.2f,0},{0,-1,0},.45f,floor) || floor.normal.y<.7f) return "足元に床がありません";
        if (!EnemyNavigation::Clear(&world,position,position,definition.collisionRadius,definition.collisionHeight)) return "壁・天井・障害物と重なっています";
        return {};
    }
};
