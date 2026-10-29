#pragma once
#include <vector>
#include <random>
#include <wrl.h>
#include <d3d12.h>
#include "Particle.h"

#include <unordered_map>
#include <list>
#include <string>
#include <vector>
#include "TextureManager.h"

#include "ParticleCommon.h"
#include "RenderManager.h"

class Model;

// GPU Particle用構造体
struct Particles {
    Vector3 translate;
    Vector3 scale;
    float lifeTime;
    Vector3 velocity;
    float currentTime;
    float rotation;
    float angularVelocity;
    float padding0[2];
    Vector4 color;
};

struct PerView {
    Matrix4x4 viewProjection;
    Matrix4x4 billboardMatrix;
};

// GPU Particle用 PerFrame
struct PerFrame {
    float time;
    float deltaTime;
};

// Emitter用の汎用構造体
struct EmitterData {
    Vector3 translate; // 位置 (12 bytes)
    float radius; // 射出半径(Sphere/Cone用) (4 bytes) -> 合計 16 bytes

    uint32_t count; // 射出数 (4 bytes)
    float frequency; // 射出間隔 (4 bytes)
    float frequencyTime; // 射出間隔調整用時間 (4 bytes)
    uint32_t emit; // 射出許可 (4 bytes) -> 合計 16 bytes

    Vector3 velocityBase; // 基本速度 (12 bytes)
    float velocityVariance; // 速度の分散 (4 bytes) -> 合計 16 bytes

    float lifeTimeMin; // 最小寿命 (4 bytes)
    float lifeTimeMax; // 最大寿命 (4 bytes)
    uint32_t shapeType; // 形状 (0:Sphere, 1:Cone, 2:Box) (4 bytes)
    float shapeAngle; // Coneの広がり角 (4 bytes) -> 合計 16 bytes

    Vector3 shapeSize; // BoxのXYZサイズ (12 bytes)
    float padding1; // 16バイトアライメント用パディング (4 bytes) -> 合計 16 bytes

    Vector3 acceleration; // 加速度(重力等) (12 bytes)
    float padding2; // 16バイトアライメント用パディング (4 bytes) -> 合計 16 bytes

    Vector4 startColor; // 発生時の色 (16 bytes) -> 合計 16 bytes
    Vector4 endColor; // 消滅時の色 (16 bytes) -> 合計 16 bytes
    Vector3 scaleStart;
    float speedMin;

    Vector3 scaleEnd;
    float speedMax;

    float angleRandomDeg;
    float jitterDeg;
    float rotationStartDeg;
    float rotationRandomDeg;

    float angularVelocityMinDeg;
    float angularVelocityMaxDeg;
    float timeScale;
    float initialAge;
};

// ===============================
// 定数
// ===============================
static constexpr uint32_t kMaxInstance = 1024;
// 板ポリ1枚 = 2三角形 = 6頂点
static constexpr uint32_t kVertexCount = 6;

// ===============================
// パーティクルグループ
// ===============================
struct ParticleGroup {
    uint32_t textureSrvIndex = 0;
    Model* model = nullptr;

    // CPU用のパーティクル管理（後日不要になる可能性あり）
    std::list<Particle::ParticleData> particles;

    // UAV用リソース（DEFAULTヒープ）
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;

    uint32_t instancingSrvIndex = 0;
    uint32_t instancingUavIndex = 0; // ★追加

    // FreeListIndex用リソース (元freeCounter)
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource;
    uint32_t freeListIndexUavIndex = 0;

    // FreeList用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource;
    uint32_t freeListUavIndex = 0;

    // Emitter用リソース（UPLOADヒープ、CBV）
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource;
    EmitterData* mappedEmitter = nullptr;

    ParticleCommon::BlendMode blendMode = ParticleCommon::BlendMode::kBlendModeNormal; 
    PostEffectMode postEffectMode = PostEffectMode::FullScreen;
    bool bloomPostEffect = false;
    bool outlineBloomPostEffect = false;
    Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    Vector4 outlineBloomColor{ 1.0f, 0.22f, 0.22f, 1.0f };
    bool depthTestEnabled = true;
    
    // 手動/自動エミット制御用フラグ
    bool isAutoEmit = true;
    bool isEmitRequested = false;
    float activeTimeRemaining = 0.0f;

    // ビルボードモード
    // 0: Billboard (カメラ追従)
    // 1: Velocity Aligned (進行方向を向く)
    // 2: None (回転なし/固定)
    uint32_t billboardMode = 0;

    // JSON保存復元用
    std::string texturePath = "";
    int modelType = 0; // 0: None, 1: Primitive, 2: File
    std::string modelName = ""; // Primitiveの場合はインデックス文字列("0"~"7")、Fileの場合はパス
    std::string fromFile = "";
};



