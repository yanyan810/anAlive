#pragma once
#include "DirectXCommon.h"
#include "StringUtility.h"
#include "SrvManager.h"
#include <unordered_map>
#include <string>
#include <wrl.h>
#include <DirectXTex.h>
#include <memory>
#include <unordered_set>

class TextureManager
{
public:
    struct PreparedTexture {
        std::string key,canonical;
        std::shared_ptr<DirectX::ScratchImage> image;
    };
    // CPU-only preparation; the worker must initialize COM before using WIC.
    static PreparedTexture PrepareFile(const std::string& path);
    static PreparedTexture PrepareMemory(const std::string& key,const uint8_t* bytes,size_t size);
    void RegisterPrepared(const PreparedTexture& prepared); // Render thread only.
    std::unordered_set<std::string> GetResidentKeys() const;
    static TextureManager* GetInstance();
    void Finalize();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void LoadTexture(const std::string& filePath);

    // ★スライド通り：SRVインデックス取得
    uint32_t GetSrvIndex(const std::string& filePath) const;

    // GPUハンドル取得（filePath → srvIndex → handle）
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filePath) const;

    // メタデータ取得
    const DirectX::TexMetadata& GetMetaData(const std::string& filePath) const;

    // SRV DescriptorHeap 取得（描画側が SetDescriptorHeaps したい時用）
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() const;

    void LoadTextureFromMemory(const std::string& key, const uint8_t* data, size_t sizeBytes);
    bool HasTexture(const std::string& key) const;

private:
    static TextureManager* instance;

    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(TextureManager&) = delete;
    TextureManager& operator=(TextureManager&) = delete;

    struct TextureData {
        DirectX::TexMetadata metadata{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t srvIndex = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
    };

    // filePath を key にする（スライド通り）
    std::unordered_map<std::string, TextureData> textureDatas_;

    DirectXCommon* dx_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // 空文字のときに使う白テクスチャのキー
    static constexpr const char* kWhiteKey = "__white__";

    const TextureData& GetDataByPathOrWhite_(const std::string& filePath) const;
    TextureData& GetDataByPathOrWhite_(const std::string& filePath);
    void RegisterImage_(const std::string& key, const DirectX::ScratchImage& image);
};
