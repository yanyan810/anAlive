#include "GameScene.h"
#include "GameApp.h"
#include <limits>
#include <map>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
    Vector3 RollEscapeBody(const Vector3& heading,float roll) {
        if (roll==0) return heading;
        // Apply roll about the car's local forward axis before its road heading.
        const auto m=Matrix4x4::RotateZ(roll)*Matrix4x4::RotateXYZ(heading.x,heading.y,heading.z);
        const float horizontal=std::hypot(m.m[0][0],m.m[0][1]);
        const float yaw=std::atan2(-m.m[0][2],horizontal);
        if (horizontal<1e-6f) return {std::atan2(-m.m[2][1],m.m[1][1]),yaw,0};
        return {std::atan2(m.m[1][2],m.m[2][2]),yaw,std::atan2(m.m[0][1],m.m[0][0])};
    }

    const CameraSequenceDefinition* FindEscapeCamera(const StageLoader& level,const std::string& id) {
        const auto it=std::find_if(level.cameraSequences.begin(),level.cameraSequences.end(),
            [&](const auto& camera) { return camera.id==id && camera.enabled && !camera.points.back().playerEye; });
        return it==level.cameraSequences.end() ? nullptr : &*it;
    }

    double EscapeImpactTime(const HotelEscape& escape,const Vector3& feet,const EnemyDefinition& enemy) {
        const auto overlaps=[&](double time) {
            const auto car=escape.CarAt(time);
            if (feet.y>car.position.y+1.05f || feet.y+enemy.collisionHeight<car.position.y+.25f) return false;
            const auto delta=feet-car.position;
            const float side=std::max(0.f,std::abs(StageDot(delta,car.Right()))-.98f);
            const float end=std::max(0.f,std::abs(StageDot(delta,car.Forward()))-2.12f);
            return side*side+end*end<=enemy.collisionRadius*enemy.collisionRadius;
        };
        // The cast is stationary. Sample the moving hull on the cutscene clock, then
        // refine the first contact so playback and impact timing are independent of render frames.
        if (overlaps(escape.departure)) return escape.departure;
        constexpr double step=1.0/240;
        for (double left=escape.departure;left<escape.duration;) {
            double right=std::min<double>(escape.duration,left+step);
            if (!overlaps(right)) { left=right; continue; }
            for (int i=0;i<8;++i) {
                const double mid=(left+right)*.5;
                if (overlaps(mid)) right=mid; else left=mid;
            }
            return right;
        }
        return -1;
    }
}

