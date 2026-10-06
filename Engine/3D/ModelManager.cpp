#include "ModelManager.h"
#include "../Utility/AssetCache.h"

namespace {
std::string ModelPath(const std::string& name) {
    auto path=AssetLoading::Path(name);
    if(!path.is_absolute() && (path.begin()==path.end() || *path.begin()!=L"resources")) path=std::filesystem::path(L"resources")/path;
    return AssetLoading::Canonical(StringUtility::ConvertString(path.wstring()));
}
}

ModelManager* ModelManager::instance = nullptr;

ModelManager* ModelManager::GetInstance() {

	if (instance == nullptr) {
		instance = new ModelManager();
	}
	return instance;
}

void ModelManager::Finalize() {
	if (!instance) return;

	pending_.clear(); // Future destruction joins CPU import workers before engine shutdown.
	// モデルを片付け
	instance->models.clear();

	// ModelCommon を delete
	delete instance->modelCommon;
	instance->modelCommon = nullptr;

	delete instance;
	instance = nullptr;
}


void ModelManager::Initialize(DirectXCommon* dxCommon) {
	modelCommon = new ModelCommon;
	modelCommon->Initialize(dxCommon);


}

void ModelManager::LoadModel(const std::string& filePath) {
    const auto key=ResolveKey_(filePath);
    if(models.contains(key)) return;
    const auto path=AssetLoading::Path(key);
    std::optional<Model::ModelData> prepared;
    if(auto it=pending_.find(key);it!=pending_.end()) {
        AssetLoading::Timer wait("model.preload-join",filePath);
        auto future=std::move(it->second); pending_.erase(it); prepared=future.get();
    }
    auto model=std::make_unique<Model>();
    model->Initialize(modelCommon,StringUtility::ConvertString(path.parent_path().wstring()),StringUtility::ConvertString(path.filename().wstring()),std::move(prepared));
    models.emplace(key,std::move(model));
}

bool ModelManager::PreloadModel(const std::string& filePath) {
    const auto key=ResolveKey_(filePath);
    if(models.contains(key) || pending_.contains(key)) return true;
    if(pending_.size()>=2) return false; // Bound CPU workers and pending model memory.
    const auto path=AssetLoading::Path(key);
    const auto directory=StringUtility::ConvertString(path.parent_path().wstring()),filename=StringUtility::ConvertString(path.filename().wstring());
    auto resident=TextureManager::GetInstance()->GetResidentKeys(); // Snapshot on the render thread; worker never reads the GPU cache.
    try {
        pending_.emplace(key,std::async(std::launch::async,[directory,filename,resident=std::move(resident)]() mutable {
            const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            struct ComScope { HRESULT status; ~ComScope() { if(SUCCEEDED(status)) CoUninitialize(); } } scope{com};
            AssetLoading::Timer timer("model.cpu-preload",directory+"/"+filename);
            auto data=Model::ReadSourceData(directory,filename);
            for(const auto& t:data.embeddedTextures) if(resident.insert(t.key).second) {
                auto prepared=TextureManager::PrepareMemory(t.key,t.bytes.data(),t.bytes.size());
                if(prepared.image) data.preparedTextures.push_back(std::move(prepared));
            }
            for(const auto& m:data.materials) {
                if(m.textureFilePath.empty() || resident.contains(m.textureFilePath)) continue;
                // Embedded keys are handled above; external paths are deduplicated by their canonical name.
                if(m.textureFilePath.find("/__emb*")!=std::string::npos) continue;
                const auto canonical=AssetLoading::Canonical(m.textureFilePath);
                if(!resident.insert(canonical).second) continue;
                auto prepared=TextureManager::PrepareFile(m.textureFilePath);
                if(prepared.image) data.preparedTextures.push_back(std::move(prepared));
            }
            return data;
        }));
    } catch(const std::exception&) { return false; }

    return true;
}
bool ModelManager::IsModelPrepared(const std::string& filePath) const {
    const auto key=ResolveKey_(filePath);
    if(models.contains(key)) return true;
    const auto it=pending_.find(key);
    return it!=pending_.end() && it->second.wait_for(std::chrono::seconds(0))==std::future_status::ready;
}
Model* ModelManager::FindModel(const std::string& filePath) {
    // Procedural models retain their explicit keys.
    if(auto it=models.find(filePath);it!=models.end()) return it->second.get();
    try { const auto it=models.find(ResolveKey_(filePath)); return it!=models.end() ? it->second.get() : nullptr; }
    catch(const std::exception&) { return nullptr; }
}

std::string ModelManager::ResolveKey_(const std::string& path) const {
    if(auto it=aliases_.find(path);it!=aliases_.end()) return it->second;
    auto key=ModelPath(path); aliases_.emplace(path,key); return key;
}

Model* ModelManager::CreatePrimitiveModel(const std::string& key, const Model::ModelData& modelData) {

	// すでにあるなら再利用
	if (models.contains(key)) {
		return models.at(key).get();
	}

	// 新規作成
	std::unique_ptr<Model> model = std::make_unique<Model>();
	model->InitializeFromModelData(modelCommon, modelData);

	Model* result = model.get();
	models.insert(std::make_pair(key, std::move(model)));
	return result;
}
