#include "TitleScene.h"
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include "WinApp.h"
#include "GeometryGenerator.h"
#include <map>
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
    const auto& asset = *startDefinition_.partAsset;
    startExplosion_.source=asset.defaults[0];
    PrepareTextExplosion(app,startExplosion_,"TitleStart/"+asset.path,asset.texture);
}

void TitleScene::PrepareBackgroundExplosions(GameApp& app) {
    environmentFull_=environment_.GetModel();
    PrepareBackgroundText(app,unaliveExplosion_,"Game_Title","UNALIVE");
    PrepareBackgroundText(app,instructionExplosion_,"Start_Instruction","SHOOT TO START");
    environmentTextVariants_[0]=environmentFull_;
    // Independent destruction needs variants for either word and for both.
    // Every other mesh, including the instruction's plinth, keeps its geometry.
    for (size_t mask=1;mask<environmentTextVariants_.size();++mask) {
        auto filtered=environmentFull_->GetModelData();
        if (mask&1) for (uint32_t index : unaliveExplosion_.meshes) filtered.meshes[index].indexCount=0;
        if (mask&2) for (uint32_t index : instructionExplosion_.meshes) filtered.meshes[index].indexCount=0;
        environmentTextVariants_[mask]=ModelManager::GetInstance()->CreatePrimitiveModel(
            "TitleEnvironment/hiddenText"+std::to_string(mask)+"/"+level_.model,filtered);
    }
}

void TitleScene::PrepareBackgroundText(GameApp& app,TextExplosion& burst,const std::string& nodePrefix,const std::string& label) {
    const auto& model=environmentFull_->GetModelData();
    auto geometry=std::make_shared<EnemyPartGeometry>();
    auto& source=burst.source;
    source=EnemyPart{}; source.name=label; source.hp=source.maxHp=1;
    burst.meshes.clear();
    const auto base=Matrix4x4::MakeAffineMatrix(environment_.GetScale(),environment_.GetRotate(),environment_.GetTranslate());
    // Locate the exported title node rather than depending on mesh ordering.
    // Bake its transform once so raycasts and detached glyphs share world positions.
    const auto collect=[&](const auto& self,const Model::Node& node,const Matrix4x4& parent,bool title) -> void {
        const auto world=Matrix4x4::Multiply(node.localMatrix,parent);
        title=title || node.name.starts_with(nodePrefix) || node.name==label;
        if (title) for (uint32_t index : node.meshIndices) {
            const auto& mesh=model.meshes.at(index);
            const auto normals=Matrix4x4::Transpose(Matrix4x4::Inverse(world));
            for (uint32_t i=0;i+2<mesh.indexCount;i+=3) {
                std::array<EnemyPartVertex,3> triangle;
                std::array<Vector3,3> face;
                for (size_t v=0;v<3;++v) {
                    const auto& vertex=mesh.vertices.at(model.indices.at(mesh.startIndex+i+v));
                    const auto p=StagePoint({vertex.position.x,vertex.position.y,vertex.position.z},world);
                    const auto n=StagePoint(vertex.normal,normals)-StagePoint({},normals);
                    const float length=StageLength(n);
                    triangle[v]={p,length>1e-5f ? n*(1/length) : Vector3{0,1,0},vertex.texcoord};
                    face[v]=p;
                    if (geometry->triangles.empty() && v==0) source.bounds={p,p};
                    source.bounds.min={std::min(source.bounds.min.x,p.x),std::min(source.bounds.min.y,p.y),std::min(source.bounds.min.z,p.z)};
                    source.bounds.max={std::max(source.bounds.max.x,p.x),std::max(source.bounds.max.y,p.y),std::max(source.bounds.max.z,p.z)};
                }
                geometry->triangles.push_back(triangle); geometry->faces.push_back(face);
            }
            burst.meshes.push_back(index);
        }
        for (const auto& child : node.children) self(self,child,world,title);
    };
    collect(collect,model.rootNode,base,false);
    if (geometry->triangles.empty()) throw std::runtime_error("Title text mesh is missing: "+label);
    source.geometry=std::move(geometry);
    burst.hitParts.clear(); burst.hitParts.push_back(source);
    PrepareTextExplosion(app,burst,"TitleText/"+nodePrefix+"/"+level_.model,"resources/white1x1.png");
}

