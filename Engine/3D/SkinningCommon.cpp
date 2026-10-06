#include "SkinningCommon.h"
#include <cassert>

void SkinningCommon::Initialize(DirectXCommon* dxCommon)
{
    dx_ = dxCommon;
    CreateGraphicsPipelineState();
    CreateEnvMapGraphicsPipelineState();
    
    // Compute Shader用のセットアップ
    CreateComputeRootSignature();
    CreateComputePipelineState();
}

void SkinningCommon::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE rangePalette{};
    rangePalette.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangePalette.NumDescriptors = 1;
    rangePalette.BaseShaderRegister = 0;
    rangePalette.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeTexture{};
    rangeTexture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeTexture.NumDescriptors = 1;
    rangeTexture.BaseShaderRegister = 1;
    rangeTexture.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeEnv{};
    rangeEnv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeEnv.NumDescriptors = 1;
    rangeEnv.BaseShaderRegister = 2; // t2
    rangeEnv.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeMask{};
    rangeMask.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeMask.NumDescriptors = 1;
    rangeMask.BaseShaderRegister = 3; // t3
    rangeMask.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[12]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &rangePalette;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &rangeTexture;

    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[4].Descriptor.ShaderRegister = 1;

    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].Descriptor.ShaderRegister = 2;

    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[6].Descriptor.ShaderRegister = 3;

    params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[7].Descriptor.ShaderRegister = 4; // b4

    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[8].DescriptorTable.NumDescriptorRanges = 1;
    params[8].DescriptorTable.pDescriptorRanges = &rangeEnv;

    params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[9].Descriptor.ShaderRegister = 5; // b5 (EffectParam)

    params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[10].DescriptorTable.NumDescriptorRanges = 1;
    params[10].DescriptorTable.pDescriptorRanges = &rangeMask;

    D3D12_DESCRIPTOR_RANGE rangeShadow{};
    rangeShadow.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeShadow.NumDescriptors=1; rangeShadow.BaseShaderRegister=4;
    rangeShadow.OffsetInDescriptorsFromTableStart=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[11].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    params[11].DescriptorTable.NumDescriptorRanges=1;
    params[11].DescriptorTable.pDescriptorRanges=&rangeShadow;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{samp,samp};
    samplers[1].Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].ShaderRegister=1;
    samplers[1].AddressU=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor=D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(params);
    desc.pParameters = params;
    desc.NumStaticSamplers = 2;
    desc.pStaticSamplers = samplers;

    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(
        0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    if (sig) sig->Release();
    if (err) err->Release();
}

void SkinningCommon::CreateComputeRootSignature()
{
    // CS用のルートパラメータ
    D3D12_DESCRIPTOR_RANGE rangePalette{};
    rangePalette.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangePalette.NumDescriptors = 1;
    rangePalette.BaseShaderRegister = 0; // t0
    rangePalette.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeInputVertex{};
    rangeInputVertex.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeInputVertex.NumDescriptors = 1;
    rangeInputVertex.BaseShaderRegister = 1; // t1
    rangeInputVertex.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeInfluence{};
    rangeInfluence.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeInfluence.NumDescriptors = 1;
    rangeInfluence.BaseShaderRegister = 2; // t2
    rangeInfluence.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeOutputVertex{};
    rangeOutputVertex.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeOutputVertex.NumDescriptors = 1;
    rangeOutputVertex.BaseShaderRegister = 0; // u0
    rangeOutputVertex.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[5]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &rangePalette;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &rangeInputVertex;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &rangeInfluence;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &rangeOutputVertex;

    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].Descriptor.ShaderRegister = 0; // b0

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.NumParameters = _countof(params);
    desc.pParameters = params;

    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    assert(SUCCEEDED(hr));

    hr = dx_->GetDevice()->CreateRootSignature(
        0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&computeRootSignature_));
    assert(SUCCEEDED(hr));

    if (sig) sig->Release();
    if (err) err->Release();
}

void SkinningCommon::CreateComputePipelineState()
{
    auto cs = dx_->CompilesSharder(L"resources/shaders/Skinning.CS.hlsl", L"cs_6_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc{};
    computePipelineStateDesc.CS = {
        cs->GetBufferPointer(),
        cs->GetBufferSize()
    };
    computePipelineStateDesc.pRootSignature = computeRootSignature_.Get();

    HRESULT hr = dx_->GetDevice()->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&computePipelineState_));
    assert(SUCCEEDED(hr));
}


void SkinningCommon::CreateGraphicsPipelineState()
{
    CreateRootSignature();

    auto vs = dx_->CompilesSharder(L"resources/shaders/SkinnedObject.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompilesSharder(L"resources/shaders/Object3D.PS.hlsl", L"ps_6_0");

    for (int i = 0; i < static_cast<int>(BlendMode::kCountOfBlendMode); ++i) {
        BlendMode mode = static_cast<BlendMode>(i);

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
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeAdd:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeSubtract:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeMultily:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeScreen:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        }

        D3D12_RASTERIZER_DESC rast{};
        rast.CullMode = D3D12_CULL_MODE_NONE;
        rast.FillMode = D3D12_FILL_MODE_SOLID;

        D3D12_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = (mode == BlendMode::kBlendModeNone) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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

        HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_[i]));
        assert(SUCCEEDED(hr));
    }
}

void SkinningCommon::CreateEnvMapGraphicsPipelineState()
{
    if (!rootSignature_) {
        CreateRootSignature();
    }

    auto vs = dx_->CompilesSharder(L"resources/shaders/SkinnedObject.VS.hlsl", L"vs_6_0");
    auto ps = dx_->CompilesSharder(L"resources/shaders/SkinnedObjectEnvMap.PS.hlsl", L"ps_6_0");

    for (int i = 0; i < static_cast<int>(BlendMode::kCountOfBlendMode); ++i) {
        BlendMode mode = static_cast<BlendMode>(i);

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
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeAdd:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeSubtract:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeMultily:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case BlendMode::kBlendModeScreen:
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
            blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        }

        D3D12_RASTERIZER_DESC rast{};
        rast.CullMode = D3D12_CULL_MODE_NONE;
        rast.FillMode = D3D12_FILL_MODE_SOLID;

        D3D12_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = (mode == BlendMode::kBlendModeNone) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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

        HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&envMapPso_[i]));
        assert(SUCCEEDED(hr));
    }
}

void SkinningCommon::SetGraphicsPipelineState(BlendMode mode)
{
    auto* cmd = dx_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    const int idx = static_cast<int>(mode);
    cmd->SetPipelineState(pso_[idx].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SkinningCommon::SetGraphicsPipelineStateEnvMap(BlendMode mode)
{
    auto* cmd = dx_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    const int idx = static_cast<int>(mode);
    cmd->SetPipelineState(envMapPso_[idx].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
