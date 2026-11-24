#include "ParticleManager.h"
#include "DirectXCommon.h"
#include "Camera.h" 
#include "Model.h"
#include "ModelManager.h"
#include "GeometryGenerator.h"
#include <filesystem>
#include <algorithm>
#include <bit>
#include <cmath>
#include <imgui.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <commdlg.h>

using json = nlohmann::json;

ParticleManager* ParticleManager::GetInstance() {
    static ParticleManager instance;
    return &instance;
}

void ParticleManager::UpdateGroupModel(const std::string& modelName, int modelType, Model* newModel) {
    for (auto& [name, group] : particleGroups_) {
        if (group.modelType == modelType && group.modelName == modelName) {
            group.model = newModel;
        }
    }
}

namespace {
    constexpr int kRetiredParticleGroupKeepFrames = 8;
    constexpr size_t kEmitterStride = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    constexpr int kFlamePrimitive = 11;
    constexpr int kFlameVolumePrimitive = 12;
    constexpr int kFlameGlowPrimitive = 13;
    constexpr int kSparkStreakPrimitive = 14;
    constexpr int kSoftDiscPrimitive = 15;
    constexpr int kProceduralSmokePrimitive = 16;
    const std::string kFlameVolumeModelName = std::to_string(kFlameVolumePrimitive);
    const std::string kFlameGlowModelName = std::to_string(kFlameGlowPrimitive);
    const std::string kSparkStreakModelName = std::to_string(kSparkStreakPrimitive);
    const std::string kSoftDiscModelName = std::to_string(kSoftDiscPrimitive);
    const std::string kProceduralSmokeModelName = std::to_string(kProceduralSmokePrimitive);
    enum ParticleDrawFlags : uint32_t {
        UseTexture = 1u, FlameFacets = 2u, EmissiveBlend = 4u, FlameGlow = 8u, SparkStreak = 16u, SoftDisc = 32u
    };
    static_assert(sizeof(EmitterData) <= kEmitterStride);

    bool IsProceduralSmoke(const ParticleGroup& group) {
        return group.modelType == 1 && group.modelName == kProceduralSmokeModelName;
    }

    Vector3 TransformDirection(const Vector3& v, const Matrix4x4& matrix) {
        return {
            v.x * matrix.m[0][0] + v.y * matrix.m[1][0] + v.z * matrix.m[2][0],
            v.x * matrix.m[0][1] + v.y * matrix.m[1][1] + v.z * matrix.m[2][1],
            v.x * matrix.m[0][2] + v.y * matrix.m[1][2] + v.z * matrix.m[2][2]
        };
    }

    std::string ToResourceRelativePath(const std::filesystem::path& sourcePath) {
        std::filesystem::path normalized = sourcePath.lexically_normal();
        std::filesystem::path relative = normalized;
        bool foundResources = false;
        for (auto it = normalized.begin(); it != normalized.end(); ++it) {
            if (it->string() == "resources" || it->string() == "Resources") {
                relative.clear();
                ++it;
                for (; it != normalized.end(); ++it) {
                    relative /= *it;
                }
                foundResources = true;
                break;
            }
        }
        if (!foundResources) {
            relative = normalized.filename();
        }
        return relative.generic_string();
    }

    std::string NormalizeParticleJsonFileName(const char* text) {
        std::filesystem::path path(text ? text : "");
        std::string fileName = path.filename().string();
        if (fileName.empty()) {
            fileName = "test_particles.json";
        }
        if (std::filesystem::path(fileName).extension().empty()) {
            fileName += ".json";
        }
        return fileName;
    }

    Model::ModelData MakeParticlePrimitiveModelData(const std::vector<Model::VertexData>& vertices) {
        Model::ModelData md{};
        md.materials.push_back({ "" });

        Model::MeshData mesh{};
        mesh.materialIndex = 0;
        mesh.vertices = vertices;
        mesh.skinned = false;
        mesh.startVertex = 0;
        mesh.vertexCount = static_cast<uint32_t>(vertices.size());
        mesh.startIndex = 0;
        mesh.indexCount = static_cast<uint32_t>(vertices.size());

        md.meshes.push_back(std::move(mesh));

        md.indices.resize(vertices.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
            md.indices[i] = i;
        }

        md.rootNode.name = "PrimitiveRoot";
        md.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
        md.rootNode.meshIndices.push_back(0);

        return md;
    }

    std::vector<Model::VertexData> MakeFlameVertices() {
        // One continuous silhouette, with its base at the emitter and its tip along +Y.
        constexpr Vector2 outline[] = {
            {-0.25f, 0.00f}, {-0.67f, 0.16f}, {-0.78f, 0.35f},
            {-0.39f, 0.57f}, {-0.33f, 0.73f}, {0.09f, 1.22f},
            {0.28f, 0.76f}, {0.53f, 0.56f}, {0.73f, 0.30f},
            {0.53f, 0.13f}, {0.24f, 0.00f}
        };
        const auto vertex = [](Vector2 p) {
            Model::VertexData result{};
            result.position = {p.x, p.y, 0.0f, 1.0f};
            result.texcoord = {0.5f + p.x * 0.5f, 1.0f - p.y / 1.22f};
            result.normal = {0.0f, 0.0f, -1.0f};
            return result;
        };
        std::vector<Model::VertexData> vertices;
        vertices.reserve(std::size(outline) * 3);
        for (size_t i = 0; i < std::size(outline); ++i) {
            vertices.push_back(vertex({0.0f, 0.31f}));
            vertices.push_back(vertex(outline[i]));
            vertices.push_back(vertex(outline[(i + 1) % std::size(outline)]));
        }
        return vertices;
    }

    std::vector<Model::VertexData> MakeFlameVolumeVertices() {
        // Asymmetric rings give each face a slope and keep the muzzle end narrow.
        const Vector3 base[] = {
            { .25f, 0, 0}, { .10f, 0, .22f}, {-.15f, 0, .20f},
            {-.24f, 0,-.02f}, {-.10f, 0,-.21f}, { .14f, 0,-.19f}
        };
        const Vector3 ridge[] = {
            { .85f,.25f, .02f}, { .43f,.34f, .75f}, {-.53f,.26f, .78f},
            {-.91f,.39f,-.05f}, {-.35f,.25f,-.77f}, { .53f,.37f,-.61f}
        };
        const Vector3 taper[] = {
            { .38f,.62f,-.02f}, { .25f,.61f, .32f}, {-.27f,.71f, .34f},
            {-.38f,.58f,-.02f}, {-.13f,.68f,-.35f}, { .22f,.61f,-.39f}
        };
        const Vector3 tip{.10f, 1.24f, -.10f};
        std::vector<Model::VertexData> vertices;
        vertices.reserve(std::size(base) * 6 * 3);
        const auto triangle = [&vertices](const Vector3& a, const Vector3& b, const Vector3& c) {
            const auto normal = Matrix4x4::Normalize(Matrix4x4::Cross(b - a, c - a));
            for (const auto& p : {a, b, c}) {
                Model::VertexData vertex{};
                vertex.position = {p.x, p.y, p.z, 1.0f};
                vertex.normal = normal;
                vertices.push_back(vertex);
            }
        };
        for (size_t i = 0; i < std::size(base); ++i) {
            const size_t next = (i + 1) % std::size(base);
            triangle({}, base[i], base[next]);
            triangle(base[i], ridge[i], base[next]);
            triangle(base[next], ridge[i], ridge[next]);
            triangle(ridge[i], taper[i], ridge[next]);
            triangle(ridge[next], taper[i], taper[next]);
            triangle(taper[i], tip, taper[next]);
        }
        return vertices;
    }

    Model* GetOrMakeParticlePrimitiveModel(int typeIndex) {
        std::string key = "ParticlePrimitive_" + std::to_string(typeIndex);
        Model* m = ModelManager::GetInstance()->FindModel(key);
        if (m) return m;

        std::vector<Model::VertexData> vertices;
        switch (typeIndex) {
        case 0: vertices = GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f); break;
        case 1: vertices = GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f); break;
        case 2: vertices = GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f); break;
        case 3: vertices = GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f); break;
        case 4: vertices = GeometryGenerator::GenerateTorusTriList(32, 16, 1.0f, 0.3f); break;
        case 5: vertices = GeometryGenerator::GenerateCylinderTriList(32, 1.0f, 2.0f); break;
        case 6: vertices = GeometryGenerator::GenerateConeTriList(32, 1.0f, 2.0f); break;
        case 7: vertices = GeometryGenerator::GenerateTriangleTriListXY(2.0f, 2.0f); break;
        case 8: vertices = GeometryGenerator::GenerateCapsuleTriList(32, 8, 0.65f, 1.4f); break;
        case 9: vertices = GeometryGenerator::GenerateStarTriListXY(1.1f, 0.48f, 5); break;
        case 10: vertices = GeometryGenerator::GenerateDiamondTriListXY(1.6f, 2.2f); break;
        case kFlamePrimitive: vertices = MakeFlameVertices(); break;
        case kFlameVolumePrimitive: vertices = MakeFlameVolumeVertices(); break;
        case kFlameGlowPrimitive:
            // Bounds for the local flame profile in Particle.PS.hlsl.
            vertices = GeometryGenerator::GenerateBoxTriList(2.2f, 1.6f, 2.2f);
            for (auto& vertex : vertices) {
                vertex.position.y += 0.7f;
            }
            for (size_t i = 0; i < vertices.size(); i += 3) {
                std::swap(vertices[i + 1], vertices[i + 2]);
            }
            break;
        case kSparkStreakPrimitive:
        case kSoftDiscPrimitive:
        case kProceduralSmokePrimitive:
            vertices = GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f);
            break;
        default: return nullptr;
        }

        auto modelData = MakeParticlePrimitiveModelData(vertices);
        return ModelManager::GetInstance()->CreatePrimitiveModel(key, modelData);
    }
}

void ParticleManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, ParticleCommon* particleCommon) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    particleCommon_ = particleCommon;
    // The texture slot stays valid for untextured groups without allocating or sampling a texture.
    nullTextureSrvIndex_ = srvManager_->Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC nullTexture{};
    nullTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullTexture.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullTexture.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullTexture.Texture2D.MipLevels = 1;
    dxCommon_->GetDevice()->CreateShaderResourceView(nullptr, &nullTexture,
        srvManager_->GetCPUDescriptionHandle(nullTextureSrvIndex_));
    // 繝ｩ繝ｳ繝繝蛻晄悄蛹・
    std::random_device rd;
    randomEngine_ = std::mt19937(rd());

    // 鬆らせ驟榊・遒ｺ菫・
    vertices_.resize(kVertexCount);

    // 蟾ｦ荳・0) 蜿ｳ荳・1) 蜿ｳ荳・2) / 蟾ｦ荳・0) 蜿ｳ荳・2) 蟾ｦ荳・3)
    auto setV = [&](int i, float x, float y, float u, float v) {
        vertices_[i].position[0] = x;
        vertices_[i].position[1] = y;
        vertices_[i].position[2] = 0.0f;
        vertices_[i].position[3] = 1.0f;
        vertices_[i].uv[0] = u;
        vertices_[i].uv[1] = v;
        vertices_[i].normal[0] = 0.0f;
        vertices_[i].normal[1] = 0.0f;
        vertices_[i].normal[2] = -1.0f;
        };

    // 繧ｵ繧､繧ｺ縺ｯ螂ｽ縺ｿ縲ゅ∪縺夊ｦ九∴繧狗｢ｺ隱阪↑繧牙､ｧ縺阪ａ縺ｧOK
    const float s = 0.5f;
    setV(0, -s, +s, 0.0f, 0.0f);
    setV(1, +s, +s, 1.0f, 0.0f);
    setV(2, +s, -s, 1.0f, 1.0f);
    setV(3, -s, +s, 0.0f, 0.0f);
    setV(4, +s, -s, 1.0f, 1.0f);
    setV(5, -s, -s, 0.0f, 1.0f);

    // bufferSize 繧・kVertexCount 蛻・↓縺吶ｋ
    const UINT bufferSize = sizeof(ParticleVertex) * kVertexCount;


    vertexBuffer_ = dxCommon_->CreateBufferResource(bufferSize);

    // VBV
    vbView_.BufferLocation =
        vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = bufferSize;
    vbView_.StrideInBytes = sizeof(ParticleVertex);

    // GPU縺ｸ譖ｸ縺崎ｾｼ縺ｿ
    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, vertices_.data(), bufferSize);
    vertexBuffer_->Unmap(0, nullptr);

    // PerView 繝ｪ繧ｽ繝ｼ繧ｹ菴懈・
    perViewResource_ = dxCommon_->CreateBufferResource(sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedPerView_));
    // 蛻晄悄蛟､
    mappedPerView_->viewProjection = Matrix4x4::MakeIdentity4x4();
    mappedPerView_->billboardMatrix = Matrix4x4::MakeIdentity4x4();
    mappedPerView_->projection = Matrix4x4::MakeIdentity4x4();
    mappedPerView_->cameraPosition = {};

    // PerFrame 繝ｪ繧ｽ繝ｼ繧ｹ菴懈・
    perFrameResource_ = dxCommon_->CreateBufferResource(sizeof(PerFrame));
    perFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedPerFrame_));
    mappedPerFrame_->time = 0.0f;
    mappedPerFrame_->deltaTime = 0.0f;
    time_ = 0.0f;

    // --- 繝繝溘・繝槭ユ繝ｪ繧｢繝ｫ・・Λ繧､繝・---
    // HLSL縺ｮ struct Material { float4 color; int enableLighting; float4x4 uvTransform; } (繧ｵ繧､繧ｺ: 16+4+64 = 84 -> 繧｢繝ｩ繧､繝｡繝ｳ繝郁・・縺ｧ256縺ｮ蛟肴焚)
    materialResource_ = dxCommon_->CreateBufferResource(256);
    struct DummyMaterial { Vector4 color; int enableLighting; float padding[3]; Matrix4x4 uvTransform; };
    DummyMaterial* mappedMat = nullptr;
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedMat));
    mappedMat->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mappedMat->enableLighting = 0;
    mappedMat->uvTransform = Matrix4x4::MakeIdentity4x4();

    // HLSL縺ｮ struct DirectionalLight { float4 color; float3 direction; float intensity; }
    dirLightResource_ = dxCommon_->CreateBufferResource(256);
    struct DummyLight { Vector4 color; Vector3 direction; float intensity; };
    DummyLight* mappedLight = nullptr;
    dirLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedLight));
    mappedLight->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mappedLight->direction = { 0.0f, -1.0f, 0.0f };
    mappedLight->intensity = 1.0f;
}

void ParticleManager::Finalize() {
    RetireAllGroups_();
    retiredParticleGroups_.clear();
    vertexBuffer_.Reset();
    if (nullTextureSrvIndex_ != 0) {
        srvManager_->Retire(nullTextureSrvIndex_);
        nullTextureSrvIndex_ = 0;
    }
    emissionUpload_.Reset();
    mappedEmissionUpload_ = nullptr;
    emissionCapacity_ = 0;
}

void ParticleManager::ClearGroups()
{
    RetireAllGroups_();
}

void ParticleManager::RemoveGroup(const std::string& groupName)
{
    RetireGroup_(groupName);
}

void ParticleManager::RetireAllGroups_()
{
    if (particleGroups_.empty()) {
        return;
    }

    RetiredParticleGroups retired;
    retired.groups = std::move(particleGroups_);
    retired.framesRemaining = kRetiredParticleGroupKeepFrames;
    retiredParticleGroups_.push_back(std::move(retired));
    particleGroups_.clear();
    editorSelectedGroupName_.clear();
}

void ParticleManager::RetireGroup_(const std::string& groupName)
{
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) {
        return;
    }

    RetiredParticleGroups retired;
    retired.groups.emplace(groupName, std::move(it->second));
    retired.framesRemaining = kRetiredParticleGroupKeepFrames;
    retiredParticleGroups_.push_back(std::move(retired));
    particleGroups_.erase(it);

    if (editorSelectedGroupName_ == groupName) {
        editorSelectedGroupName_.clear();
    }
}

void ParticleManager::CollectRetiredGroups_()
{
    for (auto& retired : retiredParticleGroups_) {
        --retired.framesRemaining;
    }
    retiredParticleGroups_.erase(
        std::remove_if(
            retiredParticleGroups_.begin(),
            retiredParticleGroups_.end(),
            [](const RetiredParticleGroups& retired) {
                return retired.framesRemaining <= 0;
            }),
        retiredParticleGroups_.end());
}

void ParticleManager::Update(float dt, const Camera& camera)
{
    CollectRetiredGroups_();

    // 笘・螳溘き繝｡繝ｩ縺九ｉ蜿門ｾ・
    const Matrix4x4& vp = camera.GetViewProjectionMatrix();
    const Matrix4x4& cameraMatrix = camera.GetWorldMatrix();

    Matrix4x4 billboardMatrix = cameraMatrix;
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;

    // PerView 縺ｮ譖ｴ譁ｰ
    if (mappedPerView_) {
        mappedPerView_->viewProjection = vp;
        mappedPerView_->billboardMatrix = billboardMatrix;
        mappedPerView_->projection = camera.GetProjectionMatrix();
        const auto& eye = camera.GetTranslate();
        mappedPerView_->cameraPosition = {eye.x, eye.y, eye.z, 0.0f};
    }

    // PerFrame 縺ｮ譖ｴ譁ｰ
    time_ += dt;
    if (mappedPerFrame_) {
        mappedPerFrame_->time = time_;
        mappedPerFrame_->deltaTime = dt;
    }

    // Emitter縺ｮ譖ｴ譁ｰ
    for (auto& [name, group] : particleGroups_) {
        if (group.mappedEmitter) {
            if (group.isAutoEmit) {
                group.mappedEmitter->frequencyTime += dt; // ﾎｴ繧ｿ繧､繝繧貞刈邂・
                // 蟆・・髢馴囈繧剃ｸ雁屓縺｣縺溘ｉ蟆・・險ｱ蜿ｯ繧貞・縺励※譎る俣繧定ｪｿ謨ｴ
                if (group.mappedEmitter->frequency <= group.mappedEmitter->frequencyTime) {
                    group.mappedEmitter->frequencyTime -= group.mappedEmitter->frequency;
                    group.mappedEmitter->emit = 1;
                } else {
                    group.mappedEmitter->emit = 0;
                }
            } else {
                // 閾ｪ蜍慕匱逕欅FF縺ｮ蝣ｴ蜷・
                if (group.isEmitRequested) {
                    group.mappedEmitter->emit = 1;
                    group.isEmitRequested = false; // 1繝輔Ξ繝ｼ繝縺縺・縺ｫ縺吶ｋ
                } else {
                    group.mappedEmitter->emit = 0;
                }
            }
        }
    }
}


void ParticleManager::Emit(const std::string& groupName,
    const Vector3& pos,
    uint32_t count,
    float timeScale,
    float initialAge)
{
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;

    ParticleGroup& group = it->second;
    if (group.mappedEmitter) {
        group.mappedEmitter->translate = pos;
        group.mappedEmitter->count = count;
        group.mappedEmitter->timeScale = timeScale;
        group.mappedEmitter->initialAge = initialAge;
        group.isAutoEmit = false;
        group.isEmitRequested = true;
        // タイムスケールで最大寿命を割って正確なアクティブ残り時間を設定
        float effectiveLife = group.mappedEmitter->lifeTimeMax / std::max(0.001f, timeScale);
        group.activeTimeRemaining = std::max(group.activeTimeRemaining, effectiveLife + 0.5f);
        return;
    }

    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    std::uniform_real_distribution<float> c(0.0f, 1.0f);
    std::uniform_real_distribution<float> t(1.0f, 3.0f);

    for (uint32_t i = 0; i < count; ++i) {
        Particle::ParticleData p{};
        p.transform.scale = { 1,1,1 };
        p.transform.rotate = { 0,0,0 };
        p.transform.translate = {
            pos.x + d(randomEngine_),
            pos.y + d(randomEngine_),
            pos.z + d(randomEngine_)
        };
        p.velocity = { d(randomEngine_), d(randomEngine_), d(randomEngine_) };
        p.color = { c(randomEngine_), c(randomEngine_), c(randomEngine_), 1.0f };
        p.lifeTime = t(randomEngine_) / std::max(0.001f, timeScale); // timeScaleを考慮
        p.currentTime = initialAge; // initialAgeからスタート
        group.particles.push_back(p);
    }
    group.activeTimeRemaining = std::max(group.activeTimeRemaining, 3.5f / std::max(0.001f, timeScale));
}

