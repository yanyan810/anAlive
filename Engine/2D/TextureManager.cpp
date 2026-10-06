#include "../Utility/AssetCache.h"
#include "TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <Windows.h>

TextureManager* TextureManager::instance = nullptr;
using namespace StringUtility;

static void DebugPrintA(const std::string& s) {
    OutputDebugStringA((s + "\n").c_str());
}

TextureManager* TextureManager::GetInstance()
{
    if (instance == nullptr) {
        instance = new TextureManager();
    }
    return instance;
}

void TextureManager::Finalize()
{
    delete instance;
    instance = nullptr;
}

void TextureManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dx_ = dxCommon;
    srvManager_ = srvManager;

    // ---- 1x1 白テクスチャを必ず作る（filePath="" のとき用）----
    {
        DirectX::ScratchImage whiteImg{};
        HRESULT hr = whiteImg.Initialize2D(
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            1, 1, 1, 1
        );
        assert(SUCCEEDED(hr));

        auto* img = whiteImg.GetImage(0, 0, 0);
        assert(img && img->pixels && img->rowPitch >= 4);

        img->pixels[0] = 255; // R
        img->pixels[1] = 255; // G
        img->pixels[2] = 255; // B
        img->pixels[3] = 255; // A

        TextureData& tex = textureDatas_[kWhiteKey];
        tex.metadata = whiteImg.GetMetadata();
        tex.resource = dx_->CreateTextureResource(tex.metadata);
        dx_->UploadTextureData(tex.resource, whiteImg);

        tex.srvIndex = srvManager_->Allocate();
        tex.srvHandleCPU = srvManager_->GetCPUDescriptionHandle(tex.srvIndex);
        tex.srvHandleGPU = srvManager_->GetGPUDescriptionHandle(tex.srvIndex);

        srvManager_->CreateSRVTexture2D(
            tex.srvIndex,
            tex.resource.Get(),
            tex.metadata.format,
            1
        );
    }
}

namespace {
bool LoadDerivedDDS(const std::filesystem::path& path,DirectX::ScratchImage& image) {
    if(!AssetLoading::CacheEnabled()) return false;
    AssetLoading::Timer timer("texture.cache-read",path.string());
    std::vector<uint8_t> bytes;
    return AssetLoading::LoadBlob(path,bytes) && SUCCEEDED(DirectX::LoadFromDDSMemory(bytes.data(),bytes.size(),DirectX::DDS_FLAGS_NONE,nullptr,image));
}
void SaveDerivedDDS(const std::filesystem::path& path,const DirectX::ScratchImage& image) {
    if(!AssetLoading::CacheEnabled()) return;
    AssetLoading::Timer timer("texture.cache-write",path.string());
    DirectX::Blob blob;
    if(SUCCEEDED(DirectX::SaveToDDSMemory(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::DDS_FLAGS_NONE,blob)))
        AssetLoading::SaveBlob(path,{static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()});
}
void BuildMips(DirectX::ScratchImage& image,const std::string& key) {
    if(DirectX::IsCompressed(image.GetMetadata().format) || image.GetMetadata().mipLevels>1) return;
    AssetLoading::Timer timer("texture.mip-generation",key);
    DirectX::ScratchImage mips;
    if(SUCCEEDED(DirectX::GenerateMipMaps(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::TEX_FILTER_SRGB,0,mips)))
        image=std::move(mips);
}
}

