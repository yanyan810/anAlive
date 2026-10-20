#pragma once
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include "DebugJsonEditor.h"
#include "WeaponSystem.h"
#include <regex>
#include <set>

// Authoring model independent of ImGui. Unknown JSON fields and omitted defaults survive edits.
class WeaponEditor {
    using Json = nlohmann::json;
public:
    struct Entry { WeaponDefinition value; Json source, baseline; };
    std::vector<Entry> entries;
    std::string initialWeapon, error;
    bool IsOpen() const { return !file_.path.empty(); }
    static Json Encode(const WeaponDefinition& w) {
        return {{"id",w.id},{"displayName",w.displayName},{"slot",WeaponSlotName(w.slot)},
            {"type",w.type},{"rarity",w.rarity},{"weight",w.spawnWeight},
            {"fireMode",WeaponFireModeName(w.fireMode)},{"damage",w.damage},{"fireInterval",w.fireInterval},
            {"range",w.range},{"bulletSpeed",w.bulletSpeed},{"bulletLifeTime",w.bulletLifeTime},
            {"ammoPerShot",w.ammoPerShot},{"pelletCount",w.pelletCount},
            {"burstCount",w.burstCount},{"burstInterval",w.burstInterval},
            {"magazineSize",w.magazineSize},{"reserveAmmo",w.reserveAmmo},{"maxReserveAmmo",w.maxReserveAmmo},
            {"reloadMode",WeaponReloadModeName(w.reloadMode)},{"reloadTime",w.reloadTime},
            {"reloadStartTime",w.reloadStartTime},{"reloadPerRoundTime",w.reloadPerRoundTime},
            {"reloadEndTime",w.reloadEndTime},{"reloadCanInterrupt",w.reloadCanInterrupt},
            {"hipSpreadDegrees",w.hipSpreadDegrees},{"adsSpreadDegrees",w.adsSpreadDegrees},
            {"adsFov",w.adsFovDegrees},{"adsTransitionTime",w.adsTransitionTime},
            {"adsSensitivityMultiplier",w.adsSensitivityMultiplier},
            {"pickupScale",{w.pickupScale.x,w.pickupScale.y,w.pickupScale.z}},
            {"pickupColor",{w.pickupColor.x,w.pickupColor.y,w.pickupColor.z}}};
    }
    bool Open(const std::string& path, const std::string& level) {
        DebugJsonEditor file;
        WeaponSystem loaded;
        if (!file.Open(path)) { error=file.error; return false; }
        if (!loaded.Load(path,level)) { error=loaded.Error(); return false; }
        std::vector<Entry> next;
        for (size_t i=0;i<loaded.Definitions().size();++i) {
            const auto& w=loaded.Definitions()[i];
            next.push_back({w,file.document.at("weapons")[i],Encode(w)});
        }
        file_=std::move(file); entries=std::move(next); saved_=file_.document;
        initialWeapon=loaded.InitialWeapon()->id; originalInitial_=initialWeapon;
        error.clear(); return true;
    }
    Json Document() const {
        auto document=saved_;
        document["weapons"]=Json::array();
        for (const auto& entry:entries) {
            auto item=entry.source;
            const auto current=Encode(entry.value);
            for (const auto& [key,value]:current.items())
                if (!entry.baseline.contains(key) || value!=entry.baseline.at(key)) item[key]=value;
            // These legacy defaults depend on another field. Keep the draft's actual value
            // when editing that field, rather than silently changing the dependent value.
            if (!item.contains("maxReserveAmmo") && current["reserveAmmo"]!=entry.baseline.value("reserveAmmo",Json{}))
                item["maxReserveAmmo"]=current["maxReserveAmmo"];
            if (!item.contains("adsSpreadDegrees") && current["hipSpreadDegrees"]!=entry.baseline.value("hipSpreadDegrees",Json{}))
                item["adsSpreadDegrees"]=current["adsSpreadDegrees"];
            document["weapons"].push_back(std::move(item));
        }
        if (initialWeapon!=originalInitial_) document["initialWeapon"]=initialWeapon;
        return document;
    }
    bool Dirty() const { return IsOpen() && Document()!=saved_; }
    static bool ValidId(const std::string& id) {
        static const std::regex pattern("[A-Za-z][A-Za-z0-9_-]{0,63}");
        return std::regex_match(id,pattern);
    }
    bool Create(const std::string& id,const std::string& name,int duplicate=-1) {
        if (!ValidId(id)) { error="ID: use 1-64 letters, digits, '_' or '-', starting with a letter"; return false; }
        for (const auto& e:entries) if (e.value.id==id) { error="ID already exists: "+id; return false; }
        if (name.empty() || name.size()>48) { error="Display Name: required, at most 48 UTF-8 bytes"; return false; }
        Entry next;
        if (duplicate>=0 && static_cast<size_t>(duplicate)<entries.size()) {
            next=entries[static_cast<size_t>(duplicate)];
        } else {
            next.value.damage=25; next.value.fireInterval=.25f; next.value.range=100;
            next.value.magazineSize=12; next.value.reserveAmmo=60; next.value.maxReserveAmmo=60;
            next.value.reloadTime=1.5f; next.value.type="Pistol";
            next.source=Json::object(); next.baseline=Json::object();
        }
        next.value.id=id; next.value.displayName=name;
        entries.push_back(std::move(next)); error.clear(); return true;
    }
    std::string Validate() const {
        std::set<std::string> ids;
        for (const auto& e:entries) {
            const auto& w=e.value;
            const auto prefix=w.id+": ";
            // Legacy IDs are kept readable; newly created/renamed IDs use the authoring rule.
            if (w.id.empty() || (!ValidId(w.id) && (!e.baseline.contains("id") || e.baseline["id"]!=w.id)))
                return prefix+"ID must start with a letter and contain only letters, digits, '_' or '-' (max 64)";
            if (!ids.insert(w.id).second) return prefix+"duplicate ID";
            if (w.displayName.empty() || w.displayName.size()>48) return prefix+"Display Name requires 1-48 UTF-8 bytes";
            if (w.type.empty()) return prefix+"Type cannot be empty";
            const auto data=Encode(w);
            struct Limit { const char* field; double low,high; };
            for (const auto& limit:std::initializer_list<Limit>{
                {"rarity",1,5},{"weight",0,1e9},{"damage",.001,100000},{"fireInterval",.001,3600},
                {"range",.001,100000},{"bulletSpeed",.001,100000},{"bulletLifeTime",.001,60},
                {"magazineSize",1,100000},{"reserveAmmo",0,1000000},{"maxReserveAmmo",0,1000000},
                {"ammoPerShot",1,100000},{"pelletCount",1,64},{"burstCount",1,64},{"burstInterval",0,3600},
                {"reloadTime",0,3600},{"reloadStartTime",0,3600},{"reloadPerRoundTime",0,3600},{"reloadEndTime",0,3600},
                {"hipSpreadDegrees",0,45},{"adsSpreadDegrees",0,45},{"adsFov",5,120},
                {"adsTransitionTime",.01,5},{"adsSensitivityMultiplier",.05,2}}) {
                const double value=data.at(limit.field).get<double>();
                if (!std::isfinite(value) || value<static_cast<double>(static_cast<float>(limit.low)) || value>limit.high)
                    return prefix+limit.field+" must be in ["+std::to_string(limit.low)+", "+std::to_string(limit.high)+"]";
            }
            if (w.reserveAmmo>w.maxReserveAmmo) return prefix+"Reserve Ammo exceeds Max Reserve Ammo";
            if (w.ammoPerShot>w.magazineSize) return prefix+"Ammo Per Shot exceeds Magazine Size";
            for (float scale:{w.pickupScale.x,w.pickupScale.y,w.pickupScale.z})
                if (!std::isfinite(scale) || scale<=0 || scale>1e6f) return prefix+"Pickup Scale must be positive and <= 1000000";
            for (float color:{w.pickupColor.x,w.pickupColor.y,w.pickupColor.z})
                if (!std::isfinite(color) || color<0 || color>1) return prefix+"Pickup Color must be in [0, 1]";
        }
        if (!ids.contains(initialWeapon)) return "Initial Weapon: select an existing weapon before saving";
        return {};
    }
    bool Save(const std::string& level, WeaponSystem& live) {
        error=Validate(); if (!error.empty()) return false;
        file_.document=Document();
        auto prepared=live;
        if (!file_.Save([&](const std::string& temp) {
            return prepared.ReloadForEditor(temp,level) ? std::string{} : prepared.Error();
        })) { error=file_.error; return false; }
        live=std::move(prepared); saved_=file_.document; originalInitial_=initialWeapon;
        for (size_t i=0;i<entries.size();++i) {
            entries[i].source=saved_.at("weapons")[i]; entries[i].baseline=Encode(entries[i].value);
        }
        error.clear(); return true;
    }
private:
    DebugJsonEditor file_;
    Json saved_;
    std::string originalInitial_;
};
#endif
