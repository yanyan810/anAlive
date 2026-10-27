#include "RenderManager.h"

#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "TextureManager.h"

#include <cassert>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

static const char* kEffectNames[] = {
    "FullScreen (No Effect)",
    "Grayscale",
    "Vignette",
    "Bloom",
    "GaussianBlurX (Horizontal)",
    "GaussianBlurY (Vertical)",
    "GaussianBlur (Linear)",
    "Outline (Depth & Normal)",
    "RadialBlur",
    "Dissolve",
    "Random",
    "Outline Bloom",
    "Luminance Based Outline",
    "Luminance Outline Mask (Internal)",
};

static const wchar_t* kEffectPSPaths[] = {
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Grayscale.PS.hlsl",
    L"resources/shaders/Vignette.PS.hlsl",
    L"resources/shaders/Bloom.PS.hlsl",
    L"resources/shaders/GaussianBlurX.PS.hlsl",
    L"resources/shaders/GaussianBlurY.PS.hlsl",
    L"resources/shaders/Fullscreen.PS.hlsl",
    L"resources/shaders/Outline.PS.hlsl",
    L"resources/shaders/RadialBlur.PS.hlsl",
    L"resources/shaders/Dissolve.PS.hlsl",
    L"resources/shaders/Random.PS.hlsl",
    L"resources/shaders/OutlineBloom.PS.hlsl",
    L"resources/shaders/LuminanceBasedOutline.PS.hlsl",
    L"resources/shaders/LuminanceOutlineMask.PS.hlsl",
};

void RenderManager::Initialize(DirectXCommon* dx, SrvManager* srv)
{
    assert(dx);
    assert(srv);

    dx_ = dx;
    srv_ = srv;

    offscreen_ = std::make_unique<OffscreenPass>();
    postBuffers_[0] = std::make_unique<OffscreenPass>();
    postBuffers_[1] = std::make_unique<OffscreenPass>();
    particlePostLayer_ = std::make_unique<OffscreenPass>();
    particlePostBuffer_ = std::make_unique<OffscreenPass>();
    compositeBuffer_ = std::make_unique<OffscreenPass>();
    compositeBuffer2_ = std::make_unique<OffscreenPass>();
    previewBuffer_ = std::make_unique<OffscreenPass>();
    objectPostLayer_ = std::make_unique<OffscreenPass>();
    objectPostBuffer_ = std::make_unique<OffscreenPass>();

    Vector4 clearColor = { 0.08f, 0.085f, 0.09f, 1.0f };
    offscreen_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        clearColor,
        2
    );

    Vector4 postClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    postBuffers_[0]->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        3
    );
    postBuffers_[1]->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        4
    );
    Vector4 particleLayerClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    particlePostLayer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        particleLayerClearColor,
        5
    );
    particlePostBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        6
    );
    compositeBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        7
    );
    compositeBuffer2_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        8
    );
    previewBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        clearColor,
        9
    );
    objectPostLayer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        particleLayerClearColor,
        10
    );
    objectPostBuffer_->Initialize(
        dx_,
        srv_,
        WinApp::kClientWidth,
        WinApp::kClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        postClearColor,
        11
    );

    CreateCopyImageRootSignature();

    gaussianFilterCB_ = dx_->CreateBufferResource((sizeof(GaussianFilterParameter) + 0xff) & ~0xff);
    gaussianFilterCB_->Map(0, nullptr, reinterpret_cast<void**>(&gaussianFilterCBData_));
    gaussianFilterCBData_->sigma = sigma_;

    outlineCB_ = dx_->CreateBufferResource((sizeof(OutlineParameter) + 0xff) & ~0xff);
    outlineCB_->Map(0, nullptr, reinterpret_cast<void**>(&outlineCBData_));
    outlineCBData_->color = outlineColor_;
    outlineCBData_->thickness = outlineThickness_;
    outlineCBData_->threshold = outlineThreshold_;

    radialBlurCB_ = dx_->CreateBufferResource((sizeof(RadialBlurParameter) + 0xff) & ~0xff);
    radialBlurCB_->Map(0, nullptr, reinterpret_cast<void**>(&radialBlurCBData_));
    radialBlurCBData_->center = radialBlurCenter_;
    radialBlurCBData_->numSamples = radialBlurNumSamples_;
    radialBlurCBData_->blurWidth = radialBlurWidth_;

    dissolveCB_ = dx_->CreateBufferResource((sizeof(DissolveParameter) + 0xff) & ~0xff);
    dissolveCB_->Map(0, nullptr, reinterpret_cast<void**>(&dissolveCBData_));
    dissolveCBData_->edgeColor = dissolveEdgeColor_;
    dissolveCBData_->threshold = dissolveThreshold_;
    dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
    dissolveCBData_->backgroundColor = dissolveBackgroundColor_;

    randomCB_ = dx_->CreateBufferResource((sizeof(RandomParameter) + 0xff) & ~0xff);
    randomCB_->Map(0, nullptr, reinterpret_cast<void**>(&randomCBData_));
    randomCBData_->time = 0.0f;

    bloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    bloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&bloomCBData_));
    bloomCBData_->color = bloomColor_;
    bloomCBData_->intensity = bloomIntensity_;
    bloomCBData_->threshold = bloomThreshold_;
    bloomCBData_->alpha = bloomAlpha_;
    bloomCBData_->_pad = 0.0f;

    objectBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    objectBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&objectBloomCBData_));
    objectBloomCBData_->color = objectLayerBloomColor_;
    objectBloomCBData_->intensity = bloomIntensity_;
    objectBloomCBData_->threshold = bloomThreshold_;
    objectBloomCBData_->alpha = bloomAlpha_;
    objectBloomCBData_->_pad = 0.0f;

    objectOutlineBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    objectOutlineBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&objectOutlineBloomCBData_));
    objectOutlineBloomCBData_->color = objectLayerOutlineBloomColor_;
    objectOutlineBloomCBData_->intensity = bloomIntensity_;
    objectOutlineBloomCBData_->threshold = bloomThreshold_;
    objectOutlineBloomCBData_->alpha = bloomAlpha_;
    objectOutlineBloomCBData_->_pad = 0.0f;

    particleBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    particleBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&particleBloomCBData_));
    particleBloomCBData_->color = particleLayerBloomColor_;
    particleBloomCBData_->intensity = bloomIntensity_;
    particleBloomCBData_->threshold = bloomThreshold_;
    particleBloomCBData_->alpha = bloomAlpha_;
    particleBloomCBData_->_pad = 0.0f;

    particleOutlineBloomCB_ = dx_->CreateBufferResource((sizeof(BloomParameter) + 0xff) & ~0xff);
    particleOutlineBloomCB_->Map(0, nullptr, reinterpret_cast<void**>(&particleOutlineBloomCBData_));
    particleOutlineBloomCBData_->color = particleLayerOutlineBloomColor_;
    particleOutlineBloomCBData_->intensity = bloomIntensity_;
    particleOutlineBloomCBData_->threshold = bloomThreshold_;
    particleOutlineBloomCBData_->alpha = bloomAlpha_;
    particleOutlineBloomCBData_->_pad = 0.0f;

    TextureManager::GetInstance()->LoadTexture("resources/noise0.png");
    noiseSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex("resources/noise0.png");

    depthSrvIndex_ = srv_->Allocate();
    srv_->CreateSRVTexture2D(depthSrvIndex_, dx_->GetDepthStencilResource(), DXGI_FORMAT_R32_FLOAT, 1);

    for (int i = 0; i < kEffectCount; ++i) {
        CreatePipelineState(kEffectPSPaths[i], pipelineStates_[i]);
    }
    CreatePipelineState(L"resources/shaders/AdditiveComposite.PS.hlsl", additiveCompositePSO_);
    CreatePipelineState(L"resources/shaders/DeathPostEffect.PS.hlsl", deathPostPSO_);
    deathPostCB_=dx_->CreateBufferResource(256);
    deathPostCB_->Map(0,nullptr,reinterpret_cast<void**>(&deathPostData_));
    *deathPostData_={};
}

