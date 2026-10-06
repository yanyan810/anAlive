#include "TitleScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include "WinApp.h"
#include "GeometryGenerator.h"
#include <numbers>
#ifdef USE_IMGUI
#include "imgui.h"
namespace { constexpr int kCapturedMouseFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange; }
#endif

namespace {
    constexpr const char* kTitlePath = "resources/levels/title/title.json";
    constexpr const char* kLightingPath = "resources/levels/title/title_lighting.json";

}

void TitleScene::PrepareStartExplosion(GameApp& app) {
    startLetters_.clear(); startFragments_.clear();
    const auto& asset = *startDefinition_.partAsset;
    const auto& triangles = asset.defaults[0].geometry->triangles;
    // The exported Head contains all nine glyphs. Merge overlapping triangle
    // X ranges to recover whole letters, including bevels and enclosed holes.
    // No layout coordinates or second set of authored letter assets is needed.
    std::vector<Vector2> ranges;
    ranges.reserve(triangles.size());
    for (const auto& tri : triangles) {
        ranges.push_back({std::min({tri[0].position.x,tri[1].position.x,tri[2].position.x}),
            std::max({tri[0].position.x,tri[1].position.x,tri[2].position.x})});
    }
    std::sort(ranges.begin(),ranges.end(),[](const auto& a,const auto& b) { return a.x<b.x; });
    std::vector<Vector2> letters;
    for (const auto& range : ranges) {
        if (letters.empty() || range.x>letters.back().y+.0001f) letters.push_back(range);
        else letters.back().y=std::max(letters.back().y,range.y);
    }
    std::vector<Model::ModelData> geometries(letters.size());
    for (auto& geometry : geometries) {
        geometry.materials.push_back({asset.texture});
        geometry.meshes.emplace_back(); geometry.rootNode.meshIndices.push_back(0);
    }
    for (const auto& tri : triangles) {
        const float x=(tri[0].position.x+tri[1].position.x+tri[2].position.x)/3;
        const auto band=std::lower_bound(letters.begin(),letters.end(),x,
            [](const Vector2& range,float value) { return range.y+.0001f<value; });
        auto& geometry=geometries[static_cast<size_t>(band-letters.begin())];
        for (const auto& v : tri) {
            geometry.meshes[0].vertices.push_back({{v.position.x,v.position.y,v.position.z,1},v.uv,v.normal});
            geometry.indices.push_back(static_cast<uint32_t>(geometry.indices.size()));
        }
    }
    const auto prepare=[&](Model* model) {
        DetachedEnemyPart piece;
        piece.object=std::make_unique<Object3d>();
        piece.object->Initialize(app.ObjCom(),app.Dx());
        piece.object->SetCamera(&camera_); piece.object->SetModel(model); piece.object->StopAnimation();
        piece.object->SetSceneLight(&sceneLight_); piece.object->SetEnableLighting(2);
        model->GetLocalAABB(piece.motion.bounds);
        piece.object->Update(0);
        return piece;
    };
    for (size_t i=0;i<geometries.size();++i) {
        auto& geometry=geometries[i];
        geometry.meshes[0].indexCount=static_cast<uint32_t>(geometry.indices.size());
        auto* model=ModelManager::GetInstance()->CreatePrimitiveModel(
            "TitleStart/"+asset.path+"/letter"+std::to_string(i),geometry);
        startLetters_.push_back(prepare(model));
    }
    Model::ModelData box;
    box.materials.push_back({asset.texture}); box.meshes.emplace_back();
    box.meshes[0].vertices=GeometryGenerator::GenerateBoxTriList(1,1,1);
    box.meshes[0].indexCount=static_cast<uint32_t>(box.meshes[0].vertices.size());
    for (uint32_t i=0;i<box.meshes[0].indexCount;++i) box.indices.push_back(i);
    box.rootNode.meshIndices.push_back(0);
    auto* cube=ModelManager::GetInstance()->CreatePrimitiveModel("TitleStart/fragmentBox",box);
    startFragments_.reserve(kMaxStartFragments);
    for (size_t i=0;i<kMaxStartFragments;++i) startFragments_.push_back(prepare(cube));
}

