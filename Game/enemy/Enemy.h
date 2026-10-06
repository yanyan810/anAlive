#pragma once
#include "Object3d.h"
#include "EnemyParts.h"
#include "EnemyProceduralAnimation.h"
#include "EnemyAI.h"
#include "EnemyDefinition.h"
#include "EnemyExplosion.h"
#include "DetachedEnemyPart.h"
#include <random>

struct EnemyRenderAsset {
    std::shared_ptr<const EnemyAsset> source;
    std::vector<Model*> models;
    std::vector<std::vector<Model*>> chunks;
};
struct EnemyPartVisual {
    EnemyPartType type = EnemyPartType::None;
    std::unique_ptr<Object3d> object;
    bool visible = true;
};

struct DetachedEnemyPart {
    std::unique_ptr<Object3d> object;
    DetachedPartMotion motion;
    uint64_t spawnOrder = 0;
};

enum class FragmentMode { Chunk, Face };
struct FaceShard {
    std::array<Vector3,3> vertices{}; // world-oriented offsets from triangle centroid
    DetachedPartMotion motion;
    uint64_t spawnOrder = 0;
};
using DetachedEnemyFragment = DetachedEnemyPart; // Same object ownership and physics.

// Uses only the Boss model resource; no legacy AI, attacks or animation playback.
class Enemy {
public:
    // Call on the main thread after ModelManager/TextureManager initialization.
    static void PreloadAssets();
    static void ReleasePreloadedAssets();
    ~Enemy();
    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera, bool useSplitAssets = false);
    void PrepareForPool(Object3dCommon* common,DirectXCommon* dx,Camera* camera,const EnemyDefinition& definition);
    // Definition-specific slots retain their prepared GPU resources for their lifetime.
    void ResetForSpawn(uint64_t spawnId,const std::string& trigger,const Vector3& position,const Vector3& rotation);
    void RetireFromPool();
    bool CanReturnToPool() const { return IsDead() && detachedParts_.empty() && faceShards_.empty() && explosionTime_<=0; }
    using RaycastHit = EnemyPartHit;
    bool Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hit) const;
    void ShowHitFeedback(EnemyPartType part);
    void ShowHitFeedback(size_t part);
    const std::string& PartName(size_t index) const { static const std::string none="None"; return index<parts_.size()?parts_[index].name:none; }
    float ApplyDamage(size_t part,float damage,const Vector3& shotDirection);
    EnemyBulletHitResult ApplyBulletDamage(size_t part,float damage,const Vector3& direction);
    float ApplyDamage(EnemyPartType part, float damage, const Vector3& shotDirection);
    EnemyBulletHitResult ApplyBulletDamage(EnemyPartType part, float damage, const Vector3& direction);
    void ApplyExplosionDamage(const EnemyExplosion& explosion);
    void Die(const Vector3& direction = {0,1,0});
    void DrawPartDebug(const Matrix4x4& viewProjection, const Vector2& screenMin, const Vector2& screenMax, bool forceParts=false, bool forceMovement=false, bool rangeOnly=false) const;
    void DrawImGui();
    const EnemyDefinition& Definition() const { return definition_; }
    void ApplyDefinition(const EnemyDefinition& definition);
#ifdef _DEBUG
    Vector3 HeadCenterForDebug() const {
        for (size_t i=0;i<parts_.size();++i) if (parts_[i].role==EnemyPartRole::Head && !parts_[i].Destroyed())
            return EnemyPartTransformPoint((parts_[i].bounds.min+parts_[i].bounds.max)*.5f,PartWorldMatrix(i));
        return position_;
    }
    struct DebugDetached { Model* model=nullptr; DetachedPartMotion motion; uint64_t order=0; };
    struct DebugState {
        EnemyDefinition definition;
        EnemyAI ai;
        EnemyProceduralAnimation animation;
        EnemyParts parts;
        Vector3 position{},rotation{},scale{};
        uint64_t spawnId=0,nextOrder=0;
        std::string trigger;
        unsigned long long attacks=0;
        float damage=0,flash=0;
        float explosionTime=0;
        bool exploded=false,splitVisuals=false,hasHitBox=false;
        float blastHitTime=0, lastBlastDamage=0;
        Vector3 explosionCenter{};
        std::mt19937 random;
        std::vector<Model*> models;
        std::shared_ptr<const EnemyRenderAsset> asset;
        std::vector<bool> visible;
        std::vector<Transform> visualTransforms; // Diagnostics; restored from animation + bind pose.
        std::vector<DebugDetached> detached;
        std::vector<FaceShard> faces;
        FragmentMode breakMode=FragmentMode::Face;
        DetachedPartSettings detachedSettings;
        int maxFaces=256,facesPerBreak=64;
        float faceLifetime=5,spread=1.5f,outward=1.5f;
    };
    DebugState CaptureDebug() const;
    void RestoreDebug(const DebugState& state);
