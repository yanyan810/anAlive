#pragma once
#include "string"
#include "map"
#include "Model.h"
#include "ModelCommon.h"
#include "DirectXCommon.h"
#include <future>

class ModelManager
{

public:

	//初期化
	void Initialize(DirectXCommon* dxCommon);

	//シングルトンインスタンスの取得
	static ModelManager* GetInstance();

	//終了
	void Finalize();

	/// <summary>
	/// モデルファイルの読み込み
	/// </summary>
	/// <param name="filePath">モデルファイルのパス</param>
	void LoadModel(const std::string& filePath);
	// Prepare CPU data in the background. GPU finalization stays on LoadModel's calling thread.
	bool PreloadModel(const std::string& filePath);
	bool IsModelPrepared(const std::string& filePath) const;

	/// <summary>
	/// モデルの検索
	/// </summary>
	/// <param name="filePath">モデルのファイルパス</param>
	/// <returns></returns>
	Model* FindModel(const std::string& filePath);

	/// <summary>
	/// プリミティブモデルを作る
	/// </summary>
	/// <param name="key"></param>
	/// <param name="modelData"></param>
	/// <returns></returns>
	Model* CreatePrimitiveModel(const std::string& key, const Model::ModelData& modelData);

private:
	static ModelManager* instance;


	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(ModelManager&) = delete;
	ModelManager& operator=(ModelManager&) = delete;

	//モデルデータ
	std::map<std::string, std::unique_ptr<Model>> models;
	std::map<std::string, std::future<Model::ModelData>> pending_;
	mutable std::unordered_map<std::string,std::string> aliases_;
	std::string ResolveKey_(const std::string& path) const;

	ModelCommon* modelCommon = nullptr;

};