void GameScene::InitializeHotelEscape(GameApp& app) {
    escapeMode_=EscapeMode::Idle;
    if (!level_.hotelEscapeWarning.empty())
        OutputDebugStringA(("Hotel escape disabled: "+level_.hotelEscapeWarning+"\n").c_str());
    if (!level_.hotelEscape || showroom_ || placementTest_) return;
    const auto& escape=*level_.hotelEscape;
    const auto goal=std::find_if(stage_.Goals().begin(),stage_.Goals().end(),
        [&](const auto& item) { return item.id==escape.goal; });
    if (goal==stage_.Goals().end() || !FindEscapeCamera(level_,escape.cameras[0]) ||
        !FindEscapeCamera(level_,escape.cameras[1]) ||
        (!escape.launchCamera.empty() && !FindEscapeCamera(level_,escape.launchCamera))) {
        OutputDebugStringA("Hotel escape disabled: missing Goal or camera sequence\n");
        return;
    }

    const auto makeObject=[&] {
        auto object=std::make_unique<Object3d>();
        object->Initialize(app.ObjCom(),app.Dx());
        object->SetCamera(&camera_);
        object->SetEnableLighting(2);
        object->SetDirection({.3f,-1,.5f});
        object->SetIntensity(1);
        object->SetPointLightIntensity(0);
        object->SetSpotLightIntensity(0);
        return object;
    };
    escapeCar_=makeObject();
    constexpr auto carModel="levels/hotel_escape_car.glb";
    std::error_code error;
    if (std::filesystem::is_regular_file(std::filesystem::path("resources")/carModel,error)) {
        try { escapeCar_->SetModel(carModel); }
        catch (const std::exception& problem) { OutputDebugStringA(problem.what()); }
    }
    if (!escapeCar_->GetModel()) {
        escapeCar_->SetModel("cube/cube.obj");
        escapeCar_->SetScale({.95f,.65f,2.05f});
        escapeCar_->SetMaterialColor({.18f,.38f,.46f,1});
    }
    auto* model=escapeCar_->GetModel();
    struct WheelMeshes { std::vector<uint32_t> meshes; std::optional<AABB> tire; };
    std::map<std::string,WheelMeshes> groups;
    const auto meshBounds=model->GetMeshesLocalAABBs();
    const auto meshCount=model->GetMeshCount();
    if (meshBounds.size()==meshCount) for (uint32_t i=0;i<meshCount;++i) {
        const auto& name=model->GetNodeName(model->GetMeshOwnerNodeIndex(i));
        const bool tire=name=="Wheel" || name.starts_with("Wheel.");
        const bool hub=name=="Hub" || name.starts_with("Hub.");
        if (!tire && !hub) continue;
        auto& group=groups[name.substr(tire ? 5 : 3)];
        group.meshes.push_back(i);
        if (tire) group.tire=meshBounds[i].localAABB;
    }
    std::vector<uint32_t> hiddenOnBody;
    for (const auto& [suffix,group]:groups) {
        if (!group.tire) continue;
        const auto& bounds=*group.tire;
        const float radius=(bounds.max.y-bounds.min.y)*.5f;
        if (radius<=1e-4f) continue;
        auto wheel=makeObject();
        wheel->SetModel(model);
        std::vector<uint32_t> hiddenOnWheel;
        for (uint32_t i=0;i<meshCount;++i)
            if (std::find(group.meshes.begin(),group.meshes.end(),i)==group.meshes.end()) hiddenOnWheel.push_back(i);
        wheel->SetHiddenMeshes(std::move(hiddenOnWheel));
        hiddenOnBody.insert(hiddenOnBody.end(),group.meshes.begin(),group.meshes.end());
        escapeCarWheels_.push_back({std::move(wheel),(bounds.min+bounds.max)*.5f,radius});
    }
    escapeCar_->SetHiddenMeshes(std::move(hiddenOnBody));
    if (escapeCarWheels_.empty()) {
        AABB bounds;
        if (model->GetLocalAABB(bounds)) escapeUnsplitCarBounds_=bounds;
    }
    PositionEscapeCar(0);
    if (escape.roadWidth>0) for (const auto& segment:escape.route) {
        auto road=makeObject();
        road->SetModel("cube/cube.obj"); road->SetTexture("resources/white1x1.png");
        road->SetMaterialColor({.13f,.145f,.16f,1});
        road->SetRotate(segment.Heading());
        road->SetScale({escape.roadWidth*.5f,.13f,segment.length*.5f+2.5f});
        road->SetTranslate(segment.start+segment.direction*(segment.length*.5f)-Vector3{0,.14f,0});
        escapeRoad_.push_back(std::move(road));
    }
    if (!escape.hiddenMeshes.empty()) {
        const auto* stageModel=ground_.GetModel();
        for (uint32_t i=0;i<stageModel->GetMeshCount();++i) {
            const auto name=stageModel->GetNodeName(stageModel->GetMeshOwnerNodeIndex(i));
            if (std::find(escape.hiddenMeshes.begin(),escape.hiddenMeshes.end(),name)!=escape.hiddenMeshes.end())
                escapeHiddenMeshes_.push_back(i);
        }
    }

    escapeFade_.Initialize(app.SpriteCom(),app.Dx(),"resources/white1x1.png");
    const auto& size=TextureManager::GetInstance()->GetMetaData("resources/white1x1.png");
    escapeFade_.SetScale({static_cast<float>(WinApp::kClientWidth)/size.width,
        static_cast<float>(WinApp::kClientHeight)/size.height,1});
}

