#include "TitleScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include "WinApp.h"
#include <numbers>
#ifdef USE_IMGUI
#include "imgui.h"
namespace { constexpr int kCapturedMouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange; }
#endif

namespace {
    constexpr const char* kTitlePath = "resources/levels/title/title.json";
    constexpr const char* kLightingPath = "resources/levels/title/title_lighting.json";

}

void TitleScene::LoadLighting() {
    try {
#ifdef USE_IMGUI
        DebugJsonEditor editor;
        if (!editor.Open(kLightingPath)) throw std::runtime_error(editor.error);
        const auto settings = TitleLighting::FromJson(editor.document);
        lightingEditor_ = std::move(editor);
#else
        const auto settings = TitleLighting::Load(kLightingPath);
#endif
        lighting_ = settings;
        lightingStatus_ = "Loaded title_lighting.json";
    } catch (const std::exception& e) {
        lightingStatus_ = std::string("Lighting load error: ")+e.what();
        OutputDebugStringA((lightingStatus_+"\n").c_str());
    }
    lighting_.Apply(sceneLight_);
}

bool TitleScene::LoadLayout() {
    try {
        if (!level_.Load(kTitlePath) || !level_.ValidateAssets()) throw std::runtime_error(level_.Error());
        if (!weapons_.Load("resources/Data/weapons.json", kTitlePath)) throw std::runtime_error(weapons_.Error());
        if (!definitions_.Load("resources/Data/enemies.json")) throw std::runtime_error(definitions_.Error());
        if (!spawns_.Load(kTitlePath, definitions_)) throw std::runtime_error(spawns_.Error());
        if (spawns_.Points().size()!=1) throw std::runtime_error("Title requires exactly one Enemy Spawn");
        std::ifstream file(kTitlePath);
        const auto data = nlohmann::json::parse(file);
        const auto& title = data.at("title");
        weaponId_ = title.value("weaponId",std::string("pistol"));
        if (!weapons_.Find(weaponId_)) throw std::runtime_error("Unknown title weapon: "+weaponId_);
        start_.delay = title.value("explosionDelay",.45f);
        if (!std::isfinite(start_.delay) || start_.delay<.4f || start_.delay>.5f)
            throw std::runtime_error("Title explosionDelay must be between 0.4 and 0.5 second");
        const auto assetPath = title.at("startObject").at("partAsset").get<std::string>();
        const std::filesystem::path path(assetPath);
        if (path.is_absolute() || !assetPath.starts_with("resources/levels/title/"))
            throw std::runtime_error("Invalid GAME START asset path");
        for (const auto& component : path) if (component=="..") throw std::runtime_error("GAME START asset outside title resources");
        startDefinition_ = EnemyDefinition{};
        startDefinition_.id = "title_game_start";
        startDefinition_.displayName = "GAME START";
        startDefinition_.visualScaleMultiplier = {.5f,.5f,.5f}; // Enemy's base scale is 2; asset is world-baked.
        startDefinition_.partAsset = EnemyAsset::Load(assetPath);
        const auto& parts = startDefinition_.partAsset->defaults;
        if (parts.size()!=1 || parts[0].role!=EnemyPartRole::Head || !parts[0].usesLocalHp ||
            !parts[0].breakable || !parts[0].deathOnZero || parts[0].maxHp!=1)
            throw std::runtime_error("GAME START requires one Head part with 1 HP and deathOnZero");
        return true;
    } catch (const std::exception& e) { error_ = e.what(); return false; }
}

