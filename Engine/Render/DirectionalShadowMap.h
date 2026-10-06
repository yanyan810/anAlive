#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Model.h"
#include <stdexcept>

// One depth-only directional pass. The owning scene determines its casters and coverage.
class DirectionalShadowMap {
public:
    static constexpr UINT kResolution=2048;
    void Initialize(DirectXCommon* dx, SrvManager* srv) {
        dx_=dx;
        const auto check=[](HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Cannot initialize directional shadow map"); };
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width=kResolution; texture.Height=kResolution;
        texture.DepthOrArraySize=1; texture.MipLevels=1;
        texture.Format=DXGI_FORMAT_R32_TYPELESS;
        texture.SampleDesc.Count=1;
        texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format=DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth=1;
        check(dx_->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&depth_)));
        depth_->SetName(L"Title Directional Shadow Map");
        dsvHeap_=dx_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format=DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        dx_->GetDevice()->CreateDepthStencilView(depth_.Get(),&dsv,dsvHeap_->GetCPUDescriptorHandleForHeapStart());
        const auto index=srv->Allocate();
        srv->CreateSRVTexture2D(index,depth_.Get(),DXGI_FORMAT_R32_FLOAT,1);
        srv_=srv->GetGPUDescriptionHandle(index);

        D3D12_ROOT_PARAMETER root{};
        root.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        root.Constants.Num32BitValues=16; root.Constants.ShaderRegister=0;
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        signature.NumParameters=1; signature.pParameters=&root;
        Microsoft::WRL::ComPtr<ID3DBlob> bytes,error;
        check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error));
        check(dx_->GetDevice()->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root_)));
        auto vs=dx_->CompileShader(L"resources/shaders/DirectionalShadow.VS.hlsl",L"vs_6_0");
        D3D12_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,
            static_cast<UINT>(offsetof(Model::VertexData,position)),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature=root_.Get();
        pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        pso.InputLayout={&input,1};
        pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable=TRUE;
        pso.DepthStencilState.DepthEnable=TRUE;
        pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        pso.SampleDesc.Count=1; pso.SampleMask=UINT_MAX;
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.DSVFormat=DXGI_FORMAT_D32_FLOAT; // No PS and no color render targets.
        check(dx_->GetDevice()->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipeline_)));
    }
    void Begin(const Vector3& direction,const Vector3& center,float size,float lightDistance,float nearClip,float farClip) {
        const auto dir=Matrix4x4::Normalize(direction);
        const auto up=std::abs(dir.y)>.98f ? Vector3{0,0,1} : Vector3{0,1,0};
        const auto view=Matrix4x4::MakeViewMatrix(center-dir*lightDistance,center,up);
        auto projection=Matrix4x4::MakeIdentity4x4();
        projection.m[0][0]=2/size; projection.m[1][1]=2/size;
        projection.m[2][2]=1/(farClip-nearClip); // D3D depth is [0,1].
        projection.m[3][2]=-nearClip/(farClip-nearClip);
        viewProjection_=Matrix4x4::Multiply(view,projection);
        dx_->TransitionResource(depth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        auto* cmd=dx_->GetCommandList();
        const auto dsv=dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        cmd->OMSetRenderTargets(0,nullptr,FALSE,&dsv);
        cmd->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        const D3D12_VIEWPORT viewport{0,0,static_cast<float>(kResolution),static_cast<float>(kResolution),0,1};
        const D3D12_RECT rect{0,0,static_cast<LONG>(kResolution),static_cast<LONG>(kResolution)};
        cmd->RSSetViewports(1,&viewport); cmd->RSSetScissorRects(1,&rect);
        cmd->SetGraphicsRootSignature(root_.Get()); cmd->SetPipelineState(pipeline_.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawCount_=0;
    }
    void DrawMesh(const Model& model,uint32_t index,const Matrix4x4& world) {
        const auto& mesh=model.GetModelData().meshes.at(index);
        const auto wvp=Matrix4x4::Multiply(world,viewProjection_);
        auto* cmd=dx_->GetCommandList();
        cmd->SetGraphicsRoot32BitConstants(0,16,&wvp,0);
        cmd->IASetVertexBuffers(0,1,&model.GetVBV());
        cmd->IASetIndexBuffer(&model.GetIBV());
        cmd->DrawIndexedInstanced(mesh.indexCount,1,mesh.startIndex,0,0);
        ++drawCount_;
    }
    void End() { dx_->TransitionResource(depth_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); }
    const Matrix4x4& ViewProjection() const { return viewProjection_; }
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return srv_; }
    ID3D12Resource* Resource() const { return depth_.Get(); }
    size_t DrawCount() const { return drawCount_; }
private:
    DirectXCommon* dx_=nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    D3D12_GPU_DESCRIPTOR_HANDLE srv_{};
    Matrix4x4 viewProjection_=Matrix4x4::MakeIdentity4x4();
    size_t drawCount_=0;
};
