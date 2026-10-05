#ifdef _DEBUG
#include "DebugEnemyTuning.h"
#endif
#include "Enemy.h"
#include "GeometryGenerator.h"
#include "Raycast.h"
#include <nlohmann/json.hpp>
#include <numbers>
namespace {
    constexpr const char* kBossModelPath = "enemy/boss/boss.gltf";
    constexpr std::array<const char*, 6> kPartModelPaths{{
        "enemy/boss/parts/boss_head.gltf", "enemy/boss/parts/boss_body.gltf",
        "enemy/boss/parts/boss_left_arm.gltf", "enemy/boss/parts/boss_right_arm.gltf",
        "enemy/boss/parts/boss_left_leg.gltf", "enemy/boss/parts/boss_right_leg.gltf"}};
}

void Enemy::PreloadAssets() {
    // 全個体で共通のデータを一度だけ読み込み、敵の出現ごとのファイル読み込みを避ける。
    if (assetsPreloaded_) return;
    auto* models = ModelManager::GetInstance();
    models->LoadModel(kBossModelPath);
    models->LoadModel("cube/cube.obj"); // Enemy type markers.
    TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");
    splitAssetsAvailable_ = true;
    for (const auto* file : kPartModelPaths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(std::filesystem::path("resources") / file, error))
            splitAssetsAvailable_ = false;
    }
    if (splitAssetsAvailable_)
        for (const auto* file : kPartModelPaths) models->LoadModel(file);
    // Missing manifest, glTF or external buffer disables fragments only for that part.
    const std::array<const char*,6> keys{{"head","body","left_arm","right_arm","left_leg","right_leg"}};
    if (splitAssetsAvailable_) {
        try {
            std::ifstream input("resources/enemy/boss/fragments/manifest.json");
            const auto manifest = nlohmann::json::parse(input);
            for (size_t i = 0; i < keys.size(); ++i) {
                try {
                    auto files = manifest.at(keys[i]).get<std::vector<std::string>>();
                    bool valid = !files.empty() && files.size() <= 32;
                    for (const auto& file : files) {
                        const auto path = std::filesystem::path("resources") / file;
                        std::ifstream asset(path);
                        const auto data = nlohmann::json::parse(asset);
                        valid = valid && data.contains("meshes") && !data["meshes"].empty();
                        for (const auto& buffer : data.at("buffers")) {
                            const auto uri = buffer.at("uri").get<std::string>();
                            valid = valid && (uri.starts_with("data:") || std::filesystem::is_regular_file(path.parent_path()/uri));
                        }
                    }
                    if (valid) {
                        for (const auto& file : files) ModelManager::GetInstance()->LoadModel(file);
                        fragmentFiles_[i] = std::move(files);
                    }
                } catch (const std::exception&) { /* This part keeps its whole-part fallback. */ }
            }
        } catch (const std::exception&) { /* All parts keep their whole-part fallback. */ }
    }
    if (splitAssetsAvailable_) {
        try {
            std::ifstream input("resources/enemy/boss/faces/faces.json");
            const auto data = nlohmann::json::parse(input);
            if (data.at("version") != 1 || data.at("coordinate_system") != "gltf") throw std::runtime_error("Face format");
            for (size_t i=0; i<keys.size(); ++i) {
                try {
                    std::vector<std::array<Vector3,3>> faces;
                    const auto& list = data.at("parts").at(keys[i]);
                    if (list.empty() || list.size()>10000) continue;
                    for (const auto& triangle : list) {
                        if (triangle.size()!=3) throw std::runtime_error("Face size");
                        std::array<Vector3,3> vertices{};
                        for (size_t v=0;v<3;++v) {
                            if (triangle[v].size()!=3) throw std::runtime_error("Vertex size");
                            vertices[v]={-triangle[v][0].get<float>(),triangle[v][1].get<float>(),triangle[v][2].get<float>()};
                            if (!std::isfinite(vertices[v].x)||!std::isfinite(vertices[v].y)||!std::isfinite(vertices[v].z)) throw std::runtime_error("Vertex finite");
                        }
                        faces.push_back(vertices);
                    }
                    faceData_[i]=std::move(faces);
                    auto geometry=std::make_shared<EnemyPartGeometry>();
                    geometry->faces=faceData_[i];
                    legacyPartGeometry_[i]=std::move(geometry);
                } catch (const std::exception&) { }
            }
        } catch (const std::exception&) { /* Face -> Chunk -> whole part. */ }
    }
    EnemyDefinitions definitions;
    if (definitions.Load("resources/Data/enemies.json")) {
        for (const auto& [id,definition]:definitions.All()) {
            (void)id;
            if (definition.partAsset) PrepareAssetModels(definition.partAsset);
        }
    } else OutputDebugStringA(("Enemy asset preload: "+definitions.Error()+"\n").c_str());
    assetsPreloaded_ = true;
}

void Enemy::ReleasePreloadedAssets() {
    // GameApp calls this only after all scenes/enemies have been destroyed.
    for (auto& faces : faceData_) faces = {};
    for (auto& geometry : legacyPartGeometry_) geometry.reset();
    for (auto& files : fragmentFiles_) files = {};
    assetModels_.clear();
    assetsPreloaded_ = false;
    splitAssetsAvailable_ = false;
}
Enemy::~Enemy() {
    std::erase(fragmentOwners_, this);
}
void Enemy::MakeFragmentRoom() {
    size_t total = 0;
    Enemy* oldest = nullptr;
    for (auto* owner : fragmentOwners_) {
        total += owner->detachedParts_.size();
        if (!owner->detachedParts_.empty() && (!oldest ||
            owner->detachedParts_.front().spawnOrder < oldest->detachedParts_.front().spawnOrder)) oldest = owner;
    }
    if (total >= kMaxFragments && oldest) oldest->detachedParts_.erase(oldest->detachedParts_.begin());
}
void Enemy::Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera, bool useSplitAssets) {
    ai_ = {};
    attackCount_ = 0; lastAttackDamage_ = 0; attackFlash_ = 0;
    detachedParts_.clear();
    faceShards_.clear();
    PreloadAssets(); // Normally already prepared by GameApp; also supports standalone scene entry.
    if (std::find(fragmentOwners_.begin(), fragmentOwners_.end(), this) == fragmentOwners_.end()) fragmentOwners_.push_back(this);
    common_ = common; dx_ = dx; camera_ = camera;
    object_.Initialize(common, dx);
    object_.SetCamera(camera);
    object_.SetModel(kBossModelPath);
    object_.StopAnimation();
    object_.SetRotate(rotation_);
    object_.SetScale(definition_.VisualScale(scale_));
    object_.SetTranslate(position_);
    object_.SetEnableLighting(1);
    object_.SetDirection({0.3f, -1.0f, 0.5f});
    object_.SetIntensity(1.0f);
    object_.SetPointLightIntensity(0.0f);
    object_.SetSpotLightIntensity(0.0f);
    AABB bounds{};
    hasHitBox_ = object_.GetModel() && object_.GetModel()->GetLocalAABB(bounds);
    parts_=hasHitBox_ ? MakeEnemyParts(bounds) : EnemyParts{};
    visuals_.clear(); visuals_.resize(parts_.size());
    asset_.reset();
    // 部位モデルが一式そろわない場合は全身モデルを使い、欠けた姿で表示されるのを防ぐ。
    splitVisuals_ = useSplitAssets && hasHitBox_ && splitAssetsAvailable_;
    for (size_t i = 0; i < visuals_.size(); ++i) {
        auto& visual = visuals_[i];
        visual.type = parts_[i].type;
        visual.visible = true;
        visual.object.reset();
        if (!splitVisuals_) continue;
        visual.object = std::make_unique<Object3d>();
        auto& obj = *visual.object;
        obj.Initialize(common, dx);
        obj.SetCamera(camera);
        obj.SetModel(kPartModelPaths[i]);
        obj.StopAnimation();
        obj.SetEnableLighting(1);
        obj.SetDirection({0.3f, -1.0f, 0.5f});
        obj.SetIntensity(1.0f);
        obj.SetPointLightIntensity(0.0f);
        obj.SetSpotLightIntensity(0.0f);
    }
    if (splitVisuals_) PrepareFaceBatch();
    explosionTime_ = 0;
    blastHitTime_ = lastBlastDamage_ = 0;
    ApplyDefinition(definition_);
    if (useSplitAssets && !splitVisuals_)
        OutputDebugStringA("Enemy: split assets incomplete; using original Boss.\n");
}


