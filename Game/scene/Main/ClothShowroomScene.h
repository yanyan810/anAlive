#pragma once
#ifdef _DEBUG
#include "IScene.h"
#include "Object3d.h"
#include "Physics/ClothComponent.h"
#include <array>

class ClothShowroomScene final : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app,float dt) override;
    void Draw3D(GameApp& app) override;
    void Draw(GameApp&) override {}
    void DrawImGui(GameApp& app) override;
    void SelectModel(int index,bool wait=false);
    ClothComponent& Cloth() { return cloth_[selected_]; }
    Object3d& Character() { return *characters_[selected_]; }
    bool IsCharacterLoaded(int index) const { return index>=0 && index<2 && characters_[index]!=nullptr; }
    int motion=1; // Procedural Idle/Walk/Run. Source PMX/GLBs contain no clips.
    bool moving=true, paused=false;
    float gaitAmplitude=1;
    float time=0, yaw=0, cameraYaw=0, cameraPitch=.12f, cameraDistance=4.6f;
    float cameraTargetHeight=1;
    bool cameraFollowsCharacter=false;
    double lastUpdateMilliseconds=0,lastClothMilliseconds=0;
private:
    void EnsureCharacter_(int index);
    GameApp* app_=nullptr;
    int pendingSelected_=-1;
    Camera camera_;
    std::array<std::unique_ptr<Object3d>,2> characters_;
    std::array<ClothComponent,2> cloth_;
    int selected_=0;
    float lastMotionTime_=-1, motionPhase_=0;
    float motionFrequency_=1.3f, motionStride_=0, motionKnee_=0, motionLean_=0, motionActivity_=0;
};
#endif
