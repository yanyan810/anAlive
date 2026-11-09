#include "WeaponEditor.h"
#include "WeaponEditorUI.h"
#include <cassert>
#include <iostream>
#include <limits>

using Json=nlohmann::json;
static std::string ReadBytes(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main() {
    const std::string path="editor-weapons-test.json",level="editor-level-test.json";
    auto source=Json::parse(ReadBytes("../../resources/Data/weapons.json"));
    source["futureEditorMetadata"]={{"preserve",true}};
    source["weapons"][0]["futureField"]=123;
    source["weapons"][0].erase("ammoPerShot");
    source["weapons"][0].erase("reloadMode");
    std::ofstream(path)<<source;
    std::ofstream(level)<<Json{{"weaponSpawnPoints",Json::array({
        {{"id","pickup"},{"position",{0,0,0}},{"weaponPool",{"pistol"}}}})}};
    WeaponSystem live;
    assert(live.Load(path,level,WeaponRandomSettings{true,123}));
    WeaponRuntime runtime;
    assert(live.TryPickup({0,0,0},runtime));
    WeaponEditor editor;
    assert(editor.Open(path,level));
    assert(!editor.Dirty() && editor.Document()==source);
    assert(!editor.Create("","Empty"));
    assert(!editor.Create("bad id","Bad"));
    assert(!editor.Create("pistol","Duplicate"));
    assert(!editor.Create("new_weapon",""));
    assert(editor.Create("new_weapon","New Weapon"));
    assert(editor.entries.back().value.damage>0);
    const size_t newIndex=editor.entries.size()-1;
    int pump=-1;
    for (size_t i=0;i<editor.entries.size();++i) if(editor.entries[i].value.id=="pump_shotgun") pump=static_cast<int>(i);
    assert(pump>=0 && editor.Create("heavy_pump_shotgun","Heavy Pump",pump));
    assert(editor.entries.back().value.reloadMode==WeaponReloadMode::PerRound);
    assert(editor.entries.back().value.damage==editor.entries[static_cast<size_t>(pump)].value.damage);
    const auto original=ReadBytes(path);
    editor.entries[newIndex].value.damage=-1;
    assert(!editor.Save(level,live) && editor.error.find("damage")!=std::string::npos);
    assert(ReadBytes(path)==original && !live.Find("new_weapon"));
    editor.entries[newIndex].value.damage=31;
    editor.entries[newIndex].value.adsTransitionTime=.01f;
    assert(editor.Save(level,live) && !editor.Dirty());
    assert(live.Find("new_weapon")->damage==31);
    assert(!live.Pickups()[0].pickedUp && live.Pickups()[0].visible);
    assert(live.ActualSeed()==123);
    auto saved=Json::parse(ReadBytes(path));
    assert(saved["futureEditorMetadata"]==source["futureEditorMetadata"]);
    assert(saved["weapons"][0]==source["weapons"][0]);
    assert(std::filesystem::exists(path+".debug-backup"));

    // All runtime state is reset by the same Equip used by the UI.
    runtime.Equip(*live.Find("burst_rifle"));
    assert(runtime.TryFire() && runtime.BurstRemaining()>0);
    runtime.Equip(*live.Find("new_weapon"));
    assert(runtime.Magazine()==12 && runtime.Reserve()==60 && runtime.Cooldown()==0);
    assert(!runtime.Reloading() && runtime.BurstRemaining()==0 && runtime.BurstTimer()==0);
    assert(runtime.TryFire() && runtime.StartReload());
    runtime.Equip(*live.Find("heavy_pump_shotgun"));
    assert(!runtime.Reloading() && runtime.ReloadRemaining()==0);

    // Remove current-stage reference: transaction rejects it and preserves live pickups.
    editor.initialWeapon="new_weapon";
    editor.entries.erase(editor.entries.begin());
    const auto beforeDelete=ReadBytes(path);
    assert(!editor.Save(level,live));
    assert(ReadBytes(path)==beforeDelete && live.Find("pistol"));
    assert(editor.Open(path,level));
    // Remove an unreferenced duplicate successfully (equipped runtimes own their definition).
    editor.entries.pop_back();
    assert(editor.Save(level,live) && !live.Find("heavy_pump_shotgun"));
    assert(runtime.Definition().id=="heavy_pump_shotgun");

    // External writes are never overwritten; a directory replacing the temp file simulates I/O failure.
    editor.entries[0].value.damage+=1;
    std::ofstream(path,std::ios::app)<<"\n";
    const auto external=ReadBytes(path);
    assert(!editor.Save(level,live) && ReadBytes(path)==external);
    assert(editor.Open(path,level));
    editor.entries[0].value.damage+=2;
    std::filesystem::create_directory(path+".debug-tmp");
    assert(!editor.Save(level,live) && ReadBytes(path)==external);
    std::filesystem::remove(path+".debug-tmp");
    editor.entries[0].value.damage=std::numeric_limits<float>::infinity();
    assert(!editor.Save(level,live) && ReadBytes(path)==external);
    assert(editor.Open(path,level) && !editor.Dirty());
    auto legacy=source;
    legacy["weapons"][0].erase("adsSpreadDegrees");
    legacy["weapons"][0].erase("maxReserveAmmo");
    std::ofstream(path)<<legacy;
    assert(live.Load(path,level) && editor.Open(path,level));
    const float oldADS=editor.entries[0].value.adsSpreadDegrees;
    const int oldMax=editor.entries[0].value.maxReserveAmmo;
    editor.entries[0].value.reserveAmmo=0;
    editor.entries[0].value.hipSpreadDegrees=4;
    assert(editor.Save(level,live));
    assert(live.Find("pistol")->adsSpreadDegrees==oldADS && live.Find("pistol")->maxReserveAmmo==oldMax);

    // Render real ImGui frames without a GPU: catches table/child/tab stack errors.
    std::filesystem::create_directories("resources/Data");
    std::filesystem::copy_file(path,"resources/Data/weapons.json",std::filesystem::copy_options::overwrite_existing);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={1280,800}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels=nullptr; int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    WeaponEditorUI ui; ui.visible=true;
    for (int i=0;i<3;++i) {
        ImGui::NewFrame(); assert(!ui.Draw(live,level,true)); ImGui::Render();
        if (i>0) assert(ImGui::GetDrawData()->TotalVtxCount>0);
    }
    int previews=0;
    for (int i=0;i<3;++i) {
        ImGui::NewFrame();
        assert(!ui.Draw(live,level,true,[&] { ++previews; ImGui::TextUnformatted("3D scene texture"); }));
        ImGui::Render();
    }
    assert(previews==3 && ui.Selected());
    ui.mode=WeaponEditorMode::Test;
    ImGui::NewFrame();
    assert(!ui.Draw(live,level,true,[&] { ++previews; }));
    ImGui::Render(); assert(previews==3);
    ui.mode=WeaponEditorMode::Edit;
    // Test equips an owned draft copy: no save, no live definition replacement.
    const auto diskBeforeTest=ReadBytes(path);
    const float savedInterval=live.Find("pistol")->fireInterval;
    editor.entries[0].value.fireInterval=0.1f;
    runtime.Equip(editor.entries[0].value);
    assert(runtime.TryFire());
    runtime.Update(0.099f); assert(!runtime.TryFire());
    runtime.Update(0.002f); assert(runtime.TryFire());
    assert(editor.Dirty() && ReadBytes(path)==diskBeforeTest);
    assert(live.Find("pistol")->fireInterval==savedInterval);
    ImGui::DestroyContext();
    std::cout<<"Weapon Editor tests passed: create, duplicate, validation, defaults, preservation, transactional save, conflict/I/O failure, delete safety, pickup state, equip reset, revert.\n";
}