void RenderManager::SetDeathPostEffect(const DeathPostEffectParameters& parameters) {
    deathPost_=parameters;
    const auto limit=[](float value,float high) { return std::isfinite(value) ? std::clamp(value,0.0f,high) : 0.0f; };
    deathPost_.vignette=limit(parameters.vignette,.85f);
    deathPost_.desaturation=limit(parameters.desaturation,1);
    deathPost_.blurPixels=limit(parameters.blurPixels,3);
    deathPost_.chromaticPixels=limit(parameters.chromaticPixels,2);
    deathPost_.mosaicStrength=limit(parameters.mosaicStrength,1);
    deathPost_.mosaicCellSize=std::clamp(limit(parameters.mosaicCellSize,.05f),.001f,.05f);
    deathPost_.mosaicColorStrength=limit(parameters.mosaicColorStrength,1);
    deathPost_.mosaicTime=limit(parameters.mosaicTime,3600);
    deathPost_.mosaicChangeRate=limit(parameters.mosaicChangeRate,120);
    deathPost_.mosaicDensity=limit(parameters.mosaicDensity,1);
    deathPost_.mosaicOcclusion=limit(parameters.mosaicOcclusion,1);
    ViewPostEffectParameters view{parameters.doubleVisionStrength,parameters.doubleVisionOffsetPixels,parameters.doubleVisionOpacity,parameters.impactBlurPixels};
    ApplyViewPostEffects(deathPost_,view);
    deathPost_.vignetteColor={limit(parameters.vignetteColor.x,1),limit(parameters.vignetteColor.y,1),limit(parameters.vignetteColor.z,1),1};
    deathPost_.edgeInner=limit(parameters.edgeInner,.95f);
    deathPost_.edgeOuter=std::clamp(limit(parameters.edgeOuter,1.5f),deathPost_.edgeInner+.05f,1.5f);
}

void RenderManager::SetViewPostEffects(const ViewPostEffectParameters& parameters) {
    ApplyViewPostEffects(deathPost_,parameters);
}

void RenderManager::UpdateViewPostPreview(float unscaledDt) {
    if ((previewDouble_ || previewImpact_) && std::isfinite(unscaledDt) && unscaledDt>0)
        previewViewTime_+=std::min(unscaledDt,.1f);
    if (previewImpact_ && std::isfinite(unscaledDt) && unscaledDt>0)
        previewImpactTime_+=std::min(unscaledDt,.1f);
}

void RenderManager::ApplyViewPostPreview() {
    if (!previewDouble_ && !previewImpact_) return;
    auto vision=previewVision_; vision.enabled&=previewDouble_;
    auto impact=previewImpactSettings_; impact.enabled&=previewImpact_;
    auto parameters=EvaluateViewPostEffects(vision,1,impact,previewImpactTime_,previewViewTime_);
    if (impact.enabled && previewImpactHold_) parameters.impactBlurPixels=ViewPostLimit(impact.strength,0,3);
    SetViewPostEffects(parameters);
}

void RenderManager::ResetViewPostPreview() {
    previewDouble_=previewImpact_=false; previewImpactTime_=0; previewViewTime_=0;
    SetViewPostEffects({});
}

void RenderManager::SetViewPostPreview(const DoubleVisionSettings& vision,const ImpactBlurSettings& impact,
    bool previewDouble,bool previewImpact,bool holdPeak) {
    previewVision_=vision; previewImpactSettings_=impact;
    previewDouble_=previewDouble; previewImpact_=previewImpact; previewImpactHold_=holdPeak; previewImpactTime_=0; previewViewTime_=0;
}

