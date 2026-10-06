#pragma once
#include "MathStruct.h"
#include <algorithm>
#include <string>
#include <vector>
#include <format>
#include <filesystem>
#include <fstream>
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "Camera.h"
#include "SkinningCommon.h"
#include "VideoPlayerMF.h"
#include "Animator.h"
#include "Object3dLight.h"

//class Object3dCommon;

class PrimitiveCommon;

class Object3d
{

public:

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
		Matrix4x4 WorldInverseTranspose;
	};

	struct CameraGPU {
		Vector3 worldPosition;
		float pad;
	};

	struct EffectParam {
		// Outline
		Vector4 outlineColor;
		float outlineThickness;
		float enableOutline;
		float pad[2];

		// Dissolve
		Vector4 dissolveEdgeColor;
		float dissolveThreshold;
		float dissolveEdgeWidth;
		float enableDissolve;
		float pad2;

		// Random
		float enableRandom;
		float randomTime;
		float pad3[2];
	};

public:

#ifdef _DEBUG
    inline static size_t debugInitializationCount=0; // Pool regression checks (main render thread).
#endif
	void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx);
	void Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, SrvManager* srv, SkinningCommon* skinCom);


	void Update(float dt);

	void Draw();

	void SetModel(Model* model) { this->model_ = model; }

	void SetModel(const std::string& filePath);

	void SetBillboard(bool billboard) { isBillboard_ = billboard; }
	bool GetBillboard() const { return isBillboard_; }

public:
	void SetScale(const Vector3& s) { transform.scale = s; }
	void SetRotate(const Vector3& r) { transform.rotate = r; }
	void SetTranslate(const Vector3& t) { transform.translate = t; }

	const Vector3& GetScale()     const { return transform.scale; }
	const Vector3& GetRotate()    const { return transform.rotate; }
	const Vector3& GetTranslate() const { return transform.translate; }
	const Matrix4x4& GetWorldMatrix() const { return transformationMatrixDataModel->World; }

	void SetLightColor(const Vector4& color) { light_->SetDirectionalLightColor(color); }
	void SetDirection(const Vector3& direction) { light_->SetDirectionalLightDirection(direction); }
	void SetIntensity(float intensity) { light_->SetDirectionalLightIntensity(intensity); }

	const Vector4& GetLightColor() const { return light_->GetDirectionalLightColor(); }
	const Vector3& GetDirection() const { return light_->GetDirectionalLightDirection(); }
	float          GetIntensity() const { return light_->GetDirectionalLightIntensity(); }

	void SetEnableLighting(int enable) {
		EnsureInstanceMaterial_();
		if (instanceMaterialData_) {
			instanceMaterialData_->enableLighting = enable;
			instanceMaterialInitializedFromModel_ = true;
		}
	}
	void SetShininess(float s) {
		EnsureInstanceMaterial_();
		if (instanceMaterialData_) {
			instanceMaterialData_->shininess = s;
			instanceMaterialInitializedFromModel_ = true;
		}
	}
	int GetEnableLighting() const {
		return instanceMaterialData_ ? instanceMaterialData_->enableLighting : 0;
	}
	float GetShininess() const {
		return instanceMaterialData_ ? instanceMaterialData_->shininess : 0.0f;
	}

	void SetBlendMode(Object3dCommon::BlendMode m) { blendMode_ = m; }
	Object3dCommon::BlendMode GetBlendMode() const { return blendMode_; }

	void SetMaterialColor(const Vector4& c) {
		EnsureInstanceMaterial_();
		if (instanceMaterialData_) {
			instanceMaterialData_->color = c;
			instanceMaterialInitializedFromModel_ = true;
		}
	}
	Vector4 GetMaterialColor() const {
		return instanceMaterialData_ ? instanceMaterialData_->color : Vector4{ 1,1,1,1 };
	}

	void SetCamera(Camera* camera) { camera_ = camera; }

	void SetPointLightColor(const Vector4& c) { light_->SetPointLightColor(c); }
	void SetPointLightPos(const Vector3& p) { light_->SetPointLightPos(p); }
	void SetPointLightIntensity(float i) { light_->SetPointLightIntensity(i); }
	void SetPointLightRadius(float r) { light_->SetPointLightRadius(r); }
	void SetPointLightDecay(float d) { light_->SetPointLightDecay(d); }

	void SetSpotLightColor(const Vector4& c) { light_->SetSpotLightColor(c); }
	void SetSpotLightPos(const Vector3& p) { light_->SetSpotLightPos(p); }
	void SetSpotLightIntensity(float i) { light_->SetSpotLightIntensity(i); }
	void SetSpotLightDirection(const Vector3& d) { light_->SetSpotLightDirection(d); }
	void SetSpotLightDistance(float d) { light_->SetSpotLightDistance(d); }
	void SetSpotLightDecay(float d) { light_->SetSpotLightDecay(d); }
	void SetSpotLightCosAngle(float c) { light_->SetSpotLightCosAngle(c); }
	void SetSpotLightCosFalloffStart(float c) { light_->SetSpotLightCosFalloffStart(c); }
	// Non-owning scene light; the scene retains it until all its draw resources retire.
	void SetSceneLight(const Object3dLight* light) { sceneLight_ = light; }

	void SetTexture(const std::string& path);
	void ClearTextureOverride() { texturePath_.clear(); useOverrideTexture_ = false; }
	const std::string& GetTexturePath() const { return texturePath_; }

	Model* GetModel() const { return model_; }

	void SetIsVisible(bool visible) { isVisible_ = visible; }
	bool GetIsVisible() const { return isVisible_; }

	void SetEnableOutline(bool enable) { enableOutline_ = enable; }
	bool GetEnableOutline() const { return enableOutline_; }
	void SetOutlineColor(const Vector4& color) { outlineColor_ = color; }
	Vector4 GetOutlineColor() const { return outlineColor_; }
	void SetOutlineThickness(float t) { outlineThickness_ = t; }
	float GetOutlineThickness() const { return outlineThickness_; }

	void SetEnableDissolve(bool enable) { enableDissolve_ = enable; }
	bool GetEnableDissolve() const { return enableDissolve_; }
	void SetDissolveThreshold(float t) { dissolveThreshold_ = t; }
	float GetDissolveThreshold() const { return dissolveThreshold_; }
	void SetDissolveEdgeWidth(float w) { dissolveEdgeWidth_ = w; }
	float GetDissolveEdgeWidth() const { return dissolveEdgeWidth_; }
	void SetDissolveEdgeColor(const Vector4& color) { dissolveEdgeColor_ = color; }
	Vector4 GetDissolveEdgeColor() const { return dissolveEdgeColor_; }

	void SetEnableRandom(bool enable) { enableRandom_ = enable; }
	bool GetEnableRandom() const { return enableRandom_; }
	void SetRandomTime(float time) { randomTime_ = time; }

	void SetMaskTexturePath(const std::string& path) { maskTexturePath_ = path; TextureManager::GetInstance()->LoadTexture(path); }

	void SetPrimitiveCommon(PrimitiveCommon* p) { primitiveCommon_ = p; }


	//bool GetJointWorldMatrix(const std::string& jointName, Matrix4x4& out) const;


	//Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;

