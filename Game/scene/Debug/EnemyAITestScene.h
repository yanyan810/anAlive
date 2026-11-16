#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Player.h"
#include "EnemyPool.h"
#include "EnemyProjectile.h"
#include "Sprite.h"
#if defined(_DEBUG) && defined(_M_X64) && defined(USE_IMGUI)
#include "../../LiveDevelopment/LiveEnemyDiagnostics.h"
#include "../../LiveDevelopment/LiveLua.h"
#include "../../LiveDevelopment/LiveAiClient.h"
#include <map>
#endif

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
    friend void RunEnemyAITestSceneTests(GameApp& app, bool checkLiveDiagnostics, bool checkLivePhase2, bool checkLiveRestart);
#if defined(_DEBUG) && defined(_M_X64) && defined(USE_IMGUI)
    friend void RunEnemyAiChatTests(GameApp& app);
    friend void RunEnemyAiResearchTests(GameApp& app);
#endif
    void AddBox(GameApp& app,const char* id,const Vector3& center,const Vector3& half,const Vector4& color,bool collider=true);
    void Spawn(GameApp& app,const Vector3& position);
    void Remove(GameApp& app,bool all);
    void DrawDiagnostics(GameApp& app);
    void SyncProjectiles(GameApp& app);
#if defined(_DEBUG) && defined(_M_X64) && defined(USE_IMGUI)
    EnemyAIDiagnosticsInput MakeDiagnosticsInput() const;
    struct Checkpoint {
        static constexpr int kSchema = 1;
        int schema = kSchema;
        std::string scene = "EnemyAITest";
        uint64_t id = 0, nextSpawnId = 0;
        Player::DebugState player;
        Vector3 cameraPosition{}, cameraRotation{}, spawnPosition{};
        float fov = 0;
        std::vector<Enemy::LiveCheckpoint> enemies;
        EnemyProjectileSystem projectiles;
        int area = 0, selected = 0;
        std::string definition;
        bool paused = false, invulnerable = false;
    };
    enum class CheckpointAction { Capture, Restore };
    enum class JsonTarget { EnemySenses, Weapons };
    struct JsonRequest {
        JsonTarget target;
        uint64_t enemyId;
        std::string draft;
        bool apply;
    };
    struct LuaRequest {
        std::string label, source;
        bool readFile = false;
        std::string generation;
    };
    struct ScopedLua {
        std::unique_ptr<LiveLua> policy, fallback;
        bool failed = false;
    };
    struct PreparedChanges {
        std::unique_ptr<LiveLua> lua;
        std::map<uint64_t, ScopedLua> overrides;
        nlohmann::json script;
        bool replaceLua = false;
        Enemy* enemy = nullptr;
        EnemySenses senses;
        std::optional<WeaponRuntime> weapon;
        nlohmann::json weapons;
    };
    enum class AiMode { Standard, Review, Automatic };
    enum class AiVerification { Human, Joint, Ai };
    static constexpr unsigned kAiMaxAttempts = 3;
    struct AiReview {
        std::string operationId, generation, enemyId, before, prompt, model;
        uint64_t spawnId = 0, revision = 0;
        AiMode mode = AiMode::Standard;
        AiVerification verification = AiVerification::Human;
        std::string source, reason;
        nlohmann::json context;
        std::vector<LiveAiClient::Repair> failures;
        unsigned attempt = 1;
        bool validationPending = false;
        bool validated = false, approvalRequired = false, approved = false;
        std::chrono::steady_clock::time_point applyAfter{};
    };
    static std::string NewLiveId();
    void DrawAiChat(GameApp& app);
    void StartAiChat(GameApp& app, std::string prompt);
    void UpdateAiChat(GameApp& app);
    void SetResearchMode(bool enabled);
    void StartResearch(GameApp& app, std::string question);
    void UpdateResearch(GameApp& app);
    void DrawResearch(GameApp& app);
    void ValidateAiCandidate(AiReview& review);
    void StopAiChat(std::string reason, bool keepResponse = false);
    void SetAiMode(AiMode mode);
    void SetAiVerification(AiVerification mode);
    bool AiContextMatches(GameApp& app, const AiReview& review) const;
    nlohmann::json AiChanges(const AiReview& review) const;
    void RecordAi(const char* kind, bool success, const std::string& message, std::string_view errorType = {});
    static const char* AiModeName(AiMode mode);
    static const char* AiVerificationName(AiVerification mode);
    void DrawLiveDiagnosticsPanel(GameApp& app);
    void DrawLiveJsonPanel();
    void DrawLiveLuaPanel();
    bool UpdateLiveDevelopment(GameApp& app);
    Checkpoint CaptureCheckpoint() const;
    void ValidateCheckpoint(const Checkpoint& state) const;
    void RestoreCheckpoint(Checkpoint&& state) noexcept;
    nlohmann::json DescribeCheckpoint(const Checkpoint& state, const char* result) const;
    void OpenJsonDraft(JsonTarget target);
    nlohmann::json MakeJsonChange(const JsonRequest& request) const;
    void PrepareChanges(nlohmann::json& changes, bool replay, PreparedChanges& prepared);
    void CommitChanges(PreparedChanges&& prepared) noexcept;
    void EvaluateLuaPolicies();
    float LuaSpeed(const Enemy& enemy) const;
    void LoadLiveWeapons();
    LiveEnemyDiagnostics liveDiagnostics_;
    std::optional<Checkpoint> checkpoint_;
    bool manualCheckpoint_ = false;
    std::optional<CheckpointAction> checkpointAction_;
    std::optional<JsonRequest> jsonRequest_;
    std::optional<LuaRequest> luaRequest_;
    std::optional<uint64_t> halfHealthEnemy_;
    std::unique_ptr<LiveLua> lua_;
    std::map<uint64_t, ScopedLua> luaOverrides_;
    nlohmann::json liveScript_;
    std::vector<float> luaSpeeds_;
    std::vector<uint64_t> luaSpeedIds_;
    bool luaFailed_ = false;
    bool luaDraftOpen_ = false;
    bool batchLatestDll_ = false, batchLua_ = true, batchJson_ = false;
    std::string luaStatus_, luaDraft_;
    uint64_t checkpointId_ = 0, jsonEnemyId_ = 0;
    JsonTarget jsonTarget_ = JsonTarget::EnemySenses;
    std::string checkpointStatus_, jsonStatus_, jsonDraft_;
    bool jsonDraftOpen_ = false;
    bool weaponTestShot_ = false, weaponTestReload_ = false;
    nlohmann::json liveWeaponDocument_;
    LiveAiClient aiClient_;
    LiveAiClient::Settings aiSettings_;
    AiMode aiMode_ = AiMode::Standard;
    AiVerification aiVerification_ = AiVerification::Human;
    std::optional<AiReview> aiReview_;
    std::string aiPrompt_, aiStatus_, aiLastReason_, aiLastSource_, aiLastBefore_, aiSceneId_;
    std::string aiLastValidationError_;
    unsigned aiLastAttempt_ = 0;
    std::optional<bool> aiConnected_;
    bool aiResearchMode_ = false;
    ProjectResearch::Limits researchLimits_;
    ProjectResearch::Progress researchProgress_;
    std::vector<ProjectResearch::Turn> researchHistory_;
    std::string researchQuestion_, researchStatus_;
#endif
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