void TitleScene::OnEnter(GameApp& app) {
    savedClearColor_ = app.Render()->GetOffscreen()->GetClearColor();
    app.Render()->GetOffscreen()->SetClearColor({.008f,.01f,.016f,1});
    auto& input = *app.GetInput();
    input.SetCameraControlEnabled(false);
    input.SetCameraToggleKeyEnabled(false);
    input.SetMouseCaptureRect(nullptr);
#ifdef USE_IMGUI
    savedMouseFlags_ = ImGui::GetIO().ConfigFlags & kCapturedMouseFlags;
#endif
    ready_ = false; initialCapturePending_ = true; suppressFireUntilRelease_ = true;
    start_ = TitleStartSequence{};
    transitionRequested_ = false;
    enemies_.clear(); respawnTime_ = 0; nextEnemyId_ = 0;
    if (!LoadLayout()) {
        OutputDebugStringA(("Title configuration error: "+error_+"\n").c_str());
        SetWindowTextW(app.Win()->GetHwnd(), L"Title asset error - resources/levels/title/title.json");
        return;
    }
    sceneLight_.Initialize(app.Dx());
    lighting_ = TitleLighting{};
    LoadLighting();
    camera_.SetFovY(std::numbers::pi_v<float>/3);
    camera_.Update();
    app.ObjCom()->SetDefaultCamera(&camera_);
    player_.Initialize(app.ObjCom(),app.Dx(),&camera_);
    player_.SetMovementEnabled(true);
    player_.SetStage(&level_.collision,level_.playerPosition,level_.playerRotation);
    player_.CurrentWeapon().Equip(*weapons_.Find(weaponId_));
    player_.Update(input,0);
    bullets_.Initialize(app.ObjCom(),app.Dx(),&camera_);
    EnemyPoolSettings settings;
    settings.perDefinition = 0;
    for (const auto& entry : spawns_.Points()[0].enemyPool) settings.overrides[entry.id] = 1;
    pool_.Initialize(app.ObjCom(),app.Dx(),&camera_,definitions_,settings);
    SpawnEnemy();
    startTarget_ = std::make_unique<Enemy>();
    startTarget_->PrepareForPool(app.ObjCom(),app.Dx(),&camera_,startDefinition_);
    startTarget_->ResetForSpawn(nextEnemyId_++,"GAME_START",{},{});
    startTarget_->SetSceneLight(&sceneLight_);
    enemies_.push_back(startTarget_.get());
    environment_.Initialize(app.ObjCom(),app.Dx());
    environment_.SetCamera(&camera_);
    environment_.SetModel(level_.model); environment_.StopAnimation();
    environment_.SetSceneLight(&sceneLight_);
    environment_.SetEnableLighting(2);
    environment_.Update(0);
    const float width = static_cast<float>(WinApp::kClientWidth), height = static_cast<float>(WinApp::kClientHeight);
    uiView_ = Matrix4x4::MakeIdentity4x4();
    uiProjection_ = Matrix4x4::MakeOrthographicMatrix(0,0,width,height,0,100);
    const auto& white = TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    for (Sprite* sprite : {&crosshairHorizontal_, &crosshairVertical_}) {
        sprite->Initialize(app.SpriteCom(),app.Dx(),"resources/white1x1.png");
        sprite->SetAnchorPoint({.5f,.5f}); sprite->SetPosition({width*.5f,height*.5f});
        sprite->SetColor({1,1,1,1});
    }
    crosshairHorizontal_.SetScale({12.0f/static_cast<float>(white.width),2.0f/static_cast<float>(white.height),1});
    crosshairVertical_.SetScale({2.0f/static_cast<float>(white.width),12.0f/static_cast<float>(white.height),1});
    crosshairHorizontal_.Update(uiView_,uiProjection_); crosshairVertical_.Update(uiView_,uiProjection_);
    ready_ = true;
    SetWindowTextW(app.Win()->GetHwnd(), L"アンアライブ");
}
void TitleScene::SpawnEnemy() {
    const auto& point = spawns_.Points()[0];
    auto* enemy = pool_.Acquire(spawns_.SelectEnemyId(point),nextEnemyId_++,point.id,point.position,point.rotation);
    enemy->SetSceneLight(&sceneLight_);
    if (enemies_.empty()) enemies_.push_back(enemy);
    else enemies_[0] = enemy; // Preserve the GAME START target at index 1.
    enemies_[0]->UpdateVisuals(0);
}
void TitleScene::OnBulletImpact(const BulletEnemyImpact& impact) {
    if (enemies_[impact.enemyIndex] == startTarget_.get() && impact.result.damage>0 && startTarget_->IsDead()) {
        if (start_.Begin()) player_.CurrentWeapon().CancelBurst();
    }
}
void TitleScene::OnExit(GameApp& app) {
    app.Render()->GetOffscreen()->SetClearColor(savedClearColor_);
    app.GetInput()->SetCameraControlEnabled(false);
    app.GetInput()->SetMouseCaptureRect(nullptr);
    app.GetInput()->SetCameraToggleKeyEnabled(true);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags = (ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) | savedMouseFlags_;
#endif
    app.ObjCom()->SetDefaultCamera(nullptr);
    // Keep GPU resources alive until SceneManager releases the retired scene.
    ready_ = false;
}
void TitleScene::Update(GameApp& app, float dt) {
    if (!ready_ || !NextScene().empty()) return;
    auto& input = *app.GetInput();
    const bool wasCaptured = input.IsCameraControlEnabled();
    bool viewReady = true, clickedView = false;
#ifdef USE_IMGUI
    RECT rect{};
    viewReady = app.ImGui()->GetSceneImageRect(rect);
    input.SetMouseCaptureRect(viewReady ? &rect : nullptr);
    clickedView = viewReady && app.ImGui()->IsSceneImageHovered() && input.IsLeftMouseTrigger();
#else
    input.SetMouseCaptureRect(nullptr);
    clickedView = input.HasFocus() && input.IsLeftMouseTrigger();
#endif
    if (!viewReady || !input.HasFocus() || input.IsKeyTrigger(DIK_ESCAPE)) {
        input.SetCameraControlEnabled(false);
        if (input.IsKeyTrigger(DIK_ESCAPE)) initialCapturePending_ = false;
    } else if (initialCapturePending_ || clickedView) {
        input.SetCameraControlEnabled(true); initialCapturePending_ = false;
    }
    if (!wasCaptured) suppressFireUntilRelease_ = true;
    if (!input.IsLeftMousePressed()) suppressFireUntilRelease_ = false;
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags = (ImGui::GetIO().ConfigFlags & ~kCapturedMouseFlags) |
        (input.IsCameraControlEnabled() ? kCapturedMouseFlags : savedMouseFlags_);
#endif
    if (!input.HasFocus() || !viewReady) return;
    dt = std::isfinite(dt) ? std::max(0.0f,dt) : 0;
    player_.SetMovementEnabled(!start_.Starting());
    player_.Update(input,dt);
    const bool controls = wasCaptured && input.IsCameraControlEnabled() && input.HasFocus();
    UpdateWorld(app,dt,controls);
}
void TitleScene::UpdateWorld(GameApp& app, float dt, bool controls) {
    auto& input = *app.GetInput();
    // Do not charge the impact frame's preceding time to the new explosion.
    start_.Update(dt);
    if (start_.Finished() && !transitionRequested_)
        transitionRequested_ = app.Scenes().TransitionTo("Game", .75f, .75f); // Game loads Stage01.
    for (auto* enemy : enemies_) enemy->UpdateVisuals(dt); // No AI/attacks/player damage.
    if (!start_.Starting() && !enemies_.empty() && enemies_[0]->CanReturnToPool()) {
        respawnTime_ += dt;
        if (respawnTime_>=1.0f) {
            pool_.Release(enemies_[0]); SpawnEnemy(); respawnTime_ = 0;
        }
    }
    bullets_.Update(dt,level_.collision,enemies_,[this](const BulletEnemyImpact& hit) { OnBulletImpact(hit); });
    controls = controls && !start_.Starting();
    auto& weapon = player_.CurrentWeapon();
    // Extended target practice must not exhaust all ammunition and block start.
    if (!start_.Starting() && weapon.Magazine()==0 && weapon.Reserve()==0) weapon.Equip(*weapons_.Find(weaponId_));
    const int shots = player_.UpdateShooting(input,dt,controls && !suppressFireUntilRelease_,controls);
    for (int shot=0; shot<shots; ++shot)
        bullets_.Spawn(weapon.Definition(),camera_.GetWorldMatrix(),0,random_,level_.collision,enemies_);
    environment_.Update(dt);
}
void TitleScene::DrawRender(GameApp&) {
    if (!ready_) return;
    environment_.Draw();
    for (auto* enemy : enemies_) enemy->Draw(false);
    bullets_.Draw();
    for (auto* enemy : enemies_) enemy->DrawExplosion();
}
void TitleScene::DrawOverlay2D(GameApp&) {
    if (!ready_ || start_.Starting()) return;
    crosshairHorizontal_.Draw(); crosshairVertical_.Draw();
}

void TitleScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    if (!ready_) return;
    if (ImGui::Begin("Title Lighting")) {
        ImGui::TextUnformatted("Esc: release mouse to edit. Click Scene to resume shooting.");
        const bool captured = app.GetInput()->IsCameraControlEnabled();
        ImGui::BeginDisabled(captured);
        bool changed = false;
        const auto axis = [](const char* label, Vector3& direction) {
            if (!ImGui::DragFloat3(label,&direction.x,.01f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp)) return false;
            if (direction.x*direction.x+direction.y*direction.y+direction.z*direction.z<1e-10f)
                direction={0,-1,0};
            return true;
        };
        if (ImGui::CollapsingHeader("Directional Light",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("Directional");
            changed |= axis("Direction",lighting_.direction);
            changed |= ImGui::ColorEdit3("Color",&lighting_.color.x);
            changed |= ImGui::DragFloat("Intensity",&lighting_.intensity,.01f,0,10,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopID();
        }
        for (size_t i=0;i<lighting_.spots.size();++i) {
            if (!ImGui::CollapsingHeader(TitleLighting::names[i],ImGuiTreeNodeFlags_DefaultOpen)) continue;
            ImGui::PushID(static_cast<int>(i));
            auto& light=lighting_.spots[i];
            changed |= ImGui::DragFloat3("Position",&light.position.x,.05f,-1e6f,1e6f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            changed |= axis("Direction",light.direction);
            changed |= ImGui::ColorEdit3("Color",&light.color.x);
            changed |= ImGui::DragFloat("Intensity",&light.intensity,.01f,0,10,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Distance",&light.distance,.1f,.1f,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Decay",&light.decay,.01f,.1f,8,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Highlight Strength",&light.specularStrength,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Outer Angle (deg)",&light.outerAngle,1,89,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            if (light.innerAngle>light.outerAngle-.1f) { light.innerAngle=light.outerAngle-.1f; changed=true; }
            changed |= ImGui::SliderFloat("Inner Angle (deg)",&light.innerAngle,0,light.outerAngle-.1f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextUnformatted("Half-angles: inner = full light, outer = edge of cone.");
            ImGui::PopID();
        }
        if (changed) {
            lighting_.Apply(sceneLight_);
            lightingEditor_.dirty = true;
            lightingStatus_ = "Preview updated (unsaved)";
        }
        ImGui::Separator();
        if (ImGui::Button("Save Lighting")) {
            const auto data=lighting_.ToJson();
            for (const char* field : {"version","directional","spots"}) lightingEditor_.document[field]=data.at(field);
            const auto validate=[](const std::string& path) -> std::string {
                try { (void)TitleLighting::Load(path); return {}; }
                catch (const std::exception& e) { return e.what(); }
            };
            lightingStatus_ = lightingEditor_.Save(validate) ? "Saved (backup: title_lighting.json.debug-backup)" :
                "Save error: "+lightingEditor_.error;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Lighting")) LoadLighting();
        if (ImGui::Button("Reset Defaults")) {
            lighting_ = TitleLighting{};
            lighting_.Apply(sceneLight_);
            lightingEditor_.dirty = true;
            lightingStatus_ = "Defaults restored (unsaved)";
        }
        ImGui::TextWrapped("%s%s",kLightingPath,lightingEditor_.dirty ? " (unsaved)" : "");
        ImGui::TextWrapped("%s",lightingStatus_.c_str());
        ImGui::EndDisabled();
    }
    ImGui::End();
#else
    (void)app;
#endif
}
