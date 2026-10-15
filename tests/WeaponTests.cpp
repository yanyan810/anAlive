#include "WeaponSystem.h"
#include "StageProgress.h"
#include "EnemyParts.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>

using nlohmann::json;
static json Read(const char* path) { std::ifstream file(path); return json::parse(file); }
static void Save(const json& weapons, const json& level) {
    std::ofstream("weapon-test.json") << weapons;
    std::ofstream("weapon-level-test.json") << level;
}
static void TestInfiniteAmmo(const WeaponSystem& system) {
    WeaponRuntime weapon;
    for (const char* id : {"pistol","smg","burst_rifle","pump_shotgun"}) {
        auto definition = *system.Find(id);
        definition.magazineSize = definition.ammoPerShot;
        definition.reserveAmmo = 0;
        weapon.Equip(definition);
        assert(!weapon.InfiniteAmmo());
        weapon.SetInfiniteAmmo(true);
        for (int i=0; i<100; ++i) {
            assert(weapon.Step(1,true,true,true)==1);
            const int extra = weapon.Step(1,false,false,false);
            assert(extra==(definition.fireMode==WeaponFireMode::Burst ? definition.burstCount-1 : 0));
            assert(weapon.Magazine()==definition.magazineSize && weapon.Reserve()==0 && !weapon.Reloading());
        }
        assert(!weapon.StartReload());
        weapon.Equip(definition); // New game/equip returns to finite ammunition.
        assert(!weapon.InfiniteAmmo() && weapon.TryFire());
        weapon.Update(10);
        assert(!weapon.TryFire() && weapon.Magazine()==0);
    }
    weapon.Equip(*system.Find("pistol"));
    assert(weapon.TryFire() && weapon.StartReload());
    const auto cooldown = weapon.Cooldown();
    weapon.SetInfiniteAmmo(true);
    assert(!weapon.Reloading() && weapon.ReloadRemaining()==0 && weapon.Cooldown()==cooldown);
    assert(!weapon.TryFire()); // Unlimited ammo must still respect fire cadence.
    weapon.Update(1);
    assert(weapon.TryFire());
    std::cout << "Infinite ammo tests passed: repeated semi/full/burst/per-round shots, reload suppression, cadence, finite equip reset.\n";
}
static void TestExtendedModes(const WeaponSystem& system) {
    const auto pistol=*system.Find("pistol");
    const auto smg=*system.Find("smg");
    const auto burst=*system.Find("burst_rifle");
    const auto pump=*system.Find("pump_shotgun");
    const auto automatic=*system.Find("auto_shotgun");
    assert(pump.reloadMode==WeaponReloadMode::PerRound && pump.pelletCount==8);
    assert(automatic.fireMode==WeaponFireMode::FullAuto && automatic.pelletCount==6);
    assert(burst.fireMode==WeaponFireMode::Burst && burst.burstCount==3);
    WeaponRuntime weapon;
    weapon.Equip(pistol);
    assert(weapon.Step(0,true,true,false)==1);
    assert(weapon.Step(1,false,true,false)==0); // SemiAuto held
    assert(weapon.Step(0,true,true,false)==1);
    weapon.Equip(smg);
    assert(weapon.Step(0,true,true,false)==1);
    assert(weapon.Step(.04f,false,true,false)==0);
    assert(weapon.Step(.04f,false,true,false)==1);
    assert(weapon.Step(1,false,false,false)==0);
    weapon.Equip(burst);
    assert(weapon.Step(0,true,true,false)==1 && weapon.BurstRemaining()==2);
    assert(weapon.Step(.03f,true,true,false)==0); // spam cannot overlap
    assert(weapon.Step(.04f,false,false,false)==1 && weapon.BurstRemaining()==1);
    assert(weapon.Step(.07f,false,false,false)==1 && weapon.BurstRemaining()==0);
    assert(weapon.Magazine()==27 && std::abs(weapon.Cooldown()-.45)<1e-5);
    assert(weapon.Step(.44f,true,true,false)==0);
    assert(weapon.Step(.02f,false,true,false)==0); // held does not start a burst
    assert(weapon.Step(0,true,true,false)==1);
    weapon.CancelBurst(); assert(weapon.BurstRemaining()==0 && weapon.Update(1)==0);
    auto scarce=burst; scarce.magazineSize=5; scarce.ammoPerShot=2;
    weapon.Equip(scarce);
    assert(weapon.Step(0,true,true,false)==1);
    assert(weapon.Step(.07f,false,false,false)==1);
    assert(weapon.Magazine()==1 && weapon.BurstRemaining()==0);
    assert(weapon.Step(1,true,true,false)==0);
    auto instant=burst; instant.burstInterval=0;
    weapon.Equip(instant);
    assert(weapon.Step(0,true,true,false)==3 && weapon.Magazine()==27);
    // Burst scheduling and final cooldown agree across different frame rates.
    for (float dt : {1.0f/30,1.0f/144}) {
        weapon.Equip(burst);
        int shots=weapon.Step(0,true,true,false);
        float elapsed=0;
        while (elapsed<.3f) { shots+=weapon.Step(dt,false,false,false); elapsed+=dt; }
        assert(shots==3 && std::abs(weapon.Cooldown()-(.14+.45-elapsed))<1e-5);
    }
    auto emptyPump=pump; emptyPump.magazineSize=2; emptyPump.reserveAmmo=3;
    weapon.Equip(emptyPump);
    assert(weapon.TryFire()); weapon.Update(1); assert(weapon.TryFire()); weapon.Update(1);
    assert(weapon.Magazine()==0 && weapon.StartReload());
    assert(weapon.ReloadState()==WeaponReloadState::Starting && !weapon.TryFire());
    weapon.Update(.24f); assert(weapon.Magazine()==0);
    weapon.Update(.01f); assert(weapon.Magazine()==1 && weapon.Reserve()==2);
    assert(weapon.ReloadState()==WeaponReloadState::InsertingRound);
    weapon.Update(.55f); assert(weapon.Magazine()==2 && weapon.Reserve()==1);
    assert(weapon.ReloadState()==WeaponReloadState::Finishing);
    weapon.Update(.29f); assert(weapon.Reloading());
    weapon.Update(.01f); assert(!weapon.Reloading() && !weapon.StartReload());
    weapon.Equip(pump);
    for (int i=0;i<6;++i) { assert(weapon.TryFire()); weapon.Update(1); }
    assert(weapon.Magazine()==2 && weapon.StartReload());
    weapon.Update(.8f); assert(weapon.Magazine()==4);
    assert(weapon.TryFire()); assert(weapon.Magazine()==3 && !weapon.Reloading());
    const auto reserve=weapon.Reserve(); weapon.Update(10); assert(weapon.Reserve()==reserve);
    auto locked=pump; locked.reloadCanInterrupt=false;
    weapon.Equip(locked); assert(weapon.TryFire()); weapon.Update(1); assert(weapon.StartReload());
    assert(!weapon.TryFire()); weapon.Update(.25f); assert(!weapon.TryFire());
    weapon.Update(.3f); assert(weapon.TryFire());
    auto lowReserve=pump; lowReserve.reserveAmmo=1;
    weapon.Equip(lowReserve); assert(weapon.TryFire()); weapon.Update(1); assert(weapon.TryFire()); weapon.Update(1);
    assert(weapon.StartReload()); weapon.Update(.25f);
    assert(weapon.Reserve()==0 && weapon.Magazine()==7 && weapon.ReloadState()==WeaponReloadState::Finishing);
    weapon.Update(.3f); assert(!weapon.Reloading() && !weapon.StartReload());
    auto zero=pump; zero.reloadStartTime=zero.reloadPerRoundTime=zero.reloadEndTime=0;
    weapon.Equip(zero); assert(weapon.TryFire()); assert(weapon.StartReload());
    assert(!weapon.Reloading() && weapon.Magazine()==8 && weapon.Reserve()==39);
    // Eight generated rays, one ammo payment; change ammo cost without changing pellet count.
    weapon.Equip(pump);
    std::mt19937 random(1);
    int rays=0;
    for (int shots=weapon.Step(0,true,true,false); shots>0; --shots)
        for (int pellet=0; pellet<weapon.Definition().pelletCount; ++pellet) {
            const auto direction=WeaponPelletDirection({0,0,1},{1,0,0},{0,1,0},pump.hipSpreadDegrees,random);
            assert(direction.z>0); ++rays;
        }
    assert(rays==8 && weapon.Magazine()==7);
    auto doubleShot=pump; doubleShot.ammoPerShot=2;
    weapon.Equip(doubleShot); assert(weapon.Step(0,true,true,false)==1 && weapon.Magazine()==6 && weapon.Definition().pelletCount==8);
    weapon.Equip(burst); assert(weapon.Step(0,true,true,false)==1);
    weapon.Equip(pistol); assert(weapon.BurstRemaining()==0 && weapon.BurstTimer()==0 && weapon.Cooldown()==0 && !weapon.Reloading());
    assert(weapon.Update(1)==0 && weapon.Magazine()==12);
    weapon.Equip(pump); assert(weapon.TryFire()); assert(weapon.StartReload());
    weapon.Equip(smg); assert(weapon.ReloadState()==WeaponReloadState::None && weapon.ReloadRemaining()==0 && weapon.Cooldown()==0);
    WeaponSystem pickups=system;
    weapon.Equip(burst); assert(weapon.Step(0,true,true,false)==1);
    assert(pickups.TryPickup(pickups.Pickups()[0].position,weapon));
    assert(weapon.BurstRemaining()==0 && weapon.Cooldown()==0 && weapon.BurstTimer()==0);
    weapon.Equip(pump); assert(weapon.TryFire()); assert(weapon.StartReload());
    assert(pickups.TryPickup(pickups.Pickups()[1].position,weapon));
    assert(!weapon.Reloading() && weapon.ReloadRemaining()==0 && weapon.Cooldown()==0);
    const int freshMagazine=weapon.Magazine();
    assert(weapon.Update(5)==0 && weapon.Magazine()==freshMagazine);
    // Stage clear leaves the entire runtime untouched; no Step call after Cleared.
    weapon.Equip(burst); assert(weapon.Step(0,true,true,false)==1);
    const int remaining=weapon.BurstRemaining(); const auto timer=weapon.BurstTimer();
    StageProgress stage; assert(stage.LoadGoals("../../resources/levels/fps_spawns.json"));
    assert(stage.Update(0,{3,0,52}));
    if (stage.IsPlaying()) weapon.Step(1,false,false,false);
    assert(weapon.BurstRemaining()==remaining && weapon.BurstTimer()==timer);
    std::cout << "Extended weapon tests passed: Semi/Full/Burst, burst timing and exhaustion, ammo/pellet independence, PerRound phases, interruption, reserve/full stop, zero timings, equip reset, stage freeze.\n";
}
static void TestSpawnFilters(const json& definitions, const json& level) {
    WeaponSystem system;
    const auto load=[&](const json& data, const json& map) {
        Save(data,map);
        return system.Load("weapon-test.json","weapon-level-test.json",WeaponRandomSettings{true,42});
    };
    auto map=level;
    map["weaponSpawnPoints"]=json::array({{{"id","filtered"},{"position",{0,0,0}},
        {"filter",{{"slot","Sub"},{"minRarity",1},{"maxRarity",2}}}}});
    assert(load(definitions,map));
    assert(system.Pickups()[0].weaponId=="pistol");
    WeaponRuntime runtime;
    assert(system.TryPickup({0,0,0},runtime));
    assert(runtime.Definition().slot==WeaponSlot::Sub && runtime.Definition().rarity==1);
    assert(runtime.Definition().type=="Pistol" && runtime.TryFire());

    // 種類から枠を推論しない。PistolでもデータでMainを指定できる。
    auto data=definitions;
    data["weapons"][0]["slot"]="Main";
    map["weaponSpawnPoints"][0]["filter"]={{"slot","Main"},{"types",{"Pistol"}}};
    assert(load(data,map) && system.Pickups()[0].weaponId=="pistol");
    for (int rarity=1; rarity<=5; ++rarity) {
        data["weapons"][0]["rarity"]=rarity;
        map["weaponSpawnPoints"][0]["filter"]["minRarity"]=rarity;
        map["weaponSpawnPoints"][0]["filter"]["maxRarity"]=rarity;
        assert(load(data,map));
        assert(system.Find("pistol")->damage==definitions["weapons"][0]["damage"].get<float>());
    }
    map["weaponSpawnPoints"][0]["filter"]={{"slot","Main"},{"minRarity",3},{"maxRarity",4},{"types",{"Shotgun"}}};
    assert(load(definitions,map));
    assert(system.Points()[0].weaponPool.size()==2);
    for (const auto& entry : system.Points()[0].weaponPool) {
        const auto* weapon=system.Find(entry.id);
        assert(weapon->type=="Shotgun" && weapon->rarity>=3 && weapon->rarity<=4);
    }
    // 既存poolの重みを維持しつつ、条件外の高weight武器を除外する。
    map["weaponSpawnPoints"][0]["filter"]={{"slot","Main"},{"minRarity",2},{"maxRarity",3}};
    map["weaponSpawnPoints"][0]["weaponPool"]={
        {{"id","pistol"},{"weight",1000000}},{{"id","smg"},{"weight",1}},{{"id","rifle"},{"weight",9}},{{"id","shotgun"},{"weight",0}}};
    const auto point=map["weaponSpawnPoints"][0];
    map["weaponSpawnPoints"]=json::array();
    for (int i=0;i<1000;++i) {
        auto copy=point; copy["id"]="weighted_"+std::to_string(i);
        map["weaponSpawnPoints"].push_back(copy);
    }
    assert(load(definitions,map));
    const auto first=system.Pickups();
    int rifles=0;
    for (const auto& pickup : first) {
        assert(pickup.weaponId=="smg" || pickup.weaponId=="rifle");
        rifles+=pickup.weaponId=="rifle";
    }
    assert(rifles>820 && rifles<980);
    assert(load(definitions,map));
    for (size_t i=0;i<first.size();++i) assert(first[i].weaponId==system.Pickups()[i].weaponId);

    // 新フィールドを持たないデータと旧poolは、同じSeedで従来と同じ候補順・結果になる。
    assert(load(definitions,level));
    const auto existing=system.Pickups();
    data=definitions;
    for (auto& weapon : data["weapons"]) for (auto key : {"type","slot","rarity","weight"}) weapon.erase(key);
    assert(load(data,level));
    assert(system.Find("pistol")->slot==WeaponSlot::Main && system.Find("pistol")->rarity==1);
    assert(system.Find("pistol")->type=="Unknown");
    for (size_t i=0;i<existing.size();++i) assert(existing[i].weaponId==system.Pickups()[i].weaponId);

    // 全武器からの抽選では定義側weightを使う。
    data=definitions;
    for (auto& weapon : data["weapons"]) weapon["weight"]=weapon["id"]=="smg" ? 1 : 0;
    map["weaponSpawnPoints"]=json::array({{{"id","filtered"},{"position",{0,0,0}}}});
    assert(load(data,map) && system.Pickups()[0].weaponId=="smg");
    const auto reject=[&](const json& badData, const json& badMap) {
        assert(!load(badData,badMap) && !system.Error().empty());
        assert(system.Pickups()[0].weaponId=="smg"); // 失敗時は最後の有効な状態を保持する。
    };
    for (const auto& value : {json(0),json(6),json(1.5),json("2"),json(nullptr)}) {
        auto bad=definitions; bad["weapons"][0]["rarity"]=value; reject(bad,map);
    }
    for (const auto& value : {json(-1),json(1e10),json("1"),json(nullptr)}) {
        auto bad=definitions; bad["weapons"][0]["weight"]=value; reject(bad,map);
    }
    auto bad=definitions; bad["weapons"][0]["slot"]="Other"; reject(bad,map);
    bad=definitions; bad["weapons"][0]["type"]=""; reject(bad,map);
    for (const auto& filter : {
        json{{"minRarity",0}},json{{"maxRarity",6}},json{{"minRarity",3},{"maxRarity",2}},
        json{{"minRarity",1.5}},json{{"slot","Other"}},json{{"types","Shotgun"}},
        json{{"types",{""}}},json{{"minRairty",3}},json{{"slot","Sub"},{"minRarity",5}},
        json{{"types",{"MissingType"}}},json(nullptr)}) {
        auto badMap=map; badMap["weaponSpawnPoints"][0]["filter"]=filter; reject(definitions,badMap);
        assert(system.Error().find("filtered")!=std::string::npos);
    }
    auto badMap=map;
    badMap["weaponSpawnPoints"][0]["filter"]={{"slot","Sub"}};
    reject(data,badMap); // 条件一致がweight=0だけでも抽選しない。
    assert(load(definitions,map) && system.Error().empty());
}

