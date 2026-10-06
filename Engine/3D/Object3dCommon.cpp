#include "Object3dCommon.h"

void Object3dCommon::Initialize(DirectXCommon* dxCommon) {
	// 初期化処理

	dx_ = dxCommon;

    CreateGraphicsPipelineState();
    CreateEnvMapGraphicsPipelineState();
    CreateOutlineGraphicsPipelineState();

}

void Object3dCommon::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE rangeTexture{};
    rangeTexture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeTexture.NumDescriptors = 1;
    rangeTexture.BaseShaderRegister = 1;
    rangeTexture.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeEnv{};
    rangeEnv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeEnv.NumDescriptors = 1;
    rangeEnv.BaseShaderRegister = 2;
    rangeEnv.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeMask{};
    rangeMask.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeMask.NumDescriptors = 1;
    rangeMask.BaseShaderRegister = 3; // t3
    rangeMask.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[11]{};

    // b0 material
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;

    // b0 vertex transform
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;

    // t1 normal texture
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &rangeTexture;

    // b1 directional
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].Descriptor.ShaderRegister = 1;

    // b2 camera
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[4].Descriptor.ShaderRegister = 2;

    // b3 point
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].Descriptor.ShaderRegister = 3;

    // b4 spot
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[6].Descriptor.ShaderRegister = 4;

    // t2 env texture
    params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[7].DescriptorTable.NumDescriptorRanges = 1;
    params[7].DescriptorTable.pDescriptorRanges = &rangeEnv;

    // b5 effect param
    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[8].Descriptor.ShaderRegister = 5;

    // t3 mask texture
    params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[9].DescriptorTable.NumDescriptorRanges = 1;
    params[9].DescriptorTable.pDescriptorRanges = &rangeMask;

    D3D12_DESCRIPTOR_RANGE rangeShadow{};
    rangeShadow.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeShadow.NumDescriptors=1; rangeShadow.BaseShaderRegister=4;
    rangeShadow.OffsetInDescriptorsFromTableStart=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[10].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    params[10].DescriptorTable.NumDescriptorRanges=1;
    params[10].DescriptorTable.pDescriptorRanges=&rangeShadow;

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

void Object3dCommon::CreateGraphicsPipelineState() {

    CreateRootSignature();

    // === Shaders ===
    Microsoft::WRL::ComPtr<IDxcBlob> vs = dx_->CompilesSharder(L"resources/shaders/Object3D.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> ps = dx_->CompilesSharder(L"resources/shaders/Object3D.PS.hlsl", L"ps_6_0");

    for (int i = 0; i < static_cast<int>(BlendMode::kCountOfBlendMode); ++i) {
        BlendMode mode = static_cast<BlendMode>(i);

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

        // === Rasterizer ===
        D3D12_RASTERIZER_DESC rast{};
        rast.CullMode = D3D12_CULL_MODE_NONE;
        rast.FillMode = D3D12_FILL_MODE_SOLID;

        // === Depth/Stencil ===
        D3D12_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;                    // 必要に応じて FALSE（UI最前面に）でもOK
        ds.DepthWriteMask = (mode == BlendMode::kBlendModeNone || mode == BlendMode::kBlendModeNormal) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
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

        HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_[i]));
        if (FAILED(hr)) {
            OutputDebugStringA("[PSO] CreateGraphicsPipelineState FAILED\n");
            dx_->ReportLiveObjects(); // 任意
            assert(false);
        }
        assert(SUCCEEDED(hr));
    }
}

void Object3dCommon::SetGraphicsPipelineState(BlendMode mode) {
    auto* cmd = dx_->GetCommandList();

    // ルートシグネチャ
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    // グラフィックスパイプラインステート（PSO）
    const int idx = static_cast<int>(mode);
    cmd->SetPipelineState(pso_[idx].Get());
    // プリミティブトポロジ（スプライトは三角形リスト）
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


//環境マップ用
void Object3dCommon::CreateEnvMapGraphicsPipelineState() {

    // RootSignature は共通
    if (!rootSignature_) {
        CreateRootSignature();
    }

    Microsoft::WRL::ComPtr<IDxcBlob> vs =
        dx_->CompilesSharder(L"resources/shaders/Object3D.VS.hlsl", L"vs_6_0");

    Microsoft::WRL::ComPtr<IDxcBlob> ps =
        dx_->CompilesSharder(L"resources/shaders/Object3dEnvMap.PS.hlsl", L"ps_6_0");

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
        ds.DepthWriteMask = (mode == BlendMode::kBlendModeNone || mode == BlendMode::kBlendModeNormal) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
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
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(
            &psoDesc, IID_PPV_ARGS(&envMapPso_[i]));
        assert(SUCCEEDED(hr));
    }
}

void Object3dCommon::SetGraphicsPipelineStateEnvMap(BlendMode mode) {
    auto* cmd = dx_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    const int idx = static_cast<int>(mode);
    cmd->SetPipelineState(envMapPso_[idx].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dCommon::CreateOutlineGraphicsPipelineState() {
    if (!rootSignature_) {
        CreateRootSignature();
    }

    Microsoft::WRL::ComPtr<IDxcBlob> vs = dx_->CompilesSharder(L"resources/shaders/Object3dOutline.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> ps = dx_->CompilesSharder(L"resources/shaders/Object3dOutline.PS.hlsl", L"ps_6_0");

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;

    D3D12_RASTERIZER_DESC rast{};
    rast.CullMode = D3D12_CULL_MODE_FRONT; // ★裏面だけを描画（インバースハル）
    rast.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
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
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outlinePso_));
    assert(SUCCEEDED(hr));
}

void Object3dCommon::SetGraphicsPipelineStateOutline() {
    auto* cmd = dx_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(outlinePso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}