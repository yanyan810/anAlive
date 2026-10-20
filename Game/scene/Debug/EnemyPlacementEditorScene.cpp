#include "EnemyPlacementEditorScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <numbers>
#include <utility>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr float pi=std::numbers::pi_v<float>, degrees=180/pi;
#ifdef USE_IMGUI
constexpr int mouseFlags=ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange;
#endif
}

void EnemyPlacementEditorScene::OnEnter(GameApp& app) {
    app.Win()->SetCloseGuard([this] {
        if (!Dirty()) return true;
        if (test_) pendingAction_="stop";
        confirmAction_="quit"; discardModal_=true; return false;
    });
    app.GetInput()->SetCameraToggleKeyEnabled(false);
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    savedMouseFlags_=ImGui::GetIO().ConfigFlags & mouseFlags;
    ImGui::GetIO().ConfigFlags &= ~mouseFlags;
    app.ImGui()->SetWeaponWorkspace(true);
#endif
    player_.Initialize(app.ObjCom(),app.Dx(),&camera_);
    camera_.SetFovY(pi/3); camera_.SetFarClip(600);
    if (!definitions_.Load("resources/Data/enemies.json")) { status_=definitions_.Error(); return; }
    playerVisual_=std::make_unique<StartPlayerVisual>();
    playerVisual_->Initialize(app.ObjCom(),app.Dx(),&camera_,definitions_.Find("normal"),player_.Settings().cameraHeight);
    LoadStage(app,stageId_);
}
void EnemyPlacementEditorScene::OnExit(GameApp& app) {
    StopCameraPreview();
    app.Win()->SetCloseGuard({});
    if (test_) StopTest(app);
    app.GetInput()->SetCameraControlEnabled(false);
    app.GetInput()->SetMouseCaptureRect(nullptr);
    app.GetInput()->SetCameraToggleKeyEnabled(true);
    app.ObjCom()->SetDefaultCamera(nullptr);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~mouseFlags)|savedMouseFlags_;
    app.ImGui()->SetWeaponWorkspace(false);
#endif
}
void EnemyPlacementEditorScene::LoadStage(GameApp& app,const std::string& id) {
    StopCameraPreview();
    StageLoader level; EnemySpawnSystem spawns;
    const auto path="resources/levels/"+id+"/"+id+".json";
    if (!level.Load(path) || !level.ValidateAssets() || !spawns.Load(path,definitions_)) {
        status_="ステージ読み込み失敗: "+level.Error()+" "+spawns.Error(); return;
    }
    EnemyPlacements placements;
    loadError_=!placements.Load(EnemyPlacements::PathFor(path),definitions_,id);
    status_=loadError_ ? "配置読み込み失敗（保存・テストを停止）: "+placements.error : "読み込み完了。配置を選択してください。";
    warnings_=spawns.PlacementWarnings();
    spawns.SetPlacements(EnemyPlacements{}); // Original Blender counts for initial drafts/regeneration.
    app.Dx()->WaitForGPU(); previews_.clear(); shutters_.clear();
    stageId_=id; level_=std::move(level); spawns_=std::move(spawns);
    LoadPlayerVisual(path);
    LoadCameraSequences(path);
    metadata_.Load(path,level_.model);
    for (const auto& warning:metadata_.warnings) warnings_.push_back(warning);
    placements_=std::move(placements); placements_.stage=id;
    if (!loadError_) for (const auto& point:spawns_.Points()) if (!placements_.groups.contains(point.id)) {
        auto& group=placements_.groups[point.id]; group.automatic=true;
        group.overrideEnabled=point.activation!=SpawnActivation::Legacy;
        Generate(point,group,false,false); // Initial draft retains Blender's enabled counts even if geometry is invalid.
    }
    for (const auto& [spawn,_]:placements_.groups) if (!spawns_.FindPoint(spawn)) warnings_.push_back("EnemySpawn IDが変更・削除されています（保存データを保持）: "+spawn);
    baseline_=placements_.Json();
    try { originalBytes_=EnemyPlacements::ReadBytes(PlacementPath()); }
    catch (const std::exception& e) { status_=e.what(); loadError_=true; }
    selectedSpawn_.clear(); selectedId_.clear(); multiSelection_.clear();
    app.ObjCom()->SetDefaultCamera(&camera_);
    environment_.Initialize(app.ObjCom(),app.Dx()); environment_.SetCamera(&camera_);
    environment_.SetModel(level_.model); environment_.StopAnimation(); environment_.SetMaterialColor({1,1,1,1});
    environment_.SetScale({1,1,1}); environment_.SetTranslate({}); environment_.SetRotate({});
    environment_.SetEnableLighting(2); environment_.SetDirection({.3f,-1,.5f});
    environment_.SetIntensity(1); environment_.SetPointLightIntensity(0); environment_.SetSpotLightIntensity(0);
    for (const auto& shutter:level_.shutters.shutters) {
        auto object=std::make_unique<Object3d>(); object->Initialize(app.ObjCom(),app.Dx()); object->SetCamera(&camera_);
        object->SetModel(shutter.definition.model); object->StopAnimation(); object->SetTranslate(shutter.Position());
        object->SetEnableLighting(2); object->SetPointLightIntensity(0); object->SetSpotLightIntensity(0);
        shutters_.push_back(std::move(object));
    }
    player_.SetStage(&level_.collision,level_.playerPosition,level_.playerRotation); player_.RefreshVisuals();
    floor_=metadata_.FloorAt(level_.playerPosition.y); mode_=CameraMode::Free;
    camera_.SetOrthographic(false); camera_.SetTranslate(level_.playerPosition+Vector3{0,3,-4}); camera_.SetRotate({.35f,0,0}); camera_.Update();
    ready_=true; SyncPreviews(app); ApplyVisibility(); environment_.Update(0);
    for (auto& shutter:shutters_) shutter->Update(0);
    SyncPlayerVisual();
}

void EnemyPlacementEditorScene::LoadPlayerVisual(const std::string& path) {
    try {
        const auto bytes=PlayerVisualSettings::ReadBytes(path);
        const auto data=nlohmann::json::parse(bytes);
        if (data.at("stage").at("id")!=stageId_) throw std::runtime_error("Player Visual stage ID mismatch");
        const auto settings=PlayerVisualSettings::FromStage(data);
        originalStageBytes_=bytes; playerVisualSettings_=playerVisualBaseline_=settings;
        playerVisualLoadError_=false; playerVisualStatus_="読み込み完了。見た目だけに適用します。";
        SyncPlayerVisual();
    } catch (const std::exception& error) {
        playerVisualLoadError_=true; playerVisualStatus_=error.what();
        // Show the same safe fallback as the game, never the previous stage's draft.
        playerVisualSettings_=playerVisualBaseline_=PlayerVisualSettings{}; SyncPlayerVisual();
    }
}

bool EnemyPlacementEditorScene::SavePlayerVisual(const std::string& path) {
    if (playerVisualLoadError_ || !ready_) return false;
    std::string error;
    if (!playerVisualSettings_.SaveStage(path,stageId_,originalStageBytes_,error)) {
        playerVisualStatus_="保存失敗: "+error; return false;
    }
    originalStageBytes_=PlayerVisualSettings::ReadBytes(path); playerVisualBaseline_=playerVisualSettings_;
    playerVisualStatus_="Player Visualのみ保存しました。敵配置は変更していません。";
    return true;
}

void EnemyPlacementEditorScene::SyncPlayerVisual() {
    if (playerVisual_) playerVisual_->Update({{1,1,1},level_.playerRotation,level_.playerPosition},playerVisualSettings_);
}

