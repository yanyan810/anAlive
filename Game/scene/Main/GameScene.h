#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Object3d.h"
#include "Player.h"
#include "Enemy.h"
#include "EnemyPool.h"
#include "EnemySpawnSystem.h"
#include "EnemyProjectile.h"
#include "StageLoader.h"
#include "StageProjectile.h"
#include "Sprite.h"
#include "StageProgress.h"
#include "StageClearOverlay.h"
#include "GameHUD.h"
#include "ThrowableSystem.h"
#include "BulletManager.h"
#include "AudioSystem.h"
#include "StartPlayerVisual.h"
#include "GameOverSequence.h"
#include "GameOverOverlay.h"
#include "GameOverVisionOverlay.h"
#include <array>
#include <chrono>
#include <map>
#include <vector>
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
#include "DebugTimeline.h"
#include "DebugJsonEditor.h"
#ifdef USE_IMGUI
#include "EnemyEditor.h"
#include "WeaponEditorUI.h"
#endif
#endif

class TemporarySceneTransitionCheck;
class GameScene : public IScene {
public:
    explicit GameScene(bool showroom=false,std::string stageId="stage01")
        : showroom_(showroom), stageId_(std::move(stageId)) {}
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    bool IsReadyForTransition() const override { return stageLoaded_; }
    std::string InitializationError() const override {
        return stageLoaded_ ? std::string{} : "GameScene stage initialization failed";
    }
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp&) override {}
    void DrawImGui(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    bool ShowTransitionDebugUI() const override { return !showroom_; }
    // Future event -> SpawnGroup wiring belongs here; it never gates shutter motion.
    void SetShutterEventHandler(std::function<void(const ShutterEvent&)> handler) { shutterEventHandler_=std::move(handler); }
    // Event-facing API, independent of any specific switch/shutter implementation.
    bool ActivateEnemySpawn(const std::string& id) { return spawnSystem_.ActivateSpawn(id); }
    void ConfigurePlacementTest(const EnemyPlacements& placements) { placementTest_=placements; }
    void ConfigureStagePlacementTest(const nlohmann::json& stage) { placementTestStage_=stage; }
    void SetStartSequenceEnabled(bool enabled) { startSequenceEnabled_=enabled; }
    bool PlacementTestFinished() const { return player_.IsDead() || !stage_.IsPlaying(); }
private:
    friend void RunTitleGameOverDemoTests(GameApp& app);
    bool IsGameOverDemo() const { return !showroom_ && stageId_=="gameover_demo"; }
    friend void RunGameOverSequenceRuntimeTests(GameApp& app);
    void BeginGameOver(GameApp& app);
    void UpdateGameOver(GameApp& app,float dt);
    bool LeaveGameOver(GameApp& app,bool retry);
    GameOverSequence gameOver_;
    GameOverOverlay gameOverOverlay_;
    GameOverVisionOverlay gameOverVision_;
    float gameOverMenuTime_=0;
    friend void RunCameraSequenceRuntimeTests(GameApp& app);
    void BeginStartSequence(GameApp& app,bool force=false);
    void FinishStartSequence(GameApp& app);
    void RefreshSequenceVisuals();
    void ApplySequenceCamera();
    CameraSequence startSequence_;
    std::unique_ptr<StartPlayerVisual> startPlayerVisual_;
    CameraPose sequenceEye_;
    bool startSequenceEnabled_=true, sequenceOwnsCamera_=false, sequenceWaiting_=false, sequenceInputGuard_=false;
    int sequenceDebugCommand_=0; // queued by ImGui, applied before the next simulation frame
    enum class EscapeMode { Idle, Preview, Clear, Results };
    bool EscapePlaying() const { return escapeMode_==EscapeMode::Preview || escapeMode_==EscapeMode::Clear; }
    void InitializeHotelEscape(GameApp& app);
    bool BeginHotelEscape(GameApp& app, bool preview);
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    bool HandleHotelEscapeKey(GameApp& app);
#endif
    void UpdateHotelEscape(GameApp& app, float dt);
    void PositionEscapeCar(double elapsed);
    void RefreshEscapeCarVisuals();
    void DrawEscapeFade(const Matrix4x4& view, const Matrix4x4& projection);
    EscapeMode escapeMode_=EscapeMode::Idle;
    CameraSequence escapeIntroCamera_;
    std::optional<CameraSequence> escapeLaunchCamera_;

#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)

    struct EscapeDiagnosticSample {
        double time = 0;
        double frameMs = 0;
        double simulationMs = 0;
        float carSpeed = 0;
        float cameraSpeed = 0;
        int impact = 0;
        bool cameraCut = false;
    };

    std::vector<EscapeDiagnosticSample> escapeDiagnosticSamples_;
    std::chrono::steady_clock::time_point escapeDiagnosticLastTick_{};
    bool escapeDiagnosticHasTick_ = false;

    bool debugFixedEscapeCamera_ = false;
#endif

    CameraPose escapeReturnCamera_;
    std::unique_ptr<Object3d> escapeCar_;
    struct EscapeWheel { std::unique_ptr<Object3d> visual; Vector3 center; float radius; };
    std::vector<EscapeWheel> escapeCarWheels_;
    std::optional<AABB> escapeUnsplitCarBounds_;
    std::vector<std::unique_ptr<Object3d>> escapeRoad_;
    std::vector<uint32_t> escapeHiddenMeshes_;
    struct EscapeEnemy { Enemy* enemy; size_t placement; double impactTime=-1; };
    std::vector<EscapeEnemy> escapeEnemies_;
    Sprite escapeFade_;
    double escapeTime_=0;
    bool escapeWasCaptured_=false;
    friend void RunEnemyPlacementEditorTests(GameApp&);
    std::optional<EnemyPlacements> placementTest_;
    std::optional<nlohmann::json> placementTestStage_;
    friend class TemporarySceneTransitionCheck;
    friend void RunTemporarySceneTransitionCheckTests(GameApp&, TemporarySceneTransitionCheck&);
    friend void RunScreenTransitionTests(GameApp& app);
    friend void RunHotelStageRuntimeTests(GameApp& app);
    friend void RunUpperHotelRuntimeTests(GameApp& app);
    friend void RunHotelCombatPlacementTests(GameApp& app);
    friend void RunHotelEnemyStairTests(GameApp& app);
    friend void RunEnemyAIStageSmokeTests(GameApp& app,const std::string& stage);
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    friend void RunEnemyPoolTests(GameApp& app);
    friend void RunThrowableRuntimeTests(GameApp& app);
    friend void RunTitleSceneTests(GameApp& app);
#endif
    void UpdateCombat(GameApp& app, float dt, bool wasCaptured);

    struct MuzzleAnchor {
        Vector3 position{};
        bool obstructed = false;
    };

    void LoadMuzzleLightSettings();
    void ApplyMuzzleLight(float dt);
    MuzzleAnchor ResolveMuzzleAnchor() const;

    void EmitMuzzleSmoke(const Vector3& position, const Vector3& direction, float smokeScale, float verticalSmokeScale);
    void AdvanceMuzzleSmoke(float dt);
    void ClearMuzzleSmoke();
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    void DrawMuzzleEffectSettings(const WeaponDefinition& weapon);
#endif
    void UpdatePlayerBullets(float dt);
    void UpdateEnemySpawns(float dt, bool processTriggers = true);
    void ShareEnemyAlerts();
    bool HandleInteraction(bool pressed,bool allowThrowables);
    void OnBulletImpact(const BulletEnemyImpact& impact);
    void OnStageClear(GameApp& app);
    void UpdateClearMenu(GameApp& app);
    bool ReturnToTitle(GameApp& app);

    void SyncProjectileVisuals(GameApp& app);
    void SyncWeaponVisuals(GameApp& app);
    void SyncThrowableVisuals(GameApp& app);
    const char* SceneName() const {
        if (showroom_) return "Showroom";
        if (stageId_=="stage03") return "Stage03";
        return stageId_=="stage04" ? "Stage04" : "Game";
    }
    std::string LevelPath() const { return showroom_ ? "resources/levels/showroom/showroom.json" :
        stageLoaded_ ? StagePath() : "resources/levels/fps_spawns.json"; }
    std::string StagePath() const { return "resources/levels/"+stageId_+"/"+stageId_+".json"; }
    void ResetShowroomEnemies(GameApp& app);
    bool LoadShowroomTestConfig(GameApp& app,const std::filesystem::path& config);
    bool ApplyShowroomTestAsset(GameApp& app,const std::filesystem::path& path);
    void ReplaceShowroomTest(GameApp& app,const std::shared_ptr<const EnemyAsset>& asset,const std::string& behavior);
    std::filesystem::path showroomTestAsset_;
    std::string showroomTestBehavior_,showroomTestAssetError_;
    std::shared_ptr<const EnemyAsset> showroomSavedAsset_;
    bool showroomDraftApplied_=false;
    void RecycleEnemies();
    void DrawEnemyPoolStats();
    void DrawShowroomTools(GameApp& app);
    bool showroom_=false, freezeEnemies_=true;
    std::string stageId_;
    bool showEnemyLabels_=true, showWeaponLabels_=true, showEnemyMarkers_=true;
    bool showEnemyCollision_=false, showEnemyParts_=false, resetEnemiesPending_=false;
#if defined(_DEBUG) || defined(DEVELOPMENT_BUILD)
    struct DebugFrame {
        Player::DebugState player;
        std::vector<Enemy::DebugState> enemies;
        EnemyProjectileSystem projectiles;
        BulletSimulation bullets;
        EnemySpawnSystem spawns;
        WeaponSystem weapons;
        ThrowableSystem::State throwables;
        StageProgress stage;
        StageShutterSystem shutters;
        StageDoorSystem doors;
        std::vector<bool> meshesIntact;
        bool transferUsed=false;
        std::mt19937 pelletRandom;
        uint64_t nextEnemy=0,frame=0;
        unsigned long long shots=0,hits=0,attacks=0;
        int selectedEnemy=0,lastHitEnemy=-1;
        EnemyPartType lastHitPart=EnemyPartType::None;
        std::string lastHitPartName="None";
        float ads=0,fov=0,lastDamage=0,lastEnemyDamage=0,playerFlash=0;
        bool freezeEnemies=true;
    };
    DebugFrame CaptureDebug() const;
    void RestoreDebug(GameApp& app,const DebugFrame& state);
    void DrawDebugTools(GameApp& app);
    DebugTimeline<DebugFrame> debugHistory_;
    DebugJsonEditor debugJson_;
#ifdef USE_IMGUI
    WeaponEditorUI weaponEditor_;
    std::unique_ptr<Object3d> weaponPreview_;
    std::optional<WeaponRuntime> beforeWeaponTest_;
    float weaponPreviewAspect_=1.0f;
    bool showTestAssetPanel_=false;
    EnemyEditor enemyEditor_;
    bool showEnemyEditor_=false,enemyEditorTest_=false,showroomConfigPending_=false;
    std::optional<std::filesystem::path> pendingShowroomTestAsset_;
    std::vector<std::filesystem::path> showroomTestAssets_;
    void DrawShowroomTestAssetControls();
    void OpenShowroomEnemyEditor();
    void DrawWeaponWorkspace(GameApp& app);
    void UpdateWeaponPreview();
#endif
    void EquipWeaponForDebug(const WeaponDefinition& definition);
    bool debugPaused_=false;
    int debugStep_=0,debugJsonSelection_=0;
    uint64_t debugFrame_=0;
    inline static bool debugPauseOnEnter_=false;
#endif
    StageLoader level_;
    bool HasFatalStageFall() const {
        return player_.HasFatalFall() ||
            (level_.fallDeathY && player_.GetTransform().translate.y<*level_.fallDeathY);
    }
    void UpdateStageTransfer();
    bool transferUsed_=false;
    void BindRemovableMeshes();
    void SyncRemovableMeshVisuals();
    std::vector<std::vector<uint32_t>> removableMeshBindings_;
    void InitializeDoors(GameApp& app);
    void UpdateDoors(float dt);
    void SyncDoorVisuals();
    std::vector<std::unique_ptr<Object3d>> doorVisuals_;
    void InitializeShutters(GameApp& app);
    void SyncShutterVisuals();
    void ProcessShutterEvents();
    void UpdateShutterAudio(bool running);
    struct ShutterVisual {
        std::unique_ptr<Object3d> model, lamp;
        std::array<int,4> sounds{}; // switch, start, motor (loop), complete
        bool motorPlaying=false;
    };
    std::vector<ShutterVisual> shutterVisuals_;
    std::unique_ptr<AudioSystem> shutterAudio_;
    Sprite shutterPrompt_;
    std::function<void(const ShutterEvent&)> shutterEventHandler_;
    bool stageLoaded_=false, showStageColliders_=false;
    void UpdateADS(const Input& input, float dt);

    StageProgress stage_;
    StageClearOverlay clearOverlay_;
    bool showGoalDebug_ = true;
    EnemyPartType lastHitPart_ = EnemyPartType::None;
    std::string lastHitPartName_="None";
    unsigned long long shotCount_ = 0;
    unsigned long long hitCount_ = 0;
    float lastDamage_ = 0.0f;
    WeaponSystem weapons_;
    ThrowableSystem throwables_;
    ModelCommon throwableModelCommon_;
    std::unique_ptr<Model> throwableModel_;
    std::vector<std::unique_ptr<Object3d>> throwableVisuals_;
    std::vector<ThrowableSystem::Visual> throwableDrawList_;
    GameHUD gameHUD_;
    std::vector<std::unique_ptr<Object3d>> weaponVisuals_;
    std::mt19937 pelletRandom_{std::random_device{}()};
    inline static std::optional<WeaponRandomSettings> weaponSeedOverride_;
    bool initialCapturePending_ = true;
    int savedMouseFlags_ = 0;
    Camera camera_;
    Player player_;
    EnemyPool enemyPool_;
    std::vector<Enemy*> enemies_; // Active, non-owning; stable until removed from this list.
    EnemyDefinitions enemyDefinitions_;
    EnemyProjectileSystem enemyProjectiles_;
    BulletManager bullets_;
    std::vector<std::unique_ptr<Object3d>> projectileVisuals_;
    EnemySpawnSystem spawnSystem_;
    uint64_t nextEnemyId_ = 0;
    bool showSpawnDebug_ = true;
    int selectedEnemy_ = 0;
    int lastHitEnemy_ = -1;
    unsigned long long enemyAttackCount_ = 0;
    float playerDamagedFlash_ = 0;
    float lastEnemyDamage_ = 0;
    Object3d ground_;
    Sprite crosshairHorizontal_;
    Sprite crosshairVertical_;

    float adsBlend_ = 0.0f;

    // SceneをクリックしてFPS操作へ戻した時、
    // そのクリック長押しでFullAutoが始まるのを防止
    bool suppressFireUntilRelease_ = false;
    float particleDeltaTime_ = 0.0f;
    inline static const std::string kMuzzleFlashFile = "muzzle_flash.json";
    inline static const std::string kMuzzleVerticalSmokeFile = "muzzle_vertical_smoke.json";

    struct MuzzleLightSettings {
        Vector3 color{ 1.0f, 0.94f, 0.78f };
        float intensity = 2.0f;
        float duration = 0.04f;
        float range = 4.0f;
        float decay = 1.5f;
    };
    struct MuzzleWeaponScales {
        float light = 1.0f;
        float smoke = 1.0f;
        float verticalSmoke = 0.0f;
    };
    struct PendingMuzzleSmoke {
        Vector3 position;
        Vector3 direction;
        float countScale;
        float remaining;
    };
    struct MuzzleLightPulse {
        MuzzleLightSettings settings;
        float age = 0.0f;
        bool firstFrame = true;
    };
    MuzzleLightSettings muzzleLightSettings_;
    std::map<std::string, MuzzleWeaponScales> muzzleWeaponScales_;
    std::optional<MuzzleLightPulse> muzzleLightPulse_;
    std::vector<PendingMuzzleSmoke> pendingVerticalSmoke_;
#if (defined(_DEBUG) || defined(DEVELOPMENT_BUILD)) && defined(USE_IMGUI)
    DebugJsonEditor muzzleLightEditor_;
#endif

};

