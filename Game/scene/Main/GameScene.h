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
#include "BulletManager.h"
#ifdef _DEBUG
#include "DebugTimeline.h"
#include "DebugJsonEditor.h"
#ifdef USE_IMGUI
#include "WeaponEditorUI.h"
#endif
#endif

class GameScene : public IScene {
public:
    explicit GameScene(bool showroom=false) : showroom_(showroom) {}
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw(GameApp&) override {}
    void DrawImGui(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
private:
#ifdef _DEBUG
    friend void RunEnemyPoolTests(GameApp& app);
    friend void RunTitleSceneTests(GameApp& app);
#endif
    void UpdateCombat(GameApp& app, float dt, bool wasCaptured);
    void OnBulletImpact(const BulletEnemyImpact& impact);
    void OnStageClear(GameApp& app);

    void SyncProjectileVisuals(GameApp& app);
    const char* SceneName() const { return showroom_ ? "Showroom" : "Game"; }
    std::string LevelPath() const { return showroom_ ? "resources/levels/showroom/showroom.json" :
        stageLoaded_ ? "resources/levels/stage01/stage01.json" : "resources/levels/fps_spawns.json"; }
    void ResetShowroomEnemies(GameApp& app);
    void RecycleEnemies();
    void DrawEnemyPoolStats();
    void DrawShowroomTools(GameApp& app);
    bool showroom_=false, freezeEnemies_=true;
    bool showEnemyLabels_=true, showWeaponLabels_=true, showEnemyMarkers_=true;
    bool showEnemyCollision_=false, showEnemyParts_=false, resetEnemiesPending_=false;
#ifdef _DEBUG
    struct DebugFrame {
        Player::DebugState player;
        std::vector<Enemy::DebugState> enemies;
        EnemyProjectileSystem projectiles;
        BulletSimulation bullets;
        EnemySpawnSystem spawns;
        WeaponSystem weapons;
        StageProgress stage;
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

};

