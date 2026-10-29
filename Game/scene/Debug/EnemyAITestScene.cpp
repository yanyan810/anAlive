#include "EnemyAITestScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include "StageProjectile.h"
#ifdef USE_IMGUI
const char* TypeName(EnemyType type) {
    switch(type) {
    case EnemyType::Normal:return "通常型";
    case EnemyType::Ranged:return "遠距離型";
    case EnemyType::Fast:return "高速型";
    case EnemyType::Tank:return "重装型";
    default:return "爆発型";
    }
}
#include "imgui.h"
#endif

namespace {
const Vector3 areas[]={{-24,0,2},{0,0,2},{24,0,2},{-24,0,32},{0,0,32},{24,0,32}};
constexpr const char* areaNames[]={"視認距離","視野角","壁越し視認","追跡・見失い","徘徊","複数敵"};
#ifdef USE_IMGUI
const char* StateName(EnemyState s) {
    switch(s) {
    case EnemyState::Idle:return "待機";
    case EnemyState::Chase:return "追跡";
    case EnemyState::Investigate:return "最終目撃地点へ移動";
    case EnemyState::Search:return "周辺探索";
    case EnemyState::Wander:return "徘徊";
    case EnemyState::Attack:return "攻撃";
    default:return "死亡";
    }
}
const char* PathName(EnemyPathState s) {
    switch(s) {
    case EnemyPathState::None:return "待機（経路なし）";
    case EnemyPathState::Direct:return "直線移動";
    case EnemyPathState::Following:return "迂回経路を移動中";
    case EnemyPathState::Arrived:return "到着";
    case EnemyPathState::NoPath:return "経路なし・安全停止／再試行";
    case EnemyPathState::Limited:return "探索範囲・計算上限に到達";
    default:return "未対応：高低差・床のない経路";
    }
}
const char* FailureName(EnemyPathFailure failure) {
    switch(failure) {
    case EnemyPathFailure::None:return "失敗なし";
    case EnemyPathFailure::StartOccupied:return "開始地点が障害物と重なっています";
    case EnemyPathFailure::GoalOccupied:return "目的地が壁・障害物の内部です";
    case EnemyPathFailure::HeightDifference:return "高低差のある経路は未対応です";
    case EnemyPathFailure::MissingFloor:return "開始地点または目的地に床がありません";
    case EnemyPathFailure::OutsideGrid:return "目的地が探索範囲の外です";
    case EnemyPathFailure::NodeBudget:return "探索計算の上限に到達しました";
    case EnemyPathFailure::Disconnected:return "通行可能な経路が見つかりません";
    case EnemyPathFailure::BlockedSegment:return "移動区間が障害物で塞がれました";
    case EnemyPathFailure::ExcessiveDetour:return "探索地点までの迂回が長すぎます";
    default:return "周辺に有効な探索候補がありません";
    }
}
#endif
}
void EnemyAITestScene::AddBox(GameApp& app,const char* id,const Vector3& center,const Vector3& half,const Vector4& color,bool collider) {
    auto object=std::make_unique<Object3d>();
    object->Initialize(app.ObjCom(),app.Dx()); object->SetCamera(&camera_);
    object->SetModel("cube/cube.obj"); object->SetTexture("resources/white1x1.png");
    object->SetTranslate(center); object->SetScale(half); object->SetEnableLighting(0);
    object->SetMaterialColor(color); object->Update(0); geometry_.push_back(std::move(object));
    if (collider) {
        StageCollider c; c.id=id; c.position=center; c.scale=half;
        c.local={{-1,-1,-1},{1,1,1}}; c.UpdateMatrices(); world_.colliders.push_back(c);
    }
}
void EnemyAITestScene::OnEnter(GameApp& app) {
    world_={}; world_.playerGravity=true; geometry_.clear(); enemies_.clear(); projectiles_.Clear();
    app.ObjCom()->SetDefaultCamera(&camera_);
    player_.Initialize(app.ObjCom(),app.Dx(),&camera_);
    player_.SetStage(&world_,areas[0]+Vector3{0,0,-12},{}); player_.RefreshVisuals();
    AddBox(app,"floor",{0,-.25f,16},{42,.25f,42},{.12f,.14f,.17f,1});
    AddBox(app,"west",{-42,4,16},{.4f,4,42},{.24f,.28f,.32f,1});
    AddBox(app,"east",{42,4,16},{.4f,4,42},{.24f,.28f,.32f,1});
    AddBox(app,"south",{0,4,-26},{42,4,.4f},{.24f,.28f,.32f,1});
    AddBox(app,"north",{0,4,58},{42,4,.4f},{.24f,.28f,.32f,1});
    for (int i=0;i<6;++i) {
        const float shade=static_cast<float>(i)*.035f;
        AddBox(app,"zone",areas[i]+Vector3{0,.012f,0},{9,.012f,9},{.16f+shade,.22f,.26f+shade,1},false);
    }
    // Solid walls exceed every enemy eye height; gaps at both ends allow investigation detours.
    AddBox(app,"sight_wall",{24,4,-2},{6,4,.4f},{.5f,.27f,.19f,1});
    // Open approach from the south; turn right at the corner to break LOS.
    // The old cross-wall separated the enemy's initial position from this approach.
    AddBox(app,"chase_wall",{-20,4,28},{.4f,4,7},{.5f,.27f,.19f,1});
    AddBox(app,"chase_corner",{-16,4,35},{4,4,.4f},{.5f,.27f,.19f,1});
    AddBox(app,"wander_obstacle",{4,4,36},{2,4,2},{.4f,.35f,.2f,1});
    AddBox(app,"multi_obstacle",{24,4,36},{2,4,2},{.4f,.35f,.2f,1});
    for (int i=5;i<=25;i+=5) AddBox(app,"distance_mark",{-24,.04f,2-static_cast<float>(i)},{4,.025f,.04f},{.6f,.6f,.25f,1},false);
    if (!definitions_.Load("resources/Data/enemies.json")) throw std::runtime_error(definitions_.Error());
    EnemyPoolSettings settings; settings.perDefinition=2;
    pool_.Initialize(app.ObjCom(),app.Dx(),&camera_,definitions_,settings);
    for (const auto& area:areas) Spawn(app,area);
    Spawn(app,areas[5]+Vector3{-3,0,0}); Spawn(app,areas[5]+Vector3{3,0,0}); selected_=0;
    help_.Initialize(app.SpriteCom(),app.Dx(),"resources/ui/enemy_ai_help.png"); help_.SetPosition({16,16});
    app.GetInput()->SetCameraControlEnabled(false); app.GetInput()->SetCameraToggleKeyEnabled(false);
#ifdef USE_IMGUI
    savedMouseFlags_=ImGui::GetIO().ConfigFlags & (ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange);
    ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange);
    app.ImGui()->SetWeaponWorkspace(true);
