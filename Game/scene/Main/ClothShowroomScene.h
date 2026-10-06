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
    void SelectModel(int index);
    ClothComponent& Cloth() { return cloth_[selected_]; }
    Object3d& Character() { return *characters_[selected_]; }
    int motion=1; // Procedural Idle/Walk/Run. Source PMX/GLBs contain no clips.
    bool moving=true, paused=false;
    float time=0, yaw=0, cameraYaw=0, cameraPitch=.12f, cameraDistance=4.6f;
private:
    Camera camera_;
    std::array<std::unique_ptr<Object3d>,2> characters_;
    std::array<ClothComponent,2> cloth_;
    int selected_=0;
};
#endif
