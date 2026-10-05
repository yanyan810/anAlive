#pragma once

#include <array>
#include <memory>
#include <string>
#include <wrl.h>
#include <d3d12.h>

#include "OffscreenPass.h"

class DirectXCommon;
class SrvManager;
class GameApp;

enum class PostEffectMode {
    FullScreen = 0,
    Grayscale,
    Vignette,
    BoxFilter,
    GaussianBlurX,
    GaussianBlurY,
    GaussianBlur,
    Outline,
    RadialBlur,
    Dissolve,
    Random,
    OutlineBloom,
    LuminanceBasedOutline,
    LuminanceOutlineMask,

    Count
};

struct BloomParameter {
    Vector4 color;
    float intensity;
    float threshold;
    float alpha;
    float _pad;
};

class RenderManager {
public:
    void Initialize(DirectXCommon* dx, SrvManager* srv);

    void BeginOffscreen();
    void EndOffscreen();
    void BeginPreview();
    void EndPreview();
    void BeginBackBuffer();
    void BeginParticlePostLayer(PostEffectMode mode);
    void BeginParticlePostLayer(bool bloom, bool outlineBloom);
    void EndParticlePostLayer();
    void ClearParticlePostLayer();
    void BeginObjectPostLayer(bool bloom, bool outlineBloom, bool luminanceOutline = false);
    void EndObjectPostLayer();
    void ClearObjectPostLayer();

    void DrawOffscreenToBackBuffer();
    uint32_t RenderPostEffectsForSceneTexture();
    bool BeginSceneTextureOverlay();
    void EndSceneTextureOverlay();

    void DrawImGui();

    PostEffectMode GetMode() const { return currentMode_; }
    void SetMode(PostEffectMode mode);
    void SetEffectEnabled(PostEffectMode mode, bool enabled);
    bool IsEffectEnabled(PostEffectMode mode) const;
    void ClearEffects();
    void SetRadialBlurParameters(const Vector2& center, int32_t numSamples, float blurWidth);
    void SetDissolveTransition(float threshold, const Vector4& color, float edgeWidth = 0.0f);
    void SetObjectLayerBloomColor(const Vector4& color) { objectLayerBloomColor_ = color; }
    void SetObjectLayerOutlineBloomColor(const Vector4& color) { objectLayerOutlineBloomColor_ = color; }
    void SetParticleLayerBloomColor(const Vector4& color) { particleLayerBloomColor_ = color; }
    void SetParticleLayerOutlineBloomColor(const Vector4& color) { particleLayerOutlineBloomColor_ = color; }

