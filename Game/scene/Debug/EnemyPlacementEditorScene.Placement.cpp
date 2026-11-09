#include "EnemyPlacementEditorScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <numbers>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
using nlohmann::json;
constexpr float toDegrees=180/std::numbers::pi_v<float>;
Vector3 Position(const json& row) {
    const auto& p=row.at("position"); return {p[0].get<float>(),p[1].get<float>(),p[2].get<float>()};
}
json* FindRow(json& rows,const std::string& id) {
    for (auto& row:rows) if (row.value("id",std::string{})==id) return &row;
    return nullptr;
}
#ifdef USE_IMGUI
bool Text(const char* label,json& row,const char* key) {
    char buffer[256]{};
    const auto value=row.value(key,std::string{});
    strncpy_s(buffer,value.c_str(),_TRUNCATE);
    if (!ImGui::InputText(label,buffer,sizeof(buffer))) return false;
    row[key]=buffer; return true;
}
bool Choice(const char* label,json& row,const char* key,std::initializer_list<const char*> values,const char* fallback) {
    bool changed=false; const auto current=row.value(key,std::string(fallback));
    if (ImGui::BeginCombo(label,current.c_str())) {
        for (const auto* value:values) if (ImGui::Selectable(value,current==value)) { row[key]=value; changed=true; }
        ImGui::EndCombo();
    } return changed;
}
void Number(const char* label,json& row,const char* key,double fallback,double maximum=1000000) {
    double value=row.value(key,fallback);
    if (ImGui::InputDouble(label,&value,.1,1,"%.3f") && std::isfinite(value)) row[key]=std::clamp(value,0.0,maximum);
}
void Count(const char* label,json& row,const char* key,int fallback,int minimum=1,int maximum=10000) {
    int value=row.value(key,fallback);
    if (ImGui::InputInt(label,&value)) row[key]=std::clamp(value,minimum,maximum);
}
void Check(const char* label,json& row,const char* key,bool fallback) {
    bool value=row.value(key,fallback); if (ImGui::Checkbox(label,&value)) row[key]=value;
}
void Vector(const char* label,json& row,const char* key,bool angles=false,bool size=false) {
    const auto v=row.value(key,json::array({0,0,0}));
    float xyz[]={v[0].get<float>(),v[1].get<float>(),v[2].get<float>()};
    if (angles) for (auto& n:xyz) n*=toDegrees;
    if (ImGui::DragFloat3(label,xyz,angles ? 1.f : .05f)) {
        for (auto& n:xyz) {
            if (!std::isfinite(n)) return;
            if (angles) n/=toDegrees;
            n=std::clamp(n,size ? .01f : -1000000.f,1000000.f);
        }
        row[key]={xyz[0],xyz[1],xyz[2]};
    }
}
void EnemyChoice(const char* label,json& row,const char* key,const EnemyDefinitions& definitions) {
    const auto current=row.value(key,std::string{});
    if (ImGui::BeginCombo(label,current.c_str())) {
        for (const auto& [id,d]:definitions.All()) {
            const auto name=d.displayName+" ("+id+")";
            if (ImGui::Selectable(name.c_str(),current==id)) row[key]=id;
        }
        ImGui::EndCombo();
    }
}
#endif
}

bool EnemyPlacementEditorScene::RefreshStageSettings() {
    if (stagePreview_==stageDraft_) return stageSettingsValid_;
    bool placementChange=!stagePreview_.is_object();
    if (!placementChange) for (const auto* key:{"spawnPoints","spawnTriggers","spawnGroups","weaponSpawnPoints","enemyRandom","weaponRandom"})
        placementChange=placementChange || stageDraft_.value(key,json{})!=stagePreview_.value(key,json{});
    stagePreview_=stageDraft_;
    if (!placementChange) return stageSettingsValid_;
    StageLoader checked; EnemySpawnSystem enemies; WeaponSystem weapons;
    stageSettingsValid_=checked.Load(StagePath(),&stageDraft_) &&
        enemies.Load(StagePath(),definitions_,&stageDraft_) &&
        weapons.Load("resources/Data/weapons.json",StagePath(),WeaponRandomSettings{true,12345},&stageDraft_);
    if (!stageSettingsValid_) {
        stageSettingsStatus_="設定エラー（保存・テスト不可）: "+checked.Error()+" "+enemies.Error()+" "+weapons.Error();
        status_=stageSettingsStatus_; return false;
    }
    enemies.SetPlacements(EnemyPlacements{});
    spawns_=std::move(enemies); placementWeapons_=std::move(weapons);
    stageSettingsStatus_="設定の検証OK。テストプレイは未保存の武器・出現設定も使用します。";
    return true;
}

