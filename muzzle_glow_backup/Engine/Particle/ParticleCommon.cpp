#include "ParticleCommon.h"
#include <cassert>

void ParticleCommon::Initialize(DirectXCommon* dxCommon)
{
    dx_ = dxCommon;

    // RootSig は1回だけ
    CreateRootSignature();
    
    // Compute用
    CreateComputeRootSignature();
    CreateComputePipelineState();
    CreateEmitComputePipelineState(); // ★追加
    CreateUpdateComputePipelineState(); // ★追加

    // ★ 全BlendMode分 PSO を事前生成
    for (int i = 0; i < static_cast<int>(BlendMode::kCountOfBlendMode); ++i) {
        CreateGraphicsPipelineState(static_cast<BlendMode>(i), true);
        CreateGraphicsPipelineState(static_cast<BlendMode>(i), false);
    }
    CreateGraphicsPipelineState(BlendMode::kBlendModeNormal, true, true);
    CreateGraphicsPipelineState(BlendMode::kBlendModeNormal, false, true);
}

void ParticleCommon::CreateRootSignature()
{
    // ==== DescriptorRange ====
    D3D12_DESCRIPTOR_RANGE descriptorRangeInstancing{};
    descriptorRangeInstancing.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeInstancing.NumDescriptors = 1;
    descriptorRangeInstancing.BaseShaderRegister = 0; // t0 (VS)
    descriptorRangeInstancing.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE descriptorRangeTexture{};
    descriptorRangeTexture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRangeTexture.NumDescriptors = 1;
    descriptorRangeTexture.BaseShaderRegister = 0; // t0 (PS)
    descriptorRangeTexture.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ==== Root Parameters ====
    D3D12_ROOT_PARAMETER params[6]{};

    // (0) Pixel: Material CBV(b0)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0; // b0

    // (1) Vertex: Instancing SRV(t0)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &descriptorRangeInstancing;

    // (2) Pixel: Texture SRV(t0)
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &descriptorRangeTexture;

    // (3) Pixel: DirectionalLight CBV(b1)
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].Descriptor.ShaderRegister = 1; // b1

    // (4) Vertex: PerView CBV(b0)
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[4].Descriptor.ShaderRegister = 0; // b0

    // (5) Vertex: BillboardMode (RootConstants 32bit)
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[5].Constants.Num32BitValues = 5;
    params[5].Constants.ShaderRegister = 1; // b1
    params[5].Constants.RegisterSpace = 0;

    // ==== Static Sampler ====
    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0; // s0
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // ==== RootSignatureDesc ====
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(params);
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &samp;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(
        0,
        sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void ParticleCommon::CreateComputeRootSignature()
{
    // CSでは register(u0) に RWStructuredBuffer を配置する想定
    D3D12_DESCRIPTOR_RANGE range0{};
    range0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range0.NumDescriptors = 1;
    range0.BaseShaderRegister = 0; // u0
    range0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range1{};
    range1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range1.NumDescriptors = 1;
    range1.BaseShaderRegister = 1; // u1
    range1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE range2{};
    range2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range2.NumDescriptors = 1;
    range2.BaseShaderRegister = 2; // u2
    range2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[5]{};

    // 0: UAV (u0) Particles
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range0;

    // 1: UAV (u1) FreeListIndex
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range1;

    // 2: UAV (u2) FreeList
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range2;

    // 3: CBV (b0) for Emitter
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[3].Descriptor.ShaderRegister = 0; // b0

    // 4: CBV (b1) for PerFrame
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].Descriptor.ShaderRegister = 1; // b1

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.NumParameters = 5; // 変更
    desc.pParameters = params;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature_));
    assert(SUCCEEDED(hr));
}

void ParticleCommon::CreateComputePipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> cs =
        dx_->CompilesSharder(L"resources/shaders/InitializeParticle.CS.hlsl", L"cs_6_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = computeRootSignature_.Get();
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

    HRESULT hr = dx_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineState_));
    assert(SUCCEEDED(hr));
}