void ParticleManager::EmitConfigured(const std::string& groupName, const Vector3& pos, float timeScale, float initialAge)
{
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) {
        return;
    }

    uint32_t count = 1;
    if (it->second.mappedEmitter) {
        count = std::max<uint32_t>(1, it->second.mappedEmitter->count);
    }

    Emit(groupName, pos, count, timeScale, initialAge);
}

void ParticleManager::EmitDirectedFromFile(const std::string& filename, const Vector3& position,
    const Vector3& direction, const Camera& camera, float countScale) {
    if (!std::isfinite(countScale) || countScale <= 0.0f) return;
    const auto& view = camera.GetViewMatrix();
    const Vector3 cameraPosition = TransformDirection(position - camera.GetTranslate(), view);
    const Vector3 cameraDirection = TransformDirection(direction, view);
    // Matching seeds keep the core and shell aligned for each shot.
    const uint32_t emissionSeed = randomEngine_() | 1u;
    for (auto& [name, group] : particleGroups_) {
        if (group.fromFile != filename || !group.mappedEmitter || group.mappedEmitter->count == 0) {
            continue;
        }
        EmitterData emission = *group.mappedEmitter;
        emission.emit = 1;
        emission.count = static_cast<uint32_t>(std::min(
            std::round(emission.count * countScale), static_cast<float>(kMaxInstance)));
        if (emission.count == 0) continue;
        emission.translate = group.cameraSpace ? cameraPosition : position;
        emission.velocityBase = group.cameraSpace ? cameraDirection : direction;
        emission.shapeType = 1; // Cone
        emission.initialAge = 0.0f;
        emission.randomSeed = emissionSeed;
        group.pendingEmissions.push_back(emission);
    }
}

void ParticleManager::CreateParticleGroup(
    const std::string& name,
    const std::string& texturePath)
{
    if (particleGroups_.contains(name)) {
        return;
    }

    ParticleGroup group{};
    group.texturePath = texturePath;
    group.modelType = 0;
    group.modelName = "";

    // --- texture ---
    if (!texturePath.empty()) {
        TextureManager::GetInstance()->LoadTexture(texturePath);
        group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texturePath);
    }

    // --- instancing buffer (DEFAULT heap + UAV) ---
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = sizeof(Particles) * kMaxInstance;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.instancingResource));
    assert(SUCCEEDED(hr));

    // --- instancing SRV & UAV ---
    group.instancingSrvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        group.instancingSrvIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particles)
    );

    group.instancingUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.instancingUavIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particles)
    );

    // --- FreeListIndex buffer ---
    D3D12_RESOURCE_DESC counterDesc = resDesc;
    counterDesc.Width = sizeof(int32_t); // 1 element
    
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &counterDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.freeListIndexResource));
    assert(SUCCEEDED(hr));

    group.freeListIndexUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListIndexUavIndex,
        group.freeListIndexResource.Get(),
        1,
        sizeof(int32_t)
    );

    // --- FreeList buffer ---
    D3D12_RESOURCE_DESC listDesc = resDesc;
    listDesc.Width = sizeof(uint32_t) * kMaxInstance;
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &listDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&group.freeListResource));
    assert(SUCCEEDED(hr));

    group.freeListUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListUavIndex,
        group.freeListResource.Get(),
        kMaxInstance,
        sizeof(uint32_t)
    );

    // --- Compute Shader縺ｧ蛻晄悄蛹・(Dispatch) ---
    auto* cmd = dxCommon_->GetComputeCommandList();

    // barrier: COMMON -> UAV
    D3D12_RESOURCE_BARRIER barriers[3]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = group.instancingResource.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = group.freeListIndexResource.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = group.freeListResource.Get();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(3, barriers);

    particleCommon_->SetComputePipelineState(cmd);
    
    // 笘・ｿｽ蜉: DescriptorHeap繧偵さ繝槭Φ繝峨Μ繧ｹ繝医↓繧ｻ繝・ヨ縺吶ｋ
    srvManager_->PreDrawCompute(cmd);

    // UAV繧ｻ繝・ヨ (Register u0, u1, u2)
    cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
    cmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
    cmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));

    cmd->Dispatch(1, 1, 1);

    // barrier: UAV -> SRV (NON_PIXEL_SHADER_RESOURCE)
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = group.instancingResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    // --- Emitter Buffer ---
    group.emitterResource = dxCommon_->CreateBufferResource(sizeof(EmitterData));
    group.emitterResource->Map(0, nullptr, reinterpret_cast<void**>(&group.mappedEmitter));
    *group.mappedEmitter = {};
    group.mappedEmitter->count = 10;
    group.mappedEmitter->frequency = 0.5f;
    group.mappedEmitter->frequencyTime = 0.0f;
    group.mappedEmitter->translate = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->radius = 5.0f;
    group.mappedEmitter->emit = 0;

    // 諡｡蠑ｵ繝代Λ繝｡繝ｼ繧ｿ縺ｮ蛻晄悄蛹・
    group.mappedEmitter->lifeTimeMin = 0.5f;
    group.mappedEmitter->lifeTimeMax = 2.0f;
    group.mappedEmitter->velocityBase = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->velocityVariance = 0.1f;
    group.mappedEmitter->startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    group.mappedEmitter->endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    group.mappedEmitter->scaleStart = { 0.25f, 0.25f, 0.25f };
    group.mappedEmitter->scaleEnd = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->speedMin = 0.03f;
    group.mappedEmitter->speedMax = 0.12f;
    group.mappedEmitter->angleRandomDeg = 360.0f;
    group.mappedEmitter->jitterDeg = 0.0f;
    group.mappedEmitter->rotationStartDeg = 0.0f;
    group.mappedEmitter->rotationRandomDeg = 360.0f;
    group.mappedEmitter->angularVelocityMinDeg = 0.0f;
    group.mappedEmitter->angularVelocityMaxDeg = 0.0f;
    group.mappedEmitter->timeScale = 1.0f;
    group.mappedEmitter->initialAge = 0.0f;

    group.mappedEmitter->shapeType = 0; // 0:Sphere
    group.mappedEmitter->shapeAngle = 0.5f; // Cone逕ｨ
    group.mappedEmitter->shapeSize = { 5.0f, 5.0f, 5.0f }; // Box逕ｨ
    group.mappedEmitter->acceleration = { 0.0f, 0.0f, 0.0f }; // 驥榊鴨縺ｪ縺ｩ

    group.billboardMode = 0; // 0:Billboard

    particleGroups_.emplace(name, std::move(group));
}

void ParticleManager::CreateParticleGroup(
    const std::string& name,
    Model* model)
{
    if (particleGroups_.contains(name)) {
        return;
    }
    assert(model != nullptr);

    ParticleGroup group{};
    group.model = model;
    group.modelType = 2; // 繝・ヵ繧ｩ繝ｫ繝医・File縺ｨ縺励※謇ｱ縺・ｼ郁ｩｳ邏ｰ縺ｯ蜻ｼ縺ｳ蜃ｺ縺怜・萓晏ｭ假ｼ・
    group.modelName = ""; // 蛻晄悄蛟､

    // --- texture ---
    const auto& materials = model->GetMaterials();
    if (!materials.empty() && !materials[0].textureFilePath.empty()) {
        const std::string& texPath = materials[0].textureFilePath;
        if (!TextureManager::GetInstance()->HasTexture(texPath)) {
            TextureManager::GetInstance()->LoadTexture(texPath);
        }
        group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texPath);
        group.texturePath = texPath;
    } else {
        // 遨ｺ繝代せ縺ｮ縺ｨ縺阪・ TextureManager 縺ｫ逋ｽ繝・け繧ｹ繝√Ε遲峨′縺ゅｋ縺ｪ繧峨◎繧後ｒ菴ｿ縺・° 0 逡ｪ縺ｫ縺吶ｋ
        group.textureSrvIndex = 0; 
        group.texturePath = "";
    }

    // --- instancing buffer (DEFAULT heap + UAV) ---
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = sizeof(Particles) * kMaxInstance;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.instancingResource));
    assert(SUCCEEDED(hr));

    // --- instancing SRV & UAV ---
    group.instancingSrvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        group.instancingSrvIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particles)
    );

    group.instancingUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.instancingUavIndex,
        group.instancingResource.Get(),
        kMaxInstance,
        sizeof(Particles)
    );

    // --- FreeListIndex buffer ---
    D3D12_RESOURCE_DESC counterDesc = resDesc;
    counterDesc.Width = sizeof(int32_t); // 1 element
    
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &counterDesc, 
        D3D12_RESOURCE_STATE_COMMON, nullptr, 
        IID_PPV_ARGS(&group.freeListIndexResource));
    assert(SUCCEEDED(hr));

    group.freeListIndexUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListIndexUavIndex,
        group.freeListIndexResource.Get(),
        1,
        sizeof(int32_t)
    );

    // --- FreeList buffer ---
    D3D12_RESOURCE_DESC listDesc = resDesc;
    listDesc.Width = sizeof(uint32_t) * kMaxInstance;
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &listDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&group.freeListResource));
    assert(SUCCEEDED(hr));

    group.freeListUavIndex = srvManager_->Allocate();
    srvManager_->CreateUAVforStructuredBuffer(
        group.freeListUavIndex,
        group.freeListResource.Get(),
        kMaxInstance,
        sizeof(uint32_t)
    );

    // --- Compute Shader縺ｧ蛻晄悄蛹・(Dispatch) ---
    auto* computeCmd = dxCommon_->GetComputeCommandList();

    D3D12_RESOURCE_BARRIER barriers[3]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = group.instancingResource.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = group.freeListIndexResource.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = group.freeListResource.Get();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    computeCmd->ResourceBarrier(3, barriers);

    particleCommon_->SetComputePipelineState(computeCmd);
    
    srvManager_->PreDrawCompute(computeCmd);

    computeCmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
    computeCmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
    computeCmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));

    computeCmd->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = group.instancingResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    computeCmd->ResourceBarrier(1, &barrier);

    // --- Emitter Buffer ---
    // HLSL縺ｮ讒矩菴薙↓蜷医ｏ縺帙※繧ｵ繧､繧ｺ繧堤｢ｺ菫・
    group.emitterResource = dxCommon_->CreateBufferResource(sizeof(EmitterData));
    group.emitterResource->Map(0, nullptr, reinterpret_cast<void**>(&group.mappedEmitter));
    *group.mappedEmitter = {};
    group.mappedEmitter->count = 10;
    group.mappedEmitter->frequency = 0.5f;
    group.mappedEmitter->frequencyTime = 0.0f;
    group.mappedEmitter->translate = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->radius = 5.0f;
    group.mappedEmitter->emit = 0;

    // 諡｡蠑ｵ繝代Λ繝｡繝ｼ繧ｿ縺ｮ蛻晄悄蛹・
    group.mappedEmitter->lifeTimeMin = 0.5f;
    group.mappedEmitter->lifeTimeMax = 2.0f;
    group.mappedEmitter->velocityBase = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->velocityVariance = 0.1f;
    group.mappedEmitter->startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    group.mappedEmitter->endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    group.mappedEmitter->scaleStart = { 0.25f, 0.25f, 0.25f };
    group.mappedEmitter->scaleEnd = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->speedMin = 0.03f;
    group.mappedEmitter->speedMax = 0.12f;
    group.mappedEmitter->angleRandomDeg = 360.0f;
    group.mappedEmitter->jitterDeg = 0.0f;
    group.mappedEmitter->rotationStartDeg = 0.0f;
    group.mappedEmitter->rotationRandomDeg = 360.0f;
    group.mappedEmitter->angularVelocityMinDeg = 0.0f;
    group.mappedEmitter->angularVelocityMaxDeg = 0.0f;
    group.mappedEmitter->timeScale = 1.0f;
    group.mappedEmitter->initialAge = 0.0f;

    group.mappedEmitter->shapeType = 0; // 0:Sphere
    group.mappedEmitter->shapeAngle = 0.5f; // Cone逕ｨ
    group.mappedEmitter->shapeSize = { 5.0f, 5.0f, 5.0f }; // Box逕ｨ
    group.mappedEmitter->acceleration = { 0.0f, 0.0f, 0.0f }; // 驥榊鴨縺ｪ縺ｩ

    group.billboardMode = 0; // 0:Billboard

    particleGroups_.emplace(name, std::move(group));
}

