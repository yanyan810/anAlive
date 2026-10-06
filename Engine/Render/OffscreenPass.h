#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "MathStruct.h"

class DirectXCommon;
class SrvManager;

class OffscreenPass {
public:
    void Initialize(
        DirectXCommon* dx,
        SrvManager* srv,
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format,
        const Vector4& clearColor,
        uint32_t rtvIndex
    );

    void Begin();
    void BeginForPostEffect();
    void BeginForPostEffectClear();
    void BeginOverlayClear();
    void End();
    void TransitionToRenderTarget();
    void TransitionToShaderResource();

    ID3D12Resource* GetResource() const { return resource_.Get(); }
    uint32_t GetSrvIndex() const { return srvIndex_; }
    uint32_t GetRtvIndex() const { return rtvIndex_; }
    const Vector4& GetClearColor() const { return clearColor_; }
    void SetClearColor(const Vector4& color) { clearColor_ = color; }

private:
    DirectXCommon* dx_ = nullptr;
    SrvManager* srv_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t srvIndex_ = 0;
    uint32_t rtvIndex_ = 0;

    DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    Vector4 clearColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
    D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