void TitleScene::BeginStartExplosion(bool preview) {
    explosionPreview_=preview; explosionElapsed_=0;
    player_.CurrentWeapon().CancelBurst();
    // Preserve the existing damage/raycast target; title owns its special visual
    // burst. Discard its hidden generic face burst via the existing pool API.
    if (!preview) startTarget_->RetireFromPool();
    const auto& settings=explosionSettings_;
    const auto& part=startDefinition_.partAsset->defaults[0];
    const auto center=(part.bounds.min+part.bounds.max)*.5f;
    std::uniform_real_distribution<float> unit(0,1), jitter(-1,1);
    const auto spin=[&]() { return settings.angularVelocity*(.35f+.65f*unit(random_))*(jitter(random_)<0?-1.0f:1.0f); };
    const auto randomForce=[&]() { return Vector3{jitter(random_),jitter(random_),jitter(random_)}*settings.randomPower; };
    DetachedPartSettings physics;
    physics.upwardPower=settings.upwardPower; physics.lifeTime=settings.lifetime;
    physics.groundHeight=level_.playerPosition.y;
    const auto launch=[&](DetachedEnemyPart& piece,const Vector3& position,const Vector3& scale) {
        const auto outward=position-center;
        const float distance=std::hypot(outward.x,outward.y,outward.z);
        physics.launchPower=settings.explosionPower*(.8f+.4f*unit(random_));
        const auto pivot=(piece.motion.bounds.min+piece.motion.bounds.max)*.5f;
        const auto translation=position-Vector3{pivot.x*scale.x,pivot.y*scale.y,pivot.z*scale.z};
        piece.motion.Initialize(piece.motion.bounds,translation,{},scale,
            distance>1e-5f ? outward : Vector3{jitter(random_),0,jitter(random_)},
            EnemyPartType::None,{spin(),spin(),spin()},physics);
        piece.motion.velocity=piece.motion.velocity+randomForce();
    };
    // Letter meshes retain their world-baked vertices and rotate about their own
    // centers using DetachedPartMotion::Translation(), as enemy parts do.
    for (auto& letter : startLetters_) {
        const auto letterCenter=(letter.motion.bounds.min+letter.motion.bounds.max)*.5f;
        launch(letter,letterCenter,{1,1,1});
    }
    activeStartFragments_=static_cast<size_t>(std::clamp(settings.fragmentCount,0,static_cast<int>(kMaxStartFragments)));
    const auto& faces=part.geometry->faces;
    std::uniform_int_distribution<size_t> faceIndex(0,faces.size()-1);
    const auto& cameraWorld=camera_.GetWorldMatrix();
    const Vector3 right{cameraWorld.m[0][0],cameraWorld.m[0][1],cameraWorld.m[0][2]};
    const Vector3 up{cameraWorld.m[1][0],cameraWorld.m[1][1],cameraWorld.m[1][2]};
    const Vector3 forward{cameraWorld.m[2][0],cameraWorld.m[2][1],cameraWorld.m[2][2]};
    const size_t nearPassCount=std::min(size_t{4},activeStartFragments_/8);
    for (size_t i=0;i<activeStartFragments_;++i) {
        auto& fragment=startFragments_[i];
        const auto& face=faces[faceIndex(random_)];
        const auto origin=(face[0]+face[1]+face[2])*(1.0f/3);
        const float size=settings.minScale+(settings.maxScale-settings.minScale)*unit(random_);
        Vector3 scale{size,size,size};
        if (i%3==1) scale={size*2.4f,size*.45f,size*.45f};
        if (i%3==2) scale={size*1.7f,size*1.2f,size*.25f};
        const bool nearPass=i<nearPassCount && settings.explosionPower>0;
        if (nearPass) scale=scale*.55f;
        launch(fragment,origin,scale);
        fragment.motion.rotation={unit(random_)*6.28f,unit(random_)*6.28f,unit(random_)*6.28f};
        if (nearPass) {
            // Only 3-4 small pieces approach the lens. Aim beside it, and keep
            // the pass in front of the near plane instead of obscuring center.
            const float side=(i%2==0 ? -1.0f : 1.0f)*(.7f+.4f*unit(random_));
            const auto destination=camera_.GetTranslate()+forward*1.25f+right*side+up*(.15f+.35f*unit(random_));
            const float travelTime=std::clamp(start_.delay*.85f*7.5f/settings.explosionPower,.2f,.8f);
            fragment.motion.velocity=(destination-origin)*(1/travelTime)+Vector3{0,-.5f*physics.gravity*travelTime,0};
        }
        fragmentCastsShadow_[i]=!nearPass && std::max({scale.x,scale.y,scale.z})>=.20f;
    }
    // The attached target is white under the GAME START spot. Match that tint
    // for scattered pieces even after they leave the spotlight's cone.
    const auto tint=lighting_.spots[2].color;
    fragmentColor_={.35f+.65f*tint.x,.35f+.65f*tint.y,.35f+.65f*tint.z,1};
    UpdateStartExplosionVisuals();
}