std::optional<BulletHit> TitleScene::TraceUnalive(const Vector3& origin,const Vector3& direction,float distance) const {
    return TraceBackgroundText(unaliveExplosion_,0,origin,direction,distance);
}

std::optional<BulletHit> TitleScene::TraceInstruction(const Vector3& origin,const Vector3& direction,float distance) const {
    return TraceBackgroundText(instructionExplosion_,1,origin,direction,distance);
}

std::optional<BulletHit> TitleScene::TraceTitleText(const Vector3& origin,const Vector3& direction,float distance) const {
    const auto title=TraceUnalive(origin,direction,distance);
    const auto instruction=TraceInstruction(origin,direction,title ? title->distance : distance);
    return instruction ? instruction : title;
}

std::optional<BulletHit> TitleScene::TraceBackgroundText(const TextExplosion& burst,size_t index,
    const Vector3& origin,const Vector3& direction,float distance) const {
    if (burst.destroyed || burst.preview) return std::nullopt;
    EnemyPartHit part;
    if (!RaycastEnemyParts(burst.hitParts,Matrix4x4::MakeIdentity4x4(),origin,direction,distance,part)) return std::nullopt;
    BulletHit hit;
    hit.distance=part.distance; hit.position=part.position; hit.wall=false; hit.targetIndex=index;
    return hit;
}

void TitleScene::BeginUnaliveExplosion(bool preview) {
    BeginBackgroundTextExplosion(unaliveExplosion_,lighting_.spots[0].color,preview);
}

void TitleScene::RestoreUnalive() {
    RestoreBackgroundText(unaliveExplosion_);
}

void TitleScene::BeginInstructionExplosion(bool preview) {
    BeginBackgroundTextExplosion(instructionExplosion_,lighting_.spots[2].color,preview);
}

void TitleScene::RestoreInstruction() {
    RestoreBackgroundText(instructionExplosion_);
}

void TitleScene::BeginBackgroundTextExplosion(TextExplosion& burst,const Vector3& tint,bool preview) {
    if (!preview && (burst.destroyed || burst.preview)) return;
    burst.destroyed=!preview; burst.preview=preview; burst.active=true;
    RefreshTitleEnvironment();
    LaunchTextExplosion(burst,tint);
    ApplyLighting();
}

void TitleScene::RestoreBackgroundText(TextExplosion& burst) {
    burst.destroyed=false; burst.preview=false; burst.active=false;
    RefreshTitleEnvironment();
    ApplyLighting();
}

void TitleScene::RefreshTitleEnvironment() {
    const size_t mask=(unaliveExplosion_.destroyed || unaliveExplosion_.preview ? 1u : 0u) |
        (instructionExplosion_.destroyed || instructionExplosion_.preview ? 2u : 0u);
    environment_.SetModel(environmentTextVariants_[mask]);
}