void ParticleManager::SetGroupBlendMode(const std::string& groupName, ParticleCommon::BlendMode mode) {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;
    it->second.blendMode = mode;
}

bool ParticleManager::HasGroup(const std::string& groupName) const {
    return particleGroups_.find(groupName) != particleGroups_.end();
}

void ParticleManager::SetEditorSelectedGroupName(const std::string& groupName) {
    editorSelectedGroupName_ = groupName;
}

std::vector<std::string> ParticleManager::GetGroupNames() const {
    std::vector<std::string> names;
    names.reserve(particleGroups_.size());
    for (const auto& [name, group] : particleGroups_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool ParticleManager::HasPostEffectTargets() const {
    for (const auto& [name, group] : particleGroups_) {
        const bool active = group.IsActive();
        if (active && (group.bloomPostEffect || group.outlineBloomPostEffect)) {
            return true;
        }
    }
    return false;
}

Vector4 ParticleManager::GetPrimaryPostEffectBloomColor() const {
    // 1. エディタ選択中のグループがあれば最優先（アクティブ判定無視、エフェクト有効のみチェック）
    if (!editorSelectedGroupName_.empty()) {
        auto it = particleGroups_.find(editorSelectedGroupName_);
        if (it != particleGroups_.end()) {
            const auto& group = it->second;
            if (group.bloomPostEffect) {
                return group.bloomColor;
            }
        }
    }

    // 2. アクティブなグループから検索
    for (const auto& [name, group] : particleGroups_) {
        const bool active = group.IsActive();
        if (active && group.bloomPostEffect) {
            return group.bloomColor;
        }
    }

    // 3. 非アクティブでもブルーム設定があるグループがあれば返す
    for (const auto& [name, group] : particleGroups_) {
        if (group.bloomPostEffect) {
            return group.bloomColor;
        }
    }

    return { 1.0f, 0.72f, 0.22f, 1.0f };
}

Vector4 ParticleManager::GetPrimaryPostEffectOutlineBloomColor() const {
    // 1. エディタ選択中のグループがあれば最優先
    if (!editorSelectedGroupName_.empty()) {
        auto it = particleGroups_.find(editorSelectedGroupName_);
        if (it != particleGroups_.end()) {
            const auto& group = it->second;
            if (group.outlineBloomPostEffect) {
                return group.outlineBloomColor;
            }
        }
    }

    // 2. アクティブなグループから検索
    for (const auto& [name, group] : particleGroups_) {
        const bool active = group.IsActive();
        if (active && group.outlineBloomPostEffect) {
            return group.outlineBloomColor;
        }
    }

    // 3. 非アクティブでもブルーム設定があるグループがあれば返す
    for (const auto& [name, group] : particleGroups_) {
        if (group.outlineBloomPostEffect) {
            return group.outlineBloomColor;
        }
    }

    return { 1.0f, 0.22f, 0.22f, 1.0f };
}

PostEffectMode ParticleManager::GetPrimaryPostEffectMode() const {
    // 1. エディタ選択中のグループがあれば最優先
    if (!editorSelectedGroupName_.empty()) {
        auto it = particleGroups_.find(editorSelectedGroupName_);
        if (it != particleGroups_.end()) {
            const auto& group = it->second;
            if (group.outlineBloomPostEffect) return PostEffectMode::OutlineBloom;
            if (group.bloomPostEffect) return PostEffectMode::BoxFilter;
        }
    }

    // 2. アクティブなグループから検索
    for (const auto& [name, group] : particleGroups_) {
        const bool active = group.IsActive();
        if (active) {
            if (group.outlineBloomPostEffect) return PostEffectMode::OutlineBloom;
            if (group.bloomPostEffect) return PostEffectMode::BoxFilter;
        }
    }

    // 3. 非アクティブでもブルーム設定があるグループがあれば返す
    for (const auto& [name, group] : particleGroups_) {
        if (group.outlineBloomPostEffect) return PostEffectMode::OutlineBloom;
        if (group.bloomPostEffect) return PostEffectMode::BoxFilter;
    }

    return PostEffectMode::FullScreen;
}

void ParticleManager::ConfigureHitEffectPreset(const std::string& groupName) {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;

    ParticleGroup& group = it->second;
    group.blendMode = ParticleCommon::BlendMode::kBlendModeAdd;
    group.postEffectMode = PostEffectMode::OutlineBloom;
    group.depthTestEnabled = false;
    group.isAutoEmit = false;
    group.billboardMode = 0;

    if (!group.mappedEmitter) return;

    group.mappedEmitter->count = 24;
    group.mappedEmitter->frequency = 0.05f;
    group.mappedEmitter->frequencyTime = 0.0f;
    group.mappedEmitter->radius = 0.45f;
    group.mappedEmitter->emit = 0;
    group.mappedEmitter->lifeTimeMin = 0.12f;
    group.mappedEmitter->lifeTimeMax = 0.28f;
    group.mappedEmitter->velocityBase = { 0.0f, 0.03f, 0.0f };
    group.mappedEmitter->velocityVariance = 0.22f;
    group.mappedEmitter->speedMin = 0.04f;
    group.mappedEmitter->speedMax = 0.20f;
    group.mappedEmitter->shapeType = 0;
    group.mappedEmitter->shapeAngle = 0.5f;
    group.mappedEmitter->shapeSize = { 0.35f, 0.35f, 0.35f };
    group.mappedEmitter->acceleration = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->startColor = { 1.0f, 0.85f, 0.25f, 1.0f };
    group.mappedEmitter->endColor = { 1.0f, 0.15f, 0.02f, 0.0f };
    group.mappedEmitter->scaleStart = { 0.45f, 0.45f, 0.45f };
    group.mappedEmitter->scaleEnd = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->angleRandomDeg = 360.0f;
    group.mappedEmitter->jitterDeg = 0.0f;
    group.mappedEmitter->rotationStartDeg = 0.0f;
    group.mappedEmitter->rotationRandomDeg = 360.0f;
    group.mappedEmitter->angularVelocityMinDeg = -360.0f;
    group.mappedEmitter->angularVelocityMaxDeg = 360.0f;
}

void ParticleManager::ConfigureTrailPreset(const std::string& groupName) {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) return;

    ParticleGroup& group = it->second;
    group.blendMode = ParticleCommon::BlendMode::kBlendModeAdd;
    group.postEffectMode = PostEffectMode::FullScreen;
    group.depthTestEnabled = false;
    group.isAutoEmit = false;
    group.billboardMode = 0;

    if (!group.mappedEmitter) return;

    group.mappedEmitter->count = 1;
    group.mappedEmitter->frequency = 0.0f;
    group.mappedEmitter->frequencyTime = 0.0f;
    group.mappedEmitter->radius = 0.0f; // 射出半径を0にして線上に綺麗に配置する
    group.mappedEmitter->emit = 0;
    group.mappedEmitter->lifeTimeMin = 0.30f; // 寿命を少し短めにしてスタイリッシュにフェード
    group.mappedEmitter->lifeTimeMax = 0.50f;
    group.mappedEmitter->velocityBase = { 0.0f, 0.0f, 0.0f }; // 速度を完全にゼロにしてその場に固定
    group.mappedEmitter->velocityVariance = 0.0f;
    group.mappedEmitter->speedMin = 0.0f;
    group.mappedEmitter->speedMax = 0.0f;
    group.mappedEmitter->shapeType = 0;
    group.mappedEmitter->shapeAngle = 0.0f;
    group.mappedEmitter->shapeSize = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->acceleration = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->startColor = { 1.0f, 0.95f, 0.45f, 1.0f }; // 美しい光の加算
    group.mappedEmitter->endColor = { 1.0f, 0.25f, 0.05f, 0.0f };
    group.mappedEmitter->scaleStart = { 0.45f, 0.45f, 0.45f }; // 適度な太さ
    group.mappedEmitter->scaleEnd = { 0.0f, 0.0f, 0.0f };
    group.mappedEmitter->angleRandomDeg = 0.0f;
    group.mappedEmitter->jitterDeg = 0.0f;
    group.mappedEmitter->rotationStartDeg = 0.0f;
    group.mappedEmitter->rotationRandomDeg = 0.0f;
    group.mappedEmitter->angularVelocityMinDeg = 0.0f;
    group.mappedEmitter->angularVelocityMaxDeg = 0.0f;
}


void ParticleManager::UpdateCompute(ID3D12GraphicsCommandList* computeCmd) {
    size_t emissionCount = 0;
    for (const auto& [name, group] : particleGroups_) {
        emissionCount += group.pendingEmissions.size();
    }

    // PostDraw waits for the GPU before this upload buffer is reused next frame.
    if (emissionCount > emissionCapacity_) {
        emissionCapacity_ = std::max(emissionCount, emissionCapacity_ * 2);
        emissionUpload_ = dxCommon_->CreateBufferResource(emissionCapacity_ * kEmitterStride);
        emissionUpload_->Map(0, nullptr, reinterpret_cast<void**>(&mappedEmissionUpload_));
    }
    size_t uploadOffset = 0;
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;

    for (auto& [name, group] : particleGroups_) {
        if (!group.IsActive()) {
            continue;
        }

        D3D12_RESOURCE_BARRIER transition{};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = group.instancingResource.Get();
        transition.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        computeCmd->ResourceBarrier(1, &transition);
        // Synchronize the free list and its counter as well as the particle buffer.
        computeCmd->ResourceBarrier(1, &uavBarrier);

        particleCommon_->SetEmitComputePipelineState(computeCmd);
        srvManager_->PreDrawCompute(computeCmd);
        computeCmd->SetComputeRootConstantBufferView(3, group.emitterResource->GetGPUVirtualAddress());
        computeCmd->SetComputeRootConstantBufferView(4, perFrameResource_->GetGPUVirtualAddress());
        computeCmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
        computeCmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
        computeCmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));

        if (group.mappedEmitter->emit != 0) {
            computeCmd->Dispatch(1, 1, 1);
            computeCmd->ResourceBarrier(1, &uavBarrier);
        }

        particleCommon_->SetUpdateComputePipelineState(computeCmd);
        computeCmd->Dispatch(1, 1, 1);
        group.activeTimeRemaining = std::max(0.0f, group.activeTimeRemaining - mappedPerFrame_->deltaTime);

        if (!group.pendingEmissions.empty()) {
            // Give every accepted shot its own constants and a full first frame.
            particleCommon_->SetEmitComputePipelineState(computeCmd);
            for (const EmitterData& emission : group.pendingEmissions) {
                computeCmd->ResourceBarrier(1, &uavBarrier);
                std::memcpy(mappedEmissionUpload_ + uploadOffset, &emission, sizeof(emission));
                computeCmd->SetComputeRootConstantBufferView(3, emissionUpload_->GetGPUVirtualAddress() + uploadOffset);
                computeCmd->Dispatch(1, 1, 1);
                uploadOffset += kEmitterStride;
                const float life = std::max({0.001f, emission.lifeTimeMin, emission.lifeTimeMax});
                const float duration = life / std::max(0.001f, emission.timeScale) + 0.001f;
                group.activeTimeRemaining = std::max(group.activeTimeRemaining, duration);
            }
            group.pendingEmissions.clear();
        }

        std::swap(transition.Transition.StateBefore, transition.Transition.StateAfter);
        computeCmd->ResourceBarrier(1, &transition);
    }
}

