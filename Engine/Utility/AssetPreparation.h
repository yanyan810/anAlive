#pragma once
#include "AssetCache.h"
#include "Model.h"
#include <nlohmann/json.hpp>
#include <unordered_set>

// Headless pre-bake: import and create mipmapped DDS caches without a window/device.
inline int PrepareAssetCaches() {
    std::filesystem::create_directories("generated/loading");
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct ComScope { HRESULT status; ~ComScope() { if(SUCCEEDED(status)) CoUninitialize(); } } scope{com};
    try {
        std::ifstream input("generated/loading/prepare-list.json"); nlohmann::json paths; input>>paths;
        if(!paths.is_array() || paths.empty()) throw std::runtime_error("Asset preparation requires a model list");
        std::unordered_set<std::string> textures;
        size_t missing=0;
        std::ofstream missingFiles("generated/loading/missing-textures.txt");
        for(const auto& item:paths) {
            const auto name=item.get<std::string>();
            auto path=AssetLoading::Path(name);
            if(!path.is_absolute() && (path.begin()==path.end() || *path.begin()!=L"resources")) path=std::filesystem::path("resources")/path;
            auto data=Model::ReadSourceData(StringUtility::ConvertString(path.parent_path().wstring()),StringUtility::ConvertString(path.filename().wstring()));
            if(data.meshes.empty()) throw std::runtime_error("Model import failed: "+name);
            for(const auto& t:data.embeddedTextures) if(textures.insert(t.key).second) {
                if(!TextureManager::PrepareMemory(t.key,t.bytes.data(),t.bytes.size()).image) throw std::runtime_error("Embedded texture preparation failed: "+t.key);
            }
            for(const auto& m:data.materials) {
                if(m.textureFilePath.empty() || textures.contains(m.textureFilePath)) continue;
                const auto key=AssetLoading::Canonical(m.textureFilePath);
                if(textures.insert(key).second) {
                    if(!std::filesystem::is_regular_file(AssetLoading::Path(m.textureFilePath))) {
                        ++missing; missingFiles<<m.textureFilePath<<'\n'; continue; // Preserve the renderer's missing-file white fallback.
                    }
                    if(!TextureManager::PrepareFile(m.textureFilePath).image) throw std::runtime_error("Texture preparation failed: "+m.textureFilePath);
                }
            }
        }
        std::ofstream("generated/loading/prepare-result.txt")<<"PASS: "<<paths.size()<<" models, "<<textures.size()-missing<<" prepared textures, "<<missing<<" missing references kept as white fallback; CPU-only cache preparation\n";
        if(AssetLoading::profiling) AssetLoading::SaveProfile("generated/loading/prepare-stages.csv");
        return 0;
    } catch(const std::exception& error) {
        std::ofstream("generated/loading/prepare-result.txt")<<"FAIL: "<<error.what()<<'\n'; return 1;
    }
}