std::string EnemyPlacementEditorScene::NewStageObjectId(const std::string& prefix) const {
    std::set<std::string> used;
    for (const auto* key:{"colliders","spawnPoints","spawnTriggers","weaponSpawnPoints","goalTriggers","spawnGroups","shutters"})
        if (stageDraft_.contains(key)) for (const auto& row:stageDraft_.at(key)) if (row.contains("id")) used.insert(row.at("id").get<std::string>());
    for (size_t i=1;;++i) { const auto id=prefix+std::to_string(i); if (!used.contains(id)) return id; }
}

bool EnemyPlacementEditorScene::SaveStageSettings(const std::string& path) {
    const auto temporary=path+".placement-tmp";
    try {
        if (!RefreshStageSettings()) return false;
        if (PlayerVisualSettings::ReadBytes(path)!=stageSettingsBytes_) throw std::runtime_error("ステージが外部で変更されています。再読み込みしてください。");
        // Merge only the placement-owned fields, retaining camera, geometry and unknown metadata.
        auto saved=json::parse(stageSettingsBytes_);
        for (const auto* key:{"spawnPoints","spawnTriggers","spawnGroups","weaponSpawnPoints","enemyRandom","weaponRandom"}) {
            if (stageDraft_.contains(key)) saved[key]=stageDraft_.at(key); else saved.erase(key);
        }
        const auto bytes=saved.dump(2)+"\n";
        { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
          if (!file || !(file<<bytes) || !file.flush()) throw std::runtime_error("一時ファイルを書き込めません。"); }
        const auto roundtrip=json::parse(PlayerVisualSettings::ReadBytes(temporary));
        StageLoader level; EnemySpawnSystem enemies; WeaponSystem weapons;
        if (!level.Load(path,&roundtrip) || !enemies.Load(path,definitions_,&roundtrip) ||
            !weapons.Load("resources/Data/weapons.json",path,WeaponRandomSettings{true,12345},&roundtrip))
            throw std::runtime_error(level.Error()+" "+enemies.Error()+" "+weapons.Error());
        if (PlayerVisualSettings::ReadBytes(path)!=stageSettingsBytes_) throw std::runtime_error("保存中にステージが外部で変更されました。");
        std::filesystem::copy_file(path,path+".placement-backup",std::filesystem::copy_options::overwrite_existing);
        if (!MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("ステージ設定の置換に失敗: "+std::to_string(GetLastError()));
        stageSettingsBytes_=originalStageBytes_=bytes;
        stageDraft_=stageBaseline_=saved; stagePreview_=nullptr;
        stageSettingsStatus_="武器配置・敵の出現設定を保存しました。前回分: .placement-backup";
        return true;
    } catch (const std::exception& e) {
        stageSettingsStatus_="保存失敗: "+std::string(e.what());
        std::error_code ignored; std::filesystem::remove(temporary,ignored); return false;
    }
}

void EnemyPlacementEditorScene::DrawStagePlacementControls(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::SeparatorText("武器配置・敵の出現設定");
    ImGui::TextWrapped("%s",stageSettingsStatus_.c_str());
    ImGui::TextUnformatted(StageSettingsDirty() ? "ステージ設定: 未保存" : "ステージ設定: 読込・保存時の状態");
    if (ImGui::Button("武器・出現設定を保存")) pendingAction_="saveStageSettings";
    ImGui::SameLine();
    if (ImGui::Button("設定を読込値へ戻す")) stageDraft_=stageBaseline_;
    ImGui::TextWrapped("保存先: %s\n敵の個別位置・種類・有効数は「敵の個別配置を保存」で保存します。",StagePath().c_str());
    const auto atCamera=[&]() {
        auto p=camera_.GetTranslate();
        p.y=floor_>0 ? metadata_.floors[static_cast<size_t>(floor_-1)] : level_.playerPosition.y;
        EnemyPlacements::SnapFloor(p,level_.collision,3,3); return json::array({p.x,p.y,p.z});
    };
    const auto focus=[&](const json& row) {
        const auto p=Position(row); mode_=CameraMode::Free; camera_.SetOrthographic(false);
        camera_.SetTranslate(p+Vector3{0,3,-4}); camera_.SetRotate({.45f,0,0}); camera_.Update();
        floor_=metadata_.FloorAt(p.y); ApplyVisibility();
    };
    if (weaponPlacementMode_) {
        ImGui::Checkbox("武器マーカーを表示",&showWeapons_);
        auto& rows=stageDraft_["weaponSpawnPoints"];
        if (ImGui::Button("武器配置を追加（カメラ位置の床）")) {
            selectedWeapon_=NewStageObjectId("weaponSpawn.");
            rows.push_back({{"id",selectedWeapon_},{"position",atCamera()},{"rotation",{0,0,0}}});
        }
        ImGui::RadioButton("移動ギズモ##Weapon",&gizmo_,0); ImGui::SameLine(); ImGui::RadioButton("回転ギズモ##Weapon",&gizmo_,1);
        ImGui::Checkbox("グリッドスナップ##Weapon",&gridSnap_); ImGui::SliderFloat("グリッド (m)##Weapon",&grid_,.1f,5);
        ImGui::Checkbox("角度スナップ##Weapon",&rotationSnap_); ImGui::SliderFloat("角度 (度)##Weapon",&angleSnap_,1,90);
        ImGui::InputText("武器ID・名前を検索",search_,sizeof(search_));
        if (ImGui::BeginListBox("武器配置一覧",{-1,130})) {
            for (const auto& row:rows) {
                const auto id=row.at("id").get<std::string>();
                if (ImGui::Selectable(id.c_str(),selectedWeapon_==id)) { selectedWeapon_=id; selectedId_.clear(); selectedSpawn_.clear(); multiSelection_.clear(); }
            } ImGui::EndListBox();
        }
        if (auto* row=FindRow(rows,selectedWeapon_)) {
            ImGui::PushID(selectedWeapon_.c_str());
            ImGui::TextWrapped("WeaponSpawn: %s",selectedWeapon_.c_str());
            Vector("位置 (m)",*row,"position"); Vector("回転 (度)",*row,"rotation",true);
            if (ImGui::Button("この配置へカメラ移動")) focus(*row);
            ImGui::SameLine(); if (ImGui::Button("床へスナップ")) {
                auto p=Position(*row); if (EnemyPlacements::SnapFloor(p,level_.collision,3,3)) (*row)["position"]={p.x,p.y,p.z};
                else stageSettingsStatus_="上下3m以内に床がありません。";
            }
            auto filter=row->value("filter",json::object());
            const auto originalFilter=filter;
            Choice("装備枠",filter,"slot",{"All","Main","Sub"},"All");
            if (filter.value("slot",std::string{})=="All") filter.erase("slot");
            Count("最小レア度",filter,"minRarity",1,1,5); Count("最大レア度",filter,"maxRarity",5,1,5);
            std::set<std::string> types;
            for (const auto& d:placementWeapons_.Definitions()) types.insert(d.type);
            auto selectedTypes=filter.value("types",std::vector<std::string>{});
            for (const auto& type:selectedTypes) types.insert(type);
            ImGui::TextUnformatted("武器タイプ（選択なし＝すべて）");
            for (const auto& type:types) {
                bool selected=std::find(selectedTypes.begin(),selectedTypes.end(),type)!=selectedTypes.end();
                if (ImGui::Checkbox(type.c_str(),&selected)) {
                    if (selected) selectedTypes.push_back(type); else std::erase(selectedTypes,type);
                    if (selectedTypes.empty()) filter.erase("types"); else filter["types"]=selectedTypes;
                }
            }
            if (filter!=originalFilter) (*row)["filter"]=filter;
            ImGui::SeparatorText("武器カタログ / Manual Pool");
            ImGui::TextWrapped("チェックで抽選候補を固定します。未選択なら全定義から条件抽選。重み0は抽選されません。");
            auto pool=row->value("weaponPool",json::array());
            for (auto& entry:pool) if (entry.is_string()) entry={{"id",entry.get<std::string>()},{"weight",1}};
            const auto visible=[&](const WeaponDefinition& d) { return d.id.find(search_)!=std::string::npos || d.displayName.find(search_)!=std::string::npos; };
            if (ImGui::Button("検索結果をすべて選択")) {
                for (const auto& d:placementWeapons_.Definitions()) if (visible(d) && !FindRow(pool,d.id)) pool.push_back({{"id",d.id},{"weight",1}});
            }
            ImGui::SameLine(); if (ImGui::Button("全選択解除")) pool=json::array();
            for (const auto& d:placementWeapons_.Definitions()) if (visible(d)) {
                ImGui::PushID(d.id.c_str()); bool selected=FindRow(pool,d.id)!=nullptr;
                const auto label=d.displayName+" ("+d.id+") / "+WeaponSlotName(d.slot)+" / ★"+std::to_string(d.rarity)+" / "+d.type;
                if (ImGui::Checkbox(label.c_str(),&selected)) {
                    if (selected) pool.push_back({{"id",d.id},{"weight",1}});
                    else std::erase_if(pool.get_ref<json::array_t&>(),[&](const auto& entry){return entry.at("id")==d.id;});
                }
                if (auto* entry=FindRow(pool,d.id)) Number("重み",*entry,"weight",1,1e9);
                ImGui::PopID();
            }
            // Keep unknown IDs visible and removable instead of silently dropping them.
            for (size_t i=0;i<pool.size();) {
                const auto id=pool[i].at("id").get<std::string>();
                if (!placementWeapons_.Find(id)) {
                    ImGui::PushID(static_cast<int>(i)); ImGui::Text("Missing: %s",id.c_str()); ImGui::SameLine();
                    const bool remove=ImGui::Button("候補から削除"); ImGui::PopID();
                    if (remove) { pool.erase(i); continue; }
                } ++i;
            }
            if (pool.empty()) row->erase("weaponPool"); else (*row)["weaponPool"]=pool;
            WeaponSpawnFilter matcher;
            if (filter.contains("slot")) matcher.slot=filter.at("slot")=="Main" ? WeaponSlot::Main : WeaponSlot::Sub;
            matcher.minRarity=filter.value("minRarity",1); matcher.maxRarity=filter.value("maxRarity",5); matcher.types=selectedTypes;
            ImGui::SeparatorText("実際の抽選候補 (Matched Weapons)");
            int matches=0; double total=0;
            for (const auto& d:placementWeapons_.Definitions()) if (matcher.Matches(d)) {
                auto* entry=FindRow(pool,d.id); if (!pool.empty() && !entry) continue;
                const double weight=entry ? entry->value("weight",1.0) : d.spawnWeight;
                ImGui::Text("%s / weight %.3f",d.id.c_str(),weight); ++matches; total+=weight;
            }
            ImGui::Text("候補 %d / 重み合計 %.3f",matches,total);
            if (matches==0 || total<=0) ImGui::TextColored({1,.35f,.25f,1},"候補がありません。条件・候補・重みを修正してください。");
            const bool remove=ImGui::Button("この武器配置を削除"); ImGui::PopID();
            if (remove) { std::erase_if(rows.get_ref<json::array_t&>(),[&](const auto& r){return r.at("id")==selectedWeapon_;}); selectedWeapon_.clear(); }
        }
    } else {
        auto& rows=stageDraft_["spawnPoints"];
        if (ImGui::CollapsingHeader("敵出現地点・詳細設定",ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("敵出現地点を追加（カメラ位置の床）")) {
                selectedSpawn_=NewStageObjectId("enemySpawn."); selectedId_.clear(); multiSelection_.clear();
                const auto enemy=definitions_.All().begin()->first;
                rows.push_back({{"id",selectedSpawn_},{"position",atCamera()},{"rotation",{0,0,0}},
                    {"activation","StageStart"},{"enemy",enemy},{"spawnCount",1},{"maxAlive",1},
                    {"mode","Sequential"},{"initialDelay",0},{"spawnInterval",.5}});
                auto& g=placements_.groups[selectedSpawn_]; g.overrideEnabled=true;
                g.enemies.push_back({placements_.NewId(),enemy,{},0,true});
            }
            if (ImGui::BeginCombo("編集するEnemySpawn",selectedSpawn_.c_str())) {
                for (const auto& r:rows) { const auto id=r.at("id").get<std::string>();
                    if (ImGui::Selectable(id.c_str(),selectedSpawn_==id)) { selectedSpawn_=id; selectedId_.clear(); multiSelection_.clear(); }
                } ImGui::EndCombo();
            }
            if (auto* row=FindRow(rows,selectedSpawn_)) {
                ImGui::TextWrapped("発見時は索敵距離内で、間に壁のない仲間も追跡を続けます。");
                Check("出現直後からプレイヤーを追跡",*row,"pursueOnSpawn",false);
                Vector("出現原点 (m)",*row,"position"); Vector("出現原点の回転 (度)",*row,"rotation",true);
                if (ImGui::Button("出現原点へカメラ移動")) focus(*row);
                if (Choice("起動条件",*row,"activation",{"Legacy","StageStart","OnTrigger","Manual"},"Legacy")) {
                    if ((*row)["activation"]=="Legacy") {
                        row->erase("activation");
                        if (!row->contains("enemyPool")) (*row)["enemyPool"]={{{"id",definitions_.All().begin()->first},{"weight",1}}};
                    } else {
                        if (!row->contains("enemy") && !row->contains("enemyComposition")) (*row)["enemy"]=definitions_.All().begin()->first;
                        if (!row->contains("spawnCount")) (*row)["spawnCount"]=1;
                    }
                }
                if (row->contains("activation")) {
                    if ((*row)["activation"]=="OnTrigger" || (*row)["activation"]=="Manual") Text("グループID（Trigger / イベントと一致）",*row,"groupId");
                    Choice("出現方式",*row,"mode",{"Sequential","Simultaneous"},"Sequential");
                    Number("初期遅延 (秒)",*row,"initialDelay",0);
                    Number("出現間隔 (秒)",*row,"spawnInterval",.5);
                    Count("同時生存上限 Max Alive",*row,"maxAlive",row->value("spawnCount",1));
                    ImGui::TextWrapped("編集配置ONのときは下の個別配置・種類別体数を使用します。OFFのときはここで設定した種類・総数を使用します。");
                    if (!row->contains("enemy") && !row->contains("enemyComposition")) {
                        ImGui::TextWrapped("Enemy Poolから毎回ランダムに選びます。編集配置OFFで抽選を使用します。");
                        Count("出現総数",*row,"spawnCount",1);
                        auto& pool=(*row)["enemyPool"];
                        for (size_t i=0;i<pool.size();) {
                            ImGui::PushID(static_cast<int>(i)); EnemyChoice("抽選する敵",pool[i],"id",definitions_); Number("抽選の重み",pool[i],"weight",1,1e9);
                            const bool remove=ImGui::Button("候補を削除"); ImGui::PopID();
                            if (remove) { pool.erase(i); continue; } ++i;
                        }
                        if (ImGui::Button("抽選候補を追加")) pool.push_back({{"id","normal"},{"weight",1}});
                    } else if (row->contains("enemyComposition")) {
                        auto& entries=(*row)["enemyComposition"]; int total=0;
                        for (size_t i=0;i<entries.size();) {
                            ImGui::PushID(static_cast<int>(i)); EnemyChoice("敵",entries[i],"enemy",definitions_); Count("体数",entries[i],"count",1);
                            const bool remove=ImGui::Button("行を削除"); ImGui::PopID();
                            if (remove && entries.size()>1) { entries.erase(i); continue; }
                            total+=entries[i].at("count").get<int>(); ++i;
                        }
                        if (ImGui::Button("種類行を追加")) { entries.push_back({{"enemy",definitions_.All().begin()->first},{"count",1}}); ++total; }
                        (*row)["spawnCount"]=total; ImGui::Text("総数: %d",total);
                    } else {
                        EnemyChoice("既定の敵",*row,"enemy",definitions_); Count("既定の出現総数",*row,"spawnCount",1);
                        if (ImGui::Button("種類別体数へ切り替え")) {
                            (*row)["enemyComposition"]={{{"enemy",row->at("enemy")},{"count",row->at("spawnCount")}}}; row->erase("enemy");
                        }
                    }
                    if (ImGui::Button("既定の種類・体数を個別配置に反映")) {
                        EnemySpawnSystem source;
                        if (source.Load(StagePath(),definitions_,&stageDraft_)) {
                            source.SetPlacements(EnemyPlacements{});
                            if (const auto* p=source.FindPoint(selectedSpawn_)) Generate(*p,placements_.groups[selectedSpawn_],false,false);
                        } else stageSettingsStatus_=source.Error();
                    }
                } else {
                    ImGui::TextWrapped("Legacyは共有Trigger / 時刻指定グループ用です。Enemy Poolを重みで抽選します。");
                    if (!row->contains("enemyPool")) (*row)["enemyPool"]=json::array();
                    auto& pool=(*row)["enemyPool"];
                    for (size_t i=0;i<pool.size();) {
                        ImGui::PushID(static_cast<int>(i)); EnemyChoice("敵",pool[i],"id",definitions_); Number("重み",pool[i],"weight",1,1e9);
                        const bool remove=ImGui::Button("Pool行を削除"); ImGui::PopID();
                        if (remove) { pool.erase(i); continue; } ++i;
                    }
                    if (ImGui::Button("Pool行を追加")) pool.push_back({{"id",definitions_.All().begin()->first},{"weight",1}});
                }
                if (ImGui::Button("この敵出現地点を削除")) {
                    const auto id=selectedSpawn_;
                    bool referenced=false;
                    for (const auto& trigger:stageDraft_.value("spawnTriggers",json::array()))
                        for (const auto& point:trigger.value("spawnPointIds",json::array())) if (point==id) referenced=true;
                    for (const auto& wave:stageDraft_.value("spawnGroups",json::array()))
                        for (const auto& e:wave.at("enemies")) if (e.at("spawnPoint")==id) referenced=true;
                    if (row->value("activation",std::string{})=="OnTrigger")
                        for (const auto& trigger:stageDraft_.value("spawnTriggers",json::array()))
                            if (trigger.value("groupId",std::string{})==row->value("groupId",std::string{})) referenced=true;
                    if (referenced) stageSettingsStatus_="Triggerまたは時刻指定グループが参照しています。先に参照を変更・削除してください。";
                    else {
                        std::erase_if(rows.get_ref<json::array_t&>(),[&](const auto& r){return r.at("id")==id;});
                        // Retain individual drafts so reverting stage settings also restores previews.
                        selectedSpawn_.clear(); selectedId_.clear(); multiSelection_.clear();
                    }
                }
            }
        }
        if (ImGui::CollapsingHeader("Spawn Trigger（プレイヤー進入）")) {
            if (!stageDraft_.contains("spawnTriggers")) stageDraft_["spawnTriggers"]=json::array();
            auto& triggers=stageDraft_["spawnTriggers"];
            if (ImGui::Button("トリガーを追加")) {
                selectedTrigger_=NewStageObjectId("trigger.");
                auto p=atCamera(); p[1]=p[1].get<float>()+1;
                triggers.push_back({{"id",selectedTrigger_},{"position",p},{"size",{3,2,3}},{"groupId","A"},{"oneShot",true},{"initialDelay",0}});
            }
            if (ImGui::BeginCombo("Trigger",selectedTrigger_.c_str())) {
                for (const auto& t:triggers) { const auto id=t.at("id").get<std::string>(); if (ImGui::Selectable(id.c_str(),id==selectedTrigger_)) selectedTrigger_=id; }
                ImGui::EndCombo();
            }
            if (auto* t=FindRow(triggers,selectedTrigger_)) {
                Vector("範囲の中心 (m)",*t,"position"); Vector("範囲サイズ (m)",*t,"size",false,true);
                if (ImGui::Button("トリガーへカメラ移動")) focus(*t);
                Number("Trigger初期遅延 (秒)",*t,"initialDelay",0); Check("一度だけ発火",*t,"oneShot",true);
                if (t->contains("groupId")) Text("起動するグループID",*t,"groupId");
                else {
                    Count("共有の出現総数",*t,"spawnCount",1); Count("共有の同時生存上限",*t,"maxAlive",1);
                    Number("共有の出現間隔 (秒)",*t,"spawnInterval",0);
                    Choice("地点の選び方",*t,"selection",{"Random","RoundRobin"},"Random");
                    auto ids=t->at("spawnPointIds").get<std::vector<std::string>>();
                    for (const auto& r:rows) if (!r.contains("activation")) {
                        const auto id=r.at("id").get<std::string>(); bool selected=std::find(ids.begin(),ids.end(),id)!=ids.end();
                        if (ImGui::Checkbox(id.c_str(),&selected)) { if (selected) ids.push_back(id); else std::erase(ids,id); (*t)["spawnPointIds"]=ids; }
                    }
                }
                if (ImGui::Button("トリガーを削除")) { std::erase_if(triggers.get_ref<json::array_t&>(),[&](const auto& r){return r.at("id")==selectedTrigger_;}); selectedTrigger_.clear(); }
            }
        }
        if (ImGui::CollapsingHeader("時刻指定の Spawn Group")) {
            if (!stageDraft_.contains("spawnGroups")) stageDraft_["spawnGroups"]=json::array();
            auto& waves=stageDraft_["spawnGroups"];
            if (ImGui::Button("時刻グループを追加") && !rows.empty()) {
                selectedWave_=NewStageObjectId("wave.");
                waves.push_back({{"id",selectedWave_},{"time",0},{"mode","Sequential"},{"interval",.5},
                    {"enemies",{{{"enemy",definitions_.All().begin()->first},{"spawnPoint",rows.front().at("id")}}}}});
            }
            if (ImGui::BeginCombo("Spawn Group",selectedWave_.c_str())) {
                for (size_t i=0;i<waves.size();++i) { auto& w=waves[i]; const auto id=w.value("id","SpawnGroup_"+std::to_string(i+1)); if (ImGui::Selectable(id.c_str(),id==selectedWave_)) { selectedWave_=id; w["id"]=id; } }
                ImGui::EndCombo();
            }
            if (auto* w=FindRow(waves,selectedWave_)) {
                Number("開始時刻 (秒)",*w,"time",0); Choice("グループの出現方式",*w,"mode",{"Sequential","Simultaneous"},"Sequential");
                Number("グループの間隔 (秒)",*w,"interval",.5);
                auto& entries=(*w)["enemies"];
                for (size_t i=0;i<entries.size();) {
                    ImGui::PushID(static_cast<int>(i)); EnemyChoice("出現する敵",entries[i],"enemy",definitions_);
                    const auto current=entries[i].at("spawnPoint").get<std::string>();
                    if (ImGui::BeginCombo("出現地点",current.c_str())) {
                        for (const auto& r:rows) { const auto id=r.at("id").get<std::string>(); if (ImGui::Selectable(id.c_str(),id==current)) entries[i]["spawnPoint"]=id; }
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button("上へ") && i>0) std::swap(entries[i],entries[i-1]);
                    ImGui::SameLine(); if (ImGui::Button("下へ") && i+1<entries.size()) std::swap(entries[i],entries[i+1]);
                    ImGui::SameLine(); const bool remove=ImGui::Button("行を削除"); ImGui::PopID();
                    if (remove) { entries.erase(i); continue; } ++i;
                }
                if (ImGui::Button("出現行を追加") && !rows.empty()) entries.push_back({{"enemy",definitions_.All().begin()->first},{"spawnPoint",rows.front().at("id")}});
                ImGui::TextWrapped("個別配置ONの出現地点では、その配置の敵種類が優先されます。グループ行の種類を使う場合は編集配置をOFFにしてください。");
                if (ImGui::Button("時刻グループを削除")) { std::erase_if(waves.get_ref<json::array_t&>(),[&](const auto& r){return r.at("id")==selectedWave_;}); selectedWave_.clear(); }
            }
        }
    }
    if (ImGui::CollapsingHeader("抽選の固定シード")) {
        for (const auto* key:{"enemyRandom","weaponRandom"}) {
            ImGui::PushID(key); ImGui::TextUnformatted(key);
            if (!stageDraft_.contains(key)) stageDraft_[key]={{"useFixedSeed",false},{"seed",12345}};
            auto& settings=stageDraft_[key]; Check("固定シードを使用",settings,"useFixedSeed",false);
            uint32_t seed=settings.value("seed",uint32_t{12345});
            if (ImGui::InputScalar("Seed",ImGuiDataType_U32,&seed)) settings["seed"]=seed;
            ImGui::PopID();
        }
    }
#else
    (void)app;
#endif
}

void EnemyPlacementEditorScene::DrawStagePlacementMarkers(GameApp& app) {
#ifdef USE_IMGUI
    if (!showWeapons_ && weaponPlacementMode_) return;
    RECT rect{}; if (!app.ImGui()->GetSceneImageRect(rect)) return;
    const auto vp=camera_.GetViewProjectionMatrix(); auto* draw=ImGui::GetForegroundDrawList();
    const auto project=[&](const Vector3& p,ImVec2& out) {
        const float w=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
        const float z=p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2];
        if (w<=.0001f || z<0 || z>w) return false;
        const auto clip=StagePoint(p,vp);
        out={static_cast<float>(rect.left)+(clip.x/w+1)*.5f*static_cast<float>(rect.right-rect.left),
             static_cast<float>(rect.top)+(1-clip.y/w)*.5f*static_cast<float>(rect.bottom-rect.top)}; return true;
    };
    std::string picked; float nearest=16;
    if (showWeapons_) for (const auto& row:stageDraft_.at("weaponSpawnPoints")) {
        auto p=Position(row); if (!VisibleFloor(metadata_.FloorAt(p.y))) continue;
        p.y+=.2f; ImVec2 pixel; if (!project(p,pixel)) continue;
        const auto id=row.at("id").get<std::string>(); const bool selected=weaponPlacementMode_ && selectedWeapon_==id;
        const auto color=selected ? IM_COL32(255,235,90,255) : IM_COL32(255,165,45,255);
        draw->AddRect({pixel.x-7,pixel.y-7},{pixel.x+7,pixel.y+7},color,2,0,selected ? 3.f : 2.f);
        draw->AddText({pixel.x+10,pixel.y},color,id.c_str());
        const auto rotation=row.value("rotation",json::array({0,0,0})); const float yaw=rotation[1].get<float>(); ImVec2 tip;
        if (project(p+Vector3{std::sin(yaw),0,std::cos(yaw)},tip)) draw->AddLine(pixel,tip,color,2);
        const auto mouse=ImGui::GetIO().MousePos; const float distance=std::hypot(mouse.x-pixel.x,mouse.y-pixel.y);
        if (distance<nearest) { picked=id; nearest=distance; }
    }
    bool usedGizmo=false;
    const auto& io=ImGui::GetIO(); const auto mouse=io.MousePos;
    const bool hover=app.ImGui()->IsSceneImageHovered() && !app.GetInput()->IsCameraControlEnabled();
    if (weaponPlacementMode_) if (auto* row=FindRow(stageDraft_["weaponSpawnPoints"],selectedWeapon_)) {
        const auto position=Position(*row); ImVec2 center;
        if (project(position+Vector3{0,.2f,0},center)) {
            if (gizmo_==0) {
                const Vector3 axes[]={{1.5f,0,0},{0,1.5f,0},{0,0,1.5f}};
                const ImU32 colors[]={IM_COL32(255,60,60,255),IM_COL32(70,255,90,255),IM_COL32(80,130,255,255)};
                for (int axis=0;axis<3;++axis) {
                    ImVec2 end; if (!project(position+Vector3{0,.2f,0}+axes[axis],end)) continue;
                    draw->AddLine(center,end,colors[axis],3); draw->AddCircleFilled(end,6,colors[axis]);
                    if (hover && ImGui::IsMouseClicked(0) && std::hypot(mouse.x-end.x,mouse.y-end.y)<10) { dragAxis_=axis; dragWorld_=position; usedGizmo=true; }
                    if (dragAxis_==axis && ImGui::IsMouseDown(0) && !ImGui::IsMouseClicked(0)) {
                        const float sx=end.x-center.x,sy=end.y-center.y,n=sx*sx+sy*sy;
                        if (n>4) dragWorld_+=axes[axis]*((io.MouseDelta.x*sx+io.MouseDelta.y*sy)/n);
                        auto target=dragWorld_;
                        if (gridSnap_) for (float* v:{&target.x,&target.y,&target.z}) *v=std::round(*v/grid_)*grid_;
                        (*row)["position"]={target.x,target.y,target.z}; usedGizmo=true;
                    }
                }
            } else {
                auto rotation=row->value("rotation",json::array({0,0,0}));
                draw->AddCircle(center,55,IM_COL32(70,255,90,255),64,3);
                if (hover && ImGui::IsMouseClicked(0) && std::abs(std::hypot(mouse.x-center.x,mouse.y-center.y)-55)<9) { dragAxis_=3; dragYaw_=rotation[1].get<float>(); usedGizmo=true; }
                if (dragAxis_==3 && ImGui::IsMouseDown(0) && !ImGui::IsMouseClicked(0)) {
                    const float a=std::atan2(mouse.y-center.y,mouse.x-center.x),b=std::atan2(mouse.y-io.MouseDelta.y-center.y,mouse.x-io.MouseDelta.x-center.x);
                    dragYaw_+=std::remainder(a-b,2*std::numbers::pi_v<float>);
                    rotation[1]=rotationSnap_ ? std::round(dragYaw_*toDegrees/angleSnap_)*angleSnap_/toDegrees : dragYaw_;
                    (*row)["rotation"]=rotation; usedGizmo=true;
                }
            }
        }
    }
    if (!ImGui::IsMouseDown(0)) dragAxis_=-1;
    if (weaponPlacementMode_ && !usedGizmo && !picked.empty() && app.ImGui()->IsSceneImageHovered() && !app.GetInput()->IsCameraControlEnabled() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selectedWeapon_=picked;
    // Draw the exact AABBs consumed by the production trigger system.
    if (!weaponPlacementMode_) for (const auto& row:stageDraft_.value("spawnTriggers",json::array())) {
        const auto center=Position(row); const auto& size=row.at("size");
        if (!VisibleFloor(metadata_.FloorAt(center.y))) continue;
        const Vector3 half{size[0].get<float>()*.5f,size[1].get<float>()*.5f,size[2].get<float>()*.5f};
        Vector3 corners[8]; for (int i=0;i<8;++i) corners[i]=center+Vector3{i&1 ? half.x : -half.x,i&2 ? half.y : -half.y,i&4 ? half.z : -half.z};
        for (int i=0;i<8;++i) for (int bit:{1,2,4}) if (!(i&bit)) {
            ImVec2 a,b; if (project(corners[i],a) && project(corners[i|bit],b)) draw->AddLine(a,b,IM_COL32(220,100,255,180),2);
        }
        ImVec2 p; if (project(center,p)) draw->AddText(p,IM_COL32(220,100,255,255),row.at("id").get<std::string>().c_str());
    }
#else
    (void)app;
#endif
}
