#include "SpriteCommon.h"
#include "DirectXCommon.h"   // GetDevice(), CompilesSharder() を使うため
#include <d3d12.h>
#include <dxcapi.h>
#include <assert.h>

void SpriteCommon::Initialize(DirectXCommon* dx) {
    dx_ = dx;

    // 既存スプライト
    CreateGraphicsPipelineState();

    // ★追加：円マスク（1回だけ作る）
    CreateCircleMaskPipeline_();
}

void SpriteCommon::CreateRootSignature() {
    // ==== DescriptorRange (t0: SRV) ====
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ==== RootParameters ====
    D3D12_ROOT_PARAMETER params[4]{};

    // (0) Pixel: CBV0  … Material 用
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;

    // (1) Vertex: CBV0 … WVP/World 用
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;

    // (2) Pixel: SRV(t0) … テクスチャ
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range;

    // (3) Pixel: CBV1 … DirectionalLight 用
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].Descriptor.ShaderRegister = 1;

    // ==== Static Sampler ====
    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // ==== RootSignatureDesc ====
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.NumParameters = _countof(params);
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &samp;

    // Serialize & Create
    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    assert(SUCCEEDED(hr));
    hr = dx_->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
    if (sig) sig->Release();
    if (err) err->Release();
}

void SpriteCommon::CreateGraphicsPipelineState() {

    CreateRootSignature();

    // === Shaders ===
    // ※ まずは既存の Object3D シェーダを流用（あなたの main と同じ）
    Microsoft::WRL::ComPtr<IDxcBlob> vs = dx_->CompilesSharder(L"resources/shaders/Sprite2d.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> ps = dx_->CompilesSharder(L"resources/shaders/Sprite2d.PS.hlsl", L"ps_6_0");

    // === Blend ===
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // Preserve opaque scene alpha: UI/fades change RGB, not the opacity of the
    // already composited scene. ImGui::Image blends this texture once more.
    // Overwriting alpha with the fade amount made FadeIn brighten then darken.
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    // === Rasterizer ===
    D3D12_RASTERIZER_DESC rast{};
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FillMode = D3D12_FILL_MODE_SOLID;

    // === Depth/Stencil ===
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;                    // 必要に応じて FALSE（UI最前面に）でもOK
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
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

    HRESULT hr = dx_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
    assert(SUCCEEDED(hr));
}

void SpriteCommon::SetGraphicsPipelineState() {
    auto* cmd = dx_->GetCommandList();

    // ルートシグネチャ
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    // グラフィックスパイプラインステート（PSO）
    cmd->SetPipelineState(pso_.Get());
    // プリミティブトポロジ（スプライトは三角形リスト）
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SpriteCommon::CreateCircleMaskPipeline_()
{
    auto* dev = dx_->GetDevice();

    // --- RootSignature (b0 のみ) ---
    CD3DX12_ROOT_PARAMETER rp{};
    rp.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Init(1, &rp, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));

    hr = dev->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&circleMaskRootSig_));
    assert(SUCCEEDED(hr));

    // --- Shaders ---
    // ※ SV_VertexID を使うので InputLayout は null/0
    auto vs = dx_->CompilesSharder(L"resources/shaders/ui/FullscreenQuadVS.hlsl", L"vs_6_0");
    auto ps = dx_->CompilesSharder(L"resources/shaders/ui/CircleMaskPS.hlsl", L"ps_6_0");

    // --- PSO ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = circleMaskRootSig_.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    // ★深度は完全OFF（最前面に被せたいので）
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;

    pso.InputLayout = { nullptr, 0 }; // ★重要
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    pso.NumRenderTargets = 1;
    // あなたのRTVが SRGB を使ってるので合わせる
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;

    hr = dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&circleMaskPSO_));
    assert(SUCCEEDED(hr));

    // --- ConstantBuffer (Upload) ---
    circleMaskCB_ = dx_->CreateBufferResource(sizeof(MaskCB));
    hr = circleMaskCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedMaskCB_));
    assert(SUCCEEDED(hr));

    // 初期値
    mappedMaskCB_->radius = 1.0f;
    mappedMaskCB_->softness = 0.6f;
    mappedMaskCB_->pad[0] = mappedMaskCB_->pad[1] = 0.0f;
}

void SpriteCommon::DrawCircleMask(float radius01, float softness01)
{
    auto* cmd = dx_->GetCommandList();

    // CB更新
    mappedMaskCB_->radius = radius01;
    mappedMaskCB_->softness = softness01;

    // PSO/RootSig
    cmd->SetGraphicsRootSignature(circleMaskRootSig_.Get());
    cmd->SetPipelineState(circleMaskPSO_.Get());

    cmd->SetGraphicsRootConstantBufferView(0, circleMaskCB_->GetGPUVirtualAddress());

    // InputLayoutなしの DrawInstanced(6)
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);
}
