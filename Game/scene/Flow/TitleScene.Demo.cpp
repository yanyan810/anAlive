#include "TitleScene.h"
#include "GameApp.h"
#include "ModelManager.h"

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
    if (!ready_ || pendingDemoTransition_ || start_.Starting() || explosionPreview_ || transitionRequested_) return false;
    mode_=mode; bullets_.Clear(); suppressFireUntilRelease_=true;
    return true;
}

std::optional<BulletHit> TitleScene::TraceDemoText(const Vector3& origin,const Vector3& direction,float distance) const {
    if (mode_!=Mode::GameOver || pendingDemoTransition_) return {};
    EnemyPartHit part;
    if (!RaycastEnemyParts(demoTextParts_,Matrix4x4::MakeIdentity4x4(),origin,direction,distance,part)) return {};
    BulletHit hit; hit.distance=part.distance; hit.position=part.position; hit.wall=false; hit.targetIndex=2; return hit;
}

void TitleScene::BeginGameOverDemo(GameApp& app) {
    if (mode_!=Mode::GameOver || start_.Starting() || transitionRequested_) return;
    // Called after the bullet update has finished iterating its live shots.
    pendingDemoTransition_=false;
    transitionRequested_=app.Scenes().TransitionTo("GameOverDemo",.75f,.75f,{0,0,0},FadeEasing::Linear);
    if (!transitionRequested_) return;
    bullets_.Clear(); player_.CancelWeaponBurst();
    suppressFireUntilRelease_=true; initialCapturePending_=false;
    app.GetInput()->SetCameraControlEnabled(false);
}