public:
	Matrix4x4 CalculateWorldMatrix() const;

	void SetUseEnvironmentMap(bool use) { useEnvironmentMap_ = use; }
	bool GetUseEnvironmentMap() const { return useEnvironmentMap_; }

	void SetEnvironmentTexturePath(const std::string& path) { environmentTexturePath_ = path; }
	const std::string& GetEnvironmentTexturePath() const { return environmentTexturePath_; }

	void SetEnvironmentCoefficient(float v) {
		EnsureInstanceMaterial_();
		if (instanceMaterialData_) {
			instanceMaterialData_->environmentCoefficient = v;
			instanceMaterialInitializedFromModel_ = true;
		}
	}

	float GetEnvironmentCoefficient() const {
		return instanceMaterialData_ ? instanceMaterialData_->environmentCoefficient : 0.0f;
	}

private:
	void EnsureInstanceMaterial_();
    void PrepareInstanceMeshMaterials_();

	bool useEnvironmentMap_ = false;
	std::string environmentTexturePath_;

	bool isVisible_=true;

private:

	DirectXCommon* dx_ = nullptr;

	Object3dCommon* object3dCommon = nullptr;

	SkinningCommon* skinningCommon_ = nullptr;

	Model* model_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceModel;/* = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));*/
	TransformationMatrix* transformationMatrixDataModel = nullptr;

	Transform transform;
	Transform cameraTransform;
	Camera* camera_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraGPU* cameraData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> effectParamResource_;
	EffectParam* effectParamData_ = nullptr;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> instanceMeshMaterials_;
    std::vector<Model::Material*> instanceMeshMaterialData_;
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> instanceMeshMaterialCBVs_;
	Microsoft::WRL::ComPtr<ID3D12Resource> instanceMaterialResource_;
	Model::Material* instanceMaterialData_ = nullptr;
	bool instanceMaterialInitializedFromModel_ = false;
	
	bool enableOutline_ = false;
	Vector4 outlineColor_ = {1.0f, 0.0f, 0.0f, 1.0f}; // Default Red
	float outlineThickness_ = 0.05f;

	bool enableDissolve_ = false;
	float dissolveThreshold_ = 0.5f;
	float dissolveEdgeWidth_ = 0.05f;
	Vector4 dissolveEdgeColor_ = { 1.0f, 0.0f, 0.0f, 1.0f };
	std::string maskTexturePath_ = "resources/noise0.png";

	bool enableRandom_ = false;
	float randomTime_ = 0.0f;

	std::string texturePath_ = "";
	bool useOverrideTexture_ = false;
	
	Object3dCommon::BlendMode blendMode_ = Object3dCommon::BlendMode::kBlendModeNormal;
	PrimitiveCommon* primitiveCommon_ = nullptr;
	bool isBillboard_ = false;

