#include "ClothShowroomScene.h"
#ifdef _DEBUG
#include "GameApp.h"
#include "ImGuiManagaer.h"
#include <cmath>
#include <chrono>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

void ClothShowroomScene::OnEnter(GameApp& app) {
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    app.ImGui()->SetWeaponWorkspace(true);
    ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NoMouse|ImGuiConfigFlags_NoMouseCursorChange);
#endif
    app_=&app;
    EnsureCharacter_(selected_);
    // Parse/cache the other model without delaying the initial screen. No worker owns GPU objects.
    ModelManager::GetInstance()->PreloadModel(selected_==0 ? "ema/SakurabaEma_ByPOWER.pmx" : "MyGtYUhe6t/安比.pmx");
    Update(app,0);
}

void ClothShowroomScene::EnsureCharacter_(int index) {
    if(characters_[index] || !app_) return;
    const std::array<std::string,2> paths{"MyGtYUhe6t/安比.pmx","ema/SakurabaEma_ByPOWER.pmx"};
    const std::array<std::string,2> profiles{"resources/physics/anby.json","resources/physics/ema.json"};
    const size_t i=static_cast<size_t>(index);
    auto& app=*app_;
    {
        characters_[i]=std::make_unique<Object3d>();
        auto& object=*characters_[i];
        object.Initialize(app.ObjCom(),app.Dx(),app.Srv(),app.SkinCom());
        object.SetModel(paths[i]); object.SetCamera(&camera_); object.SetScale({.1f,.1f,.1f});
        object.SetRotate({0,0,0}); object.SetTranslate({0,0,0}); object.SetEnableLighting(0);
        cloth_[i].Load(profiles[i],object.GetModel()->GetSkeleton());
        object.SetPoseModifier([this,i](Model::Skeleton& pose,const Matrix4x4& world,float dt) {
            const auto begin=std::chrono::steady_clock::now();
            cloth_[i].Update(pose,world,dt);
            lastClothMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        });
    }
}

void ClothShowroomScene::OnExit(GameApp& app) {
    pendingSelected_=-1; app_=nullptr;
    app.GetInput()->SetCameraControlEnabled(false);
#ifdef USE_IMGUI
    app.ImGui()->SetWeaponWorkspace(false);
#endif
}

void ClothShowroomScene::SelectModel(int index,bool wait) {
    index=std::clamp(index,0,1);
    if(!app_) return;
    const char* path=index==0 ? "MyGtYUhe6t/安比.pmx" : "ema/SakurabaEma_ByPOWER.pmx";
    if(!wait && !characters_[index] && !ModelManager::GetInstance()->IsModelPrepared(path)) {
        if(ModelManager::GetInstance()->PreloadModel(path)) { pendingSelected_=index; return; }
    }
    EnsureCharacter_(index); selected_=index; pendingSelected_=-1; time=0; lastMotionTime_=-1; Cloth().Reset();
}

void ClothShowroomScene::Update(GameApp&,float dt) {
    const auto begin=std::chrono::steady_clock::now();
    if(pendingSelected_>=0) SelectModel(pendingSelected_);
    if (!std::isfinite(dt) || dt<0) dt=0;
    if(paused) dt=0;
    const float elapsed=std::min(dt,.1f);
    const float frequency=motion==2 ? 2.5f : 1.3f;
    const float targetStride=motion==0 ? 0 : motion==2 ? .8f : .52f;
    const float targetKnee=motion==0 ? 0 : motion==2 ? 1.1f : .65f;
    const float targetLean=motion==2 ? -.09f : 0;
    const float targetActivity=motion==0 ? 0.0f : 1.0f;
    // Integrate phase instead of time * the newly selected frequency. Retain
    // cloth momentum while blending FK parameters over about 0.2 seconds.
    if(lastMotionTime_<0 || time<lastMotionTime_) {
        motionPhase_=time*frequency*6.2831853f;
        motionFrequency_=frequency; motionStride_=targetStride; motionKnee_=targetKnee;
        motionLean_=targetLean; motionActivity_=targetActivity;
    } else {
        const float blend=1-std::exp(-elapsed/.18f);
        motionFrequency_+=(frequency-motionFrequency_)*blend;
        motionStride_+=(targetStride-motionStride_)*blend;
        motionKnee_+=(targetKnee-motionKnee_)*blend;
        motionLean_+=(targetLean-motionLean_)*blend;
        motionActivity_+=(targetActivity-motionActivity_)*blend;
    }
    time+=elapsed; lastMotionTime_=time;
    motionPhase_=std::fmod(motionPhase_+elapsed*motionFrequency_*6.2831853f,6.2831853f);
    const float phase=motionPhase_;
    const float stride=motionStride_*gaitAmplitude;
    auto& object=Character();
    object.ResetManualJointTransforms();
    const auto set=[&](const char* name,Vector3 rotation) { object.SetManualJointTransform(name,{0,0,0},rotation,{1,1,1}); };
    for(int side=0;side<2;++side) {
        const float p=phase+static_cast<float>(side)*3.14159265f;
        const float hip=std::sin(p)*stride;
        const float knee=-std::max(0.0f,std::cos(p))*motionKnee_;
        set(side==0 ? "左足D" : "右足D",{hip,0,0});
        set(side==0 ? "左ひざD" : "右ひざD",{knee,0,0});
        set(side==0 ? "左足" : "右足",{hip,0,0});
        set(side==0 ? "左ひざ" : "右ひざ",{knee,0,0});
        set(side==0 ? "左腕" : "右腕",{-hip*.45f,0,side==0 ? -.45f : .45f});
    }
    set("上半身",{motionLean_,std::sin(phase)*stride*.08f,0});
    set("頭",{0,std::sin(time)*.09f,0});
    object.SetRotate({0,yaw,0});
    object.SetTranslate({moving ? std::sin(time*1.1f)*.55f*motionActivity_ : 0,
        std::sin(time*2)*.006f*(1-motionActivity_)+std::abs(std::sin(phase))*.035f*motionActivity_,0});
    const auto translation=object.GetTranslate();
    const Vector3 center{cameraFollowsCharacter ? translation.x : 0,cameraTargetHeight,cameraFollowsCharacter ? translation.z : 0};
    const float horizontal=std::cos(cameraPitch)*cameraDistance;
    camera_.SetTranslate(center+Vector3{std::sin(cameraYaw)*horizontal,std::sin(cameraPitch)*cameraDistance,-std::cos(cameraYaw)*horizontal});
    camera_.SetRotate({cameraPitch,-cameraYaw,0}); camera_.SetFovY(.48f); camera_.SetAspect(1280.0f/720);
    camera_.Update(); object.Update(dt);
    lastUpdateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
}