void Enemy::PrepareForPool(Object3dCommon* common,DirectXCommon* dx,Camera* camera,const EnemyDefinition& definition) {
    Initialize(common,dx,camera,true);
    ApplyDefinition(definition);
    spawnDefinition_=definition_; spawnParts_=parts_; spawnAI_=ai_;
    spawnModels_.clear(); spawnModels_.reserve(visuals_.size());
    for (const auto& visual:visuals_) spawnModels_.push_back(visual.object ? visual.object->GetModel() : nullptr);
    faceShards_.reserve(kFaceCapacity);
    detachedParts_.reserve(kMaxFragments);
}
void Enemy::RetireFromPool() {
    faceShards_.clear(); detachedParts_.clear();
    explosionTime_=0; blastHitTime_=0;
    animation_.Reset();
}
void Enemy::ResetForSpawn(uint64_t spawnId,const std::string& trigger,const Vector3& position,const Vector3& rotation) {
    RetireFromPool();
    definition_=spawnDefinition_; parts_=spawnParts_; ai_=spawnAI_;
    position_=position; rotation_=rotation; scale_={2,2,2};
    SetSpawnIdentity(spawnId,trigger);
    attackCount_=0; lastAttackDamage_=0; attackFlash_=0;
    exploded_=false; explosionCenter_={}; lastBlastDamage_=0;
    breakMode_=FragmentMode::Face; detachedSettings_={};
    maxFacesPerBreak_=64; faceLifetime_=5; spreadPower_=1.5f; outwardPower_=1.5f;
    showPartColliders_=false; showMovementCollider_=false; showExplosionRange_=true;
    random_.seed(static_cast<uint32_t>(spawnId)^0x9e3779b9u);
#ifdef _DEBUG
    dimensionFileStatus_.clear();
#endif
    for (size_t i=0;i<visuals_.size();++i) {
        auto& visual=visuals_[i]; visual.visible=true;
        if (visual.object) { visual.object->SetModel(spawnModels_[i]); visual.object->StopAnimation(); }
    }
    if (typeMarker_) {
        const auto& color=definition_.typeMarker.color;
        typeMarker_->SetMaterialColor({color.x,color.y,color.z,1});
    }
    UpdateVisuals(0);
}
void Enemy::ApplyDefinition(const EnemyDefinition& definition) {
    definition_=definition;
    animation_.Reset();
    ai_={}; exploded_=false;
    ai_.settings={definition.detectionRange,definition.attackRange,definition.moveSpeed,
        definition.attackDamage,definition.attackInterval,definition.IsRanged(),definition.minRange,definition.maxRange};
    if (definition.partAsset) {
        asset_=PrepareAssetModels(definition.partAsset);
        parts_=asset_->source->Instantiate(definition.hpMultiplier);
        splitVisuals_=true; hasHitBox_=true;
        visuals_.clear(); visuals_.resize(parts_.size());
        for (size_t i=0;i<parts_.size();++i) {
            auto& visual=visuals_[i]; visual.type=parts_[i].type;
            visual.object=std::make_unique<Object3d>();
            visual.object->Initialize(common_,dx_); visual.object->SetCamera(camera_);
            visual.object->SetModel(asset_->models[i]); visual.object->SetEnableLighting(1);
            visual.object->SetDirection({.3f,-1,.5f}); visual.object->SetIntensity(1);
            visual.object->SetPointLightIntensity(0); visual.object->SetSpotLightIntensity(0);
        }
        PrepareFaceBatch();
    } else {
        if (asset_) {
            asset_.reset();
            AABB bounds{}; hasHitBox_=object_.GetModel()->GetLocalAABB(bounds);
            parts_=hasHitBox_ ? MakeEnemyParts(bounds) : EnemyParts{};
            splitVisuals_=hasHitBox_ && splitAssetsAvailable_;
            visuals_.clear(); visuals_.resize(parts_.size());
            if (splitVisuals_) for (size_t i=0;i<parts_.size();++i) {
                auto& visual=visuals_[i]; visual.type=parts_[i].type;
                visual.object=std::make_unique<Object3d>();
                visual.object->Initialize(common_,dx_); visual.object->SetCamera(camera_);
                visual.object->SetModel(kPartModelPaths[i]); visual.object->StopAnimation();
                visual.object->SetEnableLighting(1); visual.object->SetDirection({.3f,-1,.5f});
                visual.object->SetIntensity(1); visual.object->SetPointLightIntensity(0); visual.object->SetSpotLightIntensity(0);
            }
        }
        ApplyEnemyHpMultiplier(parts_,definition.hpMultiplier);
        ConfigureLegacyPartCollision();
    }
    if (!splitVisuals_ && !fallbackVisual_) {
        fallbackVisual_=std::make_unique<Object3d>();
        fallbackVisual_->Initialize(common_,dx_); fallbackVisual_->SetCamera(camera_);
        fallbackVisual_->SetModel(object_.GetModel()); fallbackVisual_->StopAnimation();
        fallbackVisual_->SetDirection({.3f,-1,.5f}); fallbackVisual_->SetIntensity(1);
        fallbackVisual_->SetPointLightIntensity(0); fallbackVisual_->SetSpotLightIntensity(0);
    }
    PrepareExplosionVisual();
    RebuildTypeMarker();
    if (common_ && dx_) UpdateVisuals(0);
}
void Enemy::PrepareExplosionVisual() {
    // 起爆フレームにGPUリソースを生成しないよう、範囲表示用の球を先に用意する。
    if (common_ && dx_ && definition_.type == EnemyType::Bomber && !explosionVisual_) {
        Model::ModelData geometry;
        geometry.materials.push_back({"resources/white1x1.png"});
        Model::MeshData mesh;
        mesh.vertices = GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
        mesh.indexCount = static_cast<uint32_t>(mesh.vertices.size());
        geometry.indices.resize(mesh.vertices.size());
        for (uint32_t i=0; i<geometry.indices.size(); ++i) geometry.indices[i]=i;
        geometry.meshes.push_back(std::move(mesh));
        geometry.rootNode.meshIndices.push_back(0);
        explosionModelCommon_.Initialize(dx_);
        explosionModel_ = std::make_unique<Model>();
        explosionModel_->InitializeFromModelData(&explosionModelCommon_, geometry);
        explosionVisual_ = std::make_unique<Object3d>();
        explosionVisual_->Initialize(common_, dx_);
        explosionVisual_->SetCamera(camera_);
        explosionVisual_->SetModel(explosionModel_.get());
        explosionVisual_->SetEnableLighting(0);
        explosionVisual_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeAdd);
    }
}
void Enemy::RebuildTypeMarker() {
    typeMarker_.reset();
    if (!common_ || !dx_) return;
    if (definition_.typeMarker.enabled) {
        typeMarker_=std::make_unique<Object3d>();
        typeMarker_->Initialize(common_,dx_);
        typeMarker_->SetCamera(camera_);
        typeMarker_->SetModel("cube/cube.obj");
        typeMarker_->SetTexture("resources/white1x1.png");
        typeMarker_->SetEnableLighting(0);
        const auto& color=definition_.typeMarker.color;
        typeMarker_->SetMaterialColor({color.x,color.y,color.z,1});
    }

}

