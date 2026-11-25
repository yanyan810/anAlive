#pragma once
#include "DirectXCommon.h"
#include "Camera.h"
#include "Model.h"
#include "LocalLight.h"

class SrvManager;
class SkinningCommon;
class Object3dLight;

class Object3dCommon
{
public:
    enum class BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultily,
        kBlendModeScreen,
        kCountOfBlendMode,
    };

    enum class PipelineType {
        Normal,
        EnvMap,
    };

public:
    void Initialize(DirectXCommon* dxCommon);

    void SetGraphicsPipelineState(BlendMode mode);
    void SetGraphicsPipelineStateEnvMap(BlendMode mode);
    void SetGraphicsPipelineStateOutline();

    void SetSrvManager(SrvManager* srv) { srv_ = srv; }
    void SetSkinningCommon(SkinningCommon* skin) { skinCom_ = skin; }

    SrvManager* GetSrvManager() const { return srv_; }
    SkinningCommon* GetSkinningCommon() const { return skinCom_; }

    void SetDefaultCamera(Camera* camera) { defaultCamera_ = camera; }
    Camera* GetDefaultCamera() const { return defaultCamera_; }

    class ScopedLightingEnvironment {
    public:
        ScopedLightingEnvironment(Object3dCommon& common, const Object3dLight& light)
            : common_(common), previous_(common.lightingEnvironment_) { common_.lightingEnvironment_ = &light; }
        ~ScopedLightingEnvironment() { common_.lightingEnvironment_ = previous_; }
        ScopedLightingEnvironment(const ScopedLightingEnvironment&) = delete;
        ScopedLightingEnvironment& operator=(const ScopedLightingEnvironment&) = delete;
    private:
        Object3dCommon& common_;
        const Object3dLight* previous_;
    };
    const Object3dLight* GetLightingEnvironment() const { return lightingEnvironment_; }

    // The caller keeps the uploaded buffer alive until the recorded draws finish.
    class ScopedLocalLight {
    public:
        ScopedLocalLight(Object3dCommon& common, D3D12_GPU_VIRTUAL_ADDRESS address)
            : common_(common), previous_(common.localLightAddress_) { common_.localLightAddress_ = address; }
        ~ScopedLocalLight() { common_.localLightAddress_ = previous_; }
        ScopedLocalLight(const ScopedLocalLight&) = delete;
        ScopedLocalLight& operator=(const ScopedLocalLight&) = delete;

    private:
        Object3dCommon& common_;
        D3D12_GPU_VIRTUAL_ADDRESS previous_;
    };

    D3D12_GPU_VIRTUAL_ADDRESS GetLocalLightAddress() const {
        return localLightAddress_ ? localLightAddress_ : disabledLocalLight_.Address();
    }

private:
    void CreateRootSignature();
    void CreateGraphicsPipelineState();
    void CreateEnvMapGraphicsPipelineState();
    void CreateOutlineGraphicsPipelineState();

private:
    DirectXCommon* dx_ = nullptr;
    Camera* defaultCamera_ = nullptr;
    SrvManager* srv_ = nullptr;
    SkinningCommon* skinCom_ = nullptr;
    const Object3dLight* lightingEnvironment_ = nullptr;
    LocalLighting::Buffer disabledLocalLight_;
    D3D12_GPU_VIRTUAL_ADDRESS localLightAddress_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_[static_cast<int>(BlendMode::kCountOfBlendMode)];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> envMapPso_[static_cast<int>(BlendMode::kCountOfBlendMode)];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> outlinePso_;

    D3D12_INPUT_ELEMENT_DESC inputElems_[3] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, position),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, texcoord),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          (UINT)offsetof(Model::VertexData, normal),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayout_{ inputElems_, 3 };
};