void ClothShowroomScene::Draw3D(GameApp&) { if(characters_[selected_]) Character().Draw(); }

void ClothShowroomScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Cloth Physics Showroom",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoDocking);
    if(ImGui::BeginTable("Workspace",2,ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Settings",ImGuiTableColumnFlags_WidthFixed,370);
        ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("Preview"); app.ImGui()->DrawScenePreview(); ImGui::EndChild();
        ImGui::TableSetColumnIndex(0); ImGui::BeginChild("Settings");
        ImGui::TextUnformatted("Secondary motion / Verlet + PBD");
        ImGui::Text("CPU update %.2f ms (cloth %.2f ms)",lastUpdateMilliseconds,lastClothMilliseconds);
        if(ImGui::Button("Return to Showroom")) RequestChangeScene_("Showroom");
        ImGui::SameLine(); if(ImGui::Button("Game")) RequestChangeScene_("Game");
        int model=pendingSelected_>=0 ? pendingSelected_ : selected_; if(ImGui::Combo("Model",&model,"MyGtYUhe6t (Anby)\0ema\0")) SelectModel(model);
        if(pendingSelected_>=0) ImGui::TextUnformatted("Preparing model... the current preview remains interactive.");
        if(ImGui::Button("Reload bone profile")) Cloth().Load(selected_==0 ? "resources/physics/anby.json" : "resources/physics/ema.json",Character().GetModel()->GetSkeleton());
        ImGui::Combo("Procedural motion",&motion,"Idle\0Walk\0Run\0");
        ImGui::SliderFloat("Gait amplitude",&gaitAmplitude,.5f,1.5f);
        ImGui::TextWrapped("Source models contain no animation clips. This preview drives FK legs, arms and head; PMX IK/grant transforms are not evaluated.");
        ImGui::Checkbox("Move character (inertia)",&moving); ImGui::Checkbox("Pause",&paused);
        ImGui::SliderAngle("Character direction",&yaw,-180,180);
        ImGui::SliderAngle("Orbit camera",&cameraYaw,-180,180);
        ImGui::SliderAngle("Camera pitch",&cameraPitch,-40,60);
        ImGui::SliderFloat("Camera distance",&cameraDistance,1,8);
        ImGui::SliderFloat("Camera target height",&cameraTargetHeight,.2f,2.2f);
        ImGui::Checkbox("Follow character camera",&cameraFollowsCharacter);
        RECT rect{}; app.ImGui()->GetSceneImageRect(rect);
        Cloth().DrawImGui(camera_.GetViewProjectionMatrix(),{static_cast<float>(rect.left),static_cast<float>(rect.top)},{static_cast<float>(rect.right),static_cast<float>(rect.bottom)});
        ImGui::TextWrapped("Orange: bone colliders. Yellow: pinned roots. Green: free particles. Cyan: distance constraints. Edit resources/physics/*.json to select exact bones or add body colliders.");
        ImGui::EndChild(); ImGui::EndTable();
    }
    ImGui::End();
#else
    (void)app;
#endif
}
#endif