void Enemy::ConfigureLegacyPartCollision() {
    if (!splitVisuals_) return;
    for (size_t i=0;i<parts_.size() && i<legacyPartGeometry_.size();++i) {
        if (!visuals_[i].object || !visuals_[i].object->GetModel()) continue;
        // Use the actual split mesh bounds/faces rather than T-pose partitions.
        visuals_[i].object->GetModel()->GetLocalAABB(parts_[i].bounds);
        parts_[i].geometry=legacyPartGeometry_[i];
    }
}
const Matrix4x4& Enemy::PartWorldMatrix(size_t index) const {
    if (splitVisuals_ && index<visuals_.size() && visuals_[index].object)
        return visuals_[index].object->GetWorldMatrix();
    if (!splitVisuals_ && fallbackVisual_) return fallbackVisual_->GetWorldMatrix();
    return object_.GetWorldMatrix();
}
bool Enemy::Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hit) const {
    hit = {};
    return hasHitBox_ && !IsDead() && RaycastEnemyPartsTransformed(parts_,[this](size_t index) -> const Matrix4x4* {
        if (splitVisuals_ && (index>=visuals_.size() || !visuals_[index].visible || !visuals_[index].object)) return nullptr;
        return &PartWorldMatrix(index);
    },origin,direction,maxDistance,hit);
}
// 表示と起爆で同じ中心を使い、モデルの拡縮や部位位置による範囲のずれを防ぐ。
Vector3 Enemy::ExplosionCenter() const {
    Vector3 center = position_ + Vector3{0, definition_.collisionHeight * .5f, 0};
    for (size_t i=0;i<parts_.size();++i) {
        const auto& body=parts_[i];
        if (body.role == EnemyPartRole::Body) {
            center = EnemyPartTransformPoint((body.bounds.min + body.bounds.max) * .5f,
                PartWorldMatrix(i));
            break;
        }
    }
    return center;
}

EnemyBulletHitResult Enemy::ApplyBulletDamage(EnemyPartType part,float damage,const Vector3& direction) {
    return ApplyBulletDamage(FindLegacyPart(part),damage,direction);
}
EnemyBulletHitResult Enemy::ApplyBulletDamage(size_t part, float damage, const Vector3& direction) {
    if (IsDead() || part>=parts_.size() || parts_[part].Destroyed()) return {};
    // Check before applying damage so even a lethal body shot triggers the explosion.
    // 致死ダメージで死亡状態になった後では起爆条件を満たせないため、被弾前の状態で判定する。
    const bool detonate = IsBomberDetonationHit(definition_.type, LegacyRoleType(parts_[part].role), damage, IsDead()) &&
        (parts_[part].usesLocalHp || parts_[part].sharedGroup<parts_.hpGroups.size());
    EnemyBulletHitResult result;
    result.damage = ApplyDamage(part, damage, direction);
    if (!detonate) return result;

    const auto center = ExplosionCenter();
    result.explosion = EnemyExplosion{center, definition_.explosionRadius, definition_.explosionDamage};
    // 表示は実際に起爆した時だけ開始する。頭部破壊後の胴体への追撃ではここに到達しない。
    explosionCenter_ = center;
    explosionTime_ = kExplosionDuration;
    Die(direction);
    exploded_=true;
    for (auto& group:parts_.hpGroups) if (group.deathOnZero) group.hp=0;
    return result;
}

void Enemy::ApplyExplosionDamage(const EnemyExplosion& explosion) {
    if (IsDead()) return;
    const float damage = EnemyExplosionDamage(explosion, position_,
        definition_.collisionRadius, definition_.collisionHeight);
    if (damage <= 0) return;
    const auto outward = position_ + Vector3{0, definition_.collisionHeight * .5f, 0} - explosion.center;
    const float length = std::hypot(outward.x, outward.y, outward.z);
    // Blast damage uses the ordinary damage path, so other Bombers do not chain-detonate.
    size_t target=kNoEnemyPart;
    for (size_t i=0;i<parts_.size();++i) {
        const auto& part=parts_[i];
        const bool receivesDamage=(part.usesLocalHp && part.hp>0) ||
            (part.sharedGroup<parts_.hpGroups.size() && part.sharedDamageRate>0 && parts_.hpGroups[part.sharedGroup].hp>0);
        if (part.Destroyed() || !receivesDamage) continue;
        if (target==kNoEnemyPart) target=i;
        if (part.role==EnemyPartRole::Body) { target=i; break; }
    }
    if (target==kNoEnemyPart) return;
    lastBlastDamage_ = ApplyDamage(target, damage,
        length > 1e-5f ? outward * (1 / length) : Vector3{0, 1, 0});
    if (lastBlastDamage_ <= 0) return;
    // Debugの当たり判定枠に頼らず、Releaseでも被爆した個体を判別できるよう全身を強調する。
    blastHitTime_ = .9f;
    ShowHitFeedback(target);
    UpdateVisuals(0); // 命中したフレームの描画から反映し、次のAI更新を待たない。
}