void TitleScene::PrepareTextExplosion(GameApp& app,TextExplosion& burst,const std::string& key,const std::string& texture) {
    burst.letters.clear(); burst.fragments.clear();
    const auto& triangles = burst.source.geometry->triangles;
    // Weld coincident vertex positions to recover connected whole glyphs, including
    // bevels and holes. X ranges alone merge some tightly spaced instruction letters.
    std::vector<size_t> parents(triangles.size());
    for (size_t i=0;i<parents.size();++i) parents[i]=i;
    const auto root=[&](size_t i) {
        while (parents[i]!=i) { parents[i]=parents[parents[i]]; i=parents[i]; }
        return i;
    };
    std::map<std::array<float,3>,size_t> vertexFaces;
    for (size_t i=0;i<triangles.size();++i) {
        for (const auto& vertex : triangles[i]) {
            const auto& p=vertex.position;
            const auto [face,inserted]=vertexFaces.emplace(std::array<float,3>{p.x,p.y,p.z},i);
            if (!inserted) parents[root(i)]=root(face->second);
        }
    }
    struct Glyph { float left=std::numeric_limits<float>::max(); std::vector<size_t> faces; };
    std::map<size_t,Glyph> components;
    for (size_t i=0;i<triangles.size();++i) {
        auto& glyph=components[root(i)];
        glyph.faces.push_back(i);
        for (const auto& vertex : triangles[i]) glyph.left=std::min(glyph.left,vertex.position.x);
    }
    std::vector<Glyph> letters;
    for (auto& [component,glyph] : components) letters.push_back(std::move(glyph));
    std::sort(letters.begin(),letters.end(),[](const Glyph& a,const Glyph& b) { return a.left<b.left; });
    std::vector<Model::ModelData> geometries(letters.size());
    for (auto& geometry : geometries) {
        geometry.materials.push_back({texture});
        geometry.meshes.emplace_back(); geometry.rootNode.meshIndices.push_back(0);
    }
    for (size_t i=0;i<letters.size();++i) {
        auto& geometry=geometries[i];
        for (size_t face : letters[i].faces) {
            for (const auto& v : triangles[face]) {
                geometry.meshes[0].vertices.push_back({{v.position.x,v.position.y,v.position.z,1},v.uv,v.normal});
                geometry.indices.push_back(static_cast<uint32_t>(geometry.indices.size()));
            }
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
            key+"/letter"+std::to_string(i),geometry);
        burst.letters.push_back(prepare(model));
    }
    Model::ModelData box;
    box.materials.push_back({texture}); box.meshes.emplace_back();
    box.meshes[0].vertices=GeometryGenerator::GenerateBoxTriList(1,1,1);
    box.meshes[0].indexCount=static_cast<uint32_t>(box.meshes[0].vertices.size());
    for (uint32_t i=0;i<box.meshes[0].indexCount;++i) box.indices.push_back(i);
    box.rootNode.meshIndices.push_back(0);
    auto* cube=ModelManager::GetInstance()->CreatePrimitiveModel("TitleStart/fragmentBox",box);
    burst.fragments.reserve(kMaxStartFragments);
    for (size_t i=0;i<kMaxStartFragments;++i) burst.fragments.push_back(prepare(cube));
}

void TitleScene::BeginStartExplosion(bool preview) {
    explosionPreview_=preview;
    player_.CurrentWeapon().CancelBurst();
    // Preserve the existing damage/raycast target; title owns its special visual
    // burst. Discard its hidden generic face burst via the existing pool API.
    if (!preview) startTarget_->RetireFromPool();
    LaunchTextExplosion(startExplosion_,lighting_.spots[2].color);
    ApplyLighting();
}