void RenderManager::DrawViewPostEffectsImGui(DoubleVisionSettings& vision,ImpactBlurSettings& impact) {
#ifdef USE_IMGUI
    if (!viewPostDebugVisible_) return;
    ImGui::SetNextWindowSize({370,470},ImGuiCond_FirstUseEver);
    if (ImGui::Begin("View Post Effects (F9)",&viewPostDebugVisible_)) {
        ImGui::TextWrapped("Edit death effects below. Preview toggles affect gameplay view only; the sequence clock is unchanged.");
        ImGui::PushID("double");
        ImGui::SeparatorText("Double Vision");
        ImGui::Checkbox("Enable",&vision.enabled);
        ImGui::SliderFloat("Strength",&vision.strength,0,2);
        ImGui::SliderFloat2("Offset (pixels)",&vision.offsetPixels.x,-64,64);
        ImGui::SliderFloat("Opacity",&vision.opacity,0,.8f);
        ImGui::SliderFloat("Sway Amount (pixels)",&vision.swayPixels,0,32);
        ImGui::SliderFloat("Sway Speed (Hz)",&vision.swaySpeed,0,5);
        ImGui::Checkbox("Preview Double Vision",&previewDouble_);
        ImGui::PopID(); ImGui::PushID("impact");
        ImGui::SeparatorText("Impact Blur");
        ImGui::Checkbox("Enable",&impact.enabled);
        ImGui::SliderFloat("Strength (pixels)",&impact.strength,0,3);
        ImGui::SliderFloat("Duration (seconds)",&impact.duration,.02f,2);
        ImGui::Checkbox("Preview Impact Blur",&previewImpact_);
        ImGui::Checkbox("Hold Impact Peak",&previewImpactHold_);
        if (ImGui::Button("Replay Impact")) { previewImpact_=true; previewImpactHold_=false; previewImpactTime_=0; }
        ImGui::PopID();
        if (ImGui::Button("Stop Preview")) ResetViewPostPreview();
        ImGui::SameLine();
        if (ImGui::Button("Reset Settings")) { vision={}; impact={}; ResetViewPostPreview(); }
        ImGui::TextWrapped("Both preview toggles: combined. One toggle: isolated. Escape releases the mouse.");
    }
    ImGui::End();
    previewVision_=vision; previewImpactSettings_=impact;
#else
    (void)vision; (void)impact;
#endif
}

void RenderManager::SetMode(PostEffectMode mode)
{
    currentMode_ = mode;
    ClearEffects();

    if (mode != PostEffectMode::FullScreen) {
        enabledEffects_[static_cast<int>(mode)] = true;
    }
}

void RenderManager::SetEffectEnabled(PostEffectMode mode, bool enabled)
{
    const int index = static_cast<int>(mode);
    if (index <= static_cast<int>(PostEffectMode::FullScreen) || index >= kEffectCount) {
        return;
    }
    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        return;
    }

    enabledEffects_[index] = enabled;
    currentMode_ = PostEffectMode::FullScreen;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (enabledEffects_[i]) {
            currentMode_ = static_cast<PostEffectMode>(i);
            break;
        }
    }
}

bool RenderManager::IsEffectEnabled(PostEffectMode mode) const
{
    const int index = static_cast<int>(mode);
    if (index <= static_cast<int>(PostEffectMode::FullScreen) || index >= kEffectCount) {
        return false;
    }
    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        return false;
    }
    return enabledEffects_[index];
}

void RenderManager::ClearEffects()
{
    enabledEffects_.fill(false);
}

void RenderManager::SetRadialBlurParameters(const Vector2& center, int32_t numSamples, float blurWidth)
{
    radialBlurCenter_ = {
        std::clamp(center.x, 0.0f, 1.0f),
        std::clamp(center.y, 0.0f, 1.0f)
    };
    radialBlurNumSamples_ = std::max(1, numSamples);
    radialBlurWidth_ = std::max(0.0f, blurWidth);

    if (radialBlurCBData_) {
        radialBlurCBData_->center = radialBlurCenter_;
        radialBlurCBData_->numSamples = radialBlurNumSamples_;
        radialBlurCBData_->blurWidth = radialBlurWidth_;
    }
}

void RenderManager::SetDissolveTransition(float threshold, const Vector4& color, float edgeWidth)
{
    dissolveThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
    dissolveBackgroundColor_ = color;
    dissolveEdgeColor_ = color;
    dissolveEdgeWidth_ = std::max(0.0f, edgeWidth);

    if (dissolveCBData_) {
        dissolveCBData_->threshold = dissolveThreshold_;
        dissolveCBData_->backgroundColor = dissolveBackgroundColor_;
        dissolveCBData_->edgeColor = dissolveEdgeColor_;
        dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
    }
}

void RenderManager::BeginOffscreen()
{
    assert(offscreen_);
    ResetDeathPostEffect(); // Frame-local: only the drawing scene can opt in.
    offscreen_->Begin();
}

void RenderManager::EndOffscreen()
{
    assert(offscreen_);
    offscreen_->End();
}

void RenderManager::BeginPreview()
{
    assert(previewBuffer_);
    previewBuffer_->Begin();
}

void RenderManager::EndPreview()
{
    assert(previewBuffer_);
    previewBuffer_->End();
}

void RenderManager::BeginParticlePostLayer(PostEffectMode mode)
{
    BeginParticlePostLayer(mode == PostEffectMode::BoxFilter, mode == PostEffectMode::OutlineBloom);
    particlePostEffectMode_ = mode;
}

void RenderManager::BeginParticlePostLayer(bool bloom, bool outlineBloom)
{
    assert(particlePostLayer_);
    particlePostBloom_ = bloom;
    particlePostOutlineBloom_ = outlineBloom;
    particlePostEffectMode_ = outlineBloom ? PostEffectMode::OutlineBloom :
        bloom ? PostEffectMode::BoxFilter : PostEffectMode::FullScreen;
    hasParticlePostLayer_ = bloom || outlineBloom;
    if (hasParticlePostLayer_) {
        particlePostLayer_->BeginOverlayClear();
    }
}

void RenderManager::EndParticlePostLayer()
{
    if (!hasParticlePostLayer_) {
        return;
    }
}

void RenderManager::ClearParticlePostLayer()
{
    hasParticlePostLayer_ = false;
    particlePostEffectMode_ = PostEffectMode::FullScreen;
    particlePostBloom_ = false;
    particlePostOutlineBloom_ = false;
}