void TitleScene::UpdateStartExplosion(float dt) {
    if (!StartExplosionActive()) return;
    explosionElapsed_+=dt;
    for (auto& letter : startLetters_) letter.motion.Update(dt);
    for (size_t i=0;i<activeStartFragments_;++i) startFragments_[i].motion.Update(dt);
    if (explosionPreview_ && explosionElapsed_>=std::max({explosionSettings_.lifetime,
        explosionSettings_.flashDuration,explosionSettings_.shakeDuration})) explosionPreview_=false;
}

void TitleScene::UpdateStartExplosionVisuals() {
    if (!StartExplosionActive()) return;
    const bool flash=explosionSettings_.flashDuration>0 && explosionElapsed_<explosionSettings_.flashDuration;
    const float brightness=flash ? 1+2*(1-explosionElapsed_/explosionSettings_.flashDuration) : 1;
    const auto update=[&](DetachedEnemyPart& piece,bool letter) {
        auto& object=*piece.object;
        object.SetTranslate(piece.motion.Translation()); object.SetRotate(piece.motion.rotation);
        object.SetScale(piece.motion.scale);
        const auto color=letter ? Vector4{1,1,1,1} : fragmentColor_;
        object.SetMaterialColor({color.x*brightness,color.y*brightness,color.z*brightness,1});
        object.SetEnableLighting(flash ? 0 : 2); // Existing unlit material provides the short impact flash.
        object.Update(0);
    };
    for (auto& letter : startLetters_) update(letter,true);
    for (size_t i=0;i<activeStartFragments_;++i) update(startFragments_[i],false);
}