void TitleScene::LaunchTextExplosion(TextExplosion& burst,const Vector3& tint) {
    burst.elapsed=0;
    const auto& settings=explosionSettings_;
    const auto& part=burst.source;
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
    for (auto& letter : burst.letters) {
        const auto letterCenter=(letter.motion.bounds.min+letter.motion.bounds.max)*.5f;
        launch(letter,letterCenter,{1,1,1});
    }
    burst.fragmentCount=static_cast<size_t>(std::clamp(settings.fragmentCount,0,static_cast<int>(kMaxStartFragments)));
    const auto& faces=part.geometry->faces;
    std::uniform_int_distribution<size_t> faceIndex(0,faces.size()-1);
    const auto& cameraWorld=camera_.GetWorldMatrix();
    const Vector3 right{cameraWorld.m[0][0],cameraWorld.m[0][1],cameraWorld.m[0][2]};
    const Vector3 up{cameraWorld.m[1][0],cameraWorld.m[1][1],cameraWorld.m[1][2]};
    const Vector3 forward{cameraWorld.m[2][0],cameraWorld.m[2][1],cameraWorld.m[2][2]};
    const size_t nearPassCount=std::min(size_t{4},burst.fragmentCount/8);
    for (size_t i=0;i<burst.fragmentCount;++i) {
        auto& fragment=burst.fragments[i];
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
        burst.castsShadow[i]=!nearPass && std::max({scale.x,scale.y,scale.z})>=.20f;
    }
    // The attached word is white under its spot. Match that tint
    // for scattered pieces even after they leave the spotlight's cone.
    burst.color={.35f+.65f*tint.x,.35f+.65f*tint.y,.35f+.65f*tint.z,1};
    UpdateTextExplosionVisuals(burst);
}

void TitleScene::UpdateStartExplosion(float dt) {
    const float duration=std::max({explosionSettings_.lifetime,explosionSettings_.flashDuration,explosionSettings_.shakeDuration});
    if (StartExplosionActive()) {
        UpdateTextExplosion(startExplosion_,dt);
        if (explosionPreview_ && startExplosion_.elapsed>=duration) explosionPreview_=false;
    }
    for (auto* burst : {&unaliveExplosion_,&instructionExplosion_}) {
        if (!burst->active) continue;
        UpdateTextExplosion(*burst,dt);
        if (burst->elapsed>=duration) {
            burst->active=false;
            if (burst->preview) RestoreBackgroundText(*burst);
        }
    }
}

void TitleScene::UpdateTextExplosion(TextExplosion& burst,float dt) {
    burst.elapsed+=dt;
    for (auto& letter : burst.letters) letter.motion.Update(dt);
    for (size_t i=0;i<burst.fragmentCount;++i) burst.fragments[i].motion.Update(dt);
}

void TitleScene::UpdateStartExplosionVisuals() {
    if (StartExplosionActive()) UpdateTextExplosionVisuals(startExplosion_);
    for (auto* burst : {&unaliveExplosion_,&instructionExplosion_})
        if (burst->active) UpdateTextExplosionVisuals(*burst);
}

void TitleScene::UpdateTextExplosionVisuals(TextExplosion& burst) {
    const bool flash=explosionSettings_.flashDuration>0 && burst.elapsed<explosionSettings_.flashDuration;
    const float brightness=flash ? 1+2*(1-burst.elapsed/explosionSettings_.flashDuration) : 1;
    const auto update=[&](DetachedEnemyPart& piece,bool letter) {
        auto& object=*piece.object;
        object.SetTranslate(piece.motion.Translation()); object.SetRotate(piece.motion.rotation);
        object.SetScale(piece.motion.scale);
        const auto color=letter ? Vector4{1,1,1,1} : burst.color;
        object.SetMaterialColor({color.x*brightness,color.y*brightness,color.z*brightness,1});
        object.SetEnableLighting(flash ? 0 : 2); // Existing unlit material provides the short impact flash.
        object.Update(0);
    };
    for (auto& letter : burst.letters) update(letter,true);
    for (size_t i=0;i<burst.fragmentCount;++i) update(burst.fragments[i],false);
}

void TitleScene::DrawTextExplosion(TextExplosion& burst,bool shadow) {
    const auto draw=[&](DetachedEnemyPart& piece) {
        if (!piece.motion.Active()) return;
        if (shadow) piece.object->DrawDirectionalShadow(shadowMap_);
        else piece.object->Draw();
    };
    for (auto& letter : burst.letters) draw(letter);
    for (size_t i=0;i<burst.fragmentCount;++i)
        if (!shadow || burst.castsShadow[i]) draw(burst.fragments[i]);
}