void RenderManager::BeginObjectPostLayer(bool bloom, bool outlineBloom, bool luminanceOutline)
{
    assert(objectPostLayer_);
    objectPostBloom_ = bloom;
    objectPostOutlineBloom_ = outlineBloom;
    objectPostLuminanceOutline_ = luminanceOutline;
    hasObjectPostLayer_ = bloom || outlineBloom || luminanceOutline;
    if (hasObjectPostLayer_) {
        objectPostLayer_->BeginOverlayClear();
    }
}

void RenderManager::EndObjectPostLayer()
{
}

void RenderManager::ClearObjectPostLayer()
{
    hasObjectPostLayer_ = false;
    objectPostBloom_ = false;
    objectPostOutlineBloom_ = false;
    objectPostLuminanceOutline_ = false;
}

uint32_t RenderManager::GetOffscreenSrvIndex() const
{
    assert(offscreen_);
    return offscreen_->GetSrvIndex();
}

uint32_t RenderManager::GetPreviewSrvIndex() const
{
    assert(previewBuffer_);
    return previewBuffer_->GetSrvIndex();
}

void RenderManager::BeginBackBuffer()
{
    dx_->PreDraw();
}

void RenderManager::CreateCopyImageRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range0{};
    range0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range0.NumDescriptors = 1;
    range0.BaseShaderRegister = 0; // t0
    range0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range1{};
    range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range1.NumDescriptors = 1;
    range1.BaseShaderRegister = 1; // t1
    range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[9]{};
    // [0]: SRV (t0) Color
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &range0;

    // [1]: CBV (b0) Gaussian Filter
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;

    // [2]: SRV (t1) Depth
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &range1;

    // [3]: CBV (b1) Outline
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[3].Descriptor.ShaderRegister = 1;
    rootParams[3].Descriptor.RegisterSpace = 0;

    // [4]: CBV (b2) RadialBlur
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[4].Descriptor.ShaderRegister = 2;
    rootParams[4].Descriptor.RegisterSpace = 0;

    // [5]: CBV (b3) Dissolve
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[5].Descriptor.ShaderRegister = 3;
    rootParams[5].Descriptor.RegisterSpace = 0;

    // [6]: CBV (b4) Random
    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[6].Descriptor.ShaderRegister = 4;
    rootParams[6].Descriptor.RegisterSpace = 0;

    // [7]: CBV (b5) Bloom
    rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[7].Descriptor.ShaderRegister = 5;
    rootParams[7].Descriptor.RegisterSpace = 0;
    rootParams[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[8].Descriptor.ShaderRegister = 6;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(rootParams);
    desc.pParameters = rootParams;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob,
        &errBlob);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(
        0,
        sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&copyImageRootSignature_));
    assert(SUCCEEDED(hr));
}

void RenderManager::CreatePipelineState(
    const wchar_t* psPath,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO)
{
    auto vs = dx_->CompileShader(L"resources/shaders/Fullscreen.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompileShader(psPath, L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = copyImageRootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.InputLayout.pInputElementDescs = nullptr;
    desc.InputLayout.NumElements = 0;

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &desc,
        IID_PPV_ARGS(&outPSO));
    assert(SUCCEEDED(hr));
}