void ParticleManager::Draw(ID3D12GraphicsCommandList* cmd) {
    Draw(cmd, false);
}

void ParticleManager::Draw(ID3D12GraphicsCommandList* cmd, bool drawPostEffectTargets) {
    const bool hasPostEffectTargets = HasPostEffectTargets();
    drawGroups_.clear();
    for (const auto& [name, group] : particleGroups_) {
        if (!group.IsActive()) {
            continue;
        }
        const bool isPostEffectTarget = group.bloomPostEffect || group.outlineBloomPostEffect;
        if (drawPostEffectTargets && !isPostEffectTarget) {
            continue;
        }
        if (!drawPostEffectTargets && isPostEffectTarget && hasPostEffectTargets) {
            continue;
        }
        drawGroups_.push_back(&group);
    }
    std::stable_sort(drawGroups_.begin(), drawGroups_.end(), [](const ParticleGroup* a, const ParticleGroup* b) {
        return a->drawOrder < b->drawOrder;
    });

    for (const auto* drawGroup : drawGroups_) {
        const auto& group = *drawGroup;
        const bool proceduralSmoke = IsProceduralSmoke(group);
        const bool muzzleGlow = group.fromFile == "muzzle_flash.json" &&
            group.modelType == 1 && group.modelName == kSoftDiscModelName;
        // --- Graphics 縺ｫ繧医ｋ謠冗判 ---
        // 笘・繝悶Ξ繝ｳ繝牙・譖ｿ・・SO蛻・崛・・
        if (particleCommon_) {
            particleCommon_->SetBlendMode(group.blendMode);
            particleCommon_->SetDepthTestEnabled(group.depthTestEnabled);
            particleCommon_->SetGraphicsPipelineState(cmd, proceduralSmoke, muzzleGlow);
        }

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // RootParameter 0 (b0) 縺ｫ Material
        cmd->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
        // RootParameter 3 (b1) 縺ｫ DirectionalLight
        cmd->SetGraphicsRootConstantBufferView(3, dirLightResource_->GetGPUVirtualAddress());
        // PerView(CBV) 繧・RootParameter 4 縺ｫ繧ｻ繝・ヨ (b0)
        cmd->SetGraphicsRootConstantBufferView(4, perViewResource_->GetGPUVirtualAddress());
        // BillboardMode (RootConstants 32bit) 繧・RootParameter 5 縺ｫ繧ｻ繝・ヨ (b1)
        const bool textured = !proceduralSmoke && !group.texturePath.empty();
        const bool emissive = group.blendMode == ParticleCommon::BlendMode::kBlendModeEmissive;
        uint32_t drawFlags = (textured ? UseTexture : 0u) | (emissive ? EmissiveBlend : 0u);
        if (group.modelType == 1) {
            if (group.modelName == kFlameVolumeModelName) drawFlags |= FlameFacets;
            else if (group.modelName == kFlameGlowModelName) drawFlags |= FlameGlow;
            else if (group.modelName == kSparkStreakModelName) drawFlags |= SparkStreak;
            else if (group.modelName == kSoftDiscModelName) drawFlags |= SoftDisc;
        }
        const uint32_t drawSettings[] = {group.billboardMode, group.cameraSpace ? 1u : 0u,
            proceduralSmoke ? static_cast<uint32_t>(group.smokeShape) : drawFlags,
            std::bit_cast<uint32_t>(group.smokeSway), std::bit_cast<uint32_t>(group.smokeNoise)};
        cmd->SetGraphicsRoot32BitConstants(5, _countof(drawSettings), drawSettings, 0);

        srvManager_->SetGraphicsDescriptorTable(2, textured ? group.textureSrvIndex : nullTextureSrvIndex_);
        srvManager_->SetGraphicsDescriptorTable(1, group.instancingSrvIndex);

        if (group.model) {
            auto vbv = group.model->GetVBV();
            // 繝｢繝・Ν縺ｮ隱ｭ縺ｿ霎ｼ縺ｿ縺ｫ螟ｱ謨励＠縺ｦ縺・ｋ遲峨・逅・罰縺ｧ StrideInBytes 縺・0 縺ｮ蝣ｴ蜷医・繧ｼ繝ｭ髯､邂励↓縺ｪ繧九◆繧√せ繧ｭ繝・・
            if (vbv.StrideInBytes == 0) continue;

            cmd->IASetVertexBuffers(0, 1, &vbv);
            
            if (group.model->HasIndexBuffer()) {
                auto ibv = group.model->GetIBV();
                cmd->IASetIndexBuffer(&ibv);
                uint32_t indexCount = ibv.SizeInBytes / sizeof(uint32_t);
                if (indexCount > 0) {
                    cmd->DrawIndexedInstanced(indexCount, kMaxInstance, 0, 0, 0);
                }
            } else {
                uint32_t vertexCount = vbv.SizeInBytes / vbv.StrideInBytes;
                if (vertexCount > 0) {
                    cmd->DrawInstanced(vertexCount, kMaxInstance, 0, 0);
                }
            }
        } else {
            cmd->IASetVertexBuffers(0, 1, &vbView_);
            if (kVertexCount > 0) {
                // 1024蛟九☆縺ｹ縺ｦ謠冗判
                cmd->DrawInstanced(kVertexCount, kMaxInstance, 0, 0);
            }
        }
    }
}

void ParticleManager::ScanResources() {
    modelFiles_.clear();
    textureFiles_.clear();
    particleJsonFiles_.clear();

    // filesystem::path::string() はWindowsのANSIコードページへ変換するため、
    // 日本語を含むリソース名でstd::system_errorを送出する。常にUTF-8で保持する。
    const auto pathToUtf8 = [](const std::filesystem::path& value) {
        const auto utf8 = value.generic_u8string();
        return std::string(utf8.begin(), utf8.end());
    };
    
    const std::filesystem::path targetDir("Resources");
    std::error_code scanError;
    if (!std::filesystem::exists(targetDir, scanError) || scanError) {
        isResourcesScanned_ = true;
        return;
    }

    std::filesystem::path particleDir("Resources/Particles");
    scanError.clear();
    if (std::filesystem::exists(particleDir, scanError) && !scanError) {
        std::filesystem::directory_iterator it(
            particleDir, std::filesystem::directory_options::skip_permission_denied, scanError);
        const std::filesystem::directory_iterator end;
        while (it != end) {
            const auto& entry = *it;
            scanError.clear();
            if (!entry.is_regular_file(scanError) || scanError) {
                it.increment(scanError);
                continue;
            }
            std::string ext = pathToUtf8(entry.path().extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".json") {
                particleJsonFiles_.push_back(pathToUtf8(entry.path().filename()));
            }
            it.increment(scanError);
        }
        std::sort(particleJsonFiles_.begin(), particleJsonFiles_.end());
    }

    scanError.clear();
    std::filesystem::recursive_directory_iterator it(
        targetDir, std::filesystem::directory_options::skip_permission_denied, scanError);
    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        const auto& entry = *it;
        scanError.clear();
        if (!entry.is_regular_file(scanError) || scanError) {
            it.increment(scanError);
            continue;
        }
        
        std::string ext = pathToUtf8(entry.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        std::string path = pathToUtf8(entry.path());
        
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
            ext == ".pmx" || ext == ".pmd") {
            // 繝｢繝・Ν隱ｭ縺ｿ霎ｼ縺ｿ譎ゅ・蜀・Κ縺ｧ "resources/" 縺御ｻ倅ｸ弱＆繧後ｋ縺溘ａ縲∝・鬆ｭ縺ｮ "resources/" 繧貞炎髯､縺吶ｋ
            std::string lowerPath = path;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
            if (lowerPath.starts_with("resources/")) {
                path = path.substr(10);
            }
            modelFiles_.push_back(path);
        } else if (ext == ".png" || ext == ".jpg" || ext == ".dds") {
            textureFiles_.push_back(path);
        }
        it.increment(scanError);
    }
    isResourcesScanned_ = true;
}