void TitleScene::ApplyStartCameraShake() {
    float elapsed=std::numeric_limits<float>::max();
    if (StartExplosionActive()) elapsed=startExplosion_.elapsed;
    for (const auto* burst : {&unaliveExplosion_,&instructionExplosion_})
        if (burst->active) elapsed=std::min(elapsed,burst->elapsed);
    if (explosionSettings_.shakeDuration<=0 || elapsed>=explosionSettings_.shakeDuration) return;
    const float t=elapsed/explosionSettings_.shakeDuration;
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
    ResetLightFlicker();
    ApplyLighting();
}

void TitleScene::ResetLightFlicker() {
    lightFlicker_={};
    for (size_t i=0;i<lightFlicker_.size();++i) {
        const auto& f=lighting_.spots[i].flicker;
        if (f.enabled) lightFlicker_[i].remaining=std::uniform_real_distribution<float>(f.minInterval,f.maxInterval)(flickerRandom_[i]);
    }
}

void TitleScene::UpdateLightFlicker(float dt) {
    dt=std::isfinite(dt) ? std::clamp(dt,0.0f,10.0f) : 0;
    for (size_t i=0;i<lightFlicker_.size();++i) {
        auto& state=lightFlicker_[i];
        const auto& f=lighting_.spots[i].flicker;
        if (!f.enabled) { state={}; continue; }
        const auto duration=[&](float min,float max) { return std::uniform_real_distribution<float>(min,max)(flickerRandom_[i]); };
        if (state.remaining<=0) state.remaining=duration(f.minInterval,f.maxInterval);
        float remaining=dt;
        // Carry elapsed time across on/off edges so frame rate does not change
        // the burst. Separate waits keep the two title lamps out of sync.
        while (remaining>=state.remaining) {
            remaining-=state.remaining;
            if (!state.off) {
                if (state.flashesRemaining==0) state.flashesRemaining=f.flashes;
                state.off=true;
                state.remaining=duration(f.minOffTime,f.maxOffTime);
            } else {
                state.off=false;
                --state.flashesRemaining;
                state.remaining=state.flashesRemaining>0 ? duration(.06f,.16f) : duration(f.minInterval,f.maxInterval);
            }
        }
        state.remaining-=remaining;
    }
    ApplyLighting();
}