float Enemy::ApplyDamage(EnemyPartType type,float damage,const Vector3& shotDirection) {
    return ApplyDamage(FindLegacyPart(type),damage,shotDirection);
}
float Enemy::ApplyDamage(size_t index, float damage, const Vector3& shotDirection) {
    if (IsDead()) return 0;
    const float lost = DamageEnemyPart(parts_, index, damage);
    if (lost <= 0) return lost;
    if (EnemyPartsDead(parts_)) Die(shotDirection);
    else if (parts_[index].Destroyed()) DetachPart(index,shotDirection);
    return lost;
}
void Enemy::Die(const Vector3& direction) {
    if (!BeginEnemyDeath(parts_)) return;
    ai_.state=EnemyState::Dead;
    ai_.attacksThisUpdate=0;
    const auto world=Matrix4x4::MakeAffineMatrix(definition_.VisualScale(scale_),rotation_,position_);
    const auto center=position_+Vector3{0,definition_.collisionHeight*.5f,0};
    for (size_t i=0;i<parts_.size();++i) {
        const auto partCenter=EnemyPartTransformPoint((parts_[i].bounds.min+parts_[i].bounds.max)*.5f,world);
        const auto outward=partCenter-center;
        const float length=std::hypot(outward.x,outward.y,outward.z);
        DetachPart(i,(length>1e-5f ? outward*(1/length) : Vector3{0,1,0})+direction*.35f,true);
    }
}
void Enemy::DetachPart(size_t i,const Vector3& shotDirection,bool forceFaces) {
    if (!splitVisuals_ || i>=visuals_.size()) return;
    auto& visual = visuals_[i];
    if (!visual.object || !visual.visible) return;
    if ((forceFaces || breakMode_ == FragmentMode::Face) && SpawnFaces(i, shotDirection,forceFaces)) { visual.visible=false; return; }
    AABB bounds{};
    if (!visual.object->GetModel()->GetLocalAABB(bounds)) return;
    std::uniform_real_distribution<float> magnitude(2.0f,6.0f);
    std::uniform_real_distribution<float> jitter(-1.0f,1.0f);
    std::bernoulli_distribution sign;
    const auto spin = [&]() { return magnitude(random_)*(sign(random_) ? 1.0f : -1.0f); };
    const auto translation = visual.object->GetTranslate();
    const auto rotation = visual.object->GetRotate();
    const auto scale = visual.object->GetScale();
    const auto center = EnemyPartTransformPoint((bounds.min+bounds.max)*.5f,
        Matrix4x4::MakeAffineMatrix(scale,rotation,translation));
    std::vector<Model*> pieces;
    if (asset_) pieces=asset_->chunks[i];
    else if (i<fragmentFiles_.size()) for (const auto& file:fragmentFiles_[i]) pieces.push_back(ModelManager::GetInstance()->FindModel(file));
    const bool fragments = !pieces.empty();
    const size_t count = fragments ? pieces.size() : 1;
    for (size_t j = 0; j < count; ++j) {
        DetachedEnemyFragment detached;
        AABB pieceBounds = bounds;
        if (fragments) {
            detached.object = std::make_unique<Object3d>();
            detached.object->Initialize(common_, dx_);
            detached.object->SetCamera(camera_);
            detached.object->SetModel(pieces[j]);
            detached.object->StopAnimation();
            detached.object->GetModel()->GetLocalAABB(pieceBounds);
            detached.object->SetEnableLighting(1);
            detached.object->SetDirection({.3f,-1,.5f});
            detached.object->SetIntensity(1);
            detached.object->SetPointLightIntensity(0);
            detached.object->SetSpotLightIntensity(0);
        } else {
            // Keep the attached renderer for the next pooled spawn.
            detached.object=std::make_unique<Object3d>();
            detached.object->Initialize(common_,dx_); detached.object->SetCamera(camera_);
            detached.object->SetModel(visual.object->GetModel()); detached.object->StopAnimation();
            detached.object->SetEnableLighting(1); detached.object->SetDirection({.3f,-1,.5f});
            detached.object->SetIntensity(1); detached.object->SetPointLightIntensity(0); detached.object->SetSpotLightIntensity(0);
        }
        detached.motion.Initialize(pieceBounds, translation, rotation, scale, shotDirection,
            LegacyRoleType(parts_[i].role), {spin(),spin(),spin()}, detachedSettings_);
        if (fragments) {
            const auto outward = detached.motion.position-center;
            const float length = std::hypot(outward.x,outward.y,outward.z);
            detached.motion.velocity = detached.motion.velocity + Vector3{jitter(random_),jitter(random_),jitter(random_)}*spreadPower_;
            if (length > 1e-5f) detached.motion.velocity = detached.motion.velocity + outward*(outwardPower_/length);
        }
        detached.object->SetTranslate(translation);
        detached.object->SetRotate(rotation);
        detached.object->SetScale(scale);
        detached.object->SetMaterialColor({.59f,.06f,.06f,1});
        detached.object->Update(0);
        MakeFragmentRoom();
        detached.spawnOrder = nextSpawnOrder_++;
        detachedParts_.push_back(std::move(detached));
    }
    visual.visible=false;
}
void Enemy::ShowHitFeedback(EnemyPartType type) { ShowHitFeedback(FindLegacyPart(type)); }
void Enemy::ShowHitFeedback(size_t index) {
    if (index<parts_.size()) parts_[index].flashRemaining=0.2f;
}
float Enemy::Update(float dt, const Vector3& playerPosition) {
    const auto previous = position_;
    const float attackDamage = ai_.Update(position_, rotation_, playerPosition, dt, IsDead());
    const bool moving = !IsDead() && std::hypot(position_.x-previous.x, position_.z-previous.z) > 1e-6f;
    animation_.Advance(dt, moving, parts_);
    UpdateVisuals(dt);
    return attackDamage;
}
void Enemy::UpdateVisuals(float dt) {
    blastHitTime_ = std::max(0.0f, blastHitTime_ - std::max(0.0f, dt));
    explosionTime_ = std::max(0.0f, explosionTime_ - std::max(0.0f, dt));
    attackFlash_ = std::max(0.0f, attackFlash_-dt);
    for (auto& face : faceShards_) face.motion.Update(dt);
    std::erase_if(faceShards_, [](const auto& face) { return !face.motion.Active(); });
    TrimFacePool(0);
    for (auto& detached : detachedParts_) {
        detached.motion.Update(dt);
        detached.object->SetTranslate(detached.motion.Translation());
        detached.object->SetRotate(detached.motion.rotation);
        detached.object->Update(dt);
    }
    std::erase_if(detachedParts_, [](const auto& part) { return !part.motion.Active(); });
    for (auto& part:parts_) part.flashRemaining=std::max(0.0f,part.flashRemaining-std::max(0.0f,dt));
    object_.SetTranslate(position_);
    object_.SetRotate(rotation_);
    const auto visualScale=definition_.VisualScale(scale_);
    object_.SetScale(visualScale);
    object_.SetMaterialColor(blastHitTime_ > 0 ? Vector4{1, .08f, .02f, 1} : Vector4{1,1,1,1});
    object_.SetEnableLighting(blastHitTime_ > 0 ? 0 : 1);
    object_.Update(dt);
    if (typeMarker_ && !IsDead()) {
        const auto& marker=definition_.typeMarker;
        typeMarker_->SetTranslate(EnemyPartTransformPoint(marker.offset,object_.GetWorldMatrix()));
        typeMarker_->SetRotate(rotation_);
        typeMarker_->SetScale({marker.scale.x*visualScale.x,marker.scale.y*visualScale.y,marker.scale.z*visualScale.z});
        typeMarker_->Update(dt);
    }
    // The base object retains the gameplay transform. Part raycasts and debug
    // boxes use the same animated matrices as the following render objects.
    const auto pose = animation_.Sample(parts_, visualScale, !IsDead());
    if (!splitVisuals_ && fallbackVisual_) {
        const auto render = animation_.TransformFor(pose, nullptr, visualScale, rotation_, position_);
        fallbackVisual_->SetTranslate(render.translate); fallbackVisual_->SetRotate(render.rotate);
        fallbackVisual_->SetScale(visualScale);
        fallbackVisual_->SetMaterialColor(blastHitTime_ > 0 ? Vector4{1,.08f,.02f,1} : Vector4{1,1,1,1});
        fallbackVisual_->SetEnableLighting(blastHitTime_ > 0 ? 0 : 1);
        fallbackVisual_->Update(dt);
    }
    for (size_t i = 0; i < visuals_.size(); ++i) {
        if (!visuals_[i].object) continue;
        auto& obj = *visuals_[i].object;
        const auto render = animation_.TransformFor(pose, &parts_[i], visualScale, rotation_, position_);
        obj.SetTranslate(render.translate);
        obj.SetRotate(render.rotate);
        obj.SetScale(visualScale);
        Vector4 color{1,1,1,1};
        switch (parts_[i].DamageState()) {
        case EnemyPartDamageState::LightDamage: color = {1,.67f,.67f,1}; break;
        case EnemyPartDamageState::HeavyDamage: color = {1,.18f,.18f,1}; break;
        case EnemyPartDamageState::Critical: color = {.59f,.06f,.06f,1}; break;
        default: break;
        }
        if (parts_[i].flashRemaining>0) color={1,1,.3f,1};
        if (blastHitTime_ > 0) color = {1, .08f, .02f, 1};
        obj.SetEnableLighting(blastHitTime_ > 0 ? 0 : 1);
        obj.SetMaterialColor(color);
        obj.Update(dt);
    }
}

void Enemy::SetPartVisible(EnemyPartType type, bool visible) {
    for (auto& visual : visuals_) if (visual.type == type) visual.visible = visible;
}
void Enemy::DrawExplosion() {
    if (!explosionVisual_ || explosionTime_ <= 0) return;
    // 半径を拡大する演出にすると判定範囲と食い違うため、大きさは固定して透明度だけ下げる。
    const float radius = definition_.explosionRadius;
    explosionVisual_->SetTranslate(explosionCenter_);
    explosionVisual_->SetScale({radius, radius, radius});
    explosionVisual_->SetMaterialColor({1.0f, .3f, .025f, .3f * explosionTime_ / kExplosionDuration});
    explosionVisual_->Update(0);
    explosionVisual_->Draw();
}

