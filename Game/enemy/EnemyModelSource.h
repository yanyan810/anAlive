#pragma once
#include "EnemyModelSurface.h"
#include "Model.h"
#include <span>

bool IsEnemyModelFile(const std::filesystem::path& path);
std::shared_ptr<const EnemyModelSurface> ReadEnemyModel(const std::string& reference);

struct EnemyModelEditingData {
    std::vector<std::string> meshNames,materialNames;
    std::vector<std::array<uint32_t,3>> topologyVertices,skinVertices;
    std::map<std::string,Model::JointWeightData> skinWeights;
    size_t skinVertexCount=0;
};
std::shared_ptr<const EnemyModelSurface> ReadEnemyModelForEditing(const std::string& reference,EnemyModelEditingData& editing);

using EnemyModelTextures=std::vector<std::shared_ptr<const TextureManager::OwnedTexture>>;
EnemyModelTextures BuildEnemyModelTextures(const EnemyModelSurface& source);
std::unique_ptr<Model> BuildEnemyPartModel(const EnemyPartGeometry& geometry,const EnemyModelSurface& source,
    const EnemyModelTextures& textures,std::span<const uint32_t> instances={});