void GameScene::PositionEscapeCar(double elapsed) {
    const auto& escape=*level_.hotelEscape;
    const auto [position,heading]=escape.CarAt(elapsed);
    const auto motion=EscapePlaying() ? escape.BodyAt(elapsed) : HotelEscape::BodyPose{};
    const auto orientation=Matrix4x4::RotateXYZ(heading.x,heading.y,heading.z);
    for (auto& wheel:escapeCarWheels_) {
        const auto axle=position+StagePoint(wheel.center,orientation);
        const float spin=escape.WheelAngleAt(elapsed,wheel.radius,wheel.center.z<0);
        const auto rotation=heading+Vector3{spin,0,0};
        const auto spun=Matrix4x4::RotateXYZ(rotation.x,rotation.y,rotation.z);
        // The GLB positions are baked in chassis space. Recenter each wheel on its fixed axle.
        wheel.visual->SetTranslate(axle-StagePoint(wheel.center,spun));
        wheel.visual->SetRotate(rotation);
    }
    float height=motion.height;
    if (escapeUnsplitCarBounds_) {
        // Keep the lowest corner on or above the road plane when the whole mesh tilts.
        const auto scale=escapeCar_->GetScale();
        const auto tilted=Matrix4x4::Scale(scale)*Matrix4x4::RotateZ(motion.roll)*Matrix4x4::RotateX(motion.pitch);
        height=std::max(height-escapeUnsplitCarBounds_->min.y*scale.y,
            -TransformAABB(*escapeUnsplitCarBounds_,tilted).min.y);
    }
    const Vector3 up{orientation.m[1][0],orientation.m[1][1],orientation.m[1][2]};
    escapeCar_->SetTranslate(position+up*height);
    escapeCar_->SetRotate(RollEscapeBody(heading+Vector3{motion.pitch,0,0},motion.roll));
    RefreshEscapeCarVisuals();
}

void GameScene::RefreshEscapeCarVisuals() {
    if (escapeCar_) escapeCar_->Update(0);
    for (const auto& wheel:escapeCarWheels_) wheel.visual->Update(0);
}

bool GameScene::BeginHotelEscape(GameApp& app,bool preview) {
    if (!escapeCar_ || EscapePlaying() || sequenceOwnsCamera_ || gameOver_.Active() ||
        (preview && (!stage_.IsPlaying() || app.Scenes().IsTransitioning()))) return false;
    const auto& escape=*level_.hotelEscape;
    if (!preview && std::none_of(stage_.Goals().begin(),stage_.Goals().end(),
        [&](const auto& goal) { return goal.id==escape.goal && goal.activated; })) return false;

    escapeReturnCamera_={camera_.GetTranslate(),camera_.GetRotate(),camera_.GetFovY()};
    for (size_t i=0;i<escapeCameras_.size();++i)
        escapeCameras_[i].Start(*FindEscapeCamera(level_,escape.cameras[i]),escapeReturnCamera_,player_.GetTransform().translate);
    escapeLaunchCamera_.reset();
    if (const auto* launch=FindEscapeCamera(level_,escape.launchCamera))
        escapeLaunchCamera_.emplace().Start(*launch,escapeReturnCamera_,player_.GetTransform().translate);
    for (size_t i=0;i<escape.actors.size();++i) {
        const auto& actor=escape.actors[i];
        const auto* definition=enemyDefinitions_.Find(actor.enemy);
        if (!definition) {
            OutputDebugStringA(("Hotel escape: unknown enemy " + actor.enemy + "; trying normal\n").c_str());
            definition=enemyDefinitions_.Find("normal");
        }
        if (!definition) continue;
        auto* enemy=enemyPool_.Acquire(definition->id,std::numeric_limits<uint64_t>::max()-i,
            "hotelEscape",actor.position,actor.rotation);
        enemy->SetAIPaused(true);
        escapeEnemies_.push_back({enemy,i,EscapeImpactTime(escape,actor.position,*definition)});
    }
    escapeTime_=0;
    escapeMode_=preview ? EscapeMode::Preview : EscapeMode::Clear;
    escapeWasCaptured_=app.GetInput()->IsCameraControlEnabled();
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    constexpr int mouseFlags=ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~mouseFlags) | savedMouseFlags_;
#endif
    SyncRemovableMeshVisuals();
    UpdateShutterAudio(false);
    UpdateHotelEscape(app,0);
    return true;
}

#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
bool GameScene::HandleHotelEscapeKey(GameApp& app) {
    return app.GetInput()->IsKeyTrigger(DIK_K) && BeginHotelEscape(app,true);
}
#endif

