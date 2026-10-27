#include "TitleScene.h"
#include "GameApp.h"
#include "GameOverMenuInput.h"
#include "ModelManager.h"
#include <numbers>

void TitleScene::PrepareDemoText(GameApp& app) {
    // Reuse the exported font/bevels. O and V already exist in the decorative
    // title words, so no replacement font, texture or regenerated stage is needed.
    const std::array<Model*,8> glyphs{
        startExplosion_.letters.at(0).object->GetModel(),startExplosion_.letters.at(1).object->GetModel(),
        startExplosion_.letters.at(2).object->GetModel(),startExplosion_.letters.at(3).object->GetModel(),
        instructionExplosion_.letters.at(2).object->GetModel(),unaliveExplosion_.letters.at(5).object->GetModel(),
        startExplosion_.letters.at(3).object->GetModel(),startExplosion_.letters.at(7).object->GetModel()};
    const auto bounds=startExplosion_.source.bounds;
    const float height=bounds.max.y-bounds.min.y, depth=bounds.max.z-bounds.min.z;
    std::array<AABB,8> glyphBounds{};
    std::array<float,8> widths{};
    float totalWidth=0;
    for (size_t i=0;i<glyphs.size();++i) {
        glyphs[i]->GetLocalAABB(glyphBounds[i]);
        const auto& b=glyphBounds[i];
        widths[i]=(b.max.x-b.min.x)*height/std::max(.001f,b.max.y-b.min.y);
        totalWidth+=widths[i]+(i==3 ? height*.6f : i==7 ? 0 : height*.12f);
    }
    const float fit=(bounds.max.x-bounds.min.x)/totalWidth;
    Model::ModelData data; data.materials.push_back({"resources/white1x1.png"});
    data.meshes.emplace_back(); data.rootNode.meshIndices.push_back(0);
    auto geometry=std::make_shared<EnemyPartGeometry>();
    float cursor=0;
    for (size_t i=0;i<glyphs.size();++i) {
        const auto& b=glyphBounds[i]; const auto& model=glyphs[i]->GetModelData();
        const float sx=height/std::max(.001f,b.max.y-b.min.y)*fit;
        const float sy=height/std::max(.001f,b.max.y-b.min.y);
        const float sz=depth/std::max(.001f,b.max.z-b.min.z);
        const auto& mesh=model.meshes[0];
        for (uint32_t face=0;face+2<mesh.indexCount;face+=3) {
            std::array<EnemyPartVertex,3> triangle; std::array<Vector3,3> positions;
            for (uint32_t v=0;v<3;++v) {
                auto vertex=mesh.vertices.at(model.indices.at(mesh.startIndex+face+v));
                const Vector3 p{bounds.min.x+cursor*fit+(vertex.position.x-b.min.x)*sx,
                    bounds.min.y+(vertex.position.y-b.min.y)*sy,bounds.min.z+(vertex.position.z-b.min.z)*sz};
                auto n=Vector3{vertex.normal.x/sx,vertex.normal.y/sy,vertex.normal.z/sz};
                const float length=StageLength(n); if (length>1e-6f) n=n*(1/length);
                vertex.position={p.x,p.y,p.z,1}; vertex.normal=n;
                data.meshes[0].vertices.push_back(vertex); data.indices.push_back(static_cast<uint32_t>(data.indices.size()));
                triangle[v]={p,n,vertex.texcoord}; positions[v]=p;
            }
            geometry->triangles.push_back(triangle); geometry->faces.push_back(positions);
        }
        cursor+=widths[i]+(i==3 ? height*.6f : height*.12f);
    }
    data.meshes[0].indexCount=static_cast<uint32_t>(data.indices.size());
    auto* model=ModelManager::GetInstance()->CreatePrimitiveModel("TitleDemo/GAME_OVER/"+level_.model,data);
    demoText_.Initialize(app.ObjCom(),app.Dx()); demoText_.SetCamera(&camera_); demoText_.SetModel(model);
    demoText_.StopAnimation(); demoText_.SetEnableLighting(2); demoText_.SetSceneLight(&sceneLight_); demoText_.Update(0);
    auto part=startExplosion_.source; part.name="GAME OVER"; part.geometry=std::move(geometry);
    demoTextParts_.clear(); demoTextParts_.push_back(std::move(part));
}

bool TitleScene::SetMode(Mode mode) {
    if (!ready_ || gameOver_.Active() || pendingDemoDeath_ || start_.Starting() || explosionPreview_ || transitionRequested_) return false;
    mode_=mode; bullets_.Clear(); suppressFireUntilRelease_=true;
    return true;
}

std::optional<BulletHit> TitleScene::TraceDemoText(const Vector3& origin,const Vector3& direction,float distance) const {
    if (mode_!=Mode::GameOver || gameOver_.Active() || pendingDemoDeath_) return {};
    EnemyPartHit part;
    if (!RaycastEnemyParts(demoTextParts_,Matrix4x4::MakeIdentity4x4(),origin,direction,distance,part)) return {};
    BulletHit hit; hit.distance=part.distance; hit.position=part.position; hit.wall=false; hit.targetIndex=2; return hit;
}