void ParticleCommon::CreateEmitComputePipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> cs =
        dx_->CompilesSharder(L"resources/shaders/EmitParticle.CS.hlsl", L"cs_6_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = computeRootSignature_.Get();
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

    HRESULT hr = dx_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&emitComputePipelineState_));
    assert(SUCCEEDED(hr));
}

void ParticleCommon::CreateUpdateComputePipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> cs =
        dx_->CompilesSharder(L"resources/shaders/UpdateParticle.CS.hlsl", L"cs_6_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = computeRootSignature_.Get();
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

    HRESULT hr = dx_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&updateComputePipelineState_));
    assert(SUCCEEDED(hr));
}

void ParticleCommon::CreateGraphicsPipelineState(BlendMode mode, bool depthTestEnabled, bool muzzleSmoke)
{
    // === Shaders ===
    // ※ ここは “初期化時に一回だけ” 呼ばれるので、都度コンパイルでもOK
    Microsoft::WRL::ComPtr<IDxcBlob> vs =
        dx_->CompilesSharder(muzzleSmoke ? L"resources/shaders/MuzzleSmoke.VS.hlsl" : L"resources/shaders/Particle.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> ps =
        dx_->CompilesSharder(muzzleSmoke ? L"resources/shaders/MuzzleSmoke.PS.hlsl" : L"resources/shaders/Particle.PS.hlsl", L"ps_6_0");

    // === Blend ===
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    switch (mode) {
    case BlendMode::kBlendModeNone:
        blend.RenderTarget[0].BlendEnable = FALSE;
        break;

    case BlendMode::kBlendModeNormal:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        // The ImGui scene viewport samples this alpha when presenting the composed frame.
        blend.RenderTarget[0].DestBlendAlpha = muzzleSmoke ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
        break;

    case BlendMode::kBlendModeAdd:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        break;

    case BlendMode::kBlendModeSubtract:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        break;

    case BlendMode::kBlendModeMultily:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        break;

    case BlendMode::kBlendModeScreen:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        break;

    case BlendMode::kBlendModeEmissive:
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        break;

    default:
        // 念のため
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        break;
    }

    // === Rasterizer ===
    D3D12_RASTERIZER_DESC rast{};
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FillMode = D3D12_FILL_MODE_SOLID;

    // === Depth/Stencil ===
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = depthTestEnabled ? TRUE : FALSE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // === PSO ===
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = inputLayout_;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState = blend;
    psoDesc.RasterizerState = rast;
    psoDesc.DepthStencilState = ds;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    const size_t depthIndex = depthTestEnabled ? 1 : 0;
    const int idx = static_cast<int>(mode);
    auto& pipeline = muzzleSmoke ? muzzleSmokePso_[depthIndex] : pso_[depthIndex][idx];
    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipeline));
    assert(SUCCEEDED(hr));
}

void ParticleCommon::SetGraphicsPipelineState(ID3D12GraphicsCommandList* cmd, bool muzzleSmoke)
{
    if (!cmd) cmd = dx_->GetCommandList();

    cmd->SetGraphicsRootSignature(rootSignature_.Get());

    const size_t depthIndex = depthTestEnabled_ ? 1 : 0;
    const int idx = static_cast<int>(blendMode_);
    const auto& pipeline = muzzleSmoke ? muzzleSmokePso_[depthIndex] : pso_[depthIndex][idx];
    cmd->SetPipelineState(pipeline.Get());

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ParticleCommon::SetComputePipelineState(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd) cmd = dx_->GetCommandList();
    cmd->SetComputeRootSignature(computeRootSignature_.Get());
    cmd->SetPipelineState(computePipelineState_.Get());
}

void ParticleCommon::SetEmitComputePipelineState(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd) cmd = dx_->GetCommandList();
    cmd->SetComputeRootSignature(computeRootSignature_.Get());
    cmd->SetPipelineState(emitComputePipelineState_.Get());
}

void ParticleCommon::SetUpdateComputePipelineState(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd) cmd = dx_->GetCommandList();
    cmd->SetComputeRootSignature(computeRootSignature_.Get());
    cmd->SetPipelineState(updateComputePipelineState_.Get());
}
