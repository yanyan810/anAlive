#pragma once
#include "DirectXCommon.h"
#include <wrl.h>

class SrvManager
{
public:
    void Initialize(DirectXCommon* dxCommon);

    uint32_t Allocate();
    // Retirement never overwrites a descriptor referenced by submitted GPU work.
    // Collection runs once at the next frame boundary, before any draws are recorded.
    void Retire(uint32_t index,Microsoft::WRL::ComPtr<ID3D12Resource> resource={},
        Microsoft::WRL::ComPtr<ID3D12Resource> extra={});
    void CollectRetired();
    size_t ActiveCount() const { return activeCount_; }
    uint32_t HighWaterMark() const { return useIndex; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptionHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptionHandle(uint32_t index);

    // SRV生成（テクスチャ）
    void CreateSRVTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels);

    void CreateSRVTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT mipLevels);

    void PreDraw();
    void PreDrawCompute(ID3D12GraphicsCommandList* cmd);
    void SetGraphicsDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);

    // ★追加：外から heap を取れるように
    ID3D12DescriptorHeap* GetDescriptorHeap() const { return descriptorHeap.Get(); }

    // SRV生成(StructuredBuffer用)
    void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource,
        UINT numElements, UINT structureByteStride);

    // UAV生成(StructuredBuffer用)
    void CreateUAVforStructuredBuffer(uint32_t uavIndex, ID3D12Resource* pResource,
        UINT numElements, UINT structureByteStride);
private:
    DirectXCommon* dxCommon_ = nullptr;

    static const uint32_t kMaxSRVCount;
    uint32_t descriptorSize = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    uint32_t useIndex = 1;
    struct Retired { uint32_t index; Microsoft::WRL::ComPtr<ID3D12Resource> resource,extra; };
    std::vector<Retired> retired_;
    std::vector<uint32_t> freeIndices_;
    std::vector<bool> allocated_;
    size_t activeCount_=0;
};