void EnemyPlacementEditorScene::FocusPlayerVisual() {
    // Only the editor viewing camera moves. Player/FPS spawn pose remains intact.
    mode_=CameraMode::Free; camera_.SetOrthographic(false);
    const float yaw=level_.playerRotation.y;
    const Vector3 offset{2.1f*std::cos(yaw)+1.75f*std::sin(yaw),1.8f,-2.1f*std::sin(yaw)+1.75f*std::cos(yaw)};
    const auto position=level_.playerPosition+offset,target=level_.playerPosition+Vector3{0,.9f,0};
    const auto delta=target-position;
    camera_.SetTranslate(position); camera_.SetRotate({-std::atan2(delta.y,std::hypot(delta.x,delta.z)),std::atan2(delta.x,delta.z),0});
    camera_.Update(); floor_=metadata_.FloorAt(level_.playerPosition.y); ApplyVisibility();
}

void EnemyPlacementEditorScene::Generate(const EnemySpawnPoint& spawn,EnemyPlacementGroup& group,bool preserveIds,bool disableFailures) {
    std::vector<std::string> enemies;
    if (preserveIds) { for (const auto& p:group.enemies) enemies.push_back(p.enemy); }
    else if (!spawn.enemyComposition.empty()) for (const auto& row:spawn.enemyComposition)
        for (int i=0;i<row.count;++i) enemies.push_back(row.enemyId);
    else for (int i=0;i<spawn.spawnCount;++i) enemies.push_back(spawn.enemyId.empty() ?
        (spawn.enemyPool.empty() ? "normal" : spawn.enemyPool.front().id) : spawn.enemyId);
    std::vector<std::pair<Vector3,float>> occupied;
    for (const auto& [id,g]:placements_.groups) if (id!=spawn.id) {
        const auto* other=spawns_.FindPoint(id); if (!other) continue;
        for (const auto& p:g.enemies) if (p.enabled) if (const auto* d=definitions_.Find(p.enemy))
            occupied.push_back({EnemyPlacements::WorldPosition(p,other->position,other->rotation),d->collisionRadius});
    }
    std::vector<EnemyPlacement> result;
    for (size_t i=0;i<enemies.size();++i) {
        EnemyPlacement p;
        if (preserveIds && i<group.enemies.size()) p=group.enemies[i]; else p.id=placements_.NewId();
        p.enemy=enemies[i];
        const auto* definition=definitions_.Find(p.enemy); if (!definition) continue;
        if (!preserveIds) {
            const float angle=static_cast<float>(i)*2.39996323f;
            const float radius=std::min(group.radius,std::sqrt(static_cast<float>(i))*(definition->collisionRadius*2+.2f));
            auto position=spawn.position+Vector3{std::cos(angle)*radius,0,std::sin(angle)*radius};
            EnemyPlacements::SnapFloor(position,level_.collision,.6f,1.5f);
            p.offset=EnemyPlacements::LocalPosition(position,spawn.position,spawn.rotation); p.yaw=group.initialYaw;
        }
        bool found=false; std::string reason;
        // Deterministic bounded disk search. Preview, save and gameplay share the result.
        for (int attempt=0;attempt<192;++attempt) {
            const float angle=static_cast<float>(attempt)*2.39996323f;
            const float radius=group.radius*std::sqrt(static_cast<float>(attempt)/191);
            Vector3 position=spawn.position+Vector3{std::cos(angle)*radius,0,std::sin(angle)*radius};
            if (!EnemyPlacements::SnapFloor(position,level_.collision,.6f,1.5f)) { reason="足元に床がありません"; continue; }
            reason=EnemyPlacements::InvalidReason(position,*definition,level_.collision);
            if (!reason.empty()) continue;
            bool overlap=false;
            for (const auto& [other,r]:occupied) {
                const auto delta=position-other;
                if (std::abs(delta.y)<definition->collisionHeight && std::hypot(delta.x,delta.z)<r+definition->collisionRadius+.08f) { overlap=true; break; }
            }
            if (overlap) { reason="敵同士の間隔が不足しています"; continue; }
            p.offset=EnemyPlacements::LocalPosition(position,spawn.position,spawn.rotation);
            p.yaw=group.initialYaw;
            if (group.randomYaw) {
                const uint32_t seed=static_cast<uint32_t>(std::hash<std::string>{}(p.id));
                p.yaw+=static_cast<float>(seed%36000)*(pi/18000);
            }
            occupied.push_back({position,definition->collisionRadius}); found=true; break;
        }
        if (!found) {
            if (disableFailures) p.enabled=false;
            warnings_.push_back(spawn.id+" / "+p.id+(disableFailures ? ": 自動配置失敗（無効化）: " : ": 初期配置の検証失敗（元の体数を保持）: ")+reason);
        }
        result.push_back(std::move(p));
    }
    group.enemies=std::move(result);
}

bool EnemyPlacementEditorScene::VisibleFloor(int floor) const {
    return floor_==0 || floor==0 || floor==floor_ || (floor>floor_ && showUpper_) || (floor<floor_ && showLower_);
}
void EnemyPlacementEditorScene::ApplyVisibility() {
    if (!ready_) return;
    const auto* model=environment_.GetModel(); std::vector<uint32_t> hidden;
    for (uint32_t i=0;i<model->GetMeshCount();++i) {
        const auto name=model->GetNodeName(model->GetMeshOwnerNodeIndex(i));
        auto tag=metadata_.Node(name.empty() ? model->GetModelData().meshes[i].name : name);
        if (!VisibleFloor(tag.floor) || (tag.kind=="roof" && !showRoof_) || (tag.kind=="ceiling" && !showCeiling_) || (tag.kind=="wall" && !showWalls_)) hidden.push_back(i);
    }
    environment_.SetHiddenMeshes(std::move(hidden));
    for (size_t i=0;i<shutters_.size();++i) shutters_[i]->SetIsVisible(VisibleFloor(metadata_.FloorAt(level_.shutters.shutters[i].Position().y)));
}