#endif
}
void EnemyAITestScene::OnExit(GameApp& app) {
    app.GetInput()->SetCameraControlEnabled(false); app.GetInput()->SetMouseCaptureRect(nullptr);
    app.GetInput()->SetCameraToggleKeyEnabled(true); app.ObjCom()->SetDefaultCamera(nullptr);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~(ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange))|savedMouseFlags_;
    app.ImGui()->SetWeaponWorkspace(false);
#endif
    // SceneManager retains this instance until submitted GPU work completes.
}
void EnemyAITestScene::Spawn(GameApp& app,const Vector3& position) {
    (void)app;
    const auto* d=definitions_.Find(definition_);
    if (!d || !EnemyNavigation::Clear(&world_,position,position,d->collisionRadius,d->collisionHeight)) {
        status_="出現できません：壁・障害物・床のない場所です。"; return;
    }
    auto* enemy=pool_.Acquire(definition_,nextId_++,"EnemyAITest",position,{0,-1.57079632679f,0});
    enemy->Update(0,player_.GetTransform().translate,&world_,player_.Settings().cameraHeight);
    enemies_.push_back(enemy); selected_=static_cast<int>(enemies_.size())-1; status_.clear();
}
void EnemyAITestScene::Remove(GameApp& app,bool all) {
    app.Dx()->WaitForGPU();
    if (all) { enemies_.clear(); pool_.ReleaseAll(); projectiles_.Clear(); }
    else if (!enemies_.empty()) {
        auto* enemy=enemies_[selected_]; enemies_.erase(enemies_.begin()+selected_); pool_.ReleaseForReset(enemy);
    }
    selected_=enemies_.empty() ? 0 : std::clamp(selected_,0,static_cast<int>(enemies_.size())-1);
}
void EnemyAITestScene::Update(GameApp& app,float dt) {
    auto& input=*app.GetInput();
    if (input.HasFocus() && input.IsKeyTrigger(DIK_F6)) { app.Scenes().TransitionTo("Title"); return; }
    RECT rect{}; bool viewReady=true;
#ifdef USE_IMGUI
    viewReady=app.ImGui()->GetSceneImageRect(rect);
    input.SetMouseCaptureRect(viewReady ? &rect : nullptr);
#else
    (void)rect;
#endif
    if (!input.HasFocus() || !viewReady || input.IsKeyTrigger(DIK_ESCAPE)) input.SetCameraControlEnabled(false);
    else if (input.IsKeyTrigger(DIK_TAB)) input.SetCameraControlEnabled(!input.IsCameraControlEnabled());
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~(ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange))|
        (input.IsCameraControlEnabled() ? ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange : 0);