void Enemy::Draw(bool showMarker) {
    if (showMarker && typeMarker_ && !IsDead()) typeMarker_->Draw();
    DrawFaces();
    for (auto& detached : detachedParts_) detached.object->Draw();
    if (!splitVisuals_) {
        if (!IsDead()) (fallbackVisual_ ? *fallbackVisual_ : object_).Draw();
        return;
    }
    for (size_t i = 0; i < visuals_.size(); ++i) {
        const auto& visual = visuals_[i];
        if (visual.object && visual.visible && parts_[i].DamageState() != EnemyPartDamageState::Destroyed)
            visual.object->Draw();
    }
}

#ifdef USE_IMGUI
#include "imgui.h"
#endif
void Enemy::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text("Enemy ID: %s | Definition ID: %s | Type: %s", id_.c_str(), definition_.id.c_str(), EnemyTypeName(definition_.type));
    ImGui::Text("HP Multiplier: %.2f", definition_.hpMultiplier);
    for (const auto& part : parts_) if (part.role == EnemyPartRole::Body)
        ImGui::Text("Body HP: %.0f / %.0f | Last blast damage received: %.0f", part.hp, part.maxHp, lastBlastDamage_);
#ifdef _DEBUG
    // Change only dimensions: ApplyDefinition would reset HP and AI state.
    float uniform=definition_.visualScaleMultiplier.x;
    if (ImGui::DragFloat("Visual Scale (Uniform)",&uniform,.005f,.01f,5.0f,"%.3f",ImGuiSliderFlags_AlwaysClamp) && std::isfinite(uniform))
        definition_.visualScaleMultiplier={uniform,uniform,uniform};
    float components[]{definition_.visualScaleMultiplier.x,definition_.visualScaleMultiplier.y,definition_.visualScaleMultiplier.z};
    if (ImGui::DragFloat3("Visual Scale XYZ",components,.005f,.01f,5.0f,"%.3f",ImGuiSliderFlags_AlwaysClamp) &&
        std::isfinite(components[0]) && std::isfinite(components[1]) && std::isfinite(components[2]))
        definition_.visualScaleMultiplier={components[0],components[1],components[2]};
    float radius=definition_.collisionRadius,height=definition_.collisionHeight;
    if (ImGui::DragFloat("Collision Radius",&radius,.01f,.01f,20.0f,"%.3f",ImGuiSliderFlags_AlwaysClamp) && std::isfinite(radius))
        definition_.collisionRadius=radius;
    if (ImGui::DragFloat("Collision Height",&height,.01f,.01f,20.0f,"%.3f",ImGuiSliderFlags_AlwaysClamp) && std::isfinite(height))
        definition_.collisionHeight=height;
    if (ImGui::Button("Save Dimensions to JSON")) {
        std::string error;
        dimensionFileStatus_=SaveEnemyDimensions("resources/Data/enemies.json",definition_,error) ?
            "Saved dimensions for "+definition_.id+". Restart applies them to all instances." : "Save failed: "+error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Dimensions from JSON")) {
        std::string error;
        dimensionFileStatus_=LoadEnemyDimensions("resources/Data/enemies.json",definition_,error) ?
            "Loaded dimensions for "+definition_.id : "Load failed: "+error;
    }
    if (!dimensionFileStatus_.empty()) ImGui::TextWrapped("%s",dimensionFileStatus_.c_str());
    ImGui::TextWrapped("Live preview / Load: selected enemy only, including while F1-paused. Save: visualScale + collisionRadius + collisionHeight for this type in resources/Data/enemies.json. HP and AI are preserved.");
#endif
    const auto visualScale=definition_.VisualScale(scale_);
    ImGui::Text("Visual Scale: %.2f, %.2f, %.2f (multiplier)", definition_.visualScaleMultiplier.x, definition_.visualScaleMultiplier.y, definition_.visualScaleMultiplier.z);
    ImGui::Text("Effective Model Scale: %.2f, %.2f, %.2f", visualScale.x, visualScale.y, visualScale.z);
    ImGui::Text("Collision Radius: %.3f | Collision Height: %.3f (world)", definition_.collisionRadius, definition_.collisionHeight);
    ImGui::Checkbox("Show Enemy Movement Collider", &showMovementCollider_);
    if (definition_.IsRanged()) {
        ImGui::Text("Min Range: %.2f | Preferred Range: %.2f | Max Range: %.2f", definition_.minRange, definition_.preferredRange, definition_.maxRange);
        ImGui::Text("Projectile Speed: %.2f", definition_.projectileSpeed);
    }
    if (definition_.type==EnemyType::Bomber) {
        ImGui::Checkbox("Show Explosion Range (orange)", &showExplosionRange_);
        ImGui::Text("Body shot detonates | Explosion Radius: %.2f | Damage: %.2f (player + enemies)", definition_.explosionRadius, definition_.explosionDamage);
    }
    ImGui::Text("Enemy State: %s | Distance: %.2f", EnemyStateName(GetState()), ai_.distance);
    if (ImGui::Checkbox("Procedural Animation", &animation_.settings.enabled)) UpdateVisuals(0);
    if (ImGui::TreeNode("Arm Pose Tuning")) {
        bool changed = ImGui::SliderFloat("Arm Lower Angle (radians)", &animation_.settings.armLowerAngle, 0, 1.5707963f);
        changed |= ImGui::SliderFloat("Arm Swing (radians)", &animation_.settings.armSwing, 0, .35f);
        if (changed) UpdateVisuals(0);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Crawl Pose Tuning")) {
        auto& tuning = animation_.settings;
        bool changed = ImGui::SliderFloat("Prone Angle (radians)", &tuning.crawlForwardLean, 1.0f, 1.5707963f);
        changed |= ImGui::SliderFloat("Crawl Ground Clearance", &tuning.crawlGroundClearance, 0, .10f);
        changed |= ImGui::SliderFloat("Crawl Arm Reach", &tuning.crawlArmReach, .5f, 1.4f);
        changed |= ImGui::SliderFloat("Crawl Arm Stroke", &tuning.crawlArmStroke, 0, .4f);
        changed |= ImGui::SliderFloat("Crawl Speed (Hz)", &tuning.crawlHz, .2f, 3);
        if (changed) UpdateVisuals(0);
        ImGui::TreePop();
    }
    const auto locomotion = EnemyProceduralAnimation::Classify(parts_);
    ImGui::Text("Movement Pose: %s", locomotion == EnemyLocomotionPose::Normal ? "Normal" :
        locomotion == EnemyLocomotionPose::Crawl ? "Crawl" :
        locomotion == EnemyLocomotionPose::MissingLeftLeg ? "Missing Left Leg" : "Missing Right Leg");
    ImGui::Text("Last Enemy Attack: %s | Attack Count: %llu", attackFlash_ > 0 ? "HIT" : "-", attackCount_);
    ImGui::Text("Last applied damage: %.0f | Attack Cooldown: %.2f", lastAttackDamage_, ai_.cooldown);
    ImGui::SliderFloat("Detection Range", &ai_.settings.detectionRange, 1, 50);
    ImGui::SliderFloat("Attack Range", &ai_.settings.attackRange, .2f, 30);
    ImGui::SliderFloat("Enemy Move Speed", &ai_.settings.moveSpeed, 0, 10);
    ImGui::SliderFloat("Attack Damage", &ai_.settings.attackDamage, 0, 50);
    ImGui::SliderFloat("Attack Interval", &ai_.settings.attackInterval, .1f, 5);
    ImGui::TextUnformatted(splitVisuals_ ? "Visuals: six separate assets" : "Visuals: original Boss (parts unavailable)");
    if (splitVisuals_) {
        for (auto& visual : visuals_) {
            ImGui::PushID(static_cast<int>(visual.type));
            ImGui::Checkbox(parts_[static_cast<size_t>(&visual-visuals_.data())].name.c_str(), &visual.visible);
            ImGui::PopID();
        }
    }
    int mode=static_cast<int>(breakMode_);
    if (ImGui::Combo("Break Mode", &mode, "Chunk\0Face\0")) breakMode_=static_cast<FragmentMode>(mode);
    ImGui::SliderInt("Max Active Face Shards Per Enemy", &maxActiveFaces_, 1, static_cast<int>(kFaceCapacity));
    ImGui::SliderInt("Max Face Shards Per Break", &maxFacesPerBreak_, 1, static_cast<int>(kFaceCapacity));
    ImGui::SliderFloat("Face Shard Lifetime", &faceLifetime_, .1f, 15);
    ImGui::Text("Active Faces (this Enemy): %zu", faceShards_.size());
    if (ImGui::TreeNode("Detached Part")) {
        ImGui::Text("Active: %zu (settings apply to new parts)", detachedParts_.size());
        ImGui::Text("Max active across all Enemies: %zu", kMaxFragments);
        ImGui::SliderFloat("Fragment Spread", &spreadPower_, 0, 6);
        ImGui::SliderFloat("Fragment Outward", &outwardPower_, 0, 6);
        ImGui::SliderFloat("Launch Power", &detachedSettings_.launchPower, 0, 20);
        ImGui::SliderFloat("Upward Power", &detachedSettings_.upwardPower, 0, 10);
        ImGui::SliderFloat("Gravity", &detachedSettings_.gravity, -30, -1);
        ImGui::SliderFloat("Life Time", &detachedSettings_.lifeTime, .1f, 15);
        ImGui::SliderFloat("Bounce", &detachedSettings_.bounce, 0, .8f);
        ImGui::SliderFloat("Angular Velocity Scale", &detachedSettings_.angularVelocityScale, 0, 3);
        ImGui::TreePop();
    }
    ImGui::TextUnformatted("Enemy Parts");
    for (const auto& group:parts_.hpGroups)
        ImGui::Text("Group %s: %.0f / %.0f%s",group.id.c_str(),group.hp,group.maxHp,group.deathOnZero?" (death at zero)":"");
    for (const auto& part : parts_) {
        ImGui::Text("Role: %s | Local: %s | Breakable: %s",EnemyPartRoleName(part.role),part.usesLocalHp?"yes":"no",part.breakable?"yes":"no");
        if (part.sharedGroup<parts_.hpGroups.size()) ImGui::Text("Shared: %s x %.2f",parts_.hpGroups[part.sharedGroup].id.c_str(),part.sharedDamageRate);
        ImGui::Text("%-8s %.0f / %.0f | %s | Damage %.0f%%", part.name.c_str(),
            part.hp, part.maxHp, EnemyPartDamageStateName(part.DamageState()), part.DamageRate() * 100.0f);
    }
    ImGui::Checkbox("Show Enemy Part Colliders", &showPartColliders_);
    if (ImGui::TreeNode("Enemy Transform (part alignment test)")) {
        ImGui::DragFloat3("Enemy Position",&position_.x,0.05f);
        ImGui::DragFloat3("Enemy Rotation (radians)",&rotation_.x,0.01f);
        ImGui::SliderFloat3("Enemy Scale",&scale_.x,0.1f,5.0f);
        ImGui::TextUnformatted("Left/Right are the enemy's own sides. Wireframe color shows damage; hit flashes thicker.");
        ImGui::TreePop();
    }