class DirectXCommon;
class SrvManager;
class Camera;

class ParticleManager {
public:
    static ParticleManager* GetInstance();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, ParticleCommon* particleCommon); // ★追加
    void Finalize();
    bool HasGroup(const std::string& groupName) const;
    std::vector<std::string> GetGroupNames() const;
    bool HasPostEffectTargets() const;
    bool HasBloomPostEffectTargets() const;
    bool HasOutlineBloomPostEffectTargets() const;
    Vector4 GetPrimaryPostEffectBloomColor() const;
    Vector4 GetPrimaryPostEffectOutlineBloomColor() const;
    PostEffectMode GetPrimaryPostEffectMode() const;
    void ConfigureHitEffectPreset(const std::string& groupName);
    void ConfigureTrailPreset(const std::string& groupName);
    void SetEditorSelectedGroupName(const std::string& groupName);
    void SetGroupBlendMode(const std::string& groupName, ParticleCommon::BlendMode mode);            // ★追加
    void Update(float deltaTime, const Camera& camera);
    void UpdateCompute(ID3D12GraphicsCommandList* computeCmd);
    void Draw(ID3D12GraphicsCommandList* cmd);
    void Draw(ID3D12GraphicsCommandList* cmd, bool drawPostEffectTargets);
    void ClearGroups();
    void RemoveGroup(const std::string& groupName);
    void ClearAllParticles();
    void DrawImGui(); // ★追加
    void DrawImGuiContents();

    void Save(const std::string& filename);
    void Load(const std::string& filename);
    void LoadAdditional(const std::string& filename, const std::string& groupNamePrefix);
    void LoadAdditional(const std::string& filename, const std::string& groupNamePrefix, const std::vector<std::string>& skipGroupNames);
    std::vector<std::string> GetGroupNamesInFile(const std::string& filename);
    std::vector<std::string> GetGroupNamesLoadedFromFile(const std::string& filename) const;
    float GetGroupLifeTimeMax(const std::string& groupName) const;

    void CreateParticleGroup(
        const std::string& name,
        const std::string& texturePath);

    void CreateParticleGroup(
        const std::string& name,
        Model* model);

    void UpdateGroupModel(const std::string& modelName, int modelType, Model* newModel);

    void Emit(const std::string& groupName,
        const Vector3& pos,
        uint32_t count,
        float timeScale = 1.0f,
        float initialAge = 0.0f);
    void EmitConfigured(const std::string& groupName, const Vector3& pos, float timeScale = 1.0f, float initialAge = 0.0f);

private:
    void ClearGPUBuffers_(ParticleGroup& group);
    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    void LoadInternal_(const std::string& filename, bool clearExisting, const std::string& groupNamePrefix, bool forceAutoEmitOff, const std::vector<std::string>* skipGroupNames = nullptr);
    void RetireAllGroups_();
    void RetireGroup_(const std::string& groupName);
    void CollectRetiredGroups_();

private:
    // === 外部システム ===
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // === ランダム ===
    std::mt19937 randomEngine_;

    // === パーティクル頂点 ===
    struct ParticleVertex {
        float position[4]; // POSITION float4
        float uv[2];       // TEXCOORD float2
        float normal[3];   // NORMAL float3
    };
    std::vector<ParticleVertex> vertices_;

    std::unordered_map<std::string, ParticleGroup> particleGroups_;
    std::string editorSelectedGroupName_;

    struct RetiredParticleGroups {
        std::unordered_map<std::string, ParticleGroup> groups;
        int framesRemaining = 3;
    };
    std::vector<RetiredParticleGroups> retiredParticleGroups_;


    // === GPU リソース ===
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbView_{};

    uint32_t maxParticleCount_ = 1000;

    ParticleCommon* particleCommon_ = nullptr;

    // === GPU Particle用 PerView ===
    Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_;
    PerView* mappedPerView_ = nullptr;

    // === 時間管理用 PerFrame ===
    float time_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_;
    PerFrame* mappedPerFrame_ = nullptr;

    // === ダミーマテリアル＆ライト ===
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    struct MaterialForGPU {
        Vector4 color;
        int enableLighting;
        float padding[3];
        Matrix4x4 uvTransform;
    };
    MaterialForGPU* mappedMaterial_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> dirLightResource_;

    // === ImGui・ファイル検索用 ===
    void ScanResources();
    std::vector<std::string> modelFiles_;
    std::vector<std::string> textureFiles_;
    std::vector<std::string> particleJsonFiles_;
    bool isResourcesScanned_ = false;

    char saveFileName_[256] = "test_particles.json";
    bool OpenTextureFileDialog_(std::string& outTexturePath);
    bool OpenModelFileDialog_(std::string& outModelPath);
};