#endif
    if (input.HasFocus() && input.IsKeyTrigger(DIK_F5)) for (auto* e:enemies_) e->ResetAI();
    if (input.HasFocus() && input.IsKeyTrigger(DIK_P)) pauseAll_=!pauseAll_;
    dt=std::isfinite(dt) ? std::clamp(dt,0.0f,.1f) : 0;
    player_.Update(input,dt);
    // Static world transforms still need fresh camera-dependent WVP constants.
    // Keep this outside the AI pause branch so FPS movement always moves the room.
    for (auto& object:geometry_) object->Update(0);
    const auto player=player_.GetTransform().translate;
    float damage=pauseAll_ ? 0 : UpdateStageProjectiles(projectiles_,dt,player,world_);
    std::vector<Vector3> correction(enemies_.size());
    for (size_t i=0;i<enemies_.size();++i) for (size_t j=i+1;j<enemies_.size();++j) {
        if (pauseAll_ || enemies_[i]->AI().paused || enemies_[j]->AI().paused || enemies_[i]->IsDead() || enemies_[j]->IsDead()) continue;
        const auto offset=EnemySeparationOffset(enemies_[i]->GetPosition(),enemies_[i]->Definition().collisionRadius,
            enemies_[j]->GetPosition(),enemies_[j]->Definition().collisionRadius,dt);
        correction[i]+=offset; correction[j]-=offset;
    }
    for (size_t i=0;i<enemies_.size();++i) {
        auto* e=enemies_[i];
        if (pauseAll_) { e->UpdateVisuals(dt); continue; }
        const auto& d=e->Definition(); const auto previous=e->GetPosition();
        e->SetPosition(world_.Move(previous,previous+correction[i],d.collisionRadius,d.collisionHeight));
        const float hit=e->Update(dt,player,&world_,player_.Settings().cameraHeight); damage+=hit;
        if (e->PendingAttackCount()) {
            if (d.IsRanged()) projectiles_.Spawn(MakeEnemyProjectile(d,e->GetPosition()+Vector3{0,1.2f,0},player));
            e->ConfirmAttack(hit);
        }
    }
    if (!invulnerable_) player_.ApplyDamage(damage);
    SyncProjectiles(app);
}
void EnemyAITestScene::SyncProjectiles(GameApp& app) {
    while (projectileVisuals_.size()<projectiles_.Projectiles().size()) {
        auto object=std::make_unique<Object3d>(); object->Initialize(app.ObjCom(),app.Dx()); object->SetCamera(&camera_);
        object->SetModel("cube/cube.obj"); object->SetTexture("resources/white1x1.png"); object->SetEnableLighting(0);
        object->SetMaterialColor({1,.4f,.1f,1}); projectileVisuals_.push_back(std::move(object));
    }
    for (size_t i=0;i<projectiles_.Projectiles().size();++i) {
        const auto& p=projectiles_.Projectiles()[i]; auto& v=*projectileVisuals_[i];
        v.SetTranslate(p.position); v.SetScale({p.radius,p.radius,p.radius}); v.Update(0);
    }
}
void EnemyAITestScene::DrawRender(GameApp&) {
    for (auto& box:geometry_) box->Draw();
    for (auto* e:enemies_) { e->Draw(); e->DrawExplosion(); }
    for (size_t i=0;i<projectiles_.Projectiles().size();++i) projectileVisuals_[i]->Draw();
}
void EnemyAITestScene::DrawOverlay2D(GameApp&) {
    const auto view=Matrix4x4::MakeIdentity4x4();
    const auto projection=Matrix4x4::MakeOrthographicMatrix(0,0,1280,720,0,100);
    help_.Update(view,projection); help_.Draw();
}
void EnemyAITestScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(1);
    ImGui::Begin("敵AIテスト",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoDocking);
    if (ImGui::BeginTable("配置",2,ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("設定",ImGuiTableColumnFlags_WidthFixed,400); ImGui::TableSetupColumn("映像",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("映像"); app.ImGui()->DrawScenePreview(); ImGui::EndChild();
        ImGui::TableSetColumnIndex(0); ImGui::BeginChild("設定");
        ImGui::TextUnformatted("敵AIテスト / Enemy AI Test");
        ImGui::TextWrapped("Tab: UI／FPS操作切替　Esc: カーソル解放\nWASD: 移動　マウス: 視点　P: 全AI停止／再開\nF5: 全AIリセット　F6: タイトル");
        ImGui::Text("操作モード: %s",app.GetInput()->IsCameraControlEnabled() ? "FPS操作" : "UI操作");
        if (ImGui::Button("FPS操作へ切り替え（Tabでも切替）")) app.GetInput()->SetCameraControlEnabled(true);
        if (ImGui::Button("タイトルへ戻る")) app.Scenes().TransitionTo("Title");
        ImGui::Checkbox("全AIを一時停止",&pauseAll_); ImGui::Checkbox("プレイヤー無敵（テスト用）",&invulnerable_);
        if (pauseAll_) ImGui::TextWrapped("全AIは停止中です。上のチェックを外すとテストを開始します。");
        ImGui::Text("プレイヤー体力: %.0f",player_.GetHP());
        if (ImGui::Button("プレイヤー体力を回復")) player_.ResetHPForDebug();
        if (ImGui::BeginCombo("テストエリア",areaNames[area_])) {
            for (int i=0;i<6;++i) if (ImGui::Selectable(areaNames[i],area_==i)) { area_=i; spawnPosition_=areas[i]; }
            ImGui::EndCombo();
        }
        if (ImGui::Button("このエリアへ移動")) { player_.SetStage(&world_,areas[area_]+Vector3{0,0,-12},{}); player_.RefreshVisuals(); }
        if (area_==3) ImGui::TextWrapped("追跡テスト: PでAIを再開。敵に姿を見せ、右側の縦壁の手前（Z=20付近）を右へ曲がると視線が切れます。最終目撃→周辺探索→徘徊を確認し、再び姿を見せてください。");
        ImGui::SeparatorText("敵管理");
        const auto* d=definitions_.Find(definition_);
        if (ImGui::BeginCombo("敵の種類",d ? TypeName(d->type) : "未選択")) {
            for (const auto& [id,definition]:definitions_.All()) {
                const auto label=std::string(TypeName(definition.type))+" ("+id+")";
                if (ImGui::Selectable(label.c_str(),definition_==id)) definition_=id;
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat3("出現座標",&spawnPosition_.x,.1f,-40,55);
        if (ImGui::Button("敵を出現")) Spawn(app,spawnPosition_);
        ImGui::SameLine(); if (ImGui::Button("全敵を削除")) Remove(app,true);
        ImGui::TextWrapped("%s",status_.c_str());
        ImGui::Text("プール: 使用 %zu / 容量 %zu / 追加確保 %zu",pool_.Active(),pool_.Capacity(),pool_.RuntimeAllocations());
        const std::string label=enemies_.empty() ? "なし" : enemies_[selected_]->GetId();
        if (ImGui::BeginCombo("選択中の敵",label.c_str())) {
            for (size_t i=0;i<enemies_.size();++i) if (ImGui::Selectable(enemies_[i]->GetId().c_str(),selected_==static_cast<int>(i))) selected_=static_cast<int>(i);
            ImGui::EndCombo();
        }
        if (!enemies_.empty()) {
            if (ImGui::Button("選択中の敵を削除")) Remove(app,false);
        }
        if (!enemies_.empty()) {
            auto& enemy=*enemies_[selected_]; bool paused=enemy.AI().paused;
            if (ImGui::Checkbox("選択したAIを一時停止",&paused)) enemy.SetAIPaused(paused);
            if (ImGui::Button("選択したAIをリセット")) enemy.ResetAI();
            ImGui::SeparatorText("索敵設定（選択中の敵へ即時反映）");
            auto s=enemy.Senses();
            ImGui::SliderFloat("視認距離",&s.viewDistance,0,100);
            ImGui::SliderFloat("視野角（度）",&s.fieldOfView,0,360);
            ImGui::SliderFloat("発見時間（秒）",&s.detectionTime,0,5);
            ImGui::SliderFloat("見失い時間（秒）",&s.loseSightTime,0,5);
            ImGui::SliderFloat("最終目撃地点への速度倍率",&s.investigateSpeedMultiplier,0,2);
            ImGui::SliderFloat("周辺探索時間（秒）",&s.searchDuration,0,10);
            ImGui::SliderFloat("周辺探索半径",&s.searchRadius,0,15);
            ImGui::SliderFloat("周辺探索速度倍率",&s.searchSpeedMultiplier,0,2);
            ImGui::SliderFloat("到達不能の判断時間（秒）",&s.investigateUnreachableTime,0,20);
            ImGui::SliderFloat("記憶保持時間（秒）",&s.memoryRetentionTime,0,15);
            ImGui::SliderFloat("記録間隔（秒）",&s.memoryRecordInterval,.05f,2);
            ImGui::SliderFloat("記録する移動距離",&s.memoryRecordDistance,.05f,3);
            ImGui::SliderFloat("探索方向の優先度",&s.searchDirectionPriority,0,1);
            ImGui::SliderFloat("到着判定距離",&s.arrivalDistance,.05f,3);
            ImGui::SliderFloat("徘徊半径",&s.wanderRadius,0,15);
            ImGui::SliderFloat("徘徊速度倍率",&s.wanderSpeedMultiplier,0,2);
            ImGui::SliderFloat("徘徊待機時間（秒）",&s.wanderWaitTime,0,10);
            if (ImGui::Button("定義の索敵設定に戻す")) s=enemy.Definition().senses;
            enemy.ConfigureSenses(s);
            const auto& a=enemy.AI();
            ImGui::SeparatorText("AI状態");
            ImGui::Text("現在: %s / 距離: %.2f",StateName(enemy.GetState()),a.distance);
            ImGui::Text("視野角内: %s / 遮蔽物: %s",a.inFov ? "はい" : "いいえ",a.occluded ? "あり" : "なし");
            ImGui::Text("視認中: %s / 徘徊中: %s",a.visible ? "はい" : "いいえ",a.state==EnemyState::Wander ? "はい" : "いいえ");
            ImGui::ProgressBar(s.detectionTime>0 ? std::clamp(a.detection/s.detectionTime,0.0f,1.0f) : (a.visible ? 1.0f : 0.0f),ImVec2(-1,0),"発見進捗");
            if (a.hasLastSeen) ImGui::Text("最終目撃: %.2f, %.2f, %.2f",a.lastSeen.x,a.lastSeen.y,a.lastSeen.z);
            else ImGui::TextUnformatted("最終目撃: まだ発見していません");
            ImGui::Text("移動目標: %.2f, %.2f, %.2f",a.moveTarget.x,a.moveTarget.y,a.moveTarget.z);
            ImGui::Text("見失ってから: %.2f 秒 / 現在速度: %.2f",a.lostTime,pauseAll_ || a.paused ? 0.0f : a.currentSpeed);
            ImGui::Text("最終目撃地点への到着: %s",!a.hasLastSeen ? "未発見" : a.investigateUnreachable ? "到達不能・現在地から代替探索" : a.lastSeenReached ? "到着済み" : "未到着");
            ImGui::Text("周辺探索の残り: %.2f 秒 / 確認済み地点: %u",a.SearchRemaining(),a.searchPointsVisited);
            if (a.state==EnemyState::Search) {
                ImGui::Text("探索目的地: %.2f, %.2f, %.2f",a.moveTarget.x,a.moveTarget.y,a.moveTarget.z);
                if (a.searchFinishing) ImGui::TextUnformatted("探索時間終了・現在の安全な区間まで移動中");
            }
            if (a.investigateUnreachable) ImGui::TextWrapped("最終目撃地点の到達失敗: %s",PathName(a.investigateFailure));
            else ImGui::Text("経路失敗の継続: %.2f 秒",a.investigateBlockedTime);
            ImGui::TextWrapped("経路探索: %s",PathName(a.navigation.state));
            ImGui::TextWrapped("経路の失敗理由: %s",FailureName(a.navigation.failure));
            if (a.investigateUnreachable) ImGui::TextWrapped("最終目撃地点の失敗理由: %s",FailureName(a.investigateFailureReason));
            if (a.searchCandidatesRejected>0) {
                ImGui::Text("却下した探索候補: %u",a.searchCandidatesRejected);
                ImGui::TextWrapped("前回候補の失敗理由: %s",FailureName(a.searchFailure));
                ImGui::Text("却下地点: %.2f, %.2f, %.2f",a.rejectedSearchGoal.x,a.rejectedSearchGoal.y,a.rejectedSearchGoal.z);
            }
            ImGui::Text("経路探索の試行回数: %u",a.navigation.planAttempts);
            if (a.memory.hasDirection) ImGui::Text("推定逃走方向: %.2f, %.2f",a.memory.escapeDirection.x,a.memory.escapeDirection.z);
            else ImGui::TextUnformatted("推定逃走方向: 履歴不足・従来の周辺探索を使用");
            ImGui::Text("記憶した座標: %zu / 上限 %zu",a.memory.count,EnemyObservationMemory::kCapacity);
            if (ImGui::CollapsingHeader("記憶したプレイヤー座標の一覧")) {
                ImGui::TextWrapped("視認中だけ記録。時間または距離で採取し、遮蔽前後の履歴を直線で結んで逃走方向を推定しません。");
                for (size_t i=0;i<a.memory.count;++i) {
                    const auto& sample=a.memory.samples[i];
                    ImGui::Text("%zu: %.2f 秒前 / (%.2f, %.2f, %.2f) / 区間 %u",i+1,a.memory.clock-sample.time,sample.position.x,sample.position.y,sample.position.z,sample.segment);
                }
            }
            if (a.state==EnemyState::Wander) ImGui::Text("徘徊の中心: %.2f, %.2f, %.2f",a.wanderCenter.x,a.wanderCenter.y,a.wanderCenter.z);
        }
        ImGui::SeparatorText("3Dデバッグ描画（選択中の敵）");
        ImGui::Checkbox("視認距離を表示",&showDistance_); ImGui::Checkbox("視野角を表示",&showFov_);
        ImGui::Checkbox("視線Rayを表示",&showRay_); ImGui::Checkbox("最終目撃位置を表示",&showLast_);
        ImGui::Checkbox("移動目的地を表示",&showGoal_); ImGui::Checkbox("経路を表示",&showPath_); ImGui::Checkbox("現在状態を表示",&showState_);
        ImGui::Checkbox("視認した移動履歴を表示",&showMemory_); ImGui::Checkbox("推定逃走方向の矢印を表示",&showEscapeDirection_);
        ImGui::TextWrapped("橙: 最終目撃地点　紫: 追跡・最終目撃地点への移動先　水色: 周辺探索の移動先　緑: 現在の経路");
        ImGui::TextWrapped("青: 視認した座標履歴　赤の矢印: 最終視認時の推定逃走方向。履歴・方向推定には未視認の現在位置を使いません。");
        ImGui::TextWrapped("変更はこの場の選択した敵だけに反映されます。JSON保存は行いません。再出現で定義の値に戻ります。\n経路探索は同じ高さの床のみ対応。段差・階段・上下階は未対応です。");
        ImGui::EndChild(); ImGui::EndTable();
    }
    ImGui::End(); DrawDiagnostics(app);
#else
    (void)app;
#endif
}
void EnemyAITestScene::DrawDiagnostics(GameApp& app) {
#ifdef USE_IMGUI
    RECT rect{}; if (!app.ImGui()->GetSceneImageRect(rect)) return;
    const auto& vp=camera_.GetViewProjectionMatrix(); auto* draw=ImGui::GetForegroundDrawList();
    draw->PushClipRect({static_cast<float>(rect.left),static_cast<float>(rect.top)},{static_cast<float>(rect.right),static_cast<float>(rect.bottom)},true);
    const auto project=[&](const Vector3& p,ImVec2& out) {
        const float w=p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3];
        const float z=p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2];
        if (w<=.01f || z<0) return false;
        out={static_cast<float>(rect.left)+(1+(p.x*vp.m[0][0]+p.y*vp.m[1][0]+p.z*vp.m[2][0]+vp.m[3][0])/w)*.5f*(rect.right-rect.left),
            static_cast<float>(rect.top)+(1-(p.x*vp.m[0][1]+p.y*vp.m[1][1]+p.z*vp.m[2][1]+vp.m[3][1])/w)*.5f*(rect.bottom-rect.top)};
        return true;
    };
    const auto line=[&](const Vector3& a,const Vector3& b,ImU32 color) { ImVec2 x,y; if (project(a,x) && project(b,y)) draw->AddLine(x,y,color,2); };
    const auto marker=[&](const Vector3& p,ImU32 color) { line(p+Vector3{-.4f,.08f,0},p+Vector3{.4f,.08f,0},color); line(p+Vector3{0,.08f,-.4f},p+Vector3{0,.08f,.4f},color); line(p,p+Vector3{0,2,0},color); };
    for (int i=0;i<6;++i) { ImVec2 p; if (project(areas[i]+Vector3{-6,1,0},p)) draw->AddText(p,IM_COL32(255,255,220,255),areaNames[i]); }
    if (!enemies_.empty()) {
        const auto& e=*enemies_[selected_]; const auto& a=e.AI(); const auto& s=a.settings.senses; const auto pos=e.GetPosition();
        if (showDistance_) for (int i=0;i<64;++i) {
            const float t=static_cast<float>(i)*6.2831853f/64,n=static_cast<float>(i+1)*6.2831853f/64;
            line(pos+Vector3{std::sin(t)*s.viewDistance,.1f,std::cos(t)*s.viewDistance},pos+Vector3{std::sin(n)*s.viewDistance,.1f,std::cos(n)*s.viewDistance},IM_COL32(70,180,250,200));
        }
        if (showFov_) {
            // Rotation follows the current path, independent of the player's true position.
            const float yaw=e.GetRotation().y-1.57079632679f, half=s.fieldOfView*.00872664626f;
            const auto point=[&](float angle) { return a.eye+Vector3{std::sin(angle)*s.viewDistance,0,std::cos(angle)*s.viewDistance}; };
            line(a.eye,point(yaw-half),IM_COL32(255,210,50,255)); line(a.eye,point(yaw+half),IM_COL32(255,210,50,255));
            for (int i=0;i<32;++i) line(point(yaw-half+2*half*static_cast<float>(i)/32),point(yaw-half+2*half*static_cast<float>(i+1)/32),IM_COL32(255,210,50,255));
        }
        if (showRay_) line(a.eye,a.sightTarget,a.visible ? IM_COL32(80,255,100,255) : IM_COL32(255,70,70,255));
        const auto label=[&](const Vector3& p,ImU32 color,const char* name) { ImVec2 screen; if (project(p,screen)) draw->AddText(screen,color,name); };
        if (showMemory_) for (size_t i=0;i<a.memory.count;++i) {
            const auto p=a.memory.samples[i].position+Vector3{0,.25f,0};
            ImVec2 screen; if (project(p,screen)) draw->AddCircleFilled(screen,3,IM_COL32(100,150,255,255));
            if (i>0 && a.memory.samples[i].segment==a.memory.samples[i-1].segment)
                line(a.memory.samples[i-1].position+Vector3{0,.25f,0},p,IM_COL32(100,150,255,255));
        }
        if (showEscapeDirection_ && a.hasLastSeen && a.memory.hasDirection) {
            const auto origin=a.lastSeen+Vector3{0,.6f,0}; const auto tip=origin+a.memory.escapeDirection*3;
            const auto side=Vector3{-a.memory.escapeDirection.z,0,a.memory.escapeDirection.x}*.35f;
            const auto color=IM_COL32(255,100,120,255);
            line(origin,tip,color); line(tip,tip-a.memory.escapeDirection*.6f+side,color); line(tip,tip-a.memory.escapeDirection*.6f-side,color);
            label(tip+Vector3{0,.4f,0},color,"推定逃走方向");
        }
        if (showLast_ && a.hasLastSeen) { marker(a.lastSeen,IM_COL32(255,160,60,255)); label(a.lastSeen+Vector3{-.7f,2.2f,0},IM_COL32(255,160,60,255),"最終目撃地点"); }
        if (showGoal_ && a.hasLastSeen) {
            const bool searching=a.state==EnemyState::Search;
            const auto color=searching ? IM_COL32(70,210,255,255) : IM_COL32(220,100,255,255);
            marker(a.moveTarget+Vector3{0,.25f,0},color);
            label(a.moveTarget+Vector3{.5f,2.7f,0},color,searching ? "周辺探索の移動先" : a.state==EnemyState::Wander ? "徘徊の移動先" : "最終目撃地点への移動先");
        }
        if (showPath_) { auto p=pos; for (size_t i=a.navigation.next;i<a.navigation.path.size();++i) { line(p+Vector3{0,.15f,0},a.navigation.path[i]+Vector3{0,.15f,0},IM_COL32(40,255,200,255)); p=a.navigation.path[i]; } }
        if (showState_) { ImVec2 p; if (project(pos+Vector3{0,e.Definition().collisionHeight+.5f,0},p)) draw->AddText(p,IM_COL32(255,255,255,255),StateName(e.GetState())); }
    }
    draw->PopClipRect();
#else
    (void)app;
#endif
}