void RenderManager::DrawFullscreenPass(PostEffectMode mode, uint32_t srcSrvIndex, ID3D12Resource* bloomCBOverride, bool presentation)
{
    auto* cmd = dx_->GetCommandList();

    const int modeIndex = static_cast<int>(mode);
    assert(modeIndex >= 0 && modeIndex < kEffectCount);

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    const bool death=presentation && mode==PostEffectMode::FullScreen && deathPost_.Active();
    cmd->SetPipelineState(death ? deathPostPSO_.Get() : pipelineStates_[modeIndex].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptionHandle(srcSrvIndex));
    // Depth or Mask (t1)
    uint32_t t1SrvIndex = (mode == PostEffectMode::Dissolve) ? noiseSrvIndex_ : depthSrvIndex_;
    cmd->SetGraphicsRootDescriptorTable(2, srv_->GetGPUDescriptionHandle(t1SrvIndex));
    if (death) {
        *deathPostData_=deathPost_;
        cmd->SetGraphicsRootConstantBufferView(8,deathPostCB_->GetGPUVirtualAddress());
        ++deathPostDraws_;
    }

    if (mode == PostEffectMode::GaussianBlurX || mode == PostEffectMode::GaussianBlurY) {
        cmd->SetGraphicsRootConstantBufferView(1, gaussianFilterCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::BoxFilter || mode == PostEffectMode::OutlineBloom) {
        ID3D12Resource* activeBloomCB = bloomCBOverride ? bloomCBOverride : bloomCB_.Get();
        cmd->SetGraphicsRootConstantBufferView(7, activeBloomCB->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Outline ||
        mode == PostEffectMode::LuminanceBasedOutline ||
        mode == PostEffectMode::LuminanceOutlineMask) {
        cmd->SetGraphicsRootConstantBufferView(3, outlineCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::RadialBlur) {
        cmd->SetGraphicsRootConstantBufferView(4, radialBlurCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Dissolve) {
        cmd->SetGraphicsRootConstantBufferView(5, dissolveCB_->GetGPUVirtualAddress());
    } else if (mode == PostEffectMode::Random) {
#ifdef USE_IMGUI
        if (randomCBData_) {
            randomCBData_->time = (float)ImGui::GetTime();
        }
#endif
        cmd->SetGraphicsRootConstantBufferView(6, randomCB_->GetGPUVirtualAddress());
    }

    cmd->DrawInstanced(3, 1, 0, 0); ++fullscreenDraws_;
}

void RenderManager::DrawAdditiveCompositePass(uint32_t baseSrvIndex, uint32_t addSrvIndex)
{
    auto* cmd = dx_->GetCommandList();

    cmd->SetGraphicsRootSignature(copyImageRootSignature_.Get());
    cmd->SetPipelineState(additiveCompositePSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptionHandle(baseSrvIndex));
    cmd->SetGraphicsRootDescriptorTable(2, srv_->GetGPUDescriptionHandle(addSrvIndex));
    cmd->DrawInstanced(3, 1, 0, 0); ++fullscreenDraws_;
}

OffscreenPass* RenderManager::FindPostBuffer_(uint32_t srvIndex) const {
    OffscreenPass* buffers[]={offscreen_.get(),postBuffers_[0].get(),postBuffers_[1].get(),particlePostLayer_.get(),
        particlePostBuffer_.get(),objectPostLayer_.get(),objectPostBuffer_.get(),compositeBuffer_.get(),compositeBuffer2_.get(),previewBuffer_.get()};
    for (auto* pass:buffers) if (pass && pass->GetSrvIndex()==srvIndex) return pass;
    return nullptr;
}

void RenderManager::DrawFullscreenPassToBackBuffer(
    PostEffectMode mode,
    uint32_t srcSrvIndex,
    ID3D12Resource* srcResource, bool presentation)
{
    auto* source=FindPostBuffer_(srcSrvIndex);
    assert(source && source->GetResource()==srcResource);
    const bool restore=source->GetState()==D3D12_RESOURCE_STATE_RENDER_TARGET;
    source->TransitionToShaderResource();

    dx_->SetBackBufferRenderTargetForPostEffect();
    DrawFullscreenPass(mode, srcSrvIndex, nullptr, presentation);

    if (restore) source->TransitionToRenderTarget();
}

void RenderManager::DrawFullscreenPassToBuffer(
    PostEffectMode mode,
    uint32_t srcSrvIndex,
    ID3D12Resource* srcResource,
    OffscreenPass& dst,
    ID3D12Resource* bloomCBOverride, bool presentation)
{
    auto* source=FindPostBuffer_(srcSrvIndex);
    assert(source && source->GetResource()==srcResource && source!=&dst);
    const bool restore=source->GetState()==D3D12_RESOURCE_STATE_RENDER_TARGET;
    source->TransitionToShaderResource();

    dst.BeginForPostEffect();
    DrawFullscreenPass(mode, srcSrvIndex, bloomCBOverride, presentation);

    if (restore) source->TransitionToRenderTarget();
}

int RenderManager::FindLastEnabledPostEffect_() const
{
    int lastEffect = -1;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (enabledEffects_[i]) {
            lastEffect = i;
        }
    }
    return lastEffect;
}

uint32_t RenderManager::RenderPostEffectsToBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex)
{
    const int lastEffect = FindLastEnabledPostEffect_();
    int bufferIndex = 0;

    if (lastEffect < 0) {
        OffscreenPass& dst = *postBuffers_[bufferIndex];
        DrawFullscreenPassToBuffer(PostEffectMode::FullScreen, srcSrvIndex, srcResource, dst);
        dst.TransitionToShaderResource();
        return dst.GetSrvIndex();
    }

    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (!enabledEffects_[i]) {
            continue;
        }

        const PostEffectMode mode = static_cast<PostEffectMode>(i);
        if (mode == PostEffectMode::GaussianBlur) {
            OffscreenPass& dstX = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurX, srcSrvIndex, srcResource, dstX);
            srcResource = dstX.GetResource();
            srcSrvIndex = dstX.GetSrvIndex();
            bufferIndex = 1 - bufferIndex;

            OffscreenPass& dstY = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurY, srcSrvIndex, srcResource, dstY);
            srcResource = dstY.GetResource();
            srcSrvIndex = dstY.GetSrvIndex();
            if (i != lastEffect) {
                bufferIndex = 1 - bufferIndex;
            } else {
                dstY.TransitionToShaderResource();
            }
        } else {
            OffscreenPass& dst = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(mode, srcSrvIndex, srcResource, dst);
            srcResource = dst.GetResource();
            srcSrvIndex = dst.GetSrvIndex();
            if (i != lastEffect) {
                bufferIndex = 1 - bufferIndex;
            } else {
                dst.TransitionToShaderResource();
            }
        }
    }

    return srcSrvIndex;
}

void RenderManager::RenderPostEffectsToBackBuffer_(ID3D12Resource* srcResource, uint32_t srcSrvIndex)
{
    const int lastEffect = FindLastEnabledPostEffect_();
    int bufferIndex = 0;

    if (lastEffect < 0) {
        DrawFullscreenPassToBackBuffer(PostEffectMode::FullScreen, srcSrvIndex, srcResource, true);
        return;
    }

    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (!enabledEffects_[i]) {
            continue;
        }

        const PostEffectMode mode = static_cast<PostEffectMode>(i);
        if (mode == PostEffectMode::GaussianBlur) {
            OffscreenPass& dstX = *postBuffers_[bufferIndex];
            DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurX, srcSrvIndex, srcResource, dstX);
            srcResource = dstX.GetResource();
            srcSrvIndex = dstX.GetSrvIndex();
            bufferIndex = 1 - bufferIndex;

            if (i == lastEffect) {
                DrawFullscreenPassToBackBuffer(PostEffectMode::GaussianBlurY, srcSrvIndex, srcResource);
            } else {
                OffscreenPass& dstY = *postBuffers_[bufferIndex];
                DrawFullscreenPassToBuffer(PostEffectMode::GaussianBlurY, srcSrvIndex, srcResource, dstY);
                srcResource = dstY.GetResource();
                srcSrvIndex = dstY.GetSrvIndex();
                bufferIndex = 1 - bufferIndex;
            }
        } else {
            if (i == lastEffect) {
                DrawFullscreenPassToBackBuffer(mode, srcSrvIndex, srcResource);
            } else {
                OffscreenPass& dst = *postBuffers_[bufferIndex];
                DrawFullscreenPassToBuffer(mode, srcSrvIndex, srcResource, dst);
                srcResource = dst.GetResource();
                srcSrvIndex = dst.GetSrvIndex();
                bufferIndex = 1 - bufferIndex;
            }
        }
    }
}