void GameScene::UpdateHotelEscape(GameApp& app,float dt) {
    const auto& escape=*level_.hotelEscape;
    const double previous=escapeTime_;
    if (std::isfinite(dt) && dt>0) escapeTime_=std::min<double>(escape.duration,escapeTime_+dt);
    const size_t shot=escapeTime_<escape.cut ? 0 : 1;
    auto pose=shot==0 && escapeLaunchCamera_ && escapeTime_>=escape.departure ?
        escapeLaunchCamera_->Evaluate(escapeTime_-escape.departure) :
        escapeCameras_[shot].Evaluate(escapeTime_-(shot ? escape.cut : 0));

    const bool impactShot=escapeLaunchCamera_ ? shot==1 : shot==0;
    constexpr double impactReactionSeconds=.20;
    for (const auto& entry:escapeEnemies_) {
        if (entry.impactTime<0 || entry.impactTime>escapeTime_) continue;
        const double age=escapeTime_-entry.impactTime;
        if (impactShot && age<impactReactionSeconds) {
            // A single vertical bump keeps the horizontal chase speed continuous.
            const double lift=std::sin(std::numbers::pi*age/impactReactionSeconds);
            pose.position.y+=escape.shake*static_cast<float>(lift*lift);
        }
        auto& enemy=*entry.enemy;
        if (enemy.IsDead()) continue;
        const auto& actor=escape.actors[entry.placement];
        const auto car=escape.CarAt(entry.impactTime);
        const auto delta=escape.CarAt(entry.impactTime+.002).position-escape.CarAt(entry.impactTime-.002).position;
        const float speed=StageLength(delta);
        const auto movement=speed>1e-5f ? delta*(1/speed) : car.Forward();
        const float side=std::clamp(StageDot(actor.position-car.position,car.Right()),-1.f,1.f);
        const float authoredLength=StageLength(actor.launchDirection);
        const auto direction=authoredLength>1e-5f ? actor.launchDirection*(1/authoredLength) :
            movement+car.Right()*(side*.35f)+Vector3{0,.22f,0};
        enemy.BreakForImpact(direction,actor.launchPower,actor.position.y);
    }
    camera_.SetTranslate(pose.position); camera_.SetRotate(pose.rotation); camera_.SetFovY(pose.fov); camera_.Update();
    for (const auto& entry:escapeEnemies_) {
        const double start=entry.enemy->IsDead() ? std::max(previous,entry.impactTime) : previous;
        entry.enemy->UpdateVisuals(static_cast<float>(escapeTime_-start));
    }
    PositionEscapeCar(escapeTime_);
    for (auto& road:escapeRoad_) road->Update(0);
    ground_.Update(0); SyncShutterVisuals(); SyncDoorVisuals();
    if (escapeTime_<escape.duration) return;

    app.Dx()->WaitForGPU();
    for (const auto& actor:escapeEnemies_) enemyPool_.ReleaseForReset(actor.enemy);
    escapeEnemies_.clear();
    const bool preview=escapeMode_==EscapeMode::Preview;
    escapeMode_=preview ? EscapeMode::Idle : EscapeMode::Results;
    SyncRemovableMeshVisuals();
    if (!preview) { OnStageClear(app); return; }

    camera_.SetTranslate(escapeReturnCamera_.position); camera_.SetRotate(escapeReturnCamera_.rotation);
    camera_.SetFovY(escapeReturnCamera_.fov); camera_.Update();
    PositionEscapeCar(0);
    ground_.Update(0); SyncShutterVisuals(); SyncDoorVisuals();
    auto& input=*app.GetInput();
    input.SetCameraControlEnabled(escapeWasCaptured_);
#ifdef USE_IMGUI
    constexpr int mouseFlags=ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::GetIO().ConfigFlags=(ImGui::GetIO().ConfigFlags & ~mouseFlags) |
        (input.IsCameraControlEnabled() ? mouseFlags : savedMouseFlags_);
#endif
    suppressFireUntilRelease_=suppressFireUntilRelease_ || input.IsLeftMousePressed();
}

void GameScene::DrawEscapeFade(const Matrix4x4& view,const Matrix4x4& projection) {
    const auto& escape=*level_.hotelEscape;
    const auto opacity=static_cast<float>(std::clamp((escapeTime_-escape.fade)/(escape.duration-escape.fade),0.0,1.0));
    if (opacity<=0) return;
    escapeFade_.SetColor({0,0,0,opacity});
    escapeFade_.Update(view,projection);
    escapeFade_.Draw();
}
