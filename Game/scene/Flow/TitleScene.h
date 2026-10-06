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
    void PrepareBackgroundExplosions(GameApp& app);
    void BeginStartExplosion(bool preview = false);
    void BeginUnaliveExplosion(bool preview = false);
    void RestoreUnalive();
    void BeginInstructionExplosion(bool preview = false);
    void RestoreInstruction();
    std::optional<BulletHit> TraceUnalive(const Vector3& origin,const Vector3& direction,float distance) const;
    std::optional<BulletHit> TraceInstruction(const Vector3& origin,const Vector3& direction,float distance) const;
    std::optional<BulletHit> TraceTitleText(const Vector3& origin,const Vector3& direction,float distance) const;
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
    struct TextExplosion {
        EnemyPart source;
        std::vector<DetachedEnemyPart> letters, fragments;
        std::array<bool,kMaxStartFragments> castsShadow{};
        size_t fragmentCount = 0;
        float elapsed = 0;
        Vector4 color{1,1,1,1};
        EnemyParts hitParts;
        std::vector<uint32_t> meshes;
        bool destroyed = false, active = false, preview = false;
    } startExplosion_, unaliveExplosion_, instructionExplosion_;
    void PrepareBackgroundText(GameApp& app,TextExplosion& burst,const std::string& nodePrefix,const std::string& label);
    std::optional<BulletHit> TraceBackgroundText(const TextExplosion& burst,size_t index,
        const Vector3& origin,const Vector3& direction,float distance) const;
    void BeginBackgroundTextExplosion(TextExplosion& burst,const Vector3& tint,bool preview);
    void RestoreBackgroundText(TextExplosion& burst);
    void RefreshTitleEnvironment();
    void PrepareTextExplosion(GameApp& app,TextExplosion& burst,const std::string& key,const std::string& texture);
    void LaunchTextExplosion(TextExplosion& burst,const Vector3& tint);
    void UpdateTextExplosion(TextExplosion& burst,float dt);
    void UpdateTextExplosionVisuals(TextExplosion& burst);
    void DrawTextExplosion(TextExplosion& burst, bool shadow);
    Model* environmentFull_ = nullptr;
    std::array<Model*,4> environmentTextVariants_{};
    bool explosionPreview_ = false;
    void LoadLighting();
    void ResetLightFlicker();
    void UpdateLightFlicker(float dt);
    void ApplyLighting();
    TitleLighting lighting_;
    struct LampFlickerState {
        float remaining=0;
        int flashesRemaining=0;
        bool off=false;
    };
    std::array<LampFlickerState,3> lightFlicker_{};
    // Independent lamp sequences; do not change weapon spread / explosion randomness.
    std::array<std::mt19937,3> flickerRandom_{{std::mt19937{std::random_device{}()},
        std::mt19937{std::random_device{}()},std::mt19937{std::random_device{}()}}};
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