uint32_t RenderManager::RenderLayerPostEffectsToBuffer_(
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
    OffscreenPass* tempCompositeBuffer)
{
    if (bloomCBData) {
        bloomCBData->color = bloomColor;
        bloomCBData->intensity = bloomIntensity_;
        bloomCBData->threshold = bloomThreshold_;
        bloomCBData->alpha = bloomAlpha_;
        bloomCBData->_pad = 0.0f;
    }
    if (outlineBloomCBData) {
        outlineBloomCBData->color = outlineBloomColor;
        outlineBloomCBData->intensity = bloomIntensity_;
        outlineBloomCBData->threshold = bloomThreshold_;
        outlineBloomCBData->alpha = bloomAlpha_;
        outlineBloomCBData->_pad = 0.0f;
    }

    if (!bloom && !outlineBloom && !luminanceOutline) {
        return srcSrvIndex;
    }

    // Bloom系は同じマスクから並列生成して加算する。
    if (bloom && outlineBloom) {
        OffscreenPass* actualTemp = tempCompositeBuffer ? tempCompositeBuffer : compositeBuffer2_.get();

        // 1. 通常ブルームを適用 (src -> particlePostBuffer_)
        particlePostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::BoxFilter, srcSrvIndex, bloomCB);
        particlePostBuffer_->End();
        particlePostBuffer_->TransitionToShaderResource();

        // 2. アウトラインブルームを適用 (src -> objectPostBuffer_)
        objectPostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::OutlineBloom, srcSrvIndex, outlineBloomCB);
        objectPostBuffer_->End();
        objectPostBuffer_->TransitionToShaderResource();

        // 3. 両者を加算合成 (particlePostBuffer_ + objectPostBuffer_ -> actualTemp)
        actualTemp->BeginForPostEffect();
        DrawAdditiveCompositePass(particlePostBuffer_->GetSrvIndex(), objectPostBuffer_->GetSrvIndex());
        actualTemp->End();
        actualTemp->TransitionToShaderResource();

        if (!luminanceOutline) {
            return actualTemp->GetSrvIndex();
        }

        // Bloomの中間結果はactualTempへ確定済みなので、particlePostBuffer_を
        // LuminanceOutline用に再利用し、3種類目として加算する。
        particlePostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::LuminanceOutlineMask, srcSrvIndex);
        particlePostBuffer_->End();
        particlePostBuffer_->TransitionToShaderResource();

        objectPostBuffer_->BeginForPostEffect();
        DrawAdditiveCompositePass(actualTemp->GetSrvIndex(), particlePostBuffer_->GetSrvIndex());
        objectPostBuffer_->End();
        objectPostBuffer_->TransitionToShaderResource();
        return objectPostBuffer_->GetSrvIndex();
    }

    if (bloom || outlineBloom) {
        particlePostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(
            bloom ? PostEffectMode::BoxFilter : PostEffectMode::OutlineBloom,
            srcSrvIndex,
            bloom ? bloomCB : outlineBloomCB);
        particlePostBuffer_->End();
        particlePostBuffer_->TransitionToShaderResource();

        if (!luminanceOutline) {
            return particlePostBuffer_->GetSrvIndex();
        }

        objectPostBuffer_->BeginForPostEffect();
        DrawFullscreenPass(PostEffectMode::LuminanceOutlineMask, srcSrvIndex);
        objectPostBuffer_->End();
        objectPostBuffer_->TransitionToShaderResource();

        OffscreenPass* actualTemp =
            tempCompositeBuffer ? tempCompositeBuffer : compositeBuffer2_.get();
        actualTemp->BeginForPostEffect();
        DrawAdditiveCompositePass(
            particlePostBuffer_->GetSrvIndex(), objectPostBuffer_->GetSrvIndex());
        actualTemp->End();
        actualTemp->TransitionToShaderResource();
        return actualTemp->GetSrvIndex();
    }

    // LuminanceOutlineのみ。
    particlePostBuffer_->BeginForPostEffect();
    DrawFullscreenPass(PostEffectMode::LuminanceOutlineMask, srcSrvIndex);
    particlePostBuffer_->End();
    particlePostBuffer_->TransitionToShaderResource();
    return particlePostBuffer_->GetSrvIndex();
}

uint32_t RenderManager::CompositeParticlePostToBuffer_(uint32_t baseSrvIndex)
{
    if (!hasParticlePostLayer_) {
        return baseSrvIndex;
    }

    // オブジェクト効果を先に合成したフレームではcompositeBuffer_がbaseそのもの。
    // そこを粒子Bloomの中間バッファにも使うと完成済みの背景を黒いマスクで
    // 上書きしてしまうため、処理済みのobjectPostLayer_を安全な作業領域にする。
    OffscreenPass* particleTempBuffer =
        hasObjectPostLayer_ ? objectPostLayer_.get() : compositeBuffer_.get();

    const uint32_t effectSrvIndex = RenderLayerPostEffectsToBuffer_(
        particlePostLayer_->GetResource(),
        particlePostLayer_->GetSrvIndex(),
        particlePostBloom_,
        particlePostOutlineBloom_,
        particleLayerBloomColor_,
        particleBloomCB_.Get(),
        particleBloomCBData_,
        particleLayerOutlineBloomColor_,
        particleOutlineBloomCB_.Get(),
        particleOutlineBloomCBData_,
        false,
        particleTempBuffer);

    if (effectSrvIndex == particlePostLayer_->GetSrvIndex()) {
        return baseSrvIndex;
    }

    compositeBuffer2_->BeginForPostEffect();
    DrawAdditiveCompositePass(baseSrvIndex, effectSrvIndex);
    compositeBuffer2_->End();
    compositeBuffer2_->TransitionToShaderResource();
    return compositeBuffer2_->GetSrvIndex();
}

void RenderManager::CompositeParticlePostToBackBuffer_(uint32_t baseSrvIndex)
{
    if (!hasParticlePostLayer_) {
        return;
    }

    const uint32_t effectSrvIndex = RenderLayerPostEffectsToBuffer_(
        particlePostLayer_->GetResource(),
        particlePostLayer_->GetSrvIndex(),
        particlePostBloom_,
        particlePostOutlineBloom_,
        particleLayerBloomColor_,
        particleBloomCB_.Get(),
        particleBloomCBData_,
        particleLayerOutlineBloomColor_,
        particleOutlineBloomCB_.Get(),
        particleOutlineBloomCBData_,
        false,
        compositeBuffer_.get());

    if (effectSrvIndex == particlePostLayer_->GetSrvIndex()) {
        return;
    }

    dx_->SetBackBufferRenderTargetForPostEffect();
    DrawAdditiveCompositePass(baseSrvIndex, effectSrvIndex);
}