#endif
    float Update(float dt, const Vector3& playerPosition);
    void UpdateVisuals(float dt); // Includes fragment lifetime/physics, but no AI or attacks.
    const Vector3& GetPosition() const { return position_; }
    void SetPosition(const Vector3& position) { position_ = position; }
    void SetRotation(const Vector3& rotation) { rotation_ = rotation; }
    void SetSpawnIdentity(uint64_t id, const std::string& trigger) {
        spawnId_ = id;
        id_ = "Enemy_" + std::string(id < 10 ? 2 : id < 100 ? 1 : 0, '0') + std::to_string(id);
        spawnTriggerId_ = trigger;
    }
    uint64_t GetSpawnId() const { return spawnId_; }
    const std::string& GetId() const { return id_; }
    const std::string& GetSpawnTriggerId() const { return spawnTriggerId_; }
    unsigned int PendingAttackCount() const { return ai_.attacksThisUpdate; }
    void ConfirmAttack(float actualDamage) { attackCount_ += ai_.attacksThisUpdate; lastAttackDamage_ = actualDamage; attackFlash_ = .35f; }
    bool IsDead() const { return exploded_ || EnemyPartsDead(parts_); }
    EnemyState GetState() const { return IsDead() ? EnemyState::Dead : ai_.state; }
    void Draw(bool showMarker=true);
    void SetSceneLight(const Object3dLight* light) { sceneLight_ = light; }
    void DrawExplosion();
    void SetPartVisible(EnemyPartType type, bool visible);
    void SetPartVisible(size_t index,bool visible) { if (index<visuals_.size()) visuals_[index].visible=visible; }
private:
    const Object3dLight* sceneLight_ = nullptr;
    void ApplySceneLight(Object3d& object);
    EnemyDefinition spawnDefinition_;
    EnemyParts spawnParts_;
    EnemyAI spawnAI_;
    std::vector<Model*> spawnModels_;
    void DetachPart(size_t index,const Vector3& direction,bool forceFaces=false);
#ifdef _DEBUG
    std::string dimensionFileStatus_;
#endif
    void PrepareExplosionVisual();
    void PrepareFaceBatch();
    static std::shared_ptr<const EnemyRenderAsset> PrepareAssetModels(std::shared_ptr<const EnemyAsset> asset);
    inline static std::map<std::string,std::shared_ptr<const EnemyRenderAsset>> assetModels_;
    std::shared_ptr<const EnemyRenderAsset> asset_;
    size_t FindLegacyPart(EnemyPartType type) const {
        if (type==EnemyPartType::None) return kNoEnemyPart;
        for (size_t i=0;i<parts_.size();++i) if (parts_[i].type==type) return i;
        return kNoEnemyPart;
    }
    Vector3 ExplosionCenter() const;
    const Matrix4x4& PartWorldMatrix(size_t index) const;
    void ConfigureLegacyPartCollision();
    void RebuildTypeMarker();
    uint64_t spawnId_ = 0;
    std::string id_;
    std::string spawnTriggerId_;
    EnemyDefinition definition_{};
    EnemyAI ai_{};
    EnemyProceduralAnimation animation_{};
    unsigned long long attackCount_ = 0;
    float lastAttackDamage_ = 0;
    float attackFlash_ = 0;
    Object3d object_;
    std::unique_ptr<Object3d> fallbackVisual_; // Whole-model drawing, independent of collision object_.
    ModelCommon explosionModelCommon_;
    std::unique_ptr<Model> explosionModel_;
    std::unique_ptr<Object3d> explosionVisual_;
    float explosionTime_ = 0;
    bool exploded_=false;
    float blastHitTime_ = 0, lastBlastDamage_ = 0;
    Vector3 explosionCenter_{};
    static constexpr float kExplosionDuration = .7f;
    std::unique_ptr<Object3d> typeMarker_; // Visual only: never included in EnemyParts or fragments.
    FragmentMode breakMode_ = FragmentMode::Face;
    // Immutable between startup preload and shutdown; shared by every enemy.
    inline static bool assetsPreloaded_ = false;
    inline static bool splitAssetsAvailable_ = false;
    inline static std::array<std::vector<std::array<Vector3,3>>,6> faceData_{};
    inline static std::array<std::shared_ptr<const EnemyPartGeometry>,6> legacyPartGeometry_{};
    std::vector<FaceShard> faceShards_;
    ModelCommon faceModelCommon_;
    std::unique_ptr<Model> faceModel_;
    std::unique_ptr<Object3d> faceBatch_;
    inline static int maxActiveFaces_ = 256;
    int maxFacesPerBreak_ = 64;
    float faceLifetime_ = 5.0f;
    static constexpr size_t kFaceCapacity = 1024;
    void TrimFacePool(size_t reserve);
    bool SpawnFaces(size_t part, const Vector3& direction,bool deathBurst=false);
    void DrawFaces();
    Object3dCommon* common_ = nullptr;
    DirectXCommon* dx_ = nullptr;
    Camera* camera_ = nullptr;
    inline static std::array<std::vector<std::string>, 6> fragmentFiles_{};
    float spreadPower_ = 1.5f;
    float outwardPower_ = 1.5f;
    static constexpr size_t kMaxFragments = 128;
    inline static std::vector<Enemy*> fragmentOwners_{};
    inline static uint64_t nextSpawnOrder_ = 0;
    static void MakeFragmentRoom();
    std::vector<DetachedEnemyPart> detachedParts_;
    DetachedPartSettings detachedSettings_{};
    std::mt19937 random_{std::random_device{}()};
    std::vector<EnemyPartVisual> visuals_;
    bool splitVisuals_ = false;
    EnemyParts parts_{};
    bool showPartColliders_ = false;
    bool showMovementCollider_ = false;
    bool showExplosionRange_ = true;
    Vector3 position_{3.0f,0.0f,18.0f};
    Vector3 rotation_{0.0f,1.5707963f,0.0f};
    Vector3 scale_{2.0f,2.0f,2.0f};
    bool hasHitBox_ = false;
};