void EnemyPlacementEditorScene::SyncPreviews(GameApp& app) {
    std::set<std::string> wanted;
    bool removal=false;
    for (const auto& [spawn,g]:placements_.groups) if (const auto* point=spawns_.FindPoint(spawn)) for (const auto& p:g.enemies) {
        wanted.insert(p.id);
        const auto* d=definitions_.Find(p.enemy); if (!d) continue;
        auto it=previews_.find(p.id);
        if (it!=previews_.end() && it->second.definition!=p.enemy) { app.Dx()->WaitForGPU(); previews_.erase(it); }
        auto& preview=previews_[p.id];
        if (!preview.enemy) {
            preview.definition=p.enemy; preview.enemy=std::make_unique<Enemy>();
            preview.enemy->PreparePlacementPreview(app.ObjCom(),app.Dx(),&camera_,*d);
            preview.enemy->ResetForSpawn(0,spawn,{},{}); preview.enemy->SetAIPaused(true);
        }
        preview.enemy->SetPosition(EnemyPlacements::WorldPosition(p,point->position,point->rotation));
        preview.enemy->SetRotation({0,point->rotation.y+p.yaw,0});
        preview.enemy->HighlightPlacement(selectedId_==p.id || multiSelection_.contains(p.id)); preview.enemy->UpdateVisuals(0);
    }
    for (const auto& [id,_]:previews_) if (!wanted.contains(id)) { removal=true; break; }
    if (removal) { app.Dx()->WaitForGPU(); std::erase_if(previews_,[&](const auto& entry){return !wanted.contains(entry.first);}); }
}
EnemyPlacement* EnemyPlacementEditorScene::SelectedEnemy() {
    const auto it=placements_.groups.find(selectedSpawn_); if (it==placements_.groups.end()) return nullptr;
    for (auto& p:it->second.enemies) if (p.id==selectedId_) return &p;
    return nullptr;
}
Vector3 EnemyPlacementEditorScene::SelectedPosition() const {
    if (cameraSequenceMode_) {
        for (const auto& d:cameraDrafts_) if (d.id==selectedCameraSequence_)
            for (size_t i=0;i<d.points.size();++i) if (d.points[i].id==selectedCameraPoint_) return CameraPointPose(i).position;
    }
    const auto* point=SelectedSpawn(); if (!point) return level_.playerPosition;
    const auto group=placements_.groups.find(point->id);
    if (group!=placements_.groups.end()) for (const auto& p:group->second.enemies) if (p.id==selectedId_)
        return EnemyPlacements::WorldPosition(p,point->position,point->rotation);
    return point->position;
}
void EnemyPlacementEditorScene::FocusSelection() {
    if (cameraSequenceMode_) { FocusCameraPoint(); return; }
    const auto target=SelectedPosition();
    if (mode_==CameraMode::Top) camera_.SetTranslate({target.x,target.y+topHeight_,target.z});
    else if (mode_==CameraMode::FPS) {
        auto position=target+Vector3{0,0,-4};
        if (EnemyPlacements::SnapFloor(position,level_.collision) && EnemyNavigation::Clear(&level_.collision,position,position,.4f,1.8f)) {
            player_.SetStage(&level_.collision,position,{}); player_.RefreshVisuals();
        } else status_="敵の手前に安全なFPS移動先がありません";
    } else { camera_.SetTranslate(target+Vector3{0,4,-7}); camera_.SetRotate({.42f,0,0}); }
    camera_.Update();
}
void EnemyPlacementEditorScene::SwitchCamera(CameraMode mode) {
    if (mode==mode_) return;
    const auto p=camera_.GetTranslate();
    if (mode==CameraMode::FPS) {
        Vector3 feet=p-Vector3{0,player_.Settings().cameraHeight,0};
        if (EnemyPlacements::SnapFloor(feet,level_.collision,1,8) && EnemyNavigation::Clear(&level_.collision,feet,feet,.4f,1.8f))
            player_.SetStage(&level_.collision,feet,{std::clamp(camera_.GetRotate().x,-1.4f,1.4f),camera_.GetRotate().y,0});
        else status_="FPSカメラは前回の安全なプレイヤー位置を使用します";
        camera_.SetOrthographic(false); player_.RefreshVisuals();
    } else if (mode==CameraMode::Top) {
        const float base=floor_>0 ? metadata_.floors[static_cast<size_t>(floor_-1)] : level_.playerPosition.y;
        camera_.SetTranslate({p.x,base+topHeight_,p.z}); camera_.SetRotate({pi*.5f,0,0});
        camera_.SetOrthographic(true,zoom_); showRoof_=showCeiling_=showUpper_=false;
    } else if (mode==CameraMode::Front || mode==CameraMode::Side) {
        const auto target=SelectedPosition();
        camera_.SetTranslate(mode==CameraMode::Front ? target+Vector3{0,0,-topHeight_} : target+Vector3{-topHeight_,0,0});
        camera_.SetRotate(mode==CameraMode::Front ? Vector3{} : Vector3{0,pi*.5f,0}); camera_.SetOrthographic(true,zoom_);
    } else { camera_.SetOrthographic(false); camera_.SetRotate({std::min(camera_.GetRotate().x,1.4f),camera_.GetRotate().y,0}); }
    mode_=mode; camera_.Update(); ApplyVisibility();
}
void EnemyPlacementEditorScene::StartTest(GameApp& app) {
    StopCameraPreview();
    if (loadError_ || !ready_) return;
    savedCameraPosition_=camera_.GetTranslate(); savedCameraRotation_=camera_.GetRotate(); beforeTestMode_=mode_;
    test_=std::make_unique<GameScene>(false,stageId_); test_->ConfigurePlacementTest(placements_);
    test_->OnEnter(app);
    if (!test_->IsReadyForTransition()) { status_=test_->InitializationError(); StopTest(app); return; }
    status_="テスト中。F8 または「編集へ戻る」で終了します。未保存の編集内容を使用しています。";
}
void EnemyPlacementEditorScene::StopTest(GameApp& app) {
    if (!test_) return;
    app.Dx()->WaitForGPU(); test_->OnExit(app); test_.reset();
    app.ObjCom()->SetDefaultCamera(&camera_); app.GetInput()->SetCameraToggleKeyEnabled(false);
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    app.ImGui()->SetWeaponWorkspace(true); ImGui::GetIO().ConfigFlags &= ~mouseFlags;
#endif
    mode_=beforeTestMode_; camera_.SetTranslate(savedCameraPosition_); camera_.SetRotate(savedCameraRotation_);
    camera_.SetOrthographic(OrthographicView(),zoom_); camera_.Update(); SyncPreviews(app);
    environment_.Update(0); for (auto& shutter:shutters_) shutter->Update(0);
    status_="編集へ戻りました。テスト中の敵の移動は配置データへ反映しません。";
}
void EnemyPlacementEditorScene::RequestAction(const std::string& action) {
    if (Dirty()) { confirmAction_=action; discardModal_=true; }
    else pendingAction_=action;
}
void EnemyPlacementEditorScene::Update(GameApp& app,float dt) {
    if (!pendingAction_.empty()) {
        const auto action=std::exchange(pendingAction_,{});
        if (action=="test") StartTest(app);
        else if (HandleCameraAction(action)) {}
        else if (action=="stop") StopTest(app);
        else if (action=="savePlayerVisual") SavePlayerVisual(StagePath());
        else if (action=="reloadPlayerVisual") LoadPlayerVisual(StagePath());
        else if (action=="focusPlayerVisual") FocusPlayerVisual();
        else if (action=="title") { app.Scenes().TransitionTo("Title"); return; }
        else if (action=="quit") { app.Win()->SetCloseGuard({}); PostMessageW(app.Win()->GetHwnd(),WM_CLOSE,0,0); return; }
        else LoadStage(app,action=="reload" ? stageId_ : action);
    }
    auto& input=*app.GetInput();
    if (test_) {
        if (input.HasFocus() && input.IsKeyTrigger(DIK_F8)) { StopTest(app); return; }
        test_->Update(app,dt); test_->ClearNextScene_(); return;
    }
    if (!ready_) return;
    if (cameraPreviewActive_) {
        input.SetCameraControlEnabled(false);
#ifdef USE_IMGUI
        ImGui::GetIO().ConfigFlags &= ~mouseFlags;
#endif
        UpdateCameraPreview(dt);
        environment_.Update(0); for (auto& shutter:shutters_) shutter->Update(0);
        SyncPreviews(app); SyncPlayerVisual();
        return;
    }
    RECT rect{}; bool viewReady=true;
#ifdef USE_IMGUI
    viewReady=app.ImGui()->GetSceneImageRect(rect);
    input.SetMouseCaptureRect(viewReady ? &rect : nullptr);
    const bool typing=ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId);
#else
    const bool typing=false;
