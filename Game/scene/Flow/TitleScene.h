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
#include "DirectionalShadowMap.h"
#include <array>
#ifdef USE_IMGUI
#include "DebugJsonEditor.h"
#endif

class TitleScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void DrawShadow(GameApp& app) override;
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
    void PrepareStartExplosion(GameApp& app);
    void BeginStartExplosion(bool preview = false);
    void UpdateStartExplosion(float dt);
    void UpdateStartExplosionVisuals();
    void ApplyStartCameraShake();
    bool StartExplosionActive() const { return start_.Starting() || explosionPreview_; }
    struct StartExplosionSettings {
        int fragmentCount = 30;
        float explosionPower = 7.5f, randomPower = 2.0f, upwardPower = 3.2f;
        float minScale = .065f, maxScale = .16f;
        float angularVelocity = 9.0f, lifetime = 1.6f;
        float shakeStrength = .035f, shakeDuration = .14f, flashDuration = .075f;
    } explosionSettings_;
    static constexpr size_t kMaxStartFragments = 40;
    std::vector<DetachedEnemyPart> startLetters_, startFragments_;
    std::array<bool,kMaxStartFragments> fragmentCastsShadow_{};
    size_t activeStartFragments_ = 0;
    float explosionElapsed_ = 0;
    bool explosionPreview_ = false;
    Vector4 fragmentColor_{1,1,1,1};
    void LoadLighting();
    TitleLighting lighting_;
    Object3dLight sceneLight_;
    DirectionalShadowMap shadowMap_;
    std::vector<uint32_t> roomShadowMeshes_;
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