public:
	//=============
	//=============

	void PlayAnimation(const std::string& animName = "", bool loop = true) { if(animator_) animator_->PlayAnimation(animName, loop); }

	void CrossFadeTo(const std::string& animName, float fadeSec = 0.2f, bool loop = true) { if(animator_) animator_->CrossFadeTo(animName, fadeSec, loop); }

	bool IsAnimationFinished() const { return animator_ ? animator_->IsAnimationFinished() : true; }
	bool IsFading() const { return animator_ ? animator_->IsFading() : false; }

	Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const;
	bool TryGetJointWorldMatrix(const std::string& jointName, Matrix4x4& out) const;
	bool GetJointWorldPosition(const std::string& jointName, Vector3& out, const Vector3& localOffset = { 0.0f, 0.0f, 0.0f }) const;
	bool AttachObjectToJoint(Object3d& target, const std::string& jointName, const Vector3& localOffset, const Vector3& rotate, const Vector3& scale) const;
	bool HasJoint(const std::string& jointName) const;
	bool HasSkinningModel() const { return model_ && model_->HasSkinning(); }
	const Model::Skeleton* GetSkeleton() const { return model_ && model_->HasSkinning() ? &model_->GetSkeleton() : nullptr; }
	void SetManualJointTransform(int32_t jointIndex, const Vector3& translate, const Vector3& rotate, const Vector3& scale);
	bool SetManualJointTransform(const std::string& jointName, const Vector3& translate, const Vector3& rotate, const Vector3& scale);
	void ResetManualJointTransforms();

	const std::string& GetPlayingAnimName() const { static std::string empty; return animator_ ? animator_->GetPlayingAnimName() : empty; }
	void StopAnimation() { if(animator_) animator_->StopAnimation(); }
	void SetAnimationNodeName(const std::string& node) { if(animator_) animator_->SetAnimationNodeName(node); }

	bool HasAnimation() const { return animator_ ? animator_->HasAnimation() : false; }
	std::vector<std::string> GetAnimationNames() const {
		std::vector<std::string> names;
		if (!model_) return names;
		names.reserve(model_->GetAnimations().size());
		for (const auto& [name, animation] : model_->GetAnimations()) {
			(void)animation;
			names.push_back(name);
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	void SetDebugDrawBones(bool enable) { debugDrawBones_ = enable; }
	void SetDebugDrawBoneJoints(bool enable) { debugDrawBoneJoints_ = enable; }
	void SetBoneMarkerModel(const std::string& path) { boneMarkerModel_ = path; }
	void SetDebugBoneViewOffset(const Vector3& offset) { debugBoneViewOffset_ = offset; }
	void SetDebugBoneMarkerScale(float scale) {
		debugBoneMarkerScale_ = std::max(0.001f, scale);
		for (auto& marker : boneMarkers_) {
			if (marker) {
				marker->SetScale({
					debugBoneMarkerScale_,
					debugBoneMarkerScale_,
					debugBoneMarkerScale_
				});
			}
		}
	}
	void SetDebugSelectedBone(int32_t jointIndex) {
		debugSelectedBone_ = jointIndex;
		for (size_t i = 0; i < boneMarkers_.size(); ++i) {
			if (boneMarkers_[i]) {
				boneMarkers_[i]->SetMaterialColor(
					static_cast<int32_t>(i) == debugSelectedBone_
						? Vector4{ 1.0f, 0.9f, 0.05f, 1.0f }
						: Vector4{ 1.0f, 0.05f, 0.05f, 1.0f });
			}
		}
	}

private:
	std::unique_ptr<Animator> animator_;
	std::unique_ptr<Object3dLight> light_;
	const Object3dLight* sceneLight_ = nullptr;

	bool debugDrawBones_ = false;
	bool debugDrawBoneJoints_ = true;
	std::string boneMarkerModel_ = "cube/cube.obj";
	std::vector<std::unique_ptr<Object3d>> boneMarkers_;
	std::vector<std::unique_ptr<Object3d>> boneLinks_;
	Vector3 debugBoneViewOffset_{ 0.0f, 0.0f, 0.0f };
	float debugBoneMarkerScale_ = 0.03f;
	int32_t debugSelectedBone_ = -1;

	SrvManager* srvManager_ = nullptr;

private:
	int32_t swordNodeIndex_ = -1;
	uint32_t swordMeshIndex_ = 2;


public:

	//========================
	//video
	//========================

	void SetVideo(VideoPlayerMF* v) { video_ = v; useVideo_ = (v != nullptr); }

	void DrawWithOverrideSrv(const D3D12_GPU_DESCRIPTOR_HANDLE& srv);


private:
	//=======
	//video
	//=======

	bool useVideo_ = false;
	VideoPlayerMF* video_ = nullptr;

};