void TitleScene::ApplyStartCameraShake() {
    if (!StartExplosionActive() || explosionSettings_.shakeDuration<=0 ||
        explosionElapsed_>=explosionSettings_.shakeDuration) return;
    const float t=explosionElapsed_/explosionSettings_.shakeDuration;
    const float strength=explosionSettings_.shakeStrength*(1-t)*(1-t);
    const auto& world=camera_.GetWorldMatrix();
    const Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]}, up{world.m[1][0],world.m[1][1],world.m[1][2]};
    camera_.SetTranslate(camera_.GetTranslate()+right*(std::cos(t*31)*strength)+up*(std::sin(t*43+.6f)*strength*.65f));
    camera_.Update();
    // Refresh camera-dependent matrices after the render-only offset. Gameplay
    // camera is restored from Player at the next UpdateWorld, so shake cannot drift.
    for (auto* enemy : enemies_) enemy->UpdateVisuals(0);
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
    explosionPreview_=false; explosionElapsed_=0; activeStartFragments_=0;
    transitionRequested_ = false;
    enemies_.clear(); respawnTime_ = 0; nextEnemyId_ = 0;
    if (!LoadLayout()) {
        OutputDebugStringA(("Title configuration error: "+error_+"\n").c_str());
        SetWindowTextW(app.Win()->GetHwnd(), L"Title asset error - resources/levels/title/title.json");
        return;
    }
    sceneLight_.Initialize(app.Dx());
    shadowMap_.Initialize(app.Dx(),app.Srv());
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
    PrepareStartExplosion(app);
    environment_.Initialize(app.ObjCom(),app.Dx());
    environment_.SetCamera(&camera_);
    environment_.SetModel(level_.model); environment_.StopAnimation();
    roomShadowMeshes_.clear();
    // Use exported node names, so re-exporting or reordering glTF meshes keeps this selection valid.
    const auto collectRoomMeshes=[&](const auto& self,const Model::Node& node,bool room) -> void {
        room=room || node.name.starts_with("Backdrop") || node.name.starts_with("Floor");
        if (room) roomShadowMeshes_.insert(roomShadowMeshes_.end(),node.meshIndices.begin(),node.meshIndices.end());
        for (const auto& child : node.children) self(self,child,room);
    };
    collectRoomMeshes(collectRoomMeshes,environment_.GetModel()->GetModelData().rootNode,false);
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
        if (start_.Begin()) BeginStartExplosion();
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
    player_.SetMovementEnabled(!StartExplosionActive());
    player_.Update(input,dt);
    const bool controls = wasCaptured && input.IsCameraControlEnabled() && input.HasFocus();
    UpdateWorld(app,dt,controls);
}
void TitleScene::UpdateWorld(GameApp& app, float dt, bool controls) {
    auto& input = *app.GetInput();
    player_.RefreshVisuals(); // Remove last frame's shake before gameplay/raycast updates.
    // Do not charge the impact frame's preceding time to the new explosion.
    start_.Update(dt);
    UpdateStartExplosion(dt);
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
    controls = controls && !StartExplosionActive();
    auto& weapon = player_.CurrentWeapon();
    // Extended target practice must not exhaust all ammunition and block start.
    if (!start_.Starting() && weapon.Magazine()==0 && weapon.Reserve()==0) weapon.Equip(*weapons_.Find(weaponId_));
    const int shots = player_.UpdateShooting(input,dt,controls && !suppressFireUntilRelease_,controls);
    for (int shot=0; shot<shots; ++shot)
        bullets_.Spawn(weapon.Definition(),camera_.GetWorldMatrix(),0,random_,level_.collision,enemies_);
    ApplyStartCameraShake();
    UpdateStartExplosionVisuals();
    environment_.Update(dt);
}
void TitleScene::DrawShadow(GameApp&) {
    if (!ready_) return;
    const auto& shadow=lighting_.shadow;
    if (shadow.enabled && shadow.strength>0) {
        shadowMap_.Begin(TitleLighting::Unit(lighting_.direction),shadow.center,shadow.viewSize,
            shadow.lightDistance,shadow.nearClip,shadow.farClip);
        if (shadow.roomCastsShadows) environment_.DrawDirectionalShadow(shadowMap_);
        else environment_.DrawDirectionalShadow(shadowMap_,roomShadowMeshes_);
        for (auto* enemy : enemies_)
            if (enemy!=startTarget_.get() || !StartExplosionActive()) enemy->DrawDirectionalShadow(shadowMap_);
        if (StartExplosionActive()) {
            for (auto& letter : startLetters_)
                if (letter.motion.Active()) letter.object->DrawDirectionalShadow(shadowMap_);
            for (size_t i=0;i<activeStartFragments_;++i)
                if (fragmentCastsShadow_[i] && startFragments_[i].motion.Active()) startFragments_[i].object->DrawDirectionalShadow(shadowMap_);
        }
        shadowMap_.End();
    }
    sceneLight_.SetDirectionalShadow(shadowMap_.ViewProjection(),
        {shadow.enabled ? shadow.strength : 0,shadow.bias,1.0f/DirectionalShadowMap::kResolution,lighting_.ambientIntensity},shadowMap_.Srv());
}

