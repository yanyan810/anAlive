#pragma once
#include "EnemyModelSurface.h"
#include "Model.h"

bool IsEnemyModelFile(const std::filesystem::path& path);
std::shared_ptr<const EnemyModelSurface> ReadEnemyModel(const std::string& reference);

using EnemyModelTextures=std::vector<std::shared_ptr<const TextureManager::OwnedTexture>>;
EnemyModelTextures BuildEnemyModelTextures(const EnemyModelSurface& source);
std::unique_ptr<Model> BuildEnemyPartModel(const EnemyPartGeometry& geometry,const EnemyModelSurface& source,
    const EnemyModelTextures& textures);