TextureManager::PreparedTexture TextureManager::PrepareFile(const std::string& filePath) {
    if(filePath.empty()) return {};
    AssetLoading::Timer total("texture.prepare-file",filePath);
    std::string resolved=filePath;
    auto path=AssetLoading::Path(resolved);
    if(!std::filesystem::is_regular_file(path)) {
        resolved="resources/"+filePath; path=AssetLoading::Path(resolved);
        if(!std::filesystem::is_regular_file(path)) {
            DebugPrintA("[Texture] file not found: "+filePath); return {};
        }
    }
    const auto canonical=AssetLoading::Canonical(resolved);
    std::filesystem::path cache;
    if(AssetLoading::CacheEnabled()) {
        try { cache=AssetLoading::CachePath("textures","dds-v1:"+AssetLoading::FileStamp(resolved),".ytex"); }
        catch(const std::exception&) {}
    }
    DirectX::ScratchImage image;
    if(cache.empty() || !LoadDerivedDDS(cache,image)) {
        HRESULT hr=S_OK;
        {
            AssetLoading::Timer timer("texture.decode",filePath);
            auto ext=path.extension().wstring(); std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
            if(ext==L".dds") hr=DirectX::LoadFromDDSFile(path.c_str(),DirectX::DDS_FLAGS_NONE,nullptr,image);
            else if(ext==L".tga") hr=DirectX::LoadFromTGAFile(path.c_str(),nullptr,image);
            else hr=DirectX::LoadFromWICFile(path.c_str(),DirectX::WIC_FLAGS_FORCE_SRGB,nullptr,image);
        }
        if(FAILED(hr)) { DebugPrintA("[Texture] decode failed: "+filePath); return {}; }
        BuildMips(image,filePath);
        if(!cache.empty()) SaveDerivedDDS(cache,image);
    }
    return {filePath,canonical,std::make_shared<DirectX::ScratchImage>(std::move(image))};
}
void TextureManager::LoadTexture(const std::string& filePath) {
    if(filePath.empty() || textureDatas_.contains(filePath)) return;
    AssetLoading::Timer total("texture.total",filePath);
    auto path=AssetLoading::Path(filePath);
    if(!std::filesystem::is_regular_file(path)) path=std::filesystem::path("resources")/path;
    if(std::filesystem::is_regular_file(path)) {
        const auto canonical=AssetLoading::Canonical(StringUtility::ConvertString(path.wstring()));
        if(auto it=textureDatas_.find(canonical);it!=textureDatas_.end()) { textureDatas_.emplace(filePath,it->second); return; }
    }
    RegisterPrepared(PrepareFile(filePath));
}
void TextureManager::RegisterPrepared(const PreparedTexture& prepared) {
    if(prepared.key.empty() || !prepared.image || textureDatas_.contains(prepared.key)) return;
    if(!prepared.canonical.empty()) {
        if(auto it=textureDatas_.find(prepared.canonical);it!=textureDatas_.end()) { textureDatas_.emplace(prepared.key,it->second); return; }
    }
    RegisterImage_(prepared.key,*prepared.image);
    if(!prepared.canonical.empty()) textureDatas_.emplace(prepared.canonical,textureDatas_.at(prepared.key));
}
std::unordered_set<std::string> TextureManager::GetResidentKeys() const {
    std::unordered_set<std::string> keys;
    for(const auto& pair:textureDatas_) keys.insert(pair.first);
    return keys;
}
void TextureManager::RegisterImage_(const std::string& key,const DirectX::ScratchImage& image) {
    AssetLoading::Timer timer("texture.upload-record",key);
    TextureData tex;
    tex.metadata=image.GetMetadata(); tex.resource=dx_->CreateTextureResource(tex.metadata);
    dx_->UploadTextureData(tex.resource,image);
    tex.srvIndex=srvManager_->Allocate();
    tex.srvHandleCPU=srvManager_->GetCPUDescriptionHandle(tex.srvIndex);
    tex.srvHandleGPU=srvManager_->GetGPUDescriptionHandle(tex.srvIndex);
    if(tex.metadata.IsCubemap()) srvManager_->CreateSRVTextureCube(tex.srvIndex,tex.resource.Get(),tex.metadata.format,static_cast<UINT>(tex.metadata.mipLevels));
    else srvManager_->CreateSRVTexture2D(tex.srvIndex,tex.resource.Get(),tex.metadata.format,static_cast<UINT>(tex.metadata.mipLevels));
    textureDatas_.emplace(key,std::move(tex));
}

// TextureManager.cpp
bool TextureManager::HasTexture(const std::string& key) const {
    return textureDatas_.contains(key);
}

TextureManager::PreparedTexture TextureManager::PrepareMemory(const std::string& key,const uint8_t* data,size_t sizeBytes) {
    if(key.empty() || !data || !sizeBytes) return {};
    AssetLoading::Timer total("texture.prepare-embedded",key);
    std::filesystem::path cache;
    if(AssetLoading::CacheEnabled()) cache=AssetLoading::CachePath("textures","embedded-dds-v1:"+key+":"+std::to_string(AssetLoading::Hash({data,sizeBytes})),".ytex");
    DirectX::ScratchImage image;
    if(cache.empty() || !LoadDerivedDDS(cache,image)) {
        HRESULT hr;
        { AssetLoading::Timer timer("texture.decode-embedded",key);
          hr=DirectX::LoadFromWICMemory(data,sizeBytes,DirectX::WIC_FLAGS_FORCE_SRGB,nullptr,image); }
        if(FAILED(hr)) { DebugPrintA("[Texture] embedded decode failed: "+key); return {}; }
        BuildMips(image,key);
        if(!cache.empty()) SaveDerivedDDS(cache,image);
    }
    return {key,{},std::make_shared<DirectX::ScratchImage>(std::move(image))};
}
void TextureManager::LoadTextureFromMemory(const std::string& key,const uint8_t* data,size_t sizeBytes) {
    if(textureDatas_.contains(key)) return;
    AssetLoading::Timer total("texture.total-embedded",key);
    RegisterPrepared(PrepareMemory(key,data,sizeBytes));
}


const TextureManager::TextureData&
TextureManager::GetDataByPathOrWhite_(const std::string& filePath) const
{
    if (filePath.empty()) {
        return textureDatas_.at(kWhiteKey);
    }
    auto it = textureDatas_.find(filePath);
    if (it == textureDatas_.end()) {
        return textureDatas_.at(kWhiteKey);
    }
    return it->second;
}

TextureManager::TextureData&
TextureManager::GetDataByPathOrWhite_(const std::string& filePath)
{
    if (filePath.empty()) {
        return textureDatas_.at(kWhiteKey);
    }
    auto it = textureDatas_.find(filePath);
    if (it == textureDatas_.end()) {
        return textureDatas_.at(kWhiteKey);
    }
    return it->second;
}

uint32_t TextureManager::GetSrvIndex(const std::string& filePath) const
{
    return GetDataByPathOrWhite_(filePath).srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath) const
{
    return GetDataByPathOrWhite_(filePath).srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath) const
{
    return GetDataByPathOrWhite_(filePath).metadata;
}

ID3D12DescriptorHeap* TextureManager::GetSrvDescriptorHeap() const
{
    assert(srvManager_);
    return srvManager_->GetDescriptorHeap();
}
