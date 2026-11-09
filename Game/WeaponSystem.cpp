#include "WeaponSystem.h"
#include "StageWorld.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

namespace {
using nlohmann::json;
json Read(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);
    auto data = json::parse(file);
    if (!data.is_object()) throw std::runtime_error("Expected object: " + path);
    return data;
}
float Number(const json& value, float minimum, float maximum) {
    if (!value.is_number()) throw std::runtime_error("Expected numeric weapon parameter");
    const float result = value.get<float>();
    if (!std::isfinite(result) || result < minimum || result > maximum) throw std::runtime_error("Weapon parameter out of range");
    return result;
}
int Integer(const json& value, int minimum, int maximum) {
    if (!value.is_number_integer()) throw std::runtime_error("Ammo/pellet count must be an integer");
    const double result = value.get<double>();
    if (result < minimum || result > maximum) throw std::runtime_error("Ammo/pellet count out of range");
    return static_cast<int>(result);
}
WeaponSlot Slot(const json& value) {
    if (value == "Main") return WeaponSlot::Main;
    if (value == "Sub") return WeaponSlot::Sub;
    throw std::runtime_error("slot must be Main or Sub");
}
double Weight(const json& value) {
    if (!value.is_number()) throw std::runtime_error("Weapon weight must be numeric");
    const double result = value.get<double>();
    if (!std::isfinite(result) || result < 0 || result > 1e9)
        throw std::runtime_error("Invalid weapon weight");
    return result;
}
WeaponSpawnFilter Filter(const json& value) {
    if (!value.is_object()) throw std::runtime_error("Weapon filter must be an object");
    WeaponSpawnFilter filter;
    // 誤字で条件が無視されて意図しない武器が出るのを防ぐ。
    for (const auto& [key, ignored] : value.items()) {
        (void)ignored;
        if (key != "slot" && key != "minRarity" && key != "maxRarity" && key != "types")
            throw std::runtime_error("Unknown weapon filter field: " + key);
    }
    if (value.contains("slot")) filter.slot = Slot(value.at("slot"));
    filter.minRarity = Integer(value.value("minRarity", json(1)), 1, 5);
    filter.maxRarity = Integer(value.value("maxRarity", json(5)), 1, 5);
    if (filter.minRarity > filter.maxRarity) throw std::runtime_error("minRarity exceeds maxRarity");
    if (value.contains("types")) {
        if (!value.at("types").is_array()) throw std::runtime_error("filter.types must be an array");
        for (const auto& type : value.at("types")) {
            const auto name = type.get<std::string>();
            if (name.empty()) throw std::runtime_error("Empty weapon filter type");
            filter.types.push_back(name);
        }
    }
    return filter;
}
Vector3 Vector(const json& value) {
    if (!value.is_array() || value.size()!=3) throw std::runtime_error("Expected 3-component vector");
    return {Number(value[0],-1e6f,1e6f), Number(value[1],-1e6f,1e6f), Number(value[2],-1e6f,1e6f)};
}
std::optional<Vector3> DropPosition(const Vector3& player,const Vector3& source,float yaw,
    const WeaponDefinition& weapon,const StageWorld* world) {
    const float radius=std::max(weapon.pickupScale.x,weapon.pickupScale.z);
    const float halfHeight=weapon.pickupScale.y;
    const auto away=player-source;
    const float distance=std::hypot(away.x,away.z);
    const auto side=Vector3{std::cos(yaw),0,-std::sin(yaw)};
    const auto forward=Vector3{std::sin(yaw),0,std::cos(yaw)};
    const auto preferred=distance>.1f ? Vector3{away.x/distance,0,away.z/distance} : side;
    for (const auto& offset:{preferred*1.1f,side*1.1f,side*-1.1f,forward*1.1f,forward*-1.1f,Vector3{}}) {
        auto feet=player+offset;
        if (world) {
            feet=world->Move(player,feet,radius,halfHeight*2,false,true);
            bool supported=false; float supportDistance=30;
            const auto ray=feet+Vector3{0,halfHeight+.35f,0};
            for (const auto& c:world->colliders) {
                if (c.playerOnly) continue;
                float extent[3];
                for (int i=0;i<3;++i) extent[i]=radius*std::hypot(c.inverse.m[0][i],c.inverse.m[2][i])+halfHeight*std::abs(c.inverse.m[1][i]);
                StageHit support;
                if (StageWorld::CastBox(c,ray,{0,-1,0},supportDistance,{extent[0],extent[1],extent[2]},support) && support.normal.y>=.7f) {
                    supported=true; supportDistance=support.distance;
                }
            }
            if (supported) feet.y=ray.y-supportDistance-halfHeight;
            else if (world->playerGravity) continue;
            // A drop beside a ledge stays on this floor; use the player's own
            // support as the final fallback rather than sending it to a lower storey.
            if (StageLength(offset)>.01f && std::abs(feet.y-player.y)>1) continue;
            const auto center=feet+Vector3{0,halfHeight+.015f,0};
            bool clear=true;
            for (const auto& c:world->colliders) {
                if (c.playerOnly) continue;
                float extent[3];
                for (int i=0;i<3;++i) extent[i]=radius*std::hypot(c.inverse.m[0][i],c.inverse.m[2][i])+halfHeight*std::abs(c.inverse.m[1][i]);
                StageHit hit;
                if (StageWorld::CastBox(c,center,{1,0,0},0,{extent[0],extent[1],extent[2]},hit)) { clear=false; break; }
            }
            if (!clear) continue;
        }
        return feet+Vector3{0,halfHeight+.015f,0};
    }
    return std::nullopt;
}
}

