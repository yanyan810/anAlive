#pragma once
#include "IScene.h"
#include "EnemyPlacementMetadata.h"
#include "EnemySpawnSystem.h"
#include "StageLoader.h"
#include "Player.h"
#include "Enemy.h"
#include "scene/Main/GameScene.h"
#include <set>

class EnemyPlacementEditorScene final : public IScene {
public:
    void OnEnter(GameApp&) override;
    void OnExit(GameApp&) override;
    void Update(GameApp&,float) override;
    void DrawRender(GameApp&) override;
    void DrawOverlay2D(GameApp& app) override { if (test_) test_->DrawOverlay2D(app); }
    void Draw(GameApp&) override {}
    void DrawImGui(GameApp&) override;
    bool ShowTransitionDebugUI() const override { return false; }
private:
    friend void RunEnemyPlacementEditorTests(GameApp&);
    enum class CameraMode { Free, FPS, Top };
    void LoadStage(GameApp&,const std::string&);
    void SyncPreviews(GameApp&);
    void ApplyVisibility();
    void Generate(const EnemySpawnPoint&,EnemyPlacementGroup&,bool preserveIds,bool disableFailures=true);
    void StartTest(GameApp&);
    void StopTest(GameApp&);
    void SwitchCamera(CameraMode);
    void FocusSelection();
    void DrawViewportTools(GameApp&);
    void DrawControls(GameApp&);
    void RequestAction(const std::string&);
    bool Dirty() const { return placements_.Json()!=baseline_; }
    const EnemySpawnPoint* SelectedSpawn() const { return spawns_.FindPoint(selectedSpawn_); }
    EnemyPlacement* SelectedEnemy();
    Vector3 SelectedPosition() const;
    bool VisibleFloor(int floor) const;
    std::string StagePath() const { return "resources/levels/"+stageId_+"/"+stageId_+".json"; }
    std::string PlacementPath() const { return EnemyPlacements::PathFor(StagePath()); }
    Camera camera_;
    Player player_;
    StageLoader level_;
    Object3d environment_;
    std::vector<std::unique_ptr<Object3d>> shutters_;
    EnemyDefinitions definitions_;
    EnemySpawnSystem spawns_;
    EnemyPlacements placements_;
    EnemyPlacementMetadata metadata_;
    struct Preview { std::string definition; std::unique_ptr<Enemy> enemy; };
    std::map<std::string,Preview> previews_;
    std::unique_ptr<GameScene> test_;
    nlohmann::json baseline_;
    std::string stageId_="stage03",selectedSpawn_,selectedId_,status_,originalBytes_;
    std::string pendingAction_,confirmAction_;
    std::vector<std::string> warnings_;
    std::set<std::string> multiSelection_;
    char search_[128]{};
    CameraMode mode_=CameraMode::Free, beforeTestMode_=CameraMode::Free;
    Vector3 savedCameraPosition_{},savedCameraRotation_{};
    float speed_=8,topHeight_=28,zoom_=26,grid_=0.5f,angleSnap_=15;
    bool gridSnap_=false,rotationSnap_=false;
    int floor_=3,savedMouseFlags_=0,gizmo_=0,dragAxis_=-1;
    Vector3 dragWorld_{};
    float dragYaw_=0;
    bool selectingBox_=false;
    Vector2 boxStart_{};
    bool ready_=false,loadError_=false,showRoof_=false,showCeiling_=false,showUpper_=false,showLower_=false;
    bool showWalls_=true,showEnemies_=true,showSpawns_=true,showColliders_=false,showGrid_=false;
    bool saveModal_=false,discardModal_=false;
};