int main() {
    const auto definitions=Read("../../resources/Data/weapons.json");
    const auto level=Read("../../resources/levels/fps_spawns.json");
    WeaponSystem system;
    assert(system.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json",WeaponRandomSettings{true,12345}));
    assert(system.Points().size()==3 && system.InitialWeapon()->id==definitions.at("initialWeapon").get<std::string>());
    TestExtendedModes(system);
    TestInfiniteAmmo(system);
    // Filter assertions use a fixed seven-weapon catalog; editor-created weapons
    // must not change the number or identity of random candidates in this fixture.
    auto filterDefinitions=definitions;
    const std::array<std::string,7> filterIds{"pistol","smg","rifle","shotgun","pump_shotgun","auto_shotgun","burst_rifle"};
    auto& filterWeapons=filterDefinitions["weapons"];
    filterWeapons.erase(std::remove_if(filterWeapons.begin(),filterWeapons.end(),[&](const auto& value) {
        return std::find(filterIds.begin(),filterIds.end(),value.at("id").template get<std::string>())==filterIds.end();
    }),filterWeapons.end());
    filterDefinitions["initialWeapon"]="pistol";
    TestSpawnFilters(filterDefinitions,level);
    const auto pistol = *system.Find("pistol");
    const auto smg = *system.Find("smg");
    const auto rifle = *system.Find("rifle");
    const auto shotgun = *system.Find("shotgun");

    {
        // FireMode
        assert(pistol.fireMode == WeaponFireMode::SemiAuto);
        assert(smg.fireMode == WeaponFireMode::FullAuto);
        assert(rifle.fireMode == WeaponFireMode::FullAuto);
        assert(shotgun.fireMode == WeaponFireMode::SemiAuto);

        // SemiAuto:
        // 押した瞬間だけ発射要求
        assert(WeaponWantsFire(
            WeaponFireMode::SemiAuto,
            true,
            true));

        // 長押しだけでは次弾を撃たない
        assert(!WeaponWantsFire(
            WeaponFireMode::SemiAuto,
            false,
            true));

        // FullAuto:
        // 最初のクリックでも撃つ
        assert(WeaponWantsFire(
            WeaponFireMode::FullAuto,
            true,
            true));

        // クリックTriggerがなくても長押し中なら撃つ
        assert(WeaponWantsFire(
            WeaponFireMode::FullAuto,
            false,
            true));

        // 離したら撃たない
        assert(!WeaponWantsFire(
            WeaponFireMode::FullAuto,
            false,
            false));

        // ADS設定
        assert(pistol.adsFovDegrees == 50.0f);
        assert(smg.adsFovDegrees == 45.0f);
        assert(rifle.adsFovDegrees == 35.0f);
        assert(shotgun.adsFovDegrees == 48.0f);

        assert(pistol.adsSensitivityMultiplier == 0.90f);
        assert(smg.adsSensitivityMultiplier == 0.80f);
        assert(rifle.adsSensitivityMultiplier == 0.70f);
        assert(shotgun.adsSensitivityMultiplier == 0.85f);

        // ADS時のSpreadはHipより狭い
        assert(pistol.adsSpreadDegrees <= pistol.hipSpreadDegrees);
        assert(smg.adsSpreadDegrees <= smg.hipSpreadDegrees);
        assert(rifle.adsSpreadDegrees <= rifle.hipSpreadDegrees);
        assert(shotgun.adsSpreadDegrees <= shotgun.hipSpreadDegrees);
    }

    {
        WeaponRuntime weapon;
        assert(!weapon.TryFire() && !weapon.StartReload());
        weapon.Equip(pistol);
        assert(weapon.Magazine()==12 && weapon.Reserve()==60);
        assert(!weapon.StartReload());
        int shots=0;
        for (int i=0; i<12; ++i) {
            if (weapon.TryFire()) ++shots;
            assert(!weapon.TryFire());
            weapon.Update(.125f); assert(!weapon.TryFire());
            weapon.Update(.125f);
        }
        assert(shots==12 && weapon.Magazine()==0 && !weapon.TryFire());
        assert(weapon.StartReload() && !weapon.StartReload() && !weapon.TryFire());
        weapon.Update(1); assert(weapon.Reloading() && weapon.Magazine()==0 && weapon.Reserve()==60);
        weapon.Update(.5f); assert(!weapon.Reloading() && weapon.Magazine()==12 && weapon.Reserve()==48);
        assert(weapon.TryFire());
        assert(weapon.StartReload());
        weapon.Equip(smg); // drops old weapon and cancels reload/cooldown
        assert(!weapon.Reloading() && weapon.Cooldown()==0 && weapon.Magazine()==30 && weapon.Reserve()==120);
        assert(weapon.TryFire()); weapon.Update(.08f); assert(weapon.TryFire());
        WeaponRuntime other; other.Equip(smg); assert(other.Magazine()==30);
    }
    {
        auto scarce=smg; scarce.reserveAmmo=10;
        WeaponRuntime weapon; weapon.Equip(scarce);
        for (int i=0;i<25;++i) { assert(weapon.TryFire()); weapon.Update(1); }
        assert(weapon.Magazine()==5 && weapon.StartReload());
        weapon.Update(2); assert(weapon.Magazine()==15 && weapon.Reserve()==0 && !weapon.StartReload());
        auto quick=pistol; quick.reloadTime=0;
        weapon.Equip(quick); assert(weapon.TryFire()); assert(weapon.StartReload());
        assert(!weapon.Reloading() && weapon.Magazine()==12 && weapon.Reserve()==59);
        const double cooldown=weapon.Cooldown();
        weapon.Update(-1); weapon.Update(std::numeric_limits<float>::quiet_NaN());
        assert(weapon.Cooldown()==cooldown);
    }
    {
        const auto selection=system.Pickups();
        WeaponRuntime weapon; weapon.Equip(pistol);
        assert(!system.TryPickup({100,0,100},weapon));
        for (size_t i=0;i<selection.size();++i) {
            assert(system.Nearest(selection[i].position)==i);
            assert(system.TryPickup(selection[i].position,weapon));
            assert(weapon.Definition().id==selection[i].weaponId);
            assert(!system.Pickups()[i].visible && system.Pickups()[i].pickedUp);
            assert(!system.TryPickup(selection[i].position,weapon));
        }
        assert(system.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json",WeaponRandomSettings{true,12345}));
        for (size_t i=0;i<selection.size();++i) {
            assert(system.Pickups()[i].weaponId==selection[i].weaponId);
            assert(system.Pickups()[i].visible && !system.Pickups()[i].pickedUp);
            bool member=false;
            for (const auto& entry:system.Points()[i].weaponPool) member |= entry.id==selection[i].weaponId;
            assert(member);
        }
    }
    {
        auto map=level;
        map["weaponSpawnPoints"][0]["position"]={1,0,0};
        map["weaponSpawnPoints"][1]["position"]={.5,0,0};
        map["weaponSpawnPoints"][2]["position"]={.5,0,0};
        map["weaponSpawnPoints"][0]["weaponPool"]={{{"id","pistol"},{"weight",0}},{{"id","smg"},{"weight",1}}};
        Save(definitions,map); assert(system.Load("weapon-test.json","weapon-level-test.json"));
        assert(system.Pickups()[0].weaponId=="smg");
        assert(system.Nearest({0,0,0})==1); // nearest, stable tie resolution
        WeaponRuntime weapon; assert(system.TryPickup({0,0,0},weapon));
        assert(system.Nearest({0,0,0})==2);
        map.erase("weaponSpawnPoints"); Save(definitions,map);
        assert(system.Load("weapon-test.json","weapon-level-test.json") && system.Pickups().empty());
    }
    {
        std::mt19937 random(42);
        const Vector3 forward{0,0,1}, right{1,0,0}, up{0,1,0};
        for (int i=0;i<100;++i) {
            const auto ray=WeaponPelletDirection(forward,right,up,shotgun.hipSpreadDegrees,random);
            assert(std::abs(ray.x*ray.x+ray.y*ray.y+ray.z*ray.z-1)<1e-5f);
            assert(ray.z>=std::cos(shotgun.hipSpreadDegrees *.01745329252f)-1e-5f);
        }
        const auto straight=WeaponPelletDirection(forward,right,up,0,random);
        assert(straight.x==0 && straight.y==0 && straight.z==1);
        WeaponRuntime weapon; weapon.Equip(shotgun);
        assert(weapon.TryFire());
        assert(weapon.Magazine()==7 && shotgun.pelletCount==8); // eight rays cost one shell
        const AABB bounds{{-.38f,.016f,-1.02f},{.22f,2.482f,1.02f}};
        auto parts=MakeEnemyParts(bounds);
        const auto& arm=parts[3];
        const auto center=(arm.bounds.min+arm.bounds.max)*.5f;
        EnemyPartHit hit;
        assert(!RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),{bounds.min.x-2,center.y,center.z},{1,0,0},1,hit));
        assert(RaycastEnemyParts(parts,Matrix4x4::MakeIdentity4x4(),{bounds.min.x-2,center.y,center.z},{1,0,0},pistol.range,hit));
        assert(DamageEnemyPart(parts,hit.part,pistol.damage)==25 && arm.hp==35);
        assert(DamageEnemyPart(parts,hit.part,smg.damage)==15 && arm.hp==20);
    }
    {
        Save(definitions,level);
        StageProgress stage; assert(stage.LoadGoals("weapon-level-test.json"));
        assert(system.Load("weapon-test.json","weapon-level-test.json"));
        WeaponRuntime weapon; weapon.Equip(pistol); assert(weapon.TryFire()); assert(weapon.StartReload());
        assert(stage.Update(1,{3,0,52}));
        const auto frozenReload=weapon.ReloadRemaining();
        int shots=0;
        if (stage.IsPlaying()) {
            weapon.Update(100);
            system.TryPickup(system.Pickups()[0].position,weapon);
            weapon.StartReload();
            if (weapon.TryFire()) ++shots;
        }
        assert(shots==0 && weapon.Magazine()==11 && weapon.ReloadRemaining()==frozenReload);
        assert(system.Pickups()[0].visible);
    }
    {
        const auto reject=[&](json data,json map) {
            Save(data,map);
            assert(!system.Load("weapon-test.json","weapon-level-test.json"));
            assert(!system.Error().empty() && system.Points().size()==3);
        };
        auto data=definitions; 
        data["weapons"][0]["magazineSize"]=0; 
        reject(data,level);

        data = definitions;
        data["weapons"][0]["fireMode"] = "Unknown";
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsFov"] = 0;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsTransitionTime"] = 0;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsSensitivityMultiplier"] = 0;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["hipSpreadDegrees"] = -1;
        reject(data, level);

        data = definitions;
        data["weapons"][0]["adsSpreadDegrees"] = -1;
        reject(data, level);

        data=definitions; data["weapons"][0]["fireInterval"]=-1; reject(data,level);
        data=definitions; data["weapons"][0]["reloadTime"]=-1; reject(data,level);
        data=definitions; data["weapons"][0]["magazineSize"]=1.5; reject(data,level);
        data=definitions; data["weapons"][0]["pelletCount"]=100; reject(data,level);
        data=definitions; data["weapons"][1]["id"]="pistol"; reject(data,level);
        for (const auto* field : {"ammoPerShot","pelletCount","burstCount","magazineSize"}) {
            data=definitions; data["weapons"][0][field]=0; reject(data,level);
            data=definitions; data["weapons"][0][field]=-1; reject(data,level);
        }
        for (const auto* field : {"burstInterval","reloadTime","reloadStartTime","reloadPerRoundTime","reloadEndTime"}) {
            data=definitions; data["weapons"][0][field]=-1; reject(data,level);
        }
        data=definitions; data["weapons"][0]["reloadMode"]="Unknown"; reject(data,level);

        auto map=level; 
        map["weaponSpawnPoints"][0]["weaponPool"]={"missing"};
        reject(definitions,map);

        map=level; map["weaponSpawnPoints"][0]["weaponPool"]=json::array(); reject(definitions,map);
        map=level; map["weaponSpawnPoints"][0]["weaponPool"]={{{"id","pistol"},{"weight",0}}}; reject(definitions,map);
        map=level; map["weaponSpawnPoints"][0]["weaponPool"]={{{"id","pistol"},{"weight",-1}}}; reject(definitions,map);
        map=level; map["weaponSpawnPoints"][1]["id"]="WeaponSpawn_01"; reject(definitions,map);
        map=level; map["weaponRandom"]["seed"]=-1; reject(definitions,map);
    }
    std::cout << "Weapon tests passed: ammo, dry fire, cooldown, reload/partial/empty reserve, equip cancellation, independent state, nearest/ties, consumed pickup, fixed-seed restart, weighted pools, legacy level, pellets/cone, fire modes, ADS config, weapon damage/range through part raycasts, cleared-stage freeze, invalid data.\n";
}
