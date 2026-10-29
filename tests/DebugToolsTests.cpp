#include "DebugEnemyTuning.h"
#include "DebugTimeline.h"
#include "DebugJsonEditor.h"
#include "EnemyAI.h"
#include "EnemyProjectile.h"
#include "EnemySpawnSystem.h"
#include "DetachedEnemyPart.h"
#include "WeaponSystem.h"
#include "StageProgress.h"
#include <cassert>
#include <iostream>
#include <limits>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
using nlohmann::json;
struct State {
    EnemyAI ai;
    EnemyParts parts=MakeEnemyParts({{0,0,0},{1,2,1}});
    EnemyProjectileSystem bullets;
    EnemySpawnSystem spawns;
    WeaponRuntime weapon;
    StageProgress stage;
    DetachedPartMotion fragment;
    Vector3 position{},rotation{};
    std::mt19937 random{123};
    uint64_t frame=0;
};
static std::string Read(const std::string& path) { std::ifstream f(path); return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()}; }
int main() {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    DebugTimeline<State> history;
    State state;
    state.ai.settings.senses.detectionTime=0; state.ai.settings.senses.fieldOfView=360;
    EnemyDefinitions definitions; assert(definitions.Load("../../resources/Data/enemies.json"));
    assert(state.spawns.Load("../../resources/levels/fps_spawns.json",definitions));
    assert(state.stage.LoadGoals("../../resources/levels/fps_spawns.json"));
    WeaponSystem weapons; assert(weapons.Load("../../resources/Data/weapons.json","../../resources/levels/fps_spawns.json"));
    state.weapon.Equip(*weapons.InitialWeapon());
    state.bullets.Spawn(MakeEnemyProjectile(*definitions.Find("ranged"),{0,1,0},{0,0,10}));
    state.fragment.Initialize({{0,0,0},{1,1,1}},{0,3,0},{},{1,1,1},{1,0,0},EnemyPartType::Head,{1,1,1},{});
    history.Push(state);
    const auto initialMagazine=state.weapon.Magazine();
    const auto initialRandom=state.random;
    state.ai.Update(state.position,state.rotation,{0,0,5},1.0f/60,false);
    state.bullets.Update(1.0f/60,{100,0,0});
    state.weapon.Step(1.0f/60,true,true,false);
    state.fragment.Update(1.0f/60);
    DamageEnemyPart(state.parts,EnemyPartType::Head,1000);
    state.stage.Update(1.0f/60,{3,0,52});
    state.spawns.Update(0,{3,0,4},[](const auto&,const auto&){return uint64_t{1};},[](uint64_t){return true;});
    (void)state.random(); state.frame=1; history.Push(state);
    const auto* back=history.Back(); assert(back && back->frame==0);
    assert(!EnemyPartsDead(back->parts) && back->stage.IsPlaying());
    assert(back->weapon.Magazine()==initialMagazine && back->fragment.age==0);
    assert(back->bullets.Projectiles()[0].position.z==0 && !back->spawns.Triggers()[0].activated);
    assert(back->random==initialRandom && back->position.z==0);
    assert(history.Back()==nullptr);
    const auto* forward=history.Forward(); assert(forward && forward->frame==1 && EnemyPartsDead(forward->parts));
    assert(!forward->stage.IsPlaying() && forward->spawns.Triggers()[0].activated);
    assert(forward->weapon.Magazine()<initialMagazine && forward->fragment.age>0);
    assert(forward->bullets.Projectiles()[0].position.z>0 && forward->position.z>0);
    assert(history.Forward()==nullptr);
    state=*history.Back(); state.frame=99; history.Push(state);
    assert(history.Size()==2 && history.Forward()==nullptr && history.Cursor()==1);
    for (uint64_t i=0;i<500;++i) { state.frame=i; history.Push(state); }
    assert(history.Size()==300);
    int count=0; while (history.Back()) ++count; assert(count==299);
    history.Clear(); assert(history.Size()==0 && !history.Back() && !history.Forward());

    const std::string path="debug-editor-test.json";
    const auto original=Read("../../resources/Data/enemies.json");
    const auto originalJson=json::parse(original);
    size_t normalIndex=0;
    while (normalIndex<originalJson["enemies"].size() && originalJson["enemies"][normalIndex]["id"]!="normal") ++normalIndex;
    assert(normalIndex<originalJson["enemies"].size());
    { std::ofstream f(path); f<<original; }
    DebugJsonEditor editor; assert(editor.Open(path));
    json number=3; DebugJsonEditor::SetNumber(number,3.25); assert(number.is_number_float() && number==3.25);
    json countValue=5; DebugJsonEditor::SetNumber(countValue,8); assert(countValue.is_number_integer() && countValue==8);
    DebugJsonEditor::SetNumber(number,std::numeric_limits<double>::infinity()); assert(number==3.25);
    const auto validate=[](const std::string& file) { EnemyDefinitions d; return d.Load(file) ? std::string{} : d.Error(); };
    editor.document["enemies"][normalIndex]["collisionRadius"]=-1;
    assert(!editor.Save(validate) && Read(path)==original && !std::filesystem::exists(path+".debug-tmp"));
    editor.document["enemies"][normalIndex]["collisionRadius"]=.4;
    assert(editor.Save(validate));
    assert(Read(path+".debug-backup")==original);
    EnemyDefinitions edited; assert(edited.Load(path) && edited.Find("normal")->collisionRadius==.4f);
    const auto changed=Read(path);
    { std::ofstream f(path); f<<original; }
    assert(!editor.Save(validate) && Read(path)==original); // External edits are never overwritten.
    assert(editor.Open(path));
    editor.document["enemies"][normalIndex]["moveSpeed"]=3.5;
    assert(editor.Save(validate) && Read(path)!=original && Read(path)!=changed);
    assert(editor.Open(path) && !editor.dirty);
    assert(!editor.Open("absent-debug-editor-file.json") && editor.path==path);
    EnemyDefinitions beforeDimensions; assert(beforeDimensions.Load(path));
    auto tuning=*beforeDimensions.Find("normal");
    tuning.visualScaleMultiplier={.71f,.72f,.73f}; tuning.collisionRadius=.43f; tuning.collisionHeight=3.7f;
    std::string dimensionError;
    const auto beforeJson=json::parse(Read(path));
    assert(SaveEnemyDimensions(path,tuning,dimensionError));
    auto expected=beforeJson;
    for (auto& item : expected["enemies"]) if (item["id"]=="normal") {
        item["visualScale"]={.71f,.72f,.73f}; item["collisionRadius"]=.43f; item["collisionHeight"]=3.7f;
    }
    assert(json::parse(Read(path))==expected); // Other fields and enemy types are untouched.
    assert(json::parse(Read(path+".debug-backup"))==beforeJson);
    tuning.visualScaleMultiplier={1,1,1}; tuning.attackDamage=987;
    assert(LoadEnemyDimensions(path,tuning,dimensionError));
    assert(tuning.visualScaleMultiplier.y==.72f && tuning.collisionRadius==.43f && tuning.collisionHeight==3.7f);
    assert(tuning.attackDamage==987); // Loading dimensions never reapplies combat settings.
    const auto savedDimensions=Read(path);
    tuning.collisionRadius=-1;
    assert(!SaveEnemyDimensions(path,tuning,dimensionError) && Read(path)==savedDimensions);
    tuning.id="missing";
    assert(!SaveEnemyDimensions(path,tuning,dimensionError) && Read(path)==savedDimensions);
    assert(!LoadEnemyDimensions(path,tuning,dimensionError) && tuning.collisionRadius==-1);
    assert(!LoadEnemyDimensions("missing-dimensions.json",tuning,dimensionError));
    std::cout<<"Enemy dimension persistence passed: save/load, backup, unrelated fields preserved, invalid/missing rejection.\n";
    std::cout<<"Debug tools tests passed: rewind/forward, death/AI/spawn/projectile/fragment/weapon/stage/random restoration, branch/capacity, numeric edits, validation/atomic save/backup/external conflict.\n";
}
