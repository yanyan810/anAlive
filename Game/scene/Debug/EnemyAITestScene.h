#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Player.h"
#include "EnemyPool.h"
#include "EnemyProjectile.h"
#include "Sprite.h"

// Independent environment; Enemy/Player/StageWorld are shared with normal gameplay.
class EnemyAITestScene final : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app,float dt) override;
    void DrawRender(GameApp&) override;
    void Draw(GameApp&) override {}
    void DrawOverlay2D(GameApp&) override;
    void DrawImGui(GameApp& app) override;
    bool ShowTransitionDebugUI() const override { return false; }
    bool UsesSceneViewport() const override { return true; }
private:
    friend void RunEnemyAITestSceneTests(GameApp& app);
    void AddBox(GameApp& app,const char* id,const Vector3& center,const Vector3& half,const Vector4& color,bool collider=true);
    void Spawn(GameApp& app,const Vector3& position);
    void Remove(GameApp& app,bool all);
    void DrawDiagnostics(GameApp& app);
    void SyncProjectiles(GameApp& app);
    Camera camera_;
    Player player_;
    StageWorld world_;
    EnemyDefinitions definitions_;
    EnemyPool pool_;
    std::vector<Enemy*> enemies_;
    std::vector<std::unique_ptr<Object3d>> geometry_,projectileVisuals_;
    EnemyProjectileSystem projectiles_;
    Sprite help_;
    int selected_=0, area_=0, savedMouseFlags_=0;
    uint64_t nextId_=0;
    std::string definition_="normal", status_;
    Vector3 spawnPosition_{-24,0,2};
    bool pauseAll_=true, invulnerable_=true;
    bool showDistance_=true, showFov_=true, showRay_=true, showLast_=true;
    bool showGoal_=true, showPath_=true, showState_=true;
    bool showMemory_=true, showEscapeDirection_=true;
};