#endif
}
void Enemy::DrawPartDebug(const Matrix4x4& vp,const Vector2& screenMin,const Vector2& screenMax, bool forceParts, bool forceMovement, bool rangeOnly) const {
#ifdef USE_IMGUI
    if (!hasHitBox_) return;
    auto* draw=ImGui::GetForegroundDrawList();
    draw->PushClipRect({screenMin.x,screenMin.y},{screenMax.x,screenMax.y},true);
    const bool movement = !rangeOnly && (showMovementCollider_ || forceMovement) && !IsDead();
    const bool explosion = showExplosionRange_ && definition_.type == EnemyType::Bomber && !IsDead();
    if (movement || explosion) {
        // This upright cylinder represents movement/separation, not the shootable parts.
        struct Clip { float x,y,z,w; };
        const auto clip=[&](const Vector3& p) -> Clip {
            return {p.x*vp.m[0][0]+p.y*vp.m[1][0]+p.z*vp.m[2][0]+vp.m[3][0],
                p.x*vp.m[0][1]+p.y*vp.m[1][1]+p.z*vp.m[2][1]+vp.m[3][1],
                p.x*vp.m[0][2]+p.y*vp.m[1][2]+p.z*vp.m[2][2]+vp.m[3][2],
                p.x*vp.m[0][3]+p.y*vp.m[1][3]+p.z*vp.m[2][3]+vp.m[3][3]};
        };
        const auto project=[&](Clip p) -> ImVec2 {
            return {screenMin.x+(p.x/p.w+1)*.5f*(screenMax.x-screenMin.x),
                screenMin.y+(1-p.y/p.w)*.5f*(screenMax.y-screenMin.y)};
        };
        const auto line=[&](const Vector3& from,const Vector3& to, ImU32 color) {
            auto a=clip(from),b=clip(to);
            if (a.z<0 && b.z<0) return;
            if ((a.z<0)!=(b.z<0)) {
                const float t=a.z/(a.z-b.z);
                const Clip intersection{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0,a.w+(b.w-a.w)*t};
                if (a.z<0) a=intersection; else b=intersection;
            }
            if (a.w>1e-5f && b.w>1e-5f) draw->AddLine(project(a),project(b),color,1.5f);
        };
        if (movement) {
        constexpr int segments=32;
        const Vector3 height{0,definition_.collisionHeight,0};
        for (int i=0;i<segments;++i) {
            const float angle=static_cast<float>(i)*6.28318530718f/static_cast<float>(segments);
            const float next=static_cast<float>(i+1)*6.28318530718f/static_cast<float>(segments);
            const auto a=position_+Vector3{std::cos(angle)*definition_.collisionRadius,0,std::sin(angle)*definition_.collisionRadius};
            const auto b=position_+Vector3{std::cos(next)*definition_.collisionRadius,0,std::sin(next)*definition_.collisionRadius};
            line(a,b,IM_COL32(70,255,170,255)); line(a+height,b+height,IM_COL32(70,255,170,255));
            if (i%8==0) line(a,a+height,IM_COL32(70,255,170,255));
        }
        }
        if (explosion) {
            // 判定は球なので水平円だけでなく縦の円も描き、高低差を含む範囲を示す。
            const auto center = ExplosionCenter();
            const float radius = definition_.explosionRadius;
            constexpr int segments = 64;
            for (int i = 0; i < segments; ++i) {
                const float a = static_cast<float>(i) * 6.28318530718f / segments;
                const float b = static_cast<float>(i + 1) * 6.28318530718f / segments;
                const float x = std::cos(a)*radius, y = std::sin(a)*radius;
                const float nx = std::cos(b)*radius, ny = std::sin(b)*radius;
                const ImU32 color = IM_COL32(255,150,40,230);
                line(center+Vector3{x,0,y},center+Vector3{nx,0,ny},color);
                line(center+Vector3{x,y,0},center+Vector3{nx,ny,0},color);
                line(center+Vector3{0,x,y},center+Vector3{0,nx,ny},color);
            }
        }
    }
    if (rangeOnly) { draw->PopClipRect(); return; }
    for (size_t index=0;index<parts_.size();++index) {
        const auto& part=parts_[index];
        if (part.DamageState() == EnemyPartDamageState::Destroyed) continue;
        if (splitVisuals_ && (index>=visuals_.size() || !visuals_[index].visible || !visuals_[index].object)) continue;
        if (!(showPartColliders_ || forceParts) && part.flashRemaining<=0) continue;
        const auto matrix=Matrix4x4::Multiply(PartWorldMatrix(index),vp);
        struct Clip { float x,y,z,w; } corners[8];
        for (int i=0;i<8;++i) {
            const Vector3 p{(i&1)?part.bounds.max.x:part.bounds.min.x,
                (i&2)?part.bounds.max.y:part.bounds.min.y,(i&4)?part.bounds.max.z:part.bounds.min.z};
            corners[i]={p.x*matrix.m[0][0]+p.y*matrix.m[1][0]+p.z*matrix.m[2][0]+matrix.m[3][0],
                p.x*matrix.m[0][1]+p.y*matrix.m[1][1]+p.z*matrix.m[2][1]+matrix.m[3][1],
                p.x*matrix.m[0][2]+p.y*matrix.m[1][2]+p.z*matrix.m[2][2]+matrix.m[3][2],
                p.x*matrix.m[0][3]+p.y*matrix.m[1][3]+p.z*matrix.m[2][3]+matrix.m[3][3]};
        }
        ImU32 color = IM_COL32(255,255,255,255);
        switch (part.DamageState()) {
        case EnemyPartDamageState::LightDamage: color = IM_COL32(255,170,170,255); break;
        case EnemyPartDamageState::HeavyDamage: color = IM_COL32(255,45,45,255); break;
        case EnemyPartDamageState::Critical: color = IM_COL32(150,15,15,255); break;
        case EnemyPartDamageState::Destroyed: color = IM_COL32(160,80,255,255); break;
        default: break;
        }
        auto project=[&](Clip p) {return ImVec2{screenMin.x+(p.x/p.w+1)*.5f*(screenMax.x-screenMin.x),
            screenMin.y+(1-p.y/p.w)*.5f*(screenMax.y-screenMin.y)};};
        for(int i=0;i<8;++i) for(int bit:{1,2,4}) {
            if (i&bit) continue;
            Clip a=corners[i], b=corners[i|bit];
            // Clip crossing edges against the near plane before perspective division.
            if (a.z<0 && b.z<0) continue;
            if ((a.z<0)!=(b.z<0)) {
                const float t=a.z/(a.z-b.z);
                const Clip intersection{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0,a.w+(b.w-a.w)*t};
                if(a.z<0)a=intersection;else b=intersection;
            }
            if(a.w>1e-5f && b.w>1e-5f) draw->AddLine(project(a),project(b),color,part.flashRemaining>0?3.0f:1.0f);
        }
        if((showPartColliders_ || forceParts) && corners[7].z>=0 && corners[7].w>1e-5f)
            draw->AddText(project(corners[7]),color,part.name.c_str());
    }
    draw->PopClipRect();
#else
    (void)rangeOnly; (void)forceParts; (void)forceMovement; (void)vp; (void)screenMin; (void)screenMax;
#endif
}