void ParticleManager::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Begin("Particle Manager");
    DrawImGuiContents();
    ImGui::End();
#endif
}

void ParticleManager::DrawImGuiContents(const std::string& settingsFile) {
#ifdef USE_IMGUI
    if (!isResourcesScanned_) {
        ScanResources();
    }

    if (settingsFile.empty()) {
        ImGui::InputText("JSON Name", saveFileName_, sizeof(saveFileName_));
        if (ImGui::BeginCombo("Open Existing JSON", saveFileName_)) {
            if (particleJsonFiles_.empty()) {
                ImGui::TextDisabled("No json files in Resources/Particles.");
            }
            for (const std::string& fileName : particleJsonFiles_) {
                const bool selected = fileName == saveFileName_;
                if (ImGui::Selectable(fileName.c_str(), selected)) {
                    std::snprintf(saveFileName_, sizeof(saveFileName_), "%s", fileName.c_str());
                    Load(fileName);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Save Particles")) {
            std::string fileName = NormalizeParticleJsonFileName(saveFileName_);
            std::snprintf(saveFileName_, sizeof(saveFileName_), "%s", fileName.c_str());
            Save(fileName);
            ScanResources();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Particles")) {
            std::string fileName = NormalizeParticleJsonFileName(saveFileName_);
            std::snprintf(saveFileName_, sizeof(saveFileName_), "%s", fileName.c_str());
            Load(fileName);
        }

        ImGui::Separator();

        if (ImGui::Button("Scan Resources")) {
            ScanResources();
        }

        if (!editorSelectedGroupName_.empty() && !HasGroup(editorSelectedGroupName_)) {
            editorSelectedGroupName_.clear();
        }

        if (editorSelectedGroupName_.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Select a particle group in Hierarchy.");
            return;
        }
    } else {
        if (ImGui::Button("Save Particles")) {
            SaveInternal_(settingsFile, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Particles")) {
            ClearParticlesFromFile(settingsFile);
            LoadAdditional(settingsFile, "");
        }
    }

    for (auto& [name, group] : particleGroups_) {
        if (settingsFile.empty() ? name != editorSelectedGroupName_ : group.fromFile != settingsFile) {
            continue;
        }
        if (ImGui::TreeNodeEx(name.c_str(), settingsFile.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            
            // 繝悶Ξ繝ｳ繝峨Δ繝ｼ繝・
            const char* blendModes[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen", "Emissive" };
            int currentBlend = static_cast<int>(group.blendMode);
            if (IsProceduralSmoke(group)) {
                ImGui::TextDisabled("Blend Mode: Normal");
            } else if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                group.blendMode = static_cast<ParticleCommon::BlendMode>(currentBlend);
            }

            bool changed = false;
            if (ImGui::Checkbox("Bloom", &group.bloomPostEffect)) {
                changed = true;
            }
            if (group.bloomPostEffect) {
                ImGui::ColorEdit4("Bloom Color", &group.bloomColor.x);
            }
            if (ImGui::Checkbox("Outline Bloom", &group.outlineBloomPostEffect)) {
                changed = true;
            }
            if (group.outlineBloomPostEffect) {
                ImGui::ColorEdit4("Outline Bloom Color", &group.outlineBloomColor.x);
            }

            if (changed) {
                if (group.outlineBloomPostEffect) {
                    group.postEffectMode = PostEffectMode::OutlineBloom;
                } else if (group.bloomPostEffect) {
                    group.postEffectMode = PostEffectMode::BoxFilter;
                } else {
                    group.postEffectMode = PostEffectMode::FullScreen;
                }
            }
            if (group.bloomPostEffect || group.outlineBloomPostEffect) {
                ImGui::TextDisabled("Dedicated post-effect layer target.");
            }
            ImGui::Checkbox("Depth Test", &group.depthTestEnabled);

            // 繝薙Ν繝懊・繝峨Δ繝ｼ繝・
            const char* billboardModes[] = {
                "Billboard (Camera Face)", "Velocity Aligned (Arrow)", "None (Fixed)", "Velocity Aligned (Camera Face)",
                "Emission Axis"
            };
            int currentBMode = static_cast<int>(group.billboardMode);
            if (ImGui::Combo("Billboard Mode", &currentBMode, billboardModes, IM_ARRAYSIZE(billboardModes))) {
                group.billboardMode = static_cast<uint32_t>(currentBMode);
            }

            // 繝｢繝・Ν驕ｸ謚・
            std::string currentModelName = group.model ? "Custom Model / Primitive" : "None (Board Polygon)";
            if (IsProceduralSmoke(group)) {
                currentModelName = "Procedural Smoke";
            }
            if (ImGui::BeginCombo("Model", currentModelName.c_str())) {
                bool isSelected = (group.model == nullptr);
                if (ImGui::Selectable("None (Board Polygon)", isSelected)) {
                    group.model = nullptr;
                    group.modelType = 0;
                    group.modelName = "";
                }
                if (isSelected) ImGui::SetItemDefaultFocus();

                ImGui::Separator();
                ImGui::Text("Primitives");
                const char* primNames[] = {
                    "Ring", "Sphere", "Box", "Plane", "Torus", "Cylinder", "Cone", "Triangle", "Capsule", "Star",
                    "Diamond", "Flame (Flat)", "Flame Volume", "Flame Glow", "Spark Streak", "Soft Disc",
                    "Procedural Smoke"
                };
                for (int i = 0; i < IM_ARRAYSIZE(primNames); ++i) {
                    if (ImGui::Selectable(primNames[i], false)) {
                        group.model = GetOrMakeParticlePrimitiveModel(i);
                        group.modelType = 1;
                        group.modelName = std::to_string(i);
                    }
                }
                ImGui::Separator();
                ImGui::Text("Files");

                for (size_t i = 0; i < modelFiles_.size(); ++i) {
                    isSelected = false; 
                    if (ImGui::Selectable(modelFiles_[i].c_str(), isSelected)) {
                        std::string path = modelFiles_[i];
                        ModelManager::GetInstance()->LoadModel(path);
                        group.model = ModelManager::GetInstance()->FindModel(path);
                        group.modelType = 2;
                        group.modelName = path;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Open Model File...##ParticleGroup")) {
                std::string modelPath;
                if (OpenModelFileDialog_(modelPath)) {
                    ModelManager::GetInstance()->LoadModel(modelPath);
                    group.model = ModelManager::GetInstance()->FindModel(modelPath);
                    group.modelType = 2;
                    group.modelName = modelPath;
                }
            }

            // 繝・け繧ｹ繝√Ε驕ｸ謚・
            std::string currentTexName = group.texturePath.empty() ? "None" : group.texturePath;
            if (ImGui::BeginCombo("Texture", currentTexName.c_str())) {
                if (ImGui::Selectable("None", group.texturePath.empty())) {
                    group.texturePath.clear();
                    group.textureSrvIndex = 0;
                }
                for (size_t i = 0; i < textureFiles_.size(); ++i) {
                    bool isSelected = (group.texturePath == textureFiles_[i]);
                    if (ImGui::Selectable(textureFiles_[i].c_str(), isSelected)) {
                        std::string path = textureFiles_[i];
                        TextureManager::GetInstance()->LoadTexture(path);
                        group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(path);
                        group.texturePath = path;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Open Texture File...")) {
                std::string texturePath;
                if (OpenTextureFileDialog_(texturePath)) {
                    TextureManager::GetInstance()->LoadTexture(texturePath);
                    group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texturePath);
                    group.texturePath = texturePath;
                }
            }

            if (IsProceduralSmoke(group)) {
                const char* smokeShapes[] = { "Normal", "Vertical" };
                int shape = static_cast<int>(group.smokeShape);
                if (ImGui::Combo("Smoke Shape", &shape, smokeShapes, IM_ARRAYSIZE(smokeShapes))) {
                    group.smokeShape = static_cast<SmokeShape>(shape);
                }
                constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
                ImGui::DragFloat("Smoke Sway", &group.smokeSway, 0.001f, 0.0f, 0.05f, "%.3f", clamp);
                ImGui::DragFloat("Smoke Noise", &group.smokeNoise, 0.01f, 0.0f, 1.0f, "%.2f", clamp);
            }

            // Emitter繝代Λ繝｡繝ｼ繧ｿ
            if (group.mappedEmitter) {
                ImGui::Separator();
                ImGui::Text("Emission Settings");
                if (settingsFile.empty()) {
                    ImGui::Checkbox("Auto Emit", &group.isAutoEmit);
                
                    if (ImGui::Button("Emit Now!")) {
                        group.isEmitRequested = true;
                        group.activeTimeRemaining = std::max(group.activeTimeRemaining, group.mappedEmitter->lifeTimeMax + 0.5f);
                    }
                }

                int count = static_cast<int>(group.mappedEmitter->count);
                const int minCount = settingsFile.empty() ? 1 : 0;
                if (ImGui::DragInt("Count", &count, 1, minCount, kMaxInstance, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                    group.mappedEmitter->count = static_cast<uint32_t>(count);
                }

                if (group.isAutoEmit) {
                    ImGui::DragFloat("Frequency", &group.mappedEmitter->frequency, 0.01f, 0.01f, 5.0f);
                }

                ImGui::Separator();
                ImGui::Text("Shape Settings");
                const char* shapeTypes[] = { "Sphere", "Cone", "Box" };
                int currentShape = 1; // Directed shots use a cone.
                if (settingsFile.empty()) {
                    currentShape = static_cast<int>(group.mappedEmitter->shapeType);
                    if (ImGui::Combo("Shape Type", &currentShape, shapeTypes, IM_ARRAYSIZE(shapeTypes))) {
                        group.mappedEmitter->shapeType = static_cast<uint32_t>(currentShape);
                    }
                    ImGui::DragFloat3("Translate", &group.mappedEmitter->translate.x, 0.1f);
                }
                
                if (currentShape == 0 || currentShape == 1) { // Sphere or Cone
                    ImGui::DragFloat("Radius", &group.mappedEmitter->radius, 0.1f);
                }
                if (currentShape == 1) { // Cone
                    ImGui::DragFloat("Angle", &group.mappedEmitter->shapeAngle, 0.01f, 0.0f, 3.14159f);
                }
                if (currentShape == 2) { // Box
                    ImGui::DragFloat3("Size", &group.mappedEmitter->shapeSize.x, 0.1f);
                }

                ImGui::Separator();
                ImGui::Text("Particle Settings");
                float lifetime[2] = { group.mappedEmitter->lifeTimeMin, group.mappedEmitter->lifeTimeMax };
                const float minLife = settingsFile.empty() ? 0.1f : 0.001f;
                if (ImGui::DragFloat2("LifeTime (Min/Max)", lifetime, minLife, minLife, 10.0f, "%.3f",
                    ImGuiSliderFlags_AlwaysClamp)) {
                    group.mappedEmitter->lifeTimeMin = std::min(lifetime[0], lifetime[1]);
                    group.mappedEmitter->lifeTimeMax = std::max(lifetime[0], lifetime[1]);
                }

                const bool smoke = IsProceduralSmoke(group);
                const bool verticalSmoke = smoke && group.smokeShape == SmokeShape::Vertical;
                const float scaleStep = smoke ? 0.001f : 0.01f;
                ImGui::DragFloat3("Scale Start", &group.mappedEmitter->scaleStart.x, scaleStep, 0.0f, 20.0f);
                ImGui::DragFloat3("Scale End", &group.mappedEmitter->scaleEnd.x, scaleStep, 0.0f, 20.0f);

                if (settingsFile.empty()) {
                    ImGui::DragFloat3("Velocity Base", &group.mappedEmitter->velocityBase.x, 0.01f);
                    ImGui::DragFloat("Velocity Variance", &group.mappedEmitter->velocityVariance, 0.01f, 0.0f, 5.0f);
                    ImGui::DragFloat("Angle Random Deg", &group.mappedEmitter->angleRandomDeg, 1.0f, 0.0f, 360.0f);
                    ImGui::DragFloat("Jitter Deg", &group.mappedEmitter->jitterDeg, 1.0f, 0.0f, 180.0f);
                }
                const float speedStep = smoke ? 0.0001f : (settingsFile.empty() ? 0.01f : 0.001f);
                const char* speedFormat = smoke ? "%.4f" : "%.3f";
                ImGui::DragFloat(verticalSmoke ? "Forward Speed Min" : "Speed Min",
                    &group.mappedEmitter->speedMin, speedStep, 0.0f, 10.0f, speedFormat);
                ImGui::DragFloat(verticalSmoke ? "Forward Speed Max" : "Speed Max",
                    &group.mappedEmitter->speedMax, speedStep, 0.0f, 10.0f, speedFormat);
                ImGui::DragFloat("Rotation Start Deg", &group.mappedEmitter->rotationStartDeg, 1.0f, -360.0f, 360.0f);
                ImGui::DragFloat("Rotation Random Deg", &group.mappedEmitter->rotationRandomDeg, 1.0f, 0.0f, 360.0f);
                float angularVelocity[2] = { group.mappedEmitter->angularVelocityMinDeg, group.mappedEmitter->angularVelocityMaxDeg };
                if (ImGui::DragFloat2("Angular Velocity Deg/s", angularVelocity, 1.0f, -1440.0f, 1440.0f)) {
                    group.mappedEmitter->angularVelocityMinDeg = angularVelocity[0];
                    group.mappedEmitter->angularVelocityMaxDeg = angularVelocity[1];
                }
                ImGui::DragFloat3(verticalSmoke ? "Acceleration (Y = Rise)" : "Acceleration (Gravity)",
                    &group.mappedEmitter->acceleration.x, scaleStep);

                ImGui::ColorEdit4("Start Color", &group.mappedEmitter->startColor.x);
                ImGui::ColorEdit4("End Color", &group.mappedEmitter->endColor.x);
            }

            ImGui::TreePop();
        }
    }

#endif
}

void ParticleManager::Save(const std::string& filename) {
    SaveInternal_(filename, false);
}

void ParticleManager::SaveInternal_(const std::string& filename, bool onlyGroupsFromFile) {
    json root = json::array();

    for (const auto& [name, group] : particleGroups_) {
        if (onlyGroupsFromFile && group.fromFile != filename) {
            continue;
        }
        json g;
        g["name"] = name;
        g["texturePath"] = group.texturePath;
        g["modelType"] = group.modelType;
        g["modelName"] = group.modelName;
        if (IsProceduralSmoke(group)) {
            g["smokeShape"] = group.smokeShape == SmokeShape::Vertical ? "vertical" : "normal";
            g["smokeSway"] = group.smokeSway;
            g["smokeNoise"] = group.smokeNoise;
        }
        g["blendMode"] = static_cast<int>(group.blendMode);
        g["postEffectMode"] = static_cast<int>(group.postEffectMode);
        g["depthTestEnabled"] = group.depthTestEnabled;
        g["billboardMode"] = group.billboardMode;
        if (group.cameraSpace) {
            g["cameraSpace"] = true;
        }
        if (group.drawOrder != 0) {
            g["drawOrder"] = group.drawOrder;
        }
        g["isAutoEmit"] = group.isAutoEmit;
        g["bloomPostEffect"] = group.bloomPostEffect;
        g["outlineBloomPostEffect"] = group.outlineBloomPostEffect;
        g["bloomColor"] = { group.bloomColor.x, group.bloomColor.y, group.bloomColor.z, group.bloomColor.w };
        g["outlineBloomColor"] = { group.outlineBloomColor.x, group.outlineBloomColor.y, group.outlineBloomColor.z, group.outlineBloomColor.w };

        if (group.mappedEmitter) {
            json e;
            e["count"] = group.mappedEmitter->count;
            e["frequency"] = group.mappedEmitter->frequency;
            e["translate"] = { group.mappedEmitter->translate.x, group.mappedEmitter->translate.y, group.mappedEmitter->translate.z };
            e["radius"] = group.mappedEmitter->radius;
            e["lifeTimeMin"] = group.mappedEmitter->lifeTimeMin;
            e["lifeTimeMax"] = group.mappedEmitter->lifeTimeMax;
            e["velocityBase"] = { group.mappedEmitter->velocityBase.x, group.mappedEmitter->velocityBase.y, group.mappedEmitter->velocityBase.z };
            e["velocityVariance"] = group.mappedEmitter->velocityVariance;
            e["startColor"] = { group.mappedEmitter->startColor.x, group.mappedEmitter->startColor.y, group.mappedEmitter->startColor.z, group.mappedEmitter->startColor.w };
            e["endColor"] = { group.mappedEmitter->endColor.x, group.mappedEmitter->endColor.y, group.mappedEmitter->endColor.z, group.mappedEmitter->endColor.w };
            e["scaleStart"] = { group.mappedEmitter->scaleStart.x, group.mappedEmitter->scaleStart.y, group.mappedEmitter->scaleStart.z };
            e["scaleEnd"] = { group.mappedEmitter->scaleEnd.x, group.mappedEmitter->scaleEnd.y, group.mappedEmitter->scaleEnd.z };
            e["speedMin"] = group.mappedEmitter->speedMin;
            e["speedMax"] = group.mappedEmitter->speedMax;
            e["angleRandomDeg"] = group.mappedEmitter->angleRandomDeg;
            e["jitterDeg"] = group.mappedEmitter->jitterDeg;
            e["rotationStartDeg"] = group.mappedEmitter->rotationStartDeg;
            e["rotationRandomDeg"] = group.mappedEmitter->rotationRandomDeg;
            e["angularVelocityMinDeg"] = group.mappedEmitter->angularVelocityMinDeg;
            e["angularVelocityMaxDeg"] = group.mappedEmitter->angularVelocityMaxDeg;
            e["shapeType"] = group.mappedEmitter->shapeType;
            e["shapeAngle"] = group.mappedEmitter->shapeAngle;
            e["shapeSize"] = { group.mappedEmitter->shapeSize.x, group.mappedEmitter->shapeSize.y, group.mappedEmitter->shapeSize.z };
            e["acceleration"] = { group.mappedEmitter->acceleration.x, group.mappedEmitter->acceleration.y, group.mappedEmitter->acceleration.z };
            e["timeScale"] = group.mappedEmitter->timeScale;
            e["initialAge"] = group.mappedEmitter->initialAge;
            g["emitter"] = e;
        }

        root.push_back(g);
    }

    std::filesystem::path dir("Resources/Particles");
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
    std::string path = dir.string() + "/" + filename;

    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

void ParticleManager::Load(const std::string& filename) {
    LoadInternal_(filename, true, "", false);
}

void ParticleManager::LoadAdditional(const std::string& filename, const std::string& groupNamePrefix) {
    LoadInternal_(filename, false, groupNamePrefix, true);
}

void ParticleManager::LoadAdditional(const std::string& filename, const std::string& groupNamePrefix, const std::vector<std::string>& skipGroupNames) {
    LoadInternal_(filename, false, groupNamePrefix, true, &skipGroupNames);
}

void ParticleManager::LoadInternal_(const std::string& filename, bool clearExisting, const std::string& groupNamePrefix, bool forceAutoEmitOff, const std::vector<std::string>* skipGroupNames) {
    std::string path = "Resources/Particles/" + filename;
    std::ifstream file(path);
    if (!file.is_open()) return;

    json root;
    file >> root;

    if (clearExisting) {
        RetireAllGroups_();
    }

    for (const auto& g : root) {
        const std::string sourceName = g["name"].get<std::string>();
        if (skipGroupNames && std::find(skipGroupNames->begin(), skipGroupNames->end(), sourceName) != skipGroupNames->end()) {
            continue;
        }

        std::string name = groupNamePrefix + sourceName;
        std::string texturePath = g["texturePath"];
        int modelType = g["modelType"];
        std::string modelName = g["modelName"];

        Model* model = nullptr;
        if (modelType == 0) {
            CreateParticleGroup(name, texturePath);
        } else if (modelType == 1) {
            int primIndex = std::stoi(modelName);
            model = GetOrMakeParticlePrimitiveModel(primIndex);
            CreateParticleGroup(name, model);
        } else if (modelType == 2) {
            ModelManager::GetInstance()->LoadModel(modelName);
            model = ModelManager::GetInstance()->FindModel(modelName);
            CreateParticleGroup(name, model);
        }

        auto& group = particleGroups_[name];
        group.model = model;
        group.texturePath = texturePath;
        if (texturePath != "") {
            TextureManager::GetInstance()->LoadTexture(texturePath);
            group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(texturePath);
        } else {
            group.textureSrvIndex = 0;
        }

        group.modelType = modelType;
        group.modelName = modelName;
        group.blendMode = static_cast<ParticleCommon::BlendMode>(g["blendMode"].get<int>());
        group.postEffectMode = static_cast<PostEffectMode>(g.value("postEffectMode", static_cast<int>(PostEffectMode::FullScreen)));
        group.depthTestEnabled = g.value("depthTestEnabled", true);
        group.billboardMode = g["billboardMode"];
        group.smokeShape = g.contains("smokeShape") && g["smokeShape"] == "vertical" ?
            SmokeShape::Vertical : SmokeShape::Normal;
        group.smokeSway = std::clamp(g.value("smokeSway", ParticleGroup::kDefaultSmokeSway), 0.0f, 0.05f);
        group.smokeNoise = std::clamp(g.value("smokeNoise", 1.0f), 0.0f, 1.0f);
        group.cameraSpace = g.value("cameraSpace", false);
        group.drawOrder = g.value("drawOrder", 0);
        group.isAutoEmit = forceAutoEmitOff ? false : g["isAutoEmit"].get<bool>();
        group.bloomPostEffect = g.value("bloomPostEffect", false);
        group.outlineBloomPostEffect = g.value("outlineBloomPostEffect", false);
        if (g.contains("bloomColor")) {
            group.bloomColor = { g["bloomColor"][0], g["bloomColor"][1], g["bloomColor"][2], g["bloomColor"][3] };
        }
        if (g.contains("outlineBloomColor")) {
            group.outlineBloomColor = { g["outlineBloomColor"][0], g["outlineBloomColor"][1], g["outlineBloomColor"][2], g["outlineBloomColor"][3] };
        }
        if (!g.contains("bloomPostEffect") && !g.contains("outlineBloomPostEffect")) {
            if (group.postEffectMode == PostEffectMode::BoxFilter) {
                group.bloomPostEffect = true;
                group.outlineBloomPostEffect = false;
            } else if (group.postEffectMode == PostEffectMode::OutlineBloom) {
                group.bloomPostEffect = false;
                group.outlineBloomPostEffect = true;
            } else {
                group.bloomPostEffect = false;
                group.outlineBloomPostEffect = false;
            }
        }
        // Bloomのチェック値を正として内部モードも同期する。
        group.postEffectMode = group.outlineBloomPostEffect ? PostEffectMode::OutlineBloom :
            group.bloomPostEffect ? PostEffectMode::BoxFilter :
            PostEffectMode::FullScreen;

        if (g.contains("emitter") && group.mappedEmitter) {
            auto e = g["emitter"];
            group.mappedEmitter->count = e["count"];
            group.mappedEmitter->frequency = e["frequency"];
            group.mappedEmitter->translate = { e["translate"][0], e["translate"][1], e["translate"][2] };
            group.mappedEmitter->radius = e["radius"];
            group.mappedEmitter->lifeTimeMin = e["lifeTimeMin"];
            group.mappedEmitter->lifeTimeMax = e["lifeTimeMax"];
            group.mappedEmitter->velocityBase = { e["velocityBase"][0], e["velocityBase"][1], e["velocityBase"][2] };
            group.mappedEmitter->velocityVariance = e["velocityVariance"];
            group.mappedEmitter->startColor = { e["startColor"][0], e["startColor"][1], e["startColor"][2], e["startColor"][3] };
            group.mappedEmitter->endColor = { e["endColor"][0], e["endColor"][1], e["endColor"][2], e["endColor"][3] };
            if (e.contains("scaleStart")) {
                group.mappedEmitter->scaleStart = { e["scaleStart"][0], e["scaleStart"][1], e["scaleStart"][2] };
            }
            if (e.contains("scaleEnd")) {
                group.mappedEmitter->scaleEnd = { e["scaleEnd"][0], e["scaleEnd"][1], e["scaleEnd"][2] };
            }
            group.mappedEmitter->speedMin = e.value("speedMin", group.mappedEmitter->speedMin);
            group.mappedEmitter->speedMax = e.value("speedMax", group.mappedEmitter->speedMax);
            group.mappedEmitter->angleRandomDeg = e.value("angleRandomDeg", group.mappedEmitter->angleRandomDeg);
            group.mappedEmitter->jitterDeg = e.value("jitterDeg", group.mappedEmitter->jitterDeg);
            group.mappedEmitter->rotationStartDeg = e.value("rotationStartDeg", group.mappedEmitter->rotationStartDeg);
            group.mappedEmitter->rotationRandomDeg = e.value("rotationRandomDeg", group.mappedEmitter->rotationRandomDeg);
            group.mappedEmitter->angularVelocityMinDeg = e.value("angularVelocityMinDeg", group.mappedEmitter->angularVelocityMinDeg);
            group.mappedEmitter->angularVelocityMaxDeg = e.value("angularVelocityMaxDeg", group.mappedEmitter->angularVelocityMaxDeg);
            group.mappedEmitter->shapeType = e["shapeType"];
            group.mappedEmitter->shapeAngle = e["shapeAngle"];
            group.mappedEmitter->shapeSize = { e["shapeSize"][0], e["shapeSize"][1], e["shapeSize"][2] };
            group.mappedEmitter->acceleration = { e["acceleration"][0], e["acceleration"][1], e["acceleration"][2] };
            group.mappedEmitter->timeScale = e.value("timeScale", 1.0f);
            group.mappedEmitter->initialAge = e.value("initialAge", 0.0f);
        }
        group.fromFile = filename;
    }
}

bool ParticleManager::HasBloomPostEffectTargets() const {
    if (!editorSelectedGroupName_.empty()) {
        auto it = particleGroups_.find(editorSelectedGroupName_);
        if (it != particleGroups_.end() && it->second.bloomPostEffect) {
            return true;
        }
    }
    for (const auto& [name, group] : particleGroups_) {
        const bool active = group.IsActive();
        if (active && group.bloomPostEffect) {
            return true;
        }
    }
    return false;
}

bool ParticleManager::HasOutlineBloomPostEffectTargets() const {
    if (!editorSelectedGroupName_.empty()) {
        auto it = particleGroups_.find(editorSelectedGroupName_);
        if (it != particleGroups_.end() && it->second.outlineBloomPostEffect) {
            return true;
        }
    }
    for (const auto& [name, group] : particleGroups_) {
        const bool active = group.IsActive();
        if (active && group.outlineBloomPostEffect) {
            return true;
        }
    }
    return false;
}

void ParticleManager::ClearGPUBuffers_(ParticleGroup& group) {
    if (!group.instancingResource || !group.freeListIndexResource || !group.freeListResource) {
        return;
    }

    auto* cmd = dxCommon_->GetComputeCommandList();

    // barrier: NON_PIXEL_SHADER_RESOURCE -> UNORDERED_ACCESS
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = group.instancingResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    cmd->ResourceBarrier(1, &uavBarrier);

    particleCommon_->SetComputePipelineState(cmd);
    srvManager_->PreDrawCompute(cmd);
    cmd->SetComputeRootDescriptorTable(0, srvManager_->GetGPUDescriptionHandle(group.instancingUavIndex));
    cmd->SetComputeRootDescriptorTable(1, srvManager_->GetGPUDescriptionHandle(group.freeListIndexUavIndex));
    cmd->SetComputeRootDescriptorTable(2, srvManager_->GetGPUDescriptionHandle(group.freeListUavIndex));
    cmd->Dispatch(1, 1, 1);

    // barrier: UNORDERED_ACCESS -> NON_PIXEL_SHADER_RESOURCE
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(1, &barrier);
}

void ParticleManager::ClearAllParticles() {
    for (auto& [name, group] : particleGroups_) {
        ClearParticles_(group);
    }
}

void ParticleManager::ClearParticlesFromFile(const std::string& filename) {
    for (auto& [name, group] : particleGroups_) {
        if (group.fromFile == filename && group.IsActive()) {
            ClearParticles_(group);
        }
    }
}

void ParticleManager::ClearParticles_(ParticleGroup& group) {
    group.particles.clear();
    group.pendingEmissions.clear();
    ClearGPUBuffers_(group);
    if (group.mappedEmitter) {
        group.mappedEmitter->emit = 0;
        group.mappedEmitter->frequencyTime = 0.0f;
    }
    group.activeTimeRemaining = 0.0f;
    group.isEmitRequested = false;
}

std::vector<std::string> ParticleManager::GetGroupNamesInFile(const std::string& filename) {
    std::vector<std::string> names;
    std::string path = "Resources/Particles/" + filename;
    std::ifstream file(path);
    if (!file.is_open()) return names;

    json root;
    try {
        file >> root;
        for (const auto& g : root) {
            if (g.contains("name")) {
                names.push_back(g["name"].get<std::string>());
            }
        }
    } catch (...) {
    }
    return names;
}

std::vector<std::string> ParticleManager::GetGroupNamesLoadedFromFile(const std::string& filename) const {
    std::vector<std::string> names;
    for (const auto& [name, group] : particleGroups_) {
        if (group.fromFile == filename) {
            names.push_back(name);
        }
    }
    return names;
}

float ParticleManager::GetGroupLifeTimeMax(const std::string& groupName) const {
    auto it = particleGroups_.find(groupName);
    if (it != particleGroups_.end() && it->second.mappedEmitter) {
        return it->second.mappedEmitter->lifeTimeMax;
    }
    return 0.0f;
}

bool ParticleManager::OpenTextureFileDialog_(std::string& outTexturePath)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Texture Files (*.png;*.jpg;*.jpeg;*.dds;*.tga)\0*.png;*.jpg;*.jpeg;*.dds;*.tga\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrInitialDir = "resources";
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    outTexturePath = ToResourceRelativePath(std::filesystem::path(filePath));
    return true;
}

bool ParticleManager::OpenModelFileDialog_(std::string& outModelPath)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Model Files (*.obj;*.gltf;*.glb;*.fbx;*.pmx;*.pmd)\0*.obj;*.gltf;*.glb;*.fbx;*.pmx;*.pmd\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrInitialDir = "resources";
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    outModelPath = ToResourceRelativePath(std::filesystem::path(filePath));
    return true;
}