uint32_t RenderManager::CompositeObjectPostToBuffer_(uint32_t baseSrvIndex)
{
    if (!hasObjectPostLayer_) {
        return baseSrvIndex;
    }

    const uint32_t effectSrvIndex = RenderLayerPostEffectsToBuffer_(
        objectPostLayer_->GetResource(),
        objectPostLayer_->GetSrvIndex(),
        objectPostBloom_,
        objectPostOutlineBloom_,
        objectLayerBloomColor_,
        objectBloomCB_.Get(),
        objectBloomCBData_,
        objectLayerOutlineBloomColor_,
        objectOutlineBloomCB_.Get(),
        objectOutlineBloomCBData_,
        objectPostLuminanceOutline_,
        compositeBuffer2_.get());

    if (effectSrvIndex == objectPostLayer_->GetSrvIndex()) {
        return baseSrvIndex;
    }

    compositeBuffer_->BeginForPostEffect();
    DrawAdditiveCompositePass(baseSrvIndex, effectSrvIndex);
    compositeBuffer_->End();
    compositeBuffer_->TransitionToShaderResource();
    return compositeBuffer_->GetSrvIndex();
}

void RenderManager::CompositeObjectPostToBackBuffer_(uint32_t baseSrvIndex)
{
    if (!hasObjectPostLayer_) {
        return;
    }

    const uint32_t effectSrvIndex = RenderLayerPostEffectsToBuffer_(
        objectPostLayer_->GetResource(),
        objectPostLayer_->GetSrvIndex(),
        objectPostBloom_,
        objectPostOutlineBloom_,
        objectLayerBloomColor_,
        objectBloomCB_.Get(),
        objectBloomCBData_,
        objectLayerOutlineBloomColor_,
        objectOutlineBloomCB_.Get(),
        objectOutlineBloomCBData_,
        objectPostLuminanceOutline_,
        compositeBuffer2_.get());

    if (effectSrvIndex == objectPostLayer_->GetSrvIndex()) {
        return;
    }

    dx_->SetBackBufferRenderTargetForPostEffect();
    DrawAdditiveCompositePass(baseSrvIndex, effectSrvIndex);
}

void RenderManager::DrawOffscreenToBackBuffer()
{
    assert(offscreen_);
    assert(postBuffers_[0]);
    assert(postBuffers_[1]);

    auto* cmd = dx_->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);
    offscreen_->TransitionToRenderTarget();

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    if (hasObjectPostLayer_ || hasParticlePostLayer_ || (deathPost_.Active() && FindLastEnabledPostEffect_()>=0)) {
        uint32_t baseSrvIndex = RenderPostEffectsToBuffer_(offscreen_->GetResource(), offscreen_->GetSrvIndex());
        if (hasObjectPostLayer_) {
            baseSrvIndex = CompositeObjectPostToBuffer_(baseSrvIndex);
        }
        if (hasParticlePostLayer_) {
            baseSrvIndex = CompositeParticlePostToBuffer_(baseSrvIndex);
        }

        auto* final=FindPostBuffer_(baseSrvIndex); assert(final);
        DrawFullscreenPassToBackBuffer(PostEffectMode::FullScreen, baseSrvIndex, final->GetResource(), true);
    } else {
        RenderPostEffectsToBackBuffer_(offscreen_->GetResource(), offscreen_->GetSrvIndex());
    }

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    dx_->SetBackBufferRenderTarget();
}

