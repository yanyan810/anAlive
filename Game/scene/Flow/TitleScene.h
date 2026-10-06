#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Player.h"
#include "EnemyPool.h"
#include "EnemySpawnSystem.h"
#include "StageLoader.h"
#include "BulletManager.h"
#include "TitleStartSequence.h"
#include "Sprite.h"
#include "TitleLighting.h"
#ifdef USE_IMGUI
#include "DebugJsonEditor.h"
#endif

class TitleScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void DrawOverlay2D(GameApp& app) override;
    void DrawImGui(GameApp& app) override;
    void Draw(GameApp&) override {}
private:
#ifdef _DEBUG
    friend void RunTitleSceneTests(GameApp& app);
#endif
    bool LoadLayout();
    void SpawnEnemy();
    void OnBulletImpact(const BulletEnemyImpact& impact);
    void UpdateWorld(GameApp& app, float dt, bool controls);
    void LoadLighting();
    TitleLighting lighting_;
    Object3dLight sceneLight_;
    std::string lightingStatus_;
    Vector4 savedClearColor_{};
#ifdef USE_IMGUI
    DebugJsonEditor lightingEditor_;
#endif
    Camera camera_;
    Player player_;
    StageLoader level_;
    WeaponSystem weapons_;
    EnemyDefinitions definitions_;
    EnemySpawnSystem spawns_;
    EnemyPool pool_;
    std::vector<Enemy*> enemies_;
    BulletManager bullets_;
    Object3d environment_;
    TitleStartSequence start_;
    EnemyDefinition startDefinition_;
    std::unique_ptr<Enemy> startTarget_;
    Sprite crosshairHorizontal_, crosshairVertical_;
    Matrix4x4 uiView_{}, uiProjection_{};
    std::mt19937 random_{std::random_device{}()};
    std::string weaponId_ = "pistol", error_;
    uint64_t nextEnemyId_ = 0;
    float respawnTime_ = 0;
    bool transitionRequested_ = false;
    bool ready_ = false, initialCapturePending_ = true, suppressFireUntilRelease_ = true;
    int savedMouseFlags_ = 0;
};