void TitleScene::BeginGameOverDemo(GameApp& app) {
    if (gameOver_.Active() || mode_!=Mode::GameOver || start_.Starting()) return;
    pendingDemoDeath_=false;
    const GameOverSequence::DeathContext death{player_.GetTransform(),
        {camera_.GetTranslate(),camera_.GetRotate(),camera_.GetFovY()},{},player_.Settings().cameraHeight};
    player_.ApplyDamage(player_.GetHP()); player_.SetMovementEnabled(false); player_.CurrentWeapon().CancelBurst();
    suppressFireUntilRelease_=true; initialCapturePending_=false; bullets_.Clear();
    auto& input=*app.GetInput(); input.SetCameraControlEnabled(false); input.SetMouseCaptureRect(nullptr);
#ifdef USE_IMGUI
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~(ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange)) | savedMouseFlags_;
#endif
    app.Render()->ResetViewPostPreview(); gameOverOverlay_.Reset();
    gameOver_.Begin(app.ObjCom(),app.Dx(),&camera_,definitions_.Find("normal"),level_.playerVisual,death);
    gameOver_.Update(0,level_.collision,camera_);
    environment_.Update(0); demoText_.Update(0);
    for (auto* enemy:enemies_) enemy->UpdateVisuals(0);
}

void TitleScene::UpdateGameOverDemo(GameApp& app,float dt) {
    app.GetInput()->SetCameraControlEnabled(false);
    if (app.Scenes().IsTransitioning() || app.Scenes().Fade().IsActive()) return;
    dt=std::isfinite(dt) ? std::max(0.0f,dt) : 0;
    const float worldDt=gameOver_.WorldDelta(dt);
    gameOver_.Update(dt,level_.collision,camera_);
    environment_.Update(0); demoText_.Update(0);
    for (auto* enemy:enemies_) enemy->UpdateVisuals(worldDt);
    UpdateStartExplosionVisuals();
    if (gameOver_.MenuReady()) if (const auto choice=UpdateGameOverMenuInput(app,gameOverOverlay_)) ChooseDemoMenu(app,*choice==0);
}

void TitleScene::ResetDemo(GameApp& app,bool normalMode) {
    // Retries can discard actor/debris constants referenced by the preceding frame.
    app.Dx()->WaitForGPU();
    gameOver_.Reset(); gameOverOverlay_.Reset(); pendingDemoDeath_=false;
    app.Render()->ResetDeathPostEffect(); app.Render()->ResetViewPostPreview();
    bullets_.Clear(); explosionPreview_=false; startExplosion_.fragmentCount=0;
    const float delay=start_.delay; start_={}; start_.delay=delay; transitionRequested_=false;
    RestoreUnalive(); RestoreInstruction();
    startTarget_->ResetForSpawn(startTarget_->GetSpawnId(),"GAME_START",{},{});
    if (!enemies_.empty()) pool_.ReleaseForReset(enemies_[0]);
    SpawnEnemy(); respawnTime_=0;
    player_.ResetHPForDebug(); player_.SetMovementEnabled(true);
    player_.SetStage(&level_.collision,level_.playerPosition,level_.playerRotation);
    player_.CurrentWeapon().Equip(*weapons_.Find(weaponId_)); player_.CurrentWeapon().SetInfiniteAmmo(true);
    camera_.SetNearZ(.1f); camera_.SetFovY(std::numbers::pi_v<float>/3); player_.RefreshVisuals();
    mode_=normalMode ? Mode::GameStart : Mode::GameOver;
    suppressFireUntilRelease_=true; initialCapturePending_=false;
    app.GetInput()->SetCameraControlEnabled(false); app.GetInput()->SetMouseCaptureRect(nullptr);
    ResetLightFlicker(); environment_.Update(0); demoText_.Update(0);
    for (auto* enemy:enemies_) enemy->UpdateVisuals(0);
}

bool TitleScene::ChooseDemoMenu(GameApp& app,bool retry) {
    if (!gameOver_.MenuReady() || app.Scenes().IsTransitioning()) return false;
    ResetDemo(app,!retry);
    if (retry) BeginGameOverDemo(app);
    return true;
}

int TitleScene::ModeSwitchHit(const Vector2& point) {
    if (point.y<20 || point.y>52 || point.x<980 || point.x>=1260) return -1;
    return point.x<1120 ? 0 : 1;
}

void TitleScene::DrawModeSwitch() {
    modeFont_.Reset();
    for (int i=0;i<2;++i) {
        const bool selected=(mode_==Mode::GameOver)==(i==1);
        modeButtons_[i].SetColor(selected ? Vector4{.12f,.22f,.27f,.9f} : Vector4{.03f,.04f,.05f,.75f});
        modeButtons_[i].Update(uiView_,uiProjection_); modeButtons_[i].Draw();
        modeFont_.DrawText(i==0 ? "GAME START" : "GAME OVER",990.0f+i*140,28,11,
            selected ? Vector4{1,.85f,.45f,1} : Vector4{.65f,.7f,.75f,1});
    }
    modeFont_.DrawText("ESC: SELECT MODE",1010,58,10,{.65f,.7f,.75f,.85f});
    modeFont_.Draw(uiView_,uiProjection_);
}