uint32_t RenderManager::RenderPostEffectsForSceneTexture()
{
    assert(offscreen_);
    assert(postBuffers_[0]);
    assert(postBuffers_[1]);

    auto* cmd = dx_->GetCommandList();
    ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
    cmd->SetDescriptorHeaps(_countof(heaps), heaps);
    offscreen_->TransitionToRenderTarget();

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    if (deathPost_.Active() && !hasObjectPostLayer_ && !hasParticlePostLayer_ && FindLastEnabledPostEffect_()<0) {
        // Fuse into the ordinary copy, rather than four full-resolution passes.
        auto& target=*postBuffers_[0];
        DrawFullscreenPassToBuffer(PostEffectMode::FullScreen,offscreen_->GetSrvIndex(),offscreen_->GetResource(),target,nullptr,true);
        target.TransitionToShaderResource(); previewSrvIndex_=target.GetSrvIndex();
    } else {
        previewSrvIndex_ = RenderPostEffectsToBuffer_(offscreen_->GetResource(), offscreen_->GetSrvIndex());
        previewSrvIndex_ = CompositeObjectPostToBuffer_(previewSrvIndex_);
        previewSrvIndex_ = CompositeParticlePostToBuffer_(previewSrvIndex_);
        if (deathPost_.Active()) {
            auto* source=FindPostBuffer_(previewSrvIndex_); assert(source);
            auto& target=*postBuffers_[source==postBuffers_[0].get() ? 1 : 0];
            DrawFullscreenPassToBuffer(PostEffectMode::FullScreen,previewSrvIndex_,source->GetResource(),target,nullptr,true);
            target.TransitionToShaderResource(); previewSrvIndex_=target.GetSrvIndex();
        }
    }

    dx_->TransitionResource(
        dx_->GetDepthStencilResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    dx_->SetBackBufferRenderTarget();
    return previewSrvIndex_;
}

bool RenderManager::BeginSceneTextureOverlay()
{
    OffscreenPass* candidates[] = {
        postBuffers_[0].get(),
        postBuffers_[1].get(),
        compositeBuffer_.get(),
        compositeBuffer2_.get(),
        previewBuffer_.get()
    };

    sceneTextureOverlayTarget_ = nullptr;
    for (OffscreenPass* pass : candidates) {
        if (pass && pass->GetSrvIndex() == previewSrvIndex_) {
            sceneTextureOverlayTarget_ = pass;
            break;
        }
    }

    if (!sceneTextureOverlayTarget_) {
        return false;
    }

    sceneTextureOverlayTarget_->BeginForPostEffect();
    return true;
}

void RenderManager::EndSceneTextureOverlay()
{
    if (!sceneTextureOverlayTarget_) {
        return;
    }

    sceneTextureOverlayTarget_->End();
    sceneTextureOverlayTarget_ = nullptr;
    dx_->SetBackBufferRenderTarget();
}

void RenderManager::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::Begin("Post Effect");

    if (ImGui::Button("Clear Effects")) {
        SetMode(PostEffectMode::FullScreen);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Bloom Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool bloomEnabled = enabledEffects_[static_cast<int>(PostEffectMode::BoxFilter)];
        if (ImGui::Checkbox("Enable Bloom", &bloomEnabled)) {
            SetEffectEnabled(PostEffectMode::BoxFilter, bloomEnabled);
        }

        bool outlineBloomEnabled = enabledEffects_[static_cast<int>(PostEffectMode::OutlineBloom)];
        if (ImGui::Checkbox("Enable Outline Bloom", &outlineBloomEnabled)) {
            SetEffectEnabled(PostEffectMode::OutlineBloom, outlineBloomEnabled);
        }

        if (ImGui::ColorEdit4("Bloom Color / Alpha", &bloomColor_.x)) {
            bloomCBData_->color = bloomColor_;
        }
        if (ImGui::SliderFloat("Bloom Intensity", &bloomIntensity_, 0.0f, 5.0f)) {
            bloomCBData_->intensity = bloomIntensity_;
        }
        if (ImGui::SliderFloat("Bloom Threshold", &bloomThreshold_, 0.0f, 1.0f)) {
            bloomCBData_->threshold = bloomThreshold_;
        }
        if (ImGui::SliderFloat("Bloom Mix Alpha", &bloomAlpha_, 0.0f, 1.0f)) {
            bloomCBData_->alpha = bloomAlpha_;
        }
        ImGui::TextDisabled("These settings are also used by particle Bloom / Outline Bloom.");
    }

    ImGui::Separator();
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY) ||
            i == static_cast<int>(PostEffectMode::LuminanceOutlineMask)) {
            continue;
        }
        bool enabled = enabledEffects_[i];
        if (ImGui::Checkbox(kEffectNames[i], &enabled)) {
            SetEffectEnabled(static_cast<PostEffectMode>(i), enabled);
        }

        if (static_cast<PostEffectMode>(i) == PostEffectMode::GaussianBlur && enabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("Sigma", &sigma_, 0.1f, 10.0f)) {
                gaussianFilterCBData_->sigma = sigma_;
            }
            ImGui::Unindent();
        }
        if ((static_cast<PostEffectMode>(i) == PostEffectMode::BoxFilter ||
            static_cast<PostEffectMode>(i) == PostEffectMode::OutlineBloom) && enabled) {
            ImGui::PushID(i);
            ImGui::Indent();
            ImGui::TextDisabled("Use Bloom Settings above.");
            ImGui::Unindent();
            ImGui::PopID();
        }
        if ((static_cast<PostEffectMode>(i) == PostEffectMode::Outline ||
            static_cast<PostEffectMode>(i) == PostEffectMode::LuminanceBasedOutline) && enabled) {
            ImGui::Indent();
            if (ImGui::ColorEdit4("Color", &outlineColor_.x)) {
                outlineCBData_->color = outlineColor_;
            }
            if (ImGui::SliderFloat("Thickness", &outlineThickness_, 0.1f, 10.0f)) {
                outlineCBData_->thickness = outlineThickness_;
            }
            if (ImGui::SliderFloat("Threshold", &outlineThreshold_, 0.0f, 1.0f)) {
                outlineCBData_->threshold = outlineThreshold_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::RadialBlur && enabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat2("Center", &radialBlurCenter_.x, 0.0f, 1.0f)) {
                radialBlurCBData_->center = radialBlurCenter_;
            }
            if (ImGui::SliderInt("NumSamples", &radialBlurNumSamples_, 0, 50)) {
                radialBlurCBData_->numSamples = radialBlurNumSamples_;
            }
            if (ImGui::SliderFloat("BlurWidth", &radialBlurWidth_, 0.0f, 0.1f)) {
                radialBlurCBData_->blurWidth = radialBlurWidth_;
            }
            ImGui::Unindent();
        }
        if (static_cast<PostEffectMode>(i) == PostEffectMode::Dissolve && enabled) {
            ImGui::Indent();
            if (ImGui::ColorEdit4("EdgeColor", &dissolveEdgeColor_.x)) {
                dissolveCBData_->edgeColor = dissolveEdgeColor_;
            }
            if (ImGui::SliderFloat("Threshold", &dissolveThreshold_, 0.0f, 1.0f)) {
                dissolveCBData_->threshold = dissolveThreshold_;
            }
            if (ImGui::SliderFloat("EdgeWidth", &dissolveEdgeWidth_, 0.0f, 0.2f)) {
                dissolveCBData_->edgeWidth = dissolveEdgeWidth_;
            }
            if (ImGui::ColorEdit4("BackgroundColor", &dissolveBackgroundColor_.x)) {
                dissolveCBData_->backgroundColor = dissolveBackgroundColor_;
            }
            ImGui::Unindent();
        }
    }

    ImGui::Separator();
    ImGui::Text("Current Chain:");
    bool any = false;
    for (int i = 1; i < kEffectCount; ++i) {
        if (i == static_cast<int>(PostEffectMode::GaussianBlurX) ||
            i == static_cast<int>(PostEffectMode::GaussianBlurY)) {
            continue;
        }
        if (enabledEffects_[i]) {
            ImGui::BulletText("%s", kEffectNames[i]);
            any = true;
        }
    }
    if (!any) {
        ImGui::BulletText("%s", kEffectNames[0]);
    }

    ImGui::End();
#endif
}