void TitleScene::ApplyLighting() {
    auto current=lighting_;
    for (size_t i=0;i<lightFlicker_.size();++i)
        if (current.spots[i].flicker.enabled && lightFlicker_[i].off)
            current.spots[i].intensity*=current.spots[i].flicker.offBrightness;
    // Destruction overrides flicker without changing the saved tuning values.
    if (unaliveExplosion_.destroyed || unaliveExplosion_.preview) current.spots[0].intensity=0;
    if (enemies_.empty() || enemies_[0]->IsDead()) current.spots[1].intensity=0;
    // GAME START and SHOOT TO START share one lamp; either destruction turns it off.
    if (StartExplosionActive() || (startTarget_ && startTarget_->IsDead()) ||
        instructionExplosion_.destroyed || instructionExplosion_.preview) current.spots[2].intensity=0;
    current.Apply(sceneLight_); // Saved base intensities and ambient/directional lighting stay intact.
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
    explosionPreview_=false; startExplosion_.elapsed=0; startExplosion_.fragmentCount=0;
    for (auto* burst : {&unaliveExplosion_,&instructionExplosion_}) {
        burst->destroyed=false; burst->active=false; burst->preview=false;
    }
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
    PrepareBackgroundExplosions(app);
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
    ApplyLighting();
}
void TitleScene::OnBulletImpact(const BulletEnemyImpact& impact) {
    if (enemies_[impact.enemyIndex] == startTarget_.get() && impact.result.damage>0 && startTarget_->IsDead()) {
        if (start_.Begin()) BeginStartExplosion();
    }
    ApplyLighting();
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
    UpdateLightFlicker(dt);
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
    const BulletTrace titleTarget=[this](const Vector3& origin,const Vector3& direction,float distance) {
        return TraceTitleText(origin,direction,distance);
    };
    bullets_.Update(dt,level_.collision,enemies_,[this](const BulletEnemyImpact& hit) { OnBulletImpact(hit); },
        titleTarget,[this](const Bullet& bullet,const BulletHit& hit) {
            if (bullet.damage<=0) return;
            if (hit.targetIndex==0) BeginUnaliveExplosion();
            else if (hit.targetIndex==1) BeginInstructionExplosion();
        });
    controls = controls && !StartExplosionActive();
    auto& weapon = player_.CurrentWeapon();
    // Extended target practice must not exhaust all ammunition and block start.
    if (!start_.Starting() && weapon.Magazine()==0 && weapon.Reserve()==0) weapon.Equip(*weapons_.Find(weaponId_));
    const int shots = player_.UpdateShooting(input,dt,controls && !suppressFireUntilRelease_,controls);
    for (int shot=0; shot<shots; ++shot)
        bullets_.Spawn(weapon.Definition(),camera_.GetWorldMatrix(),0,random_,level_.collision,enemies_,titleTarget);
    ApplyStartCameraShake();
    UpdateStartExplosionVisuals();
    environment_.Update(dt);
    ApplyLighting(); // Include this frame's impacts, respawns and preview restoration.
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
        if (StartExplosionActive()) DrawTextExplosion(startExplosion_,true);
        for (auto* burst : {&unaliveExplosion_,&instructionExplosion_})
            if (burst->active) DrawTextExplosion(*burst,true);
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
    if (StartExplosionActive()) DrawTextExplosion(startExplosion_,false);
    for (auto* burst : {&unaliveExplosion_,&instructionExplosion_})
        if (burst->active) DrawTextExplosion(*burst,false);
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
            if (ImGui::Button("Preview UNALIVE")) BeginUnaliveExplosion(true);
            ImGui::SameLine();
            if (ImGui::Button("Restore UNALIVE")) RestoreUnalive();
            if (ImGui::Button("Preview SHOOT TO START")) BeginInstructionExplosion(true);
            ImGui::SameLine();
            if (ImGui::Button("Restore SHOOT TO START")) RestoreInstruction();
            ImGui::TextUnformatted("Explosion values are session-only. Lighting save affects lights only.");
            ImGui::TextUnformatted("UNALIVE / SHOOT TO START share these settings. Shooting them does not start the game.");
            ImGui::TextUnformatted("Preview restores GAME START after Lifetime. Default fade delay: 0.45 s.");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        bool changed = false, flickerChanged = false;
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
            auto& flicker=light.flicker;
            flickerChanged |= ImGui::Checkbox("Lamp Flicker",&flicker.enabled);
            ImGui::BeginDisabled(!flicker.enabled);
            flickerChanged |= ImGui::DragFloatRange2("Flicker Interval (s)",&flicker.minInterval,&flicker.maxInterval,.05f,.2f,20,"Min %.2f","Max %.2f",ImGuiSliderFlags_AlwaysClamp);
            flickerChanged |= ImGui::DragFloatRange2("Off Duration (s)",&flicker.minOffTime,&flicker.maxOffTime,.005f,.01f,.4f,"Min %.3f","Max %.3f",ImGuiSliderFlags_AlwaysClamp);
            flickerChanged |= ImGui::SliderInt("Burst Flashes",&flicker.flashes,1,6);
            flickerChanged |= ImGui::SliderFloat("Off Brightness",&flicker.offBrightness,0,1,"%.2f");
            ImGui::EndDisabled();
            ImGui::TextUnformatted("Off Brightness: 0 = lamp off, 1 = normal brightness.");
            ImGui::PopID();
        }
        if (flickerChanged) { ResetLightFlicker(); changed=true; }
        if (changed) {
            ApplyLighting();
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
            ResetLightFlicker();
            ApplyLighting();
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