void Enemy::TrimFacePool(size_t reserve) {
    // Each enemy owns a separate face buffer. A new death burst must not evict
    // another enemy's shards before their lifetime expires.
    const size_t limit=static_cast<size_t>(std::clamp(maxActiveFaces_,1,static_cast<int>(kFaceCapacity)));
    const size_t available=limit-std::min(reserve,limit);
    if (faceShards_.size()>available)
        faceShards_.erase(faceShards_.begin(),faceShards_.begin()+(faceShards_.size()-available));
}
bool Enemy::SpawnFaces(size_t part, const Vector3& direction,bool deathBurst) {
    const auto& source=parts_[part].geometry ? parts_[part].geometry->faces : faceData_[part];
    if (source.empty() || !faceBatch_) return false;
    const auto& visual=*visuals_[part].object;
    const auto world=Matrix4x4::MakeAffineMatrix(visual.GetScale(),visual.GetRotate(),visual.GetTranslate());
    AABB partBounds{};
    if (!visual.GetModel()->GetLocalAABB(partBounds)) return false;
    const auto partCenter=EnemyPartTransformPoint((partBounds.min+partBounds.max)*.5f,world);
    // Share the death burst budget across the body so later legs do not evict
    // all of the head/torso shards spawned earlier in this same frame.
    const size_t budget=deathBurst ? std::max(size_t{1},static_cast<size_t>(maxActiveFaces_)/std::max(size_t{1},parts_.size()))
        : static_cast<size_t>(maxActiveFaces_);
    const size_t count=std::min({source.size(),static_cast<size_t>(maxFacesPerBreak_),budget});
    std::uniform_real_distribution<float> random(-1,1);
    std::uniform_real_distribution<float> spin(2,6);
    auto config=detachedSettings_; config.lifeTime=faceLifetime_;
    for (size_t i=0;i<count;++i) {
        FaceShard shard;
        const auto& triangle=source[i*source.size()/count]; // even selection, no duplicates
        std::array<Vector3,3> points;
        for (size_t v=0;v<3;++v) points[v]=EnemyPartTransformPoint(triangle[v],world);
        const auto center=(points[0]+points[1]+points[2])*(1.0f/3);
        AABB bounds{points[0]-center,points[0]-center};
        for (size_t v=0;v<3;++v) {
            shard.vertices[v]=points[v]-center;
            const auto& p=shard.vertices[v];
            bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
            bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};
        }
        const auto angular=[&]() {return spin(random_)*(random(random_)<0?-1.0f:1.0f);};
        shard.motion.Initialize(bounds,center,{}, {1,1,1},direction,parts_[part].type,{angular(),angular(),angular()},config);
        shard.motion.pivot={}; shard.motion.position=center;
        const auto outward=center-partCenter;
        const float length=std::hypot(outward.x,outward.y,outward.z);
        shard.motion.velocity=shard.motion.velocity+Vector3{random(random_),random(random_),random(random_)}*spreadPower_;
        if (length>1e-5f) shard.motion.velocity=shard.motion.velocity+outward*(outwardPower_/length);
        if (deathBurst) {
            // A face is a light shard, not a heavy torso/leg. Give every shard
            // horizontal travel and lift; downward limb directions must not
            // drive the burst straight into the floor.
            const float angle=random(random_)*std::numbers::pi_v<float>;
            Vector3 scatter{direction.x*.6f+std::cos(angle),0,direction.z*.6f+std::sin(angle)};
            const float horizontal=std::hypot(scatter.x,scatter.z);
            scatter=horizontal>1e-5f ? scatter*(1/horizontal) : Vector3{1,0,0};
            const float speed=5.5f+random(random_)*1.5f;
            shard.motion.velocity=scatter*speed;
            shard.motion.velocity.y=5.5f+random(random_)*1.5f;
        }
        TrimFacePool(1);
        shard.spawnOrder=nextSpawnOrder_++;
        faceShards_.push_back(shard);
    }
    return true;
}
void Enemy::DrawFaces() {
    if (faceShards_.empty() || !faceBatch_) return;
    uint32_t index=0;
    for (const auto& shard : faceShards_) {
        const auto matrix=Matrix4x4::MakeAffineMatrix({1,1,1},shard.motion.rotation,shard.motion.position);
        for (const int v : {0,1,2,2,1,0}) faceModel_->UpdateVertexPosition(index++,EnemyPartTransformPoint(shard.vertices[v],matrix));
    }
    for (;index<kFaceCapacity*6;++index) faceModel_->UpdateVertexPosition(index,{});
    // Vertices are world-space, but WVP must follow the current FPS camera every frame.
    faceBatch_->Update(0);
    faceBatch_->Draw();
}