bool WeaponSystem::TryPickup(const Vector3& player,WeaponRuntime& weapon,const StageWorld* world,float yaw) {
    const auto index=Nearest(player);
    if (!index) return false;
    // Copy before appending: growing pickups_ must not invalidate a selected source.
    const auto selected=pickups_[*index];
    WeaponRuntime incoming;
    if (selected.savedWeapon) incoming=*selected.savedWeapon;
    else {
        const auto* definition=Find(selected.weaponId);
        if (!definition || !incoming.Equip(*definition)) return false;
    }
    if (!weapon.Definition().id.empty()) {
        const auto position=DropPosition(player,selected.position,yaw,weapon.Definition(),world);
        if (!position) return false; // Keep both weapons intact if there is no safe ground.
        auto outgoing=weapon; outgoing.PrepareForDrop();
        WeaponPickup dropped;
        dropped.spawnPointId="dropped-"+std::to_string(nextDropId_++);
        dropped.weaponId=outgoing.Definition().id;
        dropped.position=*position; dropped.rotation={0,yaw,0}; dropped.savedWeapon=std::move(outgoing);
        if (selected.savedWeapon) pickups_[*index]=std::move(dropped);
        else {
            auto free=std::find_if(pickups_.begin()+points_.size(),pickups_.end(),[](const auto& p) { return p.pickedUp; });
            if (free!=pickups_.end()) *free=std::move(dropped);
            else pickups_.push_back(std::move(dropped));
        }
    } else if (selected.savedWeapon) {
        pickups_[*index].visible=false; pickups_[*index].pickedUp=true;
    }
    weapon=std::move(incoming);
    return true;
}
bool WeaponSystem::Load(const std::string& definitionsPath, const std::string& levelPath,
    std::optional<WeaponRandomSettings> settingsOverride, const nlohmann::json* draft) {
    try {
        const auto source = Read(definitionsPath);
        const auto level = draft ? *draft : Read(levelPath);
        WeaponSystem loaded;
        std::set<std::string> ids;
        if (!source.at("weapons").is_array()) throw std::runtime_error("weapons must be an array");
        for (const auto& item : source.at("weapons")) {
            WeaponDefinition definition;
            definition.id = item.at("id").get<std::string>();
            if (definition.id.empty() || !ids.insert(definition.id).second) throw std::runtime_error("Empty/duplicate weapon ID: " + definition.id);
            definition.displayName = item.at("displayName").get<std::string>();
            if (definition.displayName.empty() || definition.displayName.size()>48) throw std::runtime_error("Invalid weapon name: " + definition.id);
            definition.type = item.value("type", std::string("Unknown"));
            if (definition.type.empty()) throw std::runtime_error("Empty weapon type: " + definition.id);
            if (item.contains("slot")) definition.slot = Slot(item.at("slot"));
            definition.rarity = Integer(item.value("rarity", json(1)), 1, 5);
            definition.spawnWeight = Weight(item.value("weight", json(1)));
            definition.damage = Number(item.at("damage"),.001f,100000);
            definition.fireInterval = Number(item.at("fireInterval"),.001f,3600);

            const std::string fireMode =
                item.value("fireMode", std::string("SemiAuto"));

            if (fireMode == "SemiAuto") {
                definition.fireMode = WeaponFireMode::SemiAuto;
            }
            else if (fireMode == "FullAuto") {
                definition.fireMode = WeaponFireMode::FullAuto;
            }
            else if (fireMode == "Burst") {
                definition.fireMode = WeaponFireMode::Burst;
            }
            else {
                throw std::runtime_error(
                    "Unknown fireMode for weapon: " + definition.id);
            }

            definition.range = Number(item.at("range"),.001f,100000);
            // 既存の武器JSONも読み込めるよう、新しい弾パラメータは省略時に定義の初期値を使う。
            if (item.contains("bulletSpeed")) definition.bulletSpeed = Number(item.at("bulletSpeed"),.001f,100000);
            if (item.contains("bulletLifeTime")) definition.bulletLifeTime = Number(item.at("bulletLifeTime"),.001f,60);
            definition.magazineSize = Integer(item.at("magazineSize"),1,100000);
            definition.reserveAmmo = Integer(item.at("reserveAmmo"),0,1000000);
            definition.infiniteReserveAmmo = item.value("infiniteReserveAmmo",false);
            definition.maxReserveAmmo = item.contains("maxReserveAmmo") ? Integer(item.at("maxReserveAmmo"),0,1000000) : definition.reserveAmmo;
            if (definition.reserveAmmo > definition.maxReserveAmmo) throw std::runtime_error("Reserve exceeds maximum: " + definition.id);
            definition.reloadTime = Number(item.value("reloadTime",json(0)),0,3600);
            definition.ammoPerShot = Integer(item.value("ammoPerShot",json(1)),1,100000);
            definition.burstCount = Integer(item.value("burstCount",json(3)),1,64);
            definition.burstInterval = Number(item.value("burstInterval",json(.07)),0,3600);
            const auto reloadMode = item.value("reloadMode",std::string("Magazine"));
            if (reloadMode == "Magazine") definition.reloadMode = WeaponReloadMode::Magazine;
            else if (reloadMode == "PerRound") definition.reloadMode = WeaponReloadMode::PerRound;
            else throw std::runtime_error("Unknown reloadMode for weapon: " + definition.id);
            definition.reloadStartTime = Number(item.value("reloadStartTime",json(.25)),0,3600);
            definition.reloadPerRoundTime = Number(item.value("reloadPerRoundTime",json(.55)),0,3600);
            definition.reloadEndTime = Number(item.value("reloadEndTime",json(.30)),0,3600);
            definition.reloadCanInterrupt = item.value("reloadCanInterrupt",true);
            if (item.contains("pelletCount")) definition.pelletCount = Integer(item.at("pelletCount"),1,64);
            // 旧JSONとの互換性も残す
            if (item.contains("hipSpreadDegrees")) {
                definition.hipSpreadDegrees =
                    Number(item.at("hipSpreadDegrees"), 0, 45);
            }
            else if (item.contains("spreadDegrees")) {
                definition.hipSpreadDegrees =
                    Number(item.at("spreadDegrees"), 0, 45);
            }

            if (item.contains("adsSpreadDegrees")) {
                definition.adsSpreadDegrees =
                    Number(item.at("adsSpreadDegrees"), 0, 45);
            }
            else {
                definition.adsSpreadDegrees =
                    definition.hipSpreadDegrees;
            }

            if (item.contains("adsFov")) {
                definition.adsFovDegrees =
                    Number(item.at("adsFov"), 5.0f, 120.0f);
            }

            if (item.contains("adsTransitionTime")) {
                definition.adsTransitionTime =
                    Number(item.at("adsTransitionTime"), 0.01f, 5.0f);
            }

            if (item.contains("adsSensitivityMultiplier")) {
                definition.adsSensitivityMultiplier =
                    Number(
                        item.at("adsSensitivityMultiplier"),
                        0.05f,
                        2.0f);
            }

            if (item.contains("pickupScale")) definition.pickupScale = Vector(item.at("pickupScale"));
            if (definition.pickupScale.x<=0 || definition.pickupScale.y<=0 || definition.pickupScale.z<=0) throw std::runtime_error("Invalid pickup scale");
            if (item.contains("pickupColor")) definition.pickupColor = Vector(item.at("pickupColor"));
            const auto color = definition.pickupColor;
            if (color.x<0 || color.y<0 || color.z<0 || color.x>1 || color.y>1 || color.z>1) throw std::runtime_error("Invalid pickup color");
            loaded.definitions_.push_back(std::move(definition));
        }
        loaded.initialWeaponId_ = source.value("initialWeapon",std::string("pistol"));
        if (!loaded.initialWeaponId_.empty() && !loaded.Find(loaded.initialWeaponId_)) throw std::runtime_error("Unknown initial weapon");
        if (level.contains("weaponRandom")) {
            const auto& settings = level.at("weaponRandom");
            loaded.settings_.useFixedSeed = settings.value("useFixedSeed",false);
            if (settings.contains("seed")) {
                const auto& seed = settings.at("seed");
                if (!seed.is_number_integer() || seed.get<double>()<0 || seed.get<double>()>4294967295.0)
                    throw std::runtime_error("Invalid weapon seed");
                loaded.settings_.seed = seed.get<uint32_t>();
            }
        }
        if (settingsOverride) loaded.settings_ = *settingsOverride;
        loaded.actualSeed_ = loaded.settings_.useFixedSeed ? loaded.settings_.seed : std::random_device{}();
        std::mt19937 random(loaded.actualSeed_);
        ids.clear();
        if (level.contains("weaponSpawnPoints")) {
            if (!level.at("weaponSpawnPoints").is_array()) throw std::runtime_error("weaponSpawnPoints must be an array");
            for (const auto& item : level.at("weaponSpawnPoints")) {
                WeaponSpawnPoint point;
                point.id = item.at("id").get<std::string>();
                if (point.id.empty() || !ids.insert(point.id).second) throw std::runtime_error("Empty/duplicate weapon point: " + point.id);
                point.position = Vector(item.at("position"));
                if (item.contains("rotation")) point.rotation = Vector(item.at("rotation"));
                try {
                    if (item.contains("filter")) point.filter = Filter(item.at("filter"));
                    std::vector<WeaponPoolEntry> sourcePool;
                    if (item.contains("weaponPool")) {
                        const auto& pool = item.at("weaponPool");
                        if (!pool.is_array() || pool.empty()) throw std::runtime_error("Empty/invalid weapon pool");
                        // 従来の候補順・省略時weight=1を保ち、既存の固定Seed抽選を変えない。
                        for (const auto& candidate : pool) {
                            WeaponPoolEntry entry;
                            entry.id = candidate.is_string() ? candidate.get<std::string>() : candidate.at("id").get<std::string>();
                            if (!loaded.Find(entry.id)) throw std::runtime_error("Unknown pool weapon: " + entry.id);
                            if (!candidate.is_string()) entry.weight = Weight(candidate.value("weight",json(1)));
                            sourcePool.push_back(std::move(entry));
                        }
                    } else {
                        for (const auto& weapon : loaded.definitions_)
                            sourcePool.push_back({weapon.id,weapon.spawnWeight});
                    }
                    // 条件の判定を抽選より先に行い、除外された武器の重みを確率に含めない。
                    for (const auto& entry : sourcePool)
                        if (point.filter.Matches(*loaded.Find(entry.id))) point.weaponPool.push_back(entry);
                } catch (const std::exception& exception) {
                    throw std::runtime_error("Weapon spawn " + point.id + ": " + exception.what());
                }
                std::vector<double> weights;
                double total = 0;
                for (const auto& entry : point.weaponPool) {
                    weights.push_back(entry.weight);
                    total += entry.weight;
                }
                if (total<=0 || !std::isfinite(total))
                    throw std::runtime_error("Weapon spawn " + point.id + ": no positive-weight candidates match filter");
                const size_t selected = std::discrete_distribution<size_t>(weights.begin(),weights.end())(random);
                loaded.pickups_.push_back({point.id,point.weaponPool[selected].id,point.position,point.rotation,true,false,std::nullopt});
                loaded.points_.push_back(std::move(point));
            }
        }
        *this = std::move(loaded); // atomic commit; invalid data retains the previous valid state
        return true;
    } catch (const std::exception& exception) {
        error_ = exception.what();
        return false;
    }
}

bool WeaponSystem::ReloadForEditor(const std::string& definitionsPath, const std::string& levelPath) {
    WeaponSystem loaded;
    if (!loaded.Load(definitionsPath, levelPath, WeaponRandomSettings{true, actualSeed_})) {
        error_ = loaded.Error(); return false;
    }
    if (loaded.points_.size() != points_.size()) {
        error_ = "Stage changed; restart the stage before saving weapons"; return false;
    }
    for (size_t i=0; i<points_.size(); ++i) {
        if (loaded.points_[i].id != points_[i].id || !loaded.Find(pickups_[i].weaponId)) {
            error_ = "Cannot remove an active pickup weapon or change stage points; restart with updated stage references first";
            return false;
        }
    }
    for (const auto& pickup:pickups_) if (!loaded.Find(pickup.weaponId)) {
        error_="Cannot remove a weapon while its pickup remains in this stage"; return false;
    }
    loaded.pickups_ = pickups_;
    loaded.nextDropId_=nextDropId_;
    loaded.settings_ = settings_;
    loaded.actualSeed_ = actualSeed_;
    *this = std::move(loaded);
    return true;
}
