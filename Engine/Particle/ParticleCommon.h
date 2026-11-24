#pragma once
#include "DirectXCommon.h"
#include <array>

class ParticleCommon
{
public:
    enum class BlendMode {
        //!<ブレンド無し
        kBlendModeNone,
        //!<通常αブレンド。デフォルト。Src* SrcA+Dest*(1-SrcA)
        kBlendModeNormal,
        //!<加算。 Src*SrcA+Dest*1
        kBlendModeAdd,
        //!<減算。Dest*1-Src*SrA
        kBlendModeSubtract,
        //!<乗算。Src*0+Dest*Src
        kBlendModeMultily,
        //!<スクリーン。Src*(1-Dest)+Dest*1
        kBlendModeScreen,
        kBlendModeEmissive,
        //!<利用してはいけない（要素数）
        kCountOfBlendMode,
    };

public:
    void Initialize(DirectXCommon* dxCommon);

    void SetGraphicsPipelineState(ID3D12GraphicsCommandList* cmd = nullptr);
    void SetComputePipelineState(ID3D12GraphicsCommandList* cmd = nullptr);
    void SetEmitComputePipelineState(ID3D12GraphicsCommandList* cmd = nullptr); 
    void SetUpdateComputePipelineState(ID3D12GraphicsCommandList* cmd = nullptr); 

    ID3D12RootSignature* GetComputeRootSignature() const { return computeRootSignature_.Get(); }
    ID3D12PipelineState* GetComputePipelineState() const { return computePipelineState_.Get(); }

    // ★ ここではPSOを作り直さない（軽量）
    void SetBlendMode(BlendMode m) { blendMode_ = m; }
    BlendMode GetBlendMode() const { return blendMode_; }
    void SetDepthTestEnabled(bool enabled) { depthTestEnabled_ = enabled; }
    bool IsDepthTestEnabled() const { return depthTestEnabled_; }

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState(BlendMode mode, bool depthTestEnabled); // ★ mode別に作る

    void CreateComputeRootSignature();
	void CreateComputePipelineState();
    void CreateEmitComputePipelineState(); // ★追加
    void CreateUpdateComputePipelineState(); // ★追加

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;

    // ★ ブレンド分のPSOを保持
    static constexpr size_t kDepthModeCount = 2;
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
        static_cast<size_t>(BlendMode::kCountOfBlendMode)>, kDepthModeCount> pso_{};

    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emitComputePipelineState_; // ★追加
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updateComputePipelineState_; // ★追加

    DirectXCommon* dx_ = nullptr;

    // ※ 今の頂点構造(VertexData)に合わせて POSITION/TEXCOORD/NORMAL の3要素
    D3D12_INPUT_ELEMENT_DESC inputElems_[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_INPUT_LAYOUT_DESC inputLayout_{ inputElems_, 3 };

    BlendMode blendMode_ = BlendMode::kBlendModeNormal;
    bool depthTestEnabled_ = true;
};
