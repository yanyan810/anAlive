#pragma once
#include "IScene.h"
#include "EnemyPlacementMetadata.h"
#include "EnemySpawnSystem.h"
#include "StageLoader.h"
#include "Player.h"
#include "Enemy.h"
#include "scene/Main/GameScene.h"
#include <set>
#include <optional>

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
    enum class CameraMode { Free, FPS, Top, Front, Side };
    enum class PathTool { Draw, Path, Camera };
    friend void RunCameraPathEditorTests(GameApp&,const std::function<void(const wchar_t*)>&);
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
    void LoadPlayerVisual(const std::string& path);
    bool SavePlayerVisual(const std::string& path);
    void SyncPlayerVisual();
    void FocusPlayerVisual();
    void LoadCameraSequences(const std::string& path);
    bool SaveCameraSequence(const std::string& path);
    bool RevertCameraSequence(const std::string& path);
    void SetCameraSequenceMode(bool enabled);
    bool HandleCameraAction(const std::string& action);
    void DrawCameraSequenceControls(GameApp&);
    void DrawCameraSequenceViewport(GameApp&);
    CameraSequenceDefinition* SelectedCameraSequence();
    CameraSequencePoint* SelectedCameraPoint();
    void EnsureCameraSelection();
    CameraPose CameraEyePose() const;
    CameraPose CameraPointPose(size_t index) const;
    bool BuildCameraEvaluator(CameraSequence& sequence,std::string& error) const;
    bool BuildCameraGeometry(CameraSequence& sequence) const;
    void AddCameraPoint(bool duplicate);
    void DeleteCameraPoint();
    void MoveCameraPoint(int direction);
    void ReindexCameraPoints(CameraSequenceDefinition& definition);
    void CommitCameraHistory();
    void UndoCameraSequence(bool redo);
    void FocusCameraPoint();
    bool StartCameraPreview(bool playing);
    void StopCameraPreview();
    void SeekCameraPreview(double time);
    void UpdateCameraPreview(double dt);
    void ApplyCameraPreviewPose(const CameraPose& pose);
    bool OrthographicView() const { return mode_==CameraMode::Top || mode_==CameraMode::Front || mode_==CameraMode::Side; }
    void SetPathTool(PathTool tool);
    void AddDrawPoint(const Vector3& position);
    bool FinishDrawPath();
    void CancelDrawPath();
    bool InsertCameraPoint();
    std::string NewCameraPointId() const;
    void SetCameraHandle(bool outgoing,const Vector3& offset);
    void SetCameraHandleType(const std::string& type);
    void SetCameraPathType(const std::string& type);
    void MoveCameraPathSelection(const Vector3& delta);
    void RequestAction(const std::string&);
    bool PlacementsDirty() const { return placements_.Json()!=baseline_; }
    bool PlayerVisualDirty() const { return playerVisualSettings_.yawOffsetDegrees!=playerVisualBaseline_.yawOffsetDegrees; }
    bool CameraSequencesDirty() const { return CameraSequence::ToJson(cameraDrafts_)!=cameraBaseline_; }
    bool Dirty() const { return PlacementsDirty() || PlayerVisualDirty() || CameraSequencesDirty() || !drawPathPoints_.empty(); }
    const EnemySpawnPoint* SelectedSpawn() const { return spawns_.FindPoint(selectedSpawn_); }
    EnemyPlacement* SelectedEnemy();
    Vector3 SelectedPosition() const;
    bool VisibleFloor(int floor) const;
    std::string StagePath() const { return "resources/levels/"+stageId_+"/"+stageId_+".json"; }
    std::string PlacementPath() const { return EnemyPlacements::PathFor(StagePath()); }
    Camera camera_;
    Player player_;
    std::unique_ptr<StartPlayerVisual> playerVisual_;
    PlayerVisualSettings playerVisualSettings_,playerVisualBaseline_;
    std::string originalStageBytes_,playerVisualStatus_;
    bool showPlayerVisual_=true,showPlayerDirections_=true,playerVisualLoadError_=false;
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
    bool cameraSequenceMode_=false,showCameraPath_=true,cameraOverrideLoadError_=false,cameraRevertModal_=false;
    std::vector<CameraSequenceDefinition> cameraDrafts_,blenderCameraDrafts_;
    CameraSequenceOverrides cameraOverrides_;
    nlohmann::json cameraBaseline_=nlohmann::json::array();
    std::string selectedCameraSequence_="start",selectedCameraPoint_,cameraSequenceStatus_;
    std::vector<std::vector<CameraSequenceDefinition>> cameraHistory_;
    size_t cameraHistoryIndex_=0;
    char newCameraSequenceId_[128]="start";
    int cameraGizmo_=0,cameraRotationAxis_=1,cameraDragAxis_=-1;
    Vector3 cameraDragValue_{};
    PathTool pathTool_=PathTool::Camera;
    std::vector<CameraSequencePoint> drawPathPoints_;
    float drawHeight_=1.6f,selectedSegmentParameter_=.5f;
    int selectedCameraSegment_=-1,selectedCameraHandle_=-1;
    mutable CameraSequence cameraEvaluatorCache_;
    mutable nlohmann::json cameraEvaluatorKey_;
    mutable std::string cameraEvaluatorError_;
    mutable bool cameraEvaluatorValid_=false;
    mutable CameraSequence cameraGeometryCache_;
    mutable nlohmann::json cameraGeometryKey_;
    mutable bool cameraGeometryValid_=false;
    CameraSequence cameraPreview_;
    std::optional<Camera> cameraEditingView_;
    CameraMode cameraEditingMode_=CameraMode::Free;
    bool cameraPreviewActive_=false,cameraPreviewPlaying_=false;
    double cameraPreviewTime_=0;
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