#endif
    if (!input.HasFocus() || !viewReady || input.IsKeyTrigger(DIK_ESCAPE) || typing) input.SetCameraControlEnabled(false);
    else if (input.IsKeyTrigger(DIK_TAB)) input.SetCameraControlEnabled(!input.IsCameraControlEnabled());
    if (input.HasFocus() && !typing && input.IsKeyTrigger(DIK_F6)) RequestAction("title");
    if (input.HasFocus() && !typing && input.IsKeyTrigger(DIK_F)) FocusSelection();
    if (cameraSequenceMode_ && input.HasFocus() && !typing && input.IsKeyPressed(DIK_LCONTROL)) {
        if (input.IsKeyTrigger(DIK_Z)) { if (!drawPathPoints_.empty()) drawPathPoints_.pop_back(); else UndoCameraSequence(false); }
        if (input.IsKeyTrigger(DIK_Y) && drawPathPoints_.empty()) UndoCameraSequence(true);
    }
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~mouseFlags)|(input.IsCameraControlEnabled() ? mouseFlags : 0);
#endif
    dt=std::isfinite(dt) ? std::clamp(dt,0.f,.1f) : 0;
    if (mode_==CameraMode::FPS) player_.Update(input,dt);
    else if (input.IsCameraControlEnabled() && input.HasFocus()) {
        auto p=camera_.GetTranslate(),r=camera_.GetRotate(); const auto mouse=input.GetMouseDelta();
        if (mode_==CameraMode::Free) { r.x=std::clamp(r.x+static_cast<float>(mouse.y)*.003f,-1.55f,1.55f); r.y+=static_cast<float>(mouse.x)*.003f; }
        const float step=speed_*dt*(input.IsKeyPressed(DIK_LSHIFT) ? 3.f : 1.f);
        const Vector3 right{std::cos(r.y),0,-std::sin(r.y)},forward=(mode_==CameraMode::Front || mode_==CameraMode::Side) ? Vector3{0,1,0} : Vector3{std::sin(r.y),0,std::cos(r.y)};
        if (input.IsKeyPressed(DIK_W)) p+=forward*step;
        if (input.IsKeyPressed(DIK_S)) p-=forward*step;
        if (input.IsKeyPressed(DIK_D)) p+=right*step;
        if (input.IsKeyPressed(DIK_A)) p-=right*step;
        if (input.IsKeyPressed(DIK_E)) p.y+=step;
        if (input.IsKeyPressed(DIK_Q)) p.y-=step;
        if (mode_==CameraMode::Free && input.IsKeyPressed(DIK_LMENU)) {
            const auto target=SelectedPosition()+Vector3{0,1,0}; const float distance=std::max(2.f,StageLength(p-target));
            p=target-Vector3{std::sin(r.y)*std::cos(r.x),-std::sin(r.x),std::cos(r.y)*std::cos(r.x)}*distance;
        }
        camera_.SetTranslate(p); camera_.SetRotate(r);
    }
    camera_.Update(); environment_.Update(0); for (auto& shutter:shutters_) shutter->Update(0);
    SyncPreviews(app);
    SyncPlayerVisual();
}
void EnemyPlacementEditorScene::DrawRender(GameApp& app) {
    if (test_) { test_->DrawRender(app); return; }
    if (!ready_) return;
    environment_.Draw(); for (auto& shutter:shutters_) shutter->Draw();
    if (showPlayerVisual_ && playerVisual_ && (cameraPreviewActive_ || VisibleFloor(metadata_.FloorAt(level_.playerPosition.y)))) {
        const auto distance=camera_.GetTranslate()-(level_.playerPosition+Vector3{0,player_.Settings().cameraHeight,0});
        if (StageLength(distance)>(cameraPreviewActive_ ? .85f : .4f)) playerVisual_->Draw();
    }
    if (showEnemies_) for (const auto& [spawn,g]:placements_.groups) if (const auto* point=spawns_.FindPoint(spawn))
        for (const auto& p:g.enemies) if (cameraPreviewActive_ || VisibleFloor(metadata_.FloorAt(EnemyPlacements::WorldPosition(p,point->position,point->rotation).y)))
            if (const auto it=previews_.find(p.id); it!=previews_.end()) it->second.enemy->Draw(false);
}

void EnemyPlacementEditorScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Enemy Placement Editor",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
    if (test_) {
        ImGui::TextUnformatted("配置テストプレイ / F8: 編集へ戻る　Esc: マウス解放　画面クリック: FPS操作");
        if (ImGui::Button("編集へ戻る")) pendingAction_="stop";
        if (test_->PlacementTestFinished()) ImGui::TextUnformatted("テスト終了（死亡・クリア）。編集へ戻って再テストできます。");
        if (ImGui::BeginCombo("Manual 出現", "起動するEnemySpawnを選択")) {
            for (const auto& point:spawns_.Points()) if (point.activation==SpawnActivation::Manual)
                if (ImGui::Selectable(point.id.c_str())) test_->ActivateEnemySpawn(point.id);
            ImGui::EndCombo();
        }
        app.ImGui()->DrawScenePreview(); ImGui::End(); return;
    }
    if (ImGui::BeginTable("EditorLayout",2,ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("操作",ImGuiTableColumnFlags_WidthFixed,std::min(410.f,viewport->WorkSize.x*.42f));
        ImGui::TableSetupColumn("映像",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1); ImGui::BeginChild("ビュー");
        app.ImGui()->DrawScenePreview(); DrawViewportTools(app); ImGui::EndChild();
        ImGui::TableSetColumnIndex(0); ImGui::BeginChild("設定"); DrawControls(app); ImGui::EndChild(); ImGui::EndTable();
    }
    if (saveModal_) { ImGui::OpenPopup("保存内容の確認"); saveModal_=false; }
    if (ImGui::BeginPopupModal("保存内容の確認",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("有効な個別配置数を出現数に反映します。起動条件・出現間隔・Max Aliveは維持します。");
        ImGui::TextWrapped("保存先: %s",PlacementPath().c_str());
        if (ImGui::BeginChild("変更一覧",{640,260},true)) {
            const auto current=placements_.Json();
            for (const auto& row:current["spawns"]) {
                const auto id=row["spawnId"].get<std::string>();
                const nlohmann::json* old=nullptr;
                for (const auto& before:baseline_["spawns"]) if (before["spawnId"]==id) { old=&before; break; }
                if (old && *old==row) continue;
                ImGui::SeparatorText(id.c_str());
                ImGui::TextWrapped("変更後: %s",row.dump().c_str());
                if (old) ImGui::TextWrapped("変更前: %s",old->dump().c_str());
            }
            if (!PlacementsDirty()) ImGui::TextUnformatted("読み込み時からの敵配置の差分はありません。現在の自動配置を保存できます。");
        } ImGui::EndChild();
        if (ImGui::Button("確認して保存")) {
            if (placements_.Save(PlacementPath(),definitions_,originalBytes_)) {
                baseline_=placements_.Json(); originalBytes_=EnemyPlacements::ReadBytes(PlacementPath()); status_="配置を保存しました（前回ファイルは .bak）。";
                ImGui::CloseCurrentPopup();
            } else status_="保存失敗: "+placements_.error;
        }
        ImGui::SameLine(); if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
        ImGui::TextWrapped("%s",status_.c_str()); ImGui::EndPopup();
    }
    if (discardModal_) { ImGui::OpenPopup("未保存の変更があります"); discardModal_=false; }
    if (ImGui::BeginPopupModal("未保存の変更があります",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(confirmAction_=="cameraReload" ? "カメラ経路の未保存変更を破棄して再読み込みしますか？ 敵配置・Player Visualの編集は保持します。" : "現在の変更を破棄して移動・再読み込みしますか？");
        if (ImGui::Button("変更を破棄して続ける")) { pendingAction_=confirmAction_; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("編集を続ける")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
#else
    (void)app;
#endif
}

void EnemyPlacementEditorScene::DrawControls(GameApp& app) {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("Enemy Placement Editor");
    ImGui::TextUnformatted(Dirty() ? "● 未保存の変更あり" : "読み込み・保存時の状態");
    ImGui::BeginDisabled(cameraPreviewActive_);
    if (ImGui::BeginCombo("ステージ",stageId_.c_str())) {
        for (const auto& entry:std::filesystem::directory_iterator("resources/levels")) if (entry.is_directory()) {
            const auto id=entry.path().filename().string();
            if (std::filesystem::is_regular_file(entry.path()/(id+".json")) && id!="title" && id!="showroom")
                if (ImGui::Selectable(id.c_str(),id==stageId_)) RequestAction(id);
        }
        ImGui::EndCombo();
    }
    if (ImGui::RadioButton("Enemy Placement",!cameraSequenceMode_)) pendingAction_="enemyMode";
    ImGui::SameLine();
    if (ImGui::RadioButton("Camera Sequence",cameraSequenceMode_)) pendingAction_="cameraMode";
    ImGui::EndDisabled();
    if (!cameraSequenceMode_ && ImGui::CollapsingHeader("Player Visual",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("仮プレイヤーモデルを表示",&showPlayerVisual_);
        ImGui::Checkbox("プレイヤーとモデルの正面矢印",&showPlayerDirections_);
        ImGui::BeginDisabled(!ready_ || playerVisualLoadError_ || cameraPreviewActive_);
        ImGui::SliderFloat("Yaw offset##PlayerVisualSlider",&playerVisualSettings_.yawOffsetDegrees,-180,180,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::InputFloat("Yaw (degrees)##PlayerVisualInput",&playerVisualSettings_.yawOffsetDegrees,1,15,"%.2f")) {
            auto& yaw=playerVisualSettings_.yawOffsetDegrees;
            yaw=std::isfinite(yaw) ? std::clamp(yaw,-180.f,180.f) : playerVisualBaseline_.yawOffsetDegrees;
        }
        if (ImGui::Button("Player Visualのみ保存")) pendingAction_="savePlayerVisual";
        ImGui::SameLine(); if (ImGui::Button("読込値へ戻す##PlayerVisualReset")) playerVisualSettings_=playerVisualBaseline_;
        ImGui::EndDisabled();
        ImGui::BeginDisabled(cameraPreviewActive_);
        if (ImGui::Button("Player Visualを再読込（編集破棄）")) pendingAction_="reloadPlayerVisual";
        if (ImGui::Button("Player Visualへカメラ移動")) pendingAction_="focusPlayerVisual";
        ImGui::EndDisabled();
        ImGui::TextUnformatted(PlayerVisualDirty() ? "Player Visual: 未保存" : "Player Visual: 読込・保存時の状態");
        ImGui::TextColored({.2f,1,.5f,1},"Green: FPS forward (spawn)");
        ImGui::TextColored({1,.65f,.15f,1},"Orange: model forward");
        ImGui::TextWrapped("Normalの補正は +90度。FPS・Playerの向きや敵配置は変更しません。表示切替はプレビュー専用です。");
        ImGui::TextWrapped("%s",playerVisualStatus_.c_str());
    }
    if (cameraSequenceMode_) { DrawCameraSequenceControls(app); return; }
    ImGui::TextWrapped("Tab: UI／カメラ操作　Esc: カーソル解放\nWASD: 移動　Q/E: 上下　Shift: 高速\nF: 選択へ移動　Alt＋視点操作: 選択を中心に回転\n俯瞰: ホイールでズーム・中ドラッグで平行移動\nCtrl＋クリック: 複数選択　Shift＋ドラッグ: 範囲選択\nF6: タイトル　F8: テスト終了");
    ImGui::Text("操作: %s",app.GetInput()->IsCameraControlEnabled() ? "カメラ" : "UI・配置選択");
    ImGui::SeparatorText("カメラ");
    if (ImGui::RadioButton("自由",mode_==CameraMode::Free)) SwitchCamera(CameraMode::Free);
    ImGui::SameLine(); if (ImGui::RadioButton("FPS",mode_==CameraMode::FPS)) SwitchCamera(CameraMode::FPS);
    ImGui::SameLine(); if (ImGui::RadioButton("俯瞰",mode_==CameraMode::Top)) SwitchCamera(CameraMode::Top);
    ImGui::SliderFloat("移動速度",&speed_,.5f,50,"%.1f m/s");
    if (mode_==CameraMode::Top) {
        if (ImGui::SliderFloat("俯瞰の高さ",&topHeight_,5,100)) {
            auto p=camera_.GetTranslate(); p.y=(floor_>0 ? metadata_.floors[static_cast<size_t>(floor_-1)] : 0)+topHeight_; camera_.SetTranslate(p);
        }
        if (ImGui::SliderFloat("表示範囲",&zoom_,3,100)) camera_.SetOrthographic(true,zoom_);
    }
    if (ImGui::Button("選択へカメラを移動 (F)")) FocusSelection();
    ImGui::SeparatorText("編集階層");
    for (size_t i=0;i<metadata_.floors.size();++i) {
        if (i) ImGui::SameLine();
        const auto name=std::to_string(i+1)+"F";
        if (ImGui::RadioButton(name.c_str(),floor_==static_cast<int>(i+1))) {
            floor_=static_cast<int>(i+1); ApplyVisibility();
            if (mode_==CameraMode::Top) { auto p=camera_.GetTranslate(); p.y=metadata_.floors[i]+topHeight_; camera_.SetTranslate(p); }
        }
    }
    ImGui::SameLine(); if (ImGui::RadioButton("全階",floor_==0)) { floor_=0; ApplyVisibility(); }
    ImGui::BeginDisabled(loadError_ || !ready_);
    if (ImGui::Button("保存")) saveModal_=true;
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("再読み込み・編集前へ戻す")) RequestAction("reload");
    ImGui::BeginDisabled(loadError_ || !ready_);
    if (ImGui::Button("テストプレイ開始")) pendingAction_="test";
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("タイトルへ戻る")) RequestAction("title");
    ImGui::TextWrapped("%s",status_.c_str());
    if (!ready_) return;
    if (ImGui::CollapsingHeader("ステージ表示設定")) {
        bool changed=false;
        changed=ImGui::Checkbox("屋根を表示",&showRoof_) || changed;
        changed=ImGui::Checkbox("天井を表示",&showCeiling_) || changed;
        changed=ImGui::Checkbox("上階を表示",&showUpper_) || changed;
        changed=ImGui::Checkbox("下階を表示",&showLower_) || changed;
        changed=ImGui::Checkbox("壁を表示",&showWalls_) || changed;
        ImGui::Checkbox("敵モデルを表示",&showEnemies_); ImGui::Checkbox("EnemySpawnを表示",&showSpawns_);
        ImGui::Checkbox("Colliderを表示",&showColliders_); ImGui::Checkbox("グリッドを表示",&showGrid_);
        if (changed) ApplyVisibility();
        ImGui::TextWrapped("描画のみを切り替えます。Colliderはそのまま有効です。未分類の構造物は表示を維持します。");
    }
    ImGui::SeparatorText("敵配置一覧"); ImGui::InputText("ID検索",search_,sizeof(search_));
    for (size_t f=1;f<=metadata_.floors.size();++f) {
        if (floor_!=0 && floor_!=static_cast<int>(f)) continue;
        const auto label=std::to_string(f)+"F";
        if (!ImGui::TreeNodeEx(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen)) continue;
        std::map<std::string,std::vector<const EnemySpawnPoint*>> rooms;
        for (const auto& point:spawns_.Points()) {
            const auto tag=metadata_.Spawn(point.id,point.position);
            if (tag.floor==static_cast<int>(f)) rooms[tag.room].push_back(&point);
        }
        for (const auto& [room,points]:rooms) if (ImGui::TreeNodeEx(room.c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto* point:points) {
                const auto found=placements_.groups.find(point->id);
                if (found==placements_.groups.end()) continue;
                const auto& group=found->second;
                bool match=point->id.find(search_)!=std::string::npos;
                for (const auto& p:group.enemies) match=match || p.id.find(search_)!=std::string::npos || p.enemy.find(search_)!=std::string::npos;
                if (!match) continue;
                ImGui::PushID(point->id.c_str());
                const bool open=ImGui::TreeNodeEx(point->id.c_str(),ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_DefaultOpen|
                    (selectedSpawn_==point->id && selectedId_.empty() ? ImGuiTreeNodeFlags_Selected : 0));
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { selectedSpawn_=point->id; selectedId_.clear(); multiSelection_.clear(); }
                if (open) {
                    std::map<std::string,int> counts;
                    for (const auto& p:group.enemies) if (p.enabled) ++counts[p.enemy];
                    for (const auto& [enemy,count]:counts) ImGui::Text("%s × %d",enemy.c_str(),count);
                    for (const auto& p:group.enemies) {
                        const auto text=p.enemy+" / "+p.id+(p.enabled ? "" : " [無効]");
                        if (ImGui::Selectable(text.c_str(),p.id==selectedId_ || multiSelection_.contains(p.id))) {
                            if (!ImGui::GetIO().KeyCtrl) multiSelection_.clear();
                            multiSelection_.insert(p.id); selectedSpawn_=point->id; selectedId_=p.id;
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    if (const auto* point=SelectedSpawn()) {
        auto& group=placements_.groups[point->id];
        ImGui::SeparatorText("選択中のEnemySpawn"); ImGui::TextWrapped("%s",point->id.c_str());
        ImGui::Checkbox("このEnemySpawnの編集配置を使用",&group.overrideEnabled);
        if (point->activation==SpawnActivation::Legacy) ImGui::TextWrapped("旧共有設定：初期状態は元の種類抽選・出現総数を使用します。上のチェックを入れると、編集した配置・敵定義を順番に繰り返します。共有トリガーの総数は変わりません。");
        ImGui::Text("起動: %s / Max Alive: %d",point->activation==SpawnActivation::StageStart ? "Stage Start" :
            point->activation==SpawnActivation::Manual ? "Manual" : point->activation==SpawnActivation::OnTrigger ? "On Trigger" : "旧共有設定",point->maxAlive);
        ImGui::Text("初期遅延: %.2f秒 / 出現間隔: %.2f秒 / %s",point->initialDelay,point->spawnInterval,point->mode==SpawnMode::Sequential ? "Sequential" : "Simultaneous");
        ImGui::BeginDisabled(!group.overrideEnabled);
        bool automatic=group.automatic;
        if (ImGui::RadioButton("自動配置",automatic)) group.automatic=true;
        ImGui::SameLine(); if (ImGui::RadioButton("個別配置",!automatic)) group.automatic=false;
        ImGui::SliderFloat("配置半径",&group.radius,0,20,"%.1f m");
        float yaw=group.initialYaw*degrees;
        if (ImGui::DragFloat("初期Yaw（相対・度）",&yaw,1)) group.initialYaw=yaw/degrees;
        ImGui::Checkbox("ランダム方向",&group.randomYaw);
        if (ImGui::Button("現在の体数を自動再配置")) { Generate(*point,group,true); group.automatic=true; }
        ImGui::TextWrapped("有効な配置数が出現数になります。無効な敵は出現しません。追加・削除時も他の個別IDを保持します。");
        ImGui::TextWrapped("自動再配置に失敗した敵は理由を表示して無効化します。無効なモデルも編集用に表示します。");
        if (ImGui::BeginCombo("種類別体数を調整","敵の種類を選択")) {
            for (const auto& [id,d]:definitions_.All()) {
                ImGui::PushID(id.c_str());
                int count=static_cast<int>(std::count_if(group.enemies.begin(),group.enemies.end(),[&](const auto& p){return p.enemy==id;}));
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputInt(d.displayName.c_str(),&count)) {
                    count=std::clamp(count,0,500);
                    int current=static_cast<int>(std::count_if(group.enemies.begin(),group.enemies.end(),[&](const auto& p){return p.enemy==id;}));
                    while (current<count) { group.enemies.push_back({placements_.NewId(),id,{},group.initialYaw,true}); ++current; }
                    while (current>count) {
                        const auto it=std::find_if(group.enemies.rbegin(),group.enemies.rend(),[&](const auto& p){return p.enemy==id;});
                        group.enemies.erase(std::next(it).base()); --current;
                    }
                    if (group.automatic) Generate(*point,group,true);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("敵を1体追加")) {
            const auto id=placements_.NewId(); group.enemies.push_back({id,"normal",{},group.initialYaw,true});
            selectedId_=id; multiSelection_={id}; group.automatic=false;
        }
        if (auto* p=SelectedEnemy()) {
            ImGui::SeparatorText("選択中の敵"); ImGui::TextWrapped("ID: %s",p->id.c_str());
            ImGui::Checkbox("有効",&p->enabled);
            if (ImGui::BeginCombo("Enemy Definition",p->enemy.c_str())) {
                for (const auto& [id,d]:definitions_.All()) if (ImGui::Selectable((d.displayName+" ("+id+")").c_str(),id==p->enemy)) p->enemy=id;
                ImGui::EndCombo();
            }
            auto position=EnemyPlacements::WorldPosition(*p,point->position,point->rotation);
            if (ImGui::DragFloat3("位置 X / Y / Z",&position.x,.05f,-100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp)) {
                if (gridSnap_) for (float* n:{&position.x,&position.y,&position.z}) *n=std::round(*n/grid_)*grid_;
                p->offset=EnemyPlacements::LocalPosition(position,point->position,point->rotation); group.automatic=false;
            }
            float angle=(point->rotation.y+p->yaw)*degrees;
            if (ImGui::DragFloat("Yaw（度）",&angle,1,-36000,36000,"%.1f",ImGuiSliderFlags_AlwaysClamp)) {
                if (rotationSnap_) angle=std::round(angle/angleSnap_)*angleSnap_;
                p->yaw=angle/degrees-point->rotation.y; group.automatic=false;
            }
            ImGui::Checkbox("グリッドへスナップ",&gridSnap_); ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            ImGui::DragFloat("間隔",&grid_,.05f,.05f,10,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("回転スナップ",&rotationSnap_); ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            ImGui::DragFloat("角度",&angleSnap_,1,1,90,"%.0f",ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("床へスナップ")) {
                if (EnemyPlacements::SnapFloor(position,level_.collision)) { p->offset=EnemyPlacements::LocalPosition(position,point->position,point->rotation); group.automatic=false; }
                else status_="床が見つかりません（上下3mの範囲）";
            }
            if (ImGui::Button("位置をリセット")) {
                p->offset={};
                for (const auto& row:baseline_["spawns"]) if (row["spawnId"]==point->id) for (const auto& old:row["enemies"]) if (old["id"]==p->id)
                    p->offset={old["offset"][0].get<float>(),old["offset"][1].get<float>(),old["offset"][2].get<float>()};
                group.automatic=false;
            }
            ImGui::SameLine(); if (ImGui::Button("回転をリセット")) { p->yaw=group.initialYaw; group.automatic=false; }
            ImGui::RadioButton("移動ギズモ",&gizmo_,0); ImGui::SameLine(); ImGui::RadioButton("回転ギズモ",&gizmo_,1);
            if (ImGui::Button("選択中の敵を削除")) {
                const auto id=p->id; std::erase_if(group.enemies,[&](const auto& row){return row.id==id;});
                selectedId_.clear(); multiSelection_.erase(id); group.automatic=false;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::Button("選択を解除")) { selectedId_.clear(); selectedSpawn_.clear(); multiSelection_.clear(); }
    }
    if (ImGui::CollapsingHeader("配置の検証・警告",ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("古い自動配置メッセージを消去")) warnings_.clear();
        for (const auto& warning:warnings_) ImGui::TextWrapped("%s",warning.c_str());
        std::vector<std::tuple<std::string,Vector3,const EnemyDefinition*>> active;
        for (const auto& [spawn,g]:placements_.groups) if (const auto* point=spawns_.FindPoint(spawn)) for (const auto& p:g.enemies) if (p.enabled) {
            const auto* d=definitions_.Find(p.enemy); if (!d) continue;
            const auto position=EnemyPlacements::WorldPosition(p,point->position,point->rotation);
            const auto reason=EnemyPlacements::InvalidReason(position,*d,level_.collision);
            if (!reason.empty()) ImGui::TextWrapped("%s / %s: %s",spawn.c_str(),p.id.c_str(),reason.c_str());
            for (const auto& [id,other,definition]:active) {
                const auto delta=position-other;
                if (position.y<other.y+definition->collisionHeight && other.y<position.y+d->collisionHeight && std::hypot(delta.x,delta.z)<definition->collisionRadius+d->collisionRadius)
                    ImGui::TextWrapped("敵同士の重なり: %s / %s",id.c_str(),p.id.c_str());
            }
            active.emplace_back(p.id,position,d);
        }
    }
#else
    (void)app;
#endif
}

void EnemyPlacementEditorScene::DrawViewportTools(GameApp& app) {
    if (cameraSequenceMode_) { DrawCameraSequenceViewport(app); return; }
#ifdef USE_IMGUI
    RECT rect{};
    if (!ready_ || !app.ImGui()->GetSceneImageRect(rect)) return;
    const ImVec2 lo{static_cast<float>(rect.left),static_cast<float>(rect.top)},hi{static_cast<float>(rect.right),static_cast<float>(rect.bottom)};
    const auto vp=camera_.GetViewProjectionMatrix(); auto* draw=ImGui::GetForegroundDrawList();
    const auto project=[&](const Vector3& p,ImVec2& out) {
        const float w=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
        const float z=p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2];
        if (w<=.0001f || z<0 || z>w) return false;
        const auto clip=StagePoint(p,vp);
        out={lo.x+(clip.x/w+1)*.5f*(hi.x-lo.x),lo.y+(1-clip.y/w)*.5f*(hi.y-lo.y)}; return true;
    };
    const auto line=[&](const Vector3& a,const Vector3& b,ImU32 color,float thickness=1.f) {
        ImVec2 x,y; if (project(a,x) && project(b,y)) draw->AddLine(x,y,color,thickness);
    };
    const auto& io=ImGui::GetIO(); const auto mouse=io.MousePos;
    const bool hover=app.ImGui()->IsSceneImageHovered() && !app.GetInput()->IsCameraControlEnabled();
    if (hover && mode_==CameraMode::Top) {
        if (io.MouseWheel!=0) { zoom_=std::clamp(zoom_*std::pow(.85f,io.MouseWheel),3.f,150.f); camera_.SetOrthographic(true,zoom_); }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            auto p=camera_.GetTranslate(); const float scale=zoom_/(hi.y-lo.y);
            p.x-=io.MouseDelta.x*scale; p.z+=io.MouseDelta.y*scale; camera_.SetTranslate(p);
        }
    }
    draw->PushClipRect(lo,hi,true);
    if (showPlayerDirections_ && VisibleFloor(metadata_.FloorAt(level_.playerPosition.y))) {
        const auto arrow=[&](const Vector3& origin,const Vector3& direction,ImU32 color,const char* label) {
            const auto tip=origin+direction*1.8f;
            const Vector3 side{-direction.z,0,direction.x};
            line(origin,tip,color,3);
            line(tip,tip-direction*.3f+side*.18f,color,3);
            line(tip,tip-direction*.3f-side*.18f,color,3);
            ImVec2 text; if (project(tip+Vector3{0,.06f,0},text)) draw->AddText(text,color,label);
        };
        const float yaw=level_.playerRotation.y;
        // Different heights keep the two arrows readable when they align.
        arrow(level_.playerPosition+Vector3{0,.06f,0},{std::sin(yaw),0,std::cos(yaw)},IM_COL32(50,255,130,255),"FPS forward");
        arrow(level_.playerPosition+Vector3{0,.18f,0},StartPlayerVisual::Forward(yaw,playerVisualSettings_),IM_COL32(255,170,40,255),"Model forward");
    }
    if (showGrid_) {
        const float y=floor_>0 ? metadata_.floors[static_cast<size_t>(floor_-1)]+.025f : .025f;
        const auto center=SelectedPosition(); const float size=std::max(grid_,.5f);
        const float x=std::round(center.x/size)*size,z=std::round(center.z/size)*size;
        for (int i=-30;i<=30;++i) {
            const float step=static_cast<float>(i)*size;
            line({x+step,y,z-30*size},{x+step,y,z+30*size},IM_COL32(110,160,170,80));
            line({x-30*size,y,z+step},{x+30*size,y,z+step},IM_COL32(110,160,170,80));
        }
    }
    struct Marker { std::string spawn,id; Vector3 position; };
    std::vector<Marker> markers;
    for (const auto& point:spawns_.Points()) {
        if (showSpawns_ && VisibleFloor(metadata_.Spawn(point.id,point.position).floor)) {
            const auto p=point.position+Vector3{0,.15f,0}; ImVec2 pixel;
            if (project(p,pixel)) {
                draw->AddCircle(pixel,selectedSpawn_==point.id && selectedId_.empty() ? 12.f : 8.f,IM_COL32(30,240,210,255),16,2);
                draw->AddText({pixel.x+10,pixel.y},IM_COL32(70,255,220,255),point.id.c_str());
                markers.push_back({point.id,{},p});
            }
        }
        const auto group=placements_.groups.find(point.id); if (group==placements_.groups.end()) continue;
        for (const auto& p:group->second.enemies) {
            const auto position=EnemyPlacements::WorldPosition(p,point.position,point.rotation);
            if (!VisibleFloor(metadata_.FloorAt(position.y))) continue;
            const auto* definition=definitions_.Find(p.enemy); if (!definition) continue;
            const bool selected=selectedId_==p.id || multiSelection_.contains(p.id);
            const auto color=selected ? IM_COL32(255,210,20,255) : p.enabled ? IM_COL32(130,200,255,240) : IM_COL32(255,100,100,230);
            const float yaw=point.rotation.y+p.yaw;
            // Existing enemy forward is local -X: AI uses yaw=atan2(dx,dz)+pi/2.
            line(position+Vector3{0,.12f,0},position+Vector3{-std::cos(yaw)*1.4f,.12f,std::sin(yaw)*1.4f},color,3);
            ImVec2 pixel;
            if (project(position+Vector3{0,definition->collisionHeight*.5f,0},pixel)) {
                draw->AddCircle(pixel,selected ? 10.f : 5.f,color,16,2);
                if (selected) draw->AddText({pixel.x+12,pixel.y},color,p.id.c_str());
                markers.push_back({point.id,p.id,position+Vector3{0,definition->collisionHeight*.5f,0}});
            }
            if (showColliders_ || selected) {
                constexpr int segments=24; const float r=definition->collisionRadius;
                for (int i=0;i<segments;++i) {
                    const float a=2*pi*static_cast<float>(i)/segments,b=2*pi*static_cast<float>(i+1)/segments;
                    const Vector3 v{r*std::cos(a),0,r*std::sin(a)},w{r*std::cos(b),0,r*std::sin(b)},up{0,definition->collisionHeight,0};
                    line(position+v,position+w,color); line(position+v+up,position+w+up,color);
                    if (i%6==0) line(position+v,position+v+up,color);
                }
            }
        }
    }
    if (showColliders_) for (const auto& collider:level_.collision.colliders) {
        const auto bounds=TransformAABB(collider.local,collider.world);
        if (!VisibleFloor(metadata_.FloorAt(bounds.min.y))) continue;
        Vector3 corners[8];
        for (int i=0;i<8;++i) corners[i]=StagePoint({i&1 ? collider.local.max.x : collider.local.min.x,i&2 ? collider.local.max.y : collider.local.min.y,i&4 ? collider.local.max.z : collider.local.min.z},collider.world);
        for (int i=0;i<8;++i) for (const int bit:{1,2,4}) if (!(i&bit)) line(corners[i],corners[i|bit],IM_COL32(70,250,90,110));
    }
    bool usedGizmo=false;
    auto* selected=SelectedEnemy(); const auto* spawn=SelectedSpawn();
    if (selected && spawn && placements_.groups.at(spawn->id).overrideEnabled) {
        const auto position=EnemyPlacements::WorldPosition(*selected,spawn->position,spawn->rotation);
        ImVec2 center;
        if (project(position+Vector3{0,.2f,0},center)) {
            const Vector3 axes[]={{1.5f,0,0},{0,1.5f,0},{0,0,1.5f}};
            const ImU32 colors[]={IM_COL32(255,60,60,255),IM_COL32(70,255,90,255),IM_COL32(80,130,255,255)};
            if (gizmo_==0) for (int axis=0;axis<3;++axis) {
                ImVec2 end; if (!project(position+Vector3{0,.2f,0}+axes[axis],end)) continue;
                draw->AddLine(center,end,colors[axis],3); draw->AddCircleFilled(end,6,colors[axis]);
                const float dx=mouse.x-end.x,dy=mouse.y-end.y;
                if (hover && ImGui::IsMouseClicked(0) && dx*dx+dy*dy<100) { dragAxis_=axis; dragWorld_=position; usedGizmo=true; }
                if (dragAxis_==axis && ImGui::IsMouseDown(0)) {
                    const float sx=end.x-center.x,sy=end.y-center.y,n=sx*sx+sy*sy;
                    if (n>4) dragWorld_+=axes[axis]*((io.MouseDelta.x*sx+io.MouseDelta.y*sy)/n);
                    auto target=dragWorld_;
                    if (gridSnap_) for (float* v:{&target.x,&target.y,&target.z}) *v=std::round(*v/grid_)*grid_;
                    const auto delta=target-position;
                    for (auto& [source,g]:placements_.groups) if (const auto* point=spawns_.FindPoint(source)) for (auto& p:g.enemies)
                        if (g.overrideEnabled && (p.id==selectedId_ || multiSelection_.contains(p.id))) {
                            const auto old=EnemyPlacements::WorldPosition(p,point->position,point->rotation);
                            p.offset=EnemyPlacements::LocalPosition(old+delta,point->position,point->rotation); g.automatic=false;
                        }
                    usedGizmo=true;
                }
            }
            else {
                draw->AddCircle(center,55,colors[1],64,3);
                const float distance=std::hypot(mouse.x-center.x,mouse.y-center.y);
                if (hover && ImGui::IsMouseClicked(0) && std::abs(distance-55)<9) { dragAxis_=3; dragYaw_=selected->yaw; usedGizmo=true; }
                if (dragAxis_==3 && ImGui::IsMouseDown(0)) {
                    const float a=std::atan2(mouse.y-center.y,mouse.x-center.x);
                    const float b=std::atan2(mouse.y-io.MouseDelta.y-center.y,mouse.x-io.MouseDelta.x-center.x);
                    dragYaw_+=std::remainder(a-b,2*pi);
                    selected->yaw=rotationSnap_ ? std::round((dragYaw_+spawn->rotation.y)*degrees/angleSnap_)*angleSnap_/degrees-spawn->rotation.y : dragYaw_;
                    placements_.groups[spawn->id].automatic=false; usedGizmo=true;
                }
            }
        }
    }
    if (!ImGui::IsMouseDown(0)) dragAxis_=-1;
    if (hover && !usedGizmo && ImGui::IsMouseClicked(0)) {
        if (io.KeyShift) { selectingBox_=true; boxStart_={mouse.x,mouse.y}; }
        else {
            const float nx=(mouse.x-lo.x)/(hi.x-lo.x)*2-1,ny=1-(mouse.y-lo.y)/(hi.y-lo.y)*2;
            const auto inv=Matrix4x4::Inverse(vp);
            const auto unproject=[&](float z) {
                const float w=nx*inv.m[0][3]+ny*inv.m[1][3]+z*inv.m[2][3]+inv.m[3][3];
                return StagePoint({nx,ny,z},inv)*(1/w);
            };
            const auto origin=unproject(0),farPoint=unproject(1);
            const auto direction=(farPoint-origin)*(1/StageLength(farPoint-origin));
            float nearest=600; std::string picked,pickedSpawn;
            if (showEnemies_) for (const auto& marker:markers) if (!marker.id.empty()) {
                const auto it=previews_.find(marker.id); if (it==previews_.end()) continue;
                Enemy::RaycastHit hit;
                if (it->second.enemy->Raycast(origin,direction,nearest,hit)) { nearest=hit.distance; picked=marker.id; pickedSpawn=marker.spawn; }
            }
            float distance=picked.empty() ? 18.f : 0.f;
            for (const auto& marker:markers) {
                ImVec2 pixel; if (!project(marker.position,pixel)) continue;
                const float d=std::hypot(mouse.x-pixel.x,mouse.y-pixel.y);
                if (d<distance) { distance=d; picked=marker.id; pickedSpawn=marker.spawn; }
            }
            if (!pickedSpawn.empty()) {
                if (!io.KeyCtrl) multiSelection_.clear();
                if (!picked.empty()) {
                    if (io.KeyCtrl && multiSelection_.contains(picked)) multiSelection_.erase(picked); else multiSelection_.insert(picked);
                }
                selectedSpawn_=pickedSpawn; selectedId_=picked;
            }
        }
    }
    if (selectingBox_) {
        const ImVec2 a{std::min(boxStart_.x,mouse.x),std::min(boxStart_.y,mouse.y)},b{std::max(boxStart_.x,mouse.x),std::max(boxStart_.y,mouse.y)};
        draw->AddRectFilled(a,b,IM_COL32(70,150,250,40)); draw->AddRect(a,b,IM_COL32(100,200,255,255));
        if (!ImGui::IsMouseDown(0)) {
            if (!io.KeyCtrl) multiSelection_.clear();
            for (const auto& marker:markers) if (!marker.id.empty()) {
                ImVec2 pixel; if (project(marker.position,pixel) && pixel.x>=a.x && pixel.x<=b.x && pixel.y>=a.y && pixel.y<=b.y) {
                    multiSelection_.insert(marker.id); selectedId_=marker.id; selectedSpawn_=marker.spawn;
                }
            }
            selectingBox_=false;
        }
    }
    draw->PopClipRect();
#else
    (void)app;
#endif
}