#ifdef _DEBUG
Enemy::DebugState Enemy::CaptureDebug() const {
    DebugState state;
    state.definition=definition_; state.ai=ai_; state.parts=parts_;
    state.animation=animation_;
    state.position=position_; state.rotation=rotation_; state.scale=scale_;
    state.spawnId=spawnId_; state.trigger=spawnTriggerId_; state.nextOrder=nextSpawnOrder_;
    state.blastHitTime=blastHitTime_; state.lastBlastDamage=lastBlastDamage_;
    state.explosionTime=explosionTime_; state.explosionCenter=explosionCenter_;
    state.attacks=attackCount_; state.damage=lastAttackDamage_; state.flash=attackFlash_; state.random=random_;
    state.exploded=exploded_; state.splitVisuals=splitVisuals_; state.hasHitBox=hasHitBox_;
    state.asset=asset_; state.models.resize(visuals_.size()); state.visible.resize(visuals_.size());
    state.visualTransforms.resize(visuals_.size());
    for (size_t i=0;i<visuals_.size();++i) {
        state.models[i]=visuals_[i].object ? visuals_[i].object->GetModel() : nullptr;
        state.visible[i]=visuals_[i].visible;
        if (visuals_[i].object) state.visualTransforms[i]={visuals_[i].object->GetScale(),
            visuals_[i].object->GetRotate(),visuals_[i].object->GetTranslate()};
    }
    for (const auto& part : detachedParts_) state.detached.push_back({part.object->GetModel(),part.motion,part.spawnOrder});
    state.faces=faceShards_; state.breakMode=breakMode_; state.detachedSettings=detachedSettings_;
    state.maxFaces=maxActiveFaces_; state.facesPerBreak=maxFacesPerBreak_; state.faceLifetime=faceLifetime_;
    state.spread=spreadPower_; state.outward=outwardPower_;
    return state;
}
void Enemy::RestoreDebug(const DebugState& state) {
    definition_=state.definition; ai_=state.ai; parts_=state.parts;
    animation_=state.animation;
    asset_=state.asset; visuals_.resize(parts_.size());
    exploded_=state.exploded; splitVisuals_=state.splitVisuals; hasHitBox_=state.hasHitBox;
    position_=state.position; rotation_=state.rotation; scale_=state.scale;
    SetSpawnIdentity(state.spawnId,state.trigger); nextSpawnOrder_=state.nextOrder;
    blastHitTime_=state.blastHitTime; lastBlastDamage_=state.lastBlastDamage;
    explosionTime_=state.explosionTime; explosionCenter_=state.explosionCenter;
    attackCount_=state.attacks; lastAttackDamage_=state.damage; attackFlash_=state.flash; random_=state.random;
    const auto create=[&](Model* model) {
        auto object=std::make_unique<Object3d>(); object->Initialize(common_,dx_); object->SetCamera(camera_);
        object->SetModel(model); object->StopAnimation(); object->SetEnableLighting(1);
        object->SetDirection({.3f,-1,.5f}); object->SetIntensity(1); object->SetPointLightIntensity(0); object->SetSpotLightIntensity(0);
        return object;
    };
    for (size_t i=0;i<visuals_.size();++i) {
        if (state.models[i] && visuals_[i].object) visuals_[i].object->SetModel(state.models[i]);
        else if (state.models[i]) visuals_[i].object=create(state.models[i]);
        visuals_[i].type=parts_[i].type; visuals_[i].visible=state.visible[i];
    }
    detachedParts_.clear();
    for (const auto& part : state.detached) {
        DetachedEnemyPart detached; detached.object=create(part.model); detached.motion=part.motion; detached.spawnOrder=part.order;
        detached.object->SetScale(part.motion.scale); detached.object->SetMaterialColor({.59f,.06f,.06f,1});
        detachedParts_.push_back(std::move(detached));
    }
    faceShards_=state.faces; breakMode_=state.breakMode; detachedSettings_=state.detachedSettings;
    maxActiveFaces_=state.maxFaces; maxFacesPerBreak_=state.facesPerBreak; faceLifetime_=state.faceLifetime;
    spreadPower_=state.spread; outwardPower_=state.outward;
    PrepareExplosionVisual();
    // Definition-specific pool slots already own the appropriate marker.
    // Scene updates all restored visuals only after every enemy has been restored.
}
#endif

void Enemy::PrepareFaceBatch() {
    if (faceBatch_) return;
    Model::ModelData geometry;
    geometry.materials.push_back({"resources/white1x1.png"});
    Model::MeshData mesh;
    mesh.vertices.resize(kFaceCapacity*6);
    for (auto& vertex : mesh.vertices) vertex={{0,0,0,1},{0,0},{0,1,0}};
    mesh.indexCount=static_cast<uint32_t>(mesh.vertices.size());
    geometry.indices.resize(mesh.vertices.size());
    for (uint32_t v=0;v<geometry.indices.size();++v) geometry.indices[v]=v;
    geometry.meshes.push_back(std::move(mesh));
    geometry.rootNode.meshIndices.push_back(0);
    faceModelCommon_.Initialize(dx_);
    faceModel_=std::make_unique<Model>();
    faceModel_->InitializeFromModelData(&faceModelCommon_,geometry);
    faceBatch_=std::make_unique<Object3d>();
    faceBatch_->Initialize(common_,dx_);
    faceBatch_->SetCamera(camera_);
    faceBatch_->SetModel(faceModel_.get());
    faceBatch_->SetEnableLighting(0);
    faceBatch_->SetMaterialColor({.65f,.025f,.025f,1});
    faceBatch_->Update(0);
}

std::shared_ptr<const EnemyRenderAsset> Enemy::PrepareAssetModels(std::shared_ptr<const EnemyAsset> asset) {
    if (const auto found=assetModels_.find(asset->path);found!=assetModels_.end()) return found->second;
    auto result=std::make_shared<EnemyRenderAsset>(); result->source=asset;
    const auto build=[&](const std::string& key,const std::vector<std::array<EnemyPartVertex,3>>& triangles) {
        Model::ModelData geometry; geometry.materials.push_back({asset->texture});
        Model::MeshData mesh;
        for (const auto& tri:triangles) for (const auto& v:tri) {
            mesh.vertices.push_back({{v.position.x,v.position.y,v.position.z,1},v.uv,v.normal});
            geometry.indices.push_back(static_cast<uint32_t>(geometry.indices.size()));
        }
        mesh.indexCount=static_cast<uint32_t>(geometry.indices.size());
        geometry.meshes.push_back(std::move(mesh)); geometry.rootNode.meshIndices.push_back(0);
        return ModelManager::GetInstance()->CreatePrimitiveModel(key,geometry);
    };
    for (size_t i=0;i<asset->defaults.size();++i) {
        const auto& part=asset->defaults[i];
        const std::string key="EnemyAsset/"+asset->path+"/"+std::to_string(i);
        result->models.push_back(build(key,part.geometry->triangles));
        // Offline-equivalent octant chunks, shared by all instances of this asset.
        const auto buckets=PartitionEnemyChunks(*part.geometry,part.bounds);
        std::vector<Model*> chunks;
        for (size_t j=0;j<buckets.size();++j) if (!buckets[j].empty()) chunks.push_back(build(key+"/chunk"+std::to_string(j),buckets[j]));
        result->chunks.push_back(std::move(chunks));
    }
    assetModels_[asset->path]=result;
    return result;
}