void TitleScene::DrawRender(GameApp&) {
    if (!ready_) return;
    environment_.Draw();
    for (auto* enemy : enemies_)
        if (enemy!=startTarget_.get() || !StartExplosionActive()) enemy->Draw(false);
    if (StartExplosionActive()) {
        for (auto& letter : startLetters_) if (letter.motion.Active()) letter.object->Draw();
        for (size_t i=0;i<activeStartFragments_;++i)
            if (startFragments_[i].motion.Active()) startFragments_[i].object->Draw();
    }
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
        if (ImGui::CollapsingHeader("GAME START Explosion",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("StartExplosion");
            ImGui::BeginDisabled(start_.Starting());
            auto& effect=explosionSettings_;
            ImGui::SliderInt("Fragment Count",&effect.fragmentCount,0,static_cast<int>(kMaxStartFragments));
            ImGui::DragFloat("Explosion Power",&effect.explosionPower,.1f,0,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Random Power",&effect.randomPower,.05f,0,8,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Upward Power",&effect.upwardPower,.05f,0,12,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloatRange2("Fragment Scale Range",&effect.minScale,&effect.maxScale,.005f,.01f,.35f,"Min %.3f","Max %.3f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Angular Velocity (rad/s)",&effect.angularVelocity,.1f,0,25,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Fragment Lifetime",&effect.lifetime,.05f,.1f,5,"%.2f s",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Camera Shake Strength",&effect.shakeStrength,.001f,0,.15f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Camera Shake Duration",&effect.shakeDuration,.005f,0,.3f,"%.3f s",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Flash Duration",&effect.flashDuration,.005f,0,.2f,"%.3f s",ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("Fade Start Delay",&start_.delay,.01f,.1f,1.5f,"%.2f s",ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Preview Explosion (no transition)")) BeginStartExplosion(true);
            ImGui::SameLine();
            if (ImGui::Button("Reset Explosion Defaults")) {
                explosionSettings_=StartExplosionSettings{};
                start_.delay=.45f;
            }
            ImGui::TextUnformatted("Explosion values are session-only. Lighting save affects lights only.");
            ImGui::TextUnformatted("Preview restores GAME START after Lifetime. Default fade delay: 0.45 s.");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
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
            changed |= ImGui::SliderFloat("Ambient Fill",&lighting_.ambientIntensity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextUnformatted("Minimum diffuse light; unaffected by shadows.");
            ImGui::PopID();
        }
        if (ImGui::CollapsingHeader("Directional Shadow Map")) {
            auto& shadow=lighting_.shadow;
            changed |= ImGui::Checkbox("Enable Shadow",&shadow.enabled);
            changed |= ImGui::Checkbox("Room Casts Shadows",&shadow.roomCastsShadows);
            changed |= ImGui::SliderFloat("Shadow Strength",&shadow.strength,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Depth Bias",&shadow.bias,.00001f,0,.02f,"%.5f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat3("Shadow Center",&shadow.center.x,.1f,-1e6f,1e6f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("View Size",&shadow.viewSize,.1f,1,200,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Light Distance",&shadow.lightDistance,.1f,1,200,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::DragFloat("Near Clip",&shadow.nearClip,.1f,.01f,999.9f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if (shadow.farClip<shadow.nearClip+.1f) { shadow.farClip=shadow.nearClip+.1f; changed=true; }
            changed |= ImGui::DragFloat("Far Clip",&shadow.farClip,.1f,shadow.nearClip+.1f,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextUnformatted("2048 x 2048 / Depth only / 3 x 3 PCF");
            ImGui::TextUnformatted("Shadows affect Directional Light; Spot lights remain independent.");
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
            for (const char* field : {"version","directional","spots","shadow"}) lightingEditor_.document[field]=data.at(field);
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