    uint32_t GetOffscreenSrvIndex() const;
    uint32_t GetPreviewSrvIndex() const;
    OffscreenPass* GetOffscreen() const { return offscreen_.get(); }

private:
#ifdef _DEBUG
    friend void RunTitleSceneTests(GameApp& app);
#endif
    void CreateCopyImageRootSignature();
    void CreatePipelineState(
        const wchar_t* psPath,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO);
    void DrawFullscreenPass(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* bloomCBOverride = nullptr);
    void DrawFullscreenPassToBackBuffer(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* srcResource);
    void DrawFullscreenPassToBuffer(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* srcResource, OffscreenPass& dst, ID3D12Resource* bloomCBOverride = nullptr);
    void DrawAdditiveCompositePass(uint32_t baseSrvIndex, uint32_t addSrvIndex);
    int FindLastEnabledPostEffect_() const;
    uint32_t RenderPostEffectsToBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex);
    void RenderPostEffectsToBackBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex);
    uint32_t RenderLayerPostEffectsToBuffer_(
        ID3D12Resource* srcResource,
        uint32_t srcSrvIndex,
        bool bloom,
        bool outlineBloom,
        const Vector4& bloomColor,
        ID3D12Resource* bloomCB,
        BloomParameter* bloomCBData,
        const Vector4& outlineBloomColor,
        ID3D12Resource* outlineBloomCB,
        BloomParameter* outlineBloomCBData,
        bool luminanceOutline,
        OffscreenPass* tempCompositeBuffer = nullptr);
    uint32_t CompositeParticlePostToBuffer_(uint32_t baseSrvIndex);
    void CompositeParticlePostToBackBuffer_(uint32_t baseSrvIndex);
    uint32_t CompositeObjectPostToBuffer_(uint32_t baseSrvIndex);
    void CompositeObjectPostToBackBuffer_(uint32_t baseSrvIndex);
    DirectXCommon* dx_ = nullptr;
    SrvManager* srv_ = nullptr;

    std::unique_ptr<OffscreenPass> offscreen_;
    std::array<std::unique_ptr<OffscreenPass>, 2> postBuffers_;
    std::unique_ptr<OffscreenPass> particlePostLayer_;
    std::unique_ptr<OffscreenPass> particlePostBuffer_;
    std::unique_ptr<OffscreenPass> compositeBuffer_;
    std::unique_ptr<OffscreenPass> compositeBuffer2_;
    std::unique_ptr<OffscreenPass> previewBuffer_;
    std::unique_ptr<OffscreenPass> objectPostLayer_;
    std::unique_ptr<OffscreenPass> objectPostBuffer_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> copyImageRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> additiveCompositePSO_;

    static constexpr int kEffectCount = static_cast<int>(PostEffectMode::Count);
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kEffectCount> pipelineStates_;

    PostEffectMode currentMode_ = PostEffectMode::FullScreen;
    std::array<bool, kEffectCount> enabledEffects_{};

    struct GaussianFilterParameter {
        float sigma;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> gaussianFilterCB_;
    GaussianFilterParameter* gaussianFilterCBData_ = nullptr;
    float sigma_ = 4.0f;

    struct OutlineParameter {
        Vector4 color;
        float thickness;
        float threshold;
        float _pad[2];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> outlineCB_;
    OutlineParameter* outlineCBData_ = nullptr;

    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    float outlineThickness_ = 1.0f;
    float outlineThreshold_ = 0.5f;

    struct RadialBlurParameter {
        Vector2 center;
        int32_t numSamples;
        float blurWidth;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> radialBlurCB_;
    RadialBlurParameter* radialBlurCBData_ = nullptr;

    Vector2 radialBlurCenter_ = { 0.5f, 0.5f };
    int32_t radialBlurNumSamples_ = 10;
    float radialBlurWidth_ = 0.01f;

    struct DissolveParameter {
        Vector4 edgeColor;
        float threshold;
        float edgeWidth;
        float _pad[2];
        Vector4 backgroundColor;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveCB_;
    DissolveParameter* dissolveCBData_ = nullptr;

    Vector4 dissolveEdgeColor_ = { 1.0f, 0.0f, 0.0f, 1.0f };
    float dissolveThreshold_ = 0.5f;
    float dissolveEdgeWidth_ = 0.05f;
    Vector4 dissolveBackgroundColor_ = { 0.0f, 1.0f, 0.0f, 1.0f };

    struct RandomParameter {
        float time;
        float _pad[3];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> randomCB_;
    RandomParameter* randomCBData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> bloomCB_;
    BloomParameter* bloomCBData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> objectBloomCB_;
    BloomParameter* objectBloomCBData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> objectOutlineBloomCB_;
    BloomParameter* objectOutlineBloomCBData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBloomCB_;
    BloomParameter* particleBloomCBData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleOutlineBloomCB_;
    BloomParameter* particleOutlineBloomCBData_ = nullptr;


    Vector4 bloomColor_ = { 1.0f, 0.72f, 0.22f, 1.0f };
    float bloomIntensity_ = 0.9f;
    float bloomThreshold_ = 0.62f;
    float bloomAlpha_ = 1.0f;
    Vector4 objectLayerBloomColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 objectLayerOutlineBloomColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 particleLayerBloomColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 particleLayerOutlineBloomColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };

    uint32_t noiseSrvIndex_ = 0;
    uint32_t previewSrvIndex_ = 0;
    uint32_t depthSrvIndex_ = 0;
    bool hasParticlePostLayer_ = false;
    PostEffectMode particlePostEffectMode_ = PostEffectMode::FullScreen;
    bool particlePostBloom_ = false;
    bool particlePostOutlineBloom_ = false;
    bool hasObjectPostLayer_ = false;
    bool objectPostBloom_ = false;
    bool objectPostOutlineBloom_ = false;
    bool objectPostLuminanceOutline_ = false;
    OffscreenPass* sceneTextureOverlayTarget_ = nullptr;
};
