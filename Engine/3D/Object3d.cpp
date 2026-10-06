#include "../Utility/AssetLoadProfile.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "PrimitiveCommon.h"
#include "DirectionalShadowMap.h"


//Vector3 Normalize(const Vector3& v) {
//	float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
//	if (length == 0.0f) return { 0.0f, 0.0f, 0.0f };
//	return { v.x / length, v.y / length, v.z / length };
//}

static void ApplyAnimation(Model::Skeleton& skeleton, const Animation& animation, float time) {
	for (auto& joint : skeleton.joints) {

		auto it = animation.nodeAnimations.find(joint.name);
		if (it == animation.nodeAnimations.end()) {
			continue;
		}

		const NodeAnimation& na = it->second;

		Vector3 t = joint.transform.translate;
		Quaternion r = joint.transform.rotate;
		Vector3 s = joint.transform.scale;

		if (!na.translate.keyframes.empty()) t = CalculateValue(na.translate.keyframes, time);
		if (!na.rotate.keyframes.empty())    r = CalculateValue(na.rotate.keyframes, time);
		if (!na.scale.keyframes.empty())     s = CalculateValue(na.scale.keyframes, time);

		joint.transform.translate = t;
		joint.transform.rotate = r;
		joint.transform.scale = s;
	}
}

static uint32_t CalcTotalVertexCount(const Model::ModelData& modelData) {
	uint64_t total = 0;
	for (const auto& m : modelData.meshes) {
		total += m.vertices.size();
	}
	return static_cast<uint32_t>(total);
}

static Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix) {
	return {
		point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
		point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
		point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2],
	};
}

void Object3d::Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx) {
	SrvManager* srv = object3dCommon ? object3dCommon->GetSrvManager() : nullptr;
	SkinningCommon* skin = object3dCommon ? object3dCommon->GetSkinningCommon() : nullptr;

	Initialize(object3dCommon, dx, srv, skin);
}

void Object3d::Initialize(Object3dCommon* object3dCommon, DirectXCommon* dx, SrvManager* srv, SkinningCommon* skinCom) {
#ifdef _DEBUG
    ++debugInitializationCount;
#endif
	this->object3dCommon = object3dCommon;
	dx_ = dx;
	srvManager_ = srv;
	skinningCommon_ = skinCom;

	if (!dx_) {
		OutputDebugStringA("[Object3d] Initialize failed: DirectXCommon is null.\n");
		return;
	}

	transformationMatrixResourceModel= dx->CreateBufferResource(sizeof(TransformationMatrix));
	if (!transformationMatrixResourceModel) {
		OutputDebugStringA("[Object3d] Initialize failed: transformationMatrixResourceModel is null.\n");
		return;
	}
	transformationMatrixResourceModel->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixDataModel));
	transformationMatrixDataModel->WVP = Matrix4x4::MakeIdentity4x4();
	transformationMatrixDataModel->World = Matrix4x4::MakeIdentity4x4();

	light_ = std::make_unique<Object3dLight>();
	light_->Initialize(dx);
	TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");

	animator_ = std::make_unique<Animator>();
	if (model_) {
		animator_->Initialize(model_);
		if (model_->HasSkinning() && srvManager_) {
			animator_->CreateSkinCluster(
				dx_->GetDevice(),
				dx_,
				srvManager_,
				TextureManager::GetInstance()->GetSrvDescriptorHeap(),
				dx_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			);
		}
	}

	transform = { {1.0f,1.0f,1.0f},
				  {0.0f,0.0f,0.0f},
				  {0.0f,0.0f,0.0f} };
	cameraTransform = { {1.0f,1.0f,1.0f},
						{0.3f,0.0f,0.0f},
						{0.0f,4.0f,-10.0f} };

	this->camera_ = object3dCommon->GetDefaultCamera();

	cameraResource_ = dx_->CreateBufferResource(sizeof(CameraGPU));
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

	effectParamResource_ = dx_->CreateBufferResource(sizeof(EffectParam));
	effectParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&effectParamData_));
	if (effectParamData_) {
		effectParamData_->outlineColor = outlineColor_;
		effectParamData_->outlineThickness = outlineThickness_;
		effectParamData_->enableOutline = enableOutline_ ? 1.0f : 0.0f;
		effectParamData_->dissolveThreshold = dissolveThreshold_;
		effectParamData_->enableDissolve = enableDissolve_ ? 1.0f : 0.0f;
		effectParamData_->dissolveEdgeWidth = dissolveEdgeWidth_;
		effectParamData_->dissolveEdgeColor = dissolveEdgeColor_;
		
		effectParamData_->enableRandom = enableRandom_ ? 1.0f : 0.0f;
		effectParamData_->randomTime = randomTime_;
	}
	
	if (!maskTexturePath_.empty()) {
		TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
	}
}

void Object3d::EnsureInstanceMaterial_()
{
	if (!dx_) {
		return;
	}

	if (!instanceMaterialResource_) {
		instanceMaterialResource_ = dx_->CreateBufferResource(sizeof(Model::Material));
		if (!instanceMaterialResource_) {
			return;
		}
		instanceMaterialResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceMaterialData_));
		if (instanceMaterialData_) {
			*instanceMaterialData_ = {};
			instanceMaterialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
			instanceMaterialData_->enableLighting = 1;
			instanceMaterialData_->uvTransform = Matrix4x4::MakeIdentity4x4();
			instanceMaterialData_->shininess = 64.0f;
			instanceMaterialData_->environmentCoefficient = 0.0f;
		}
	}

	if (!instanceMaterialData_ || instanceMaterialInitializedFromModel_ || !model_ || !model_->GetMaterial()) {
		return;
	}

	*instanceMaterialData_ = *model_->GetMaterial();
	instanceMaterialInitializedFromModel_ = true;
}

Matrix4x4 Object3d::CalculateWorldMatrix() const {
	if (isBillboard_ && camera_) {
		Matrix4x4 s = Matrix4x4::Scale(transform.scale);
		Matrix4x4 r_local = Matrix4x4::RotateXYZ(transform.rotate.x, transform.rotate.y, transform.rotate.z);
		
		const Matrix4x4& camWorld = camera_->GetWorldMatrix();
		Matrix4x4 r_cam = Matrix4x4::MakeIdentity4x4();
		r_cam.m[0][0] = camWorld.m[0][0]; r_cam.m[0][1] = camWorld.m[0][1]; r_cam.m[0][2] = camWorld.m[0][2];
		r_cam.m[1][0] = camWorld.m[1][0]; r_cam.m[1][1] = camWorld.m[1][1]; r_cam.m[1][2] = camWorld.m[1][2];
		r_cam.m[2][0] = camWorld.m[2][0]; r_cam.m[2][1] = camWorld.m[2][1]; r_cam.m[2][2] = camWorld.m[2][2];
		
		Matrix4x4 r = Matrix4x4::Multiply(r_local, r_cam);
		Matrix4x4 t = Matrix4x4::Translation(transform.translate);
		return Matrix4x4::Multiply(Matrix4x4::Multiply(s, r), t);
	}
	return Matrix4x4::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
}

void Object3d::Update(float dt)
{
	Matrix4x4 worldMatrixModel = CalculateWorldMatrix();

	if (animator_) {
		animator_->Update(dt);
		if (poseModifier_ && animator_->IsPoseReady()) {
			poseModifier_(animator_->GetPoseSkeleton(), worldMatrixModel, dt);
		}
		animator_->UpdateSkinCluster(dx_);
	}


	if (model_) {
		if (!model_->HasSkinning()) {
			const Matrix4x4& root = model_->GetRootLocalMatrix();
			worldMatrixModel = Matrix4x4::Multiply(root, worldMatrixModel);
		}
	}

	// 3) camera
	if (!camera_) {
		camera_ = object3dCommon->GetDefaultCamera();
	}

	if (debugDrawBones_ && model_ && model_->HasSkinning() && animator_ && animator_->IsPoseReady()) {
		EnsureBoneDebugObjects_();
		const auto& poseSkeleton = animator_->GetPoseSkeleton();
		size_t linkIndex = 0;
		for (size_t i = 0; i < poseSkeleton.joints.size() && i < boneMarkers_.size(); ++i) {

			const auto& j = poseSkeleton.joints[i];

			Matrix4x4 jointWorld =
				Matrix4x4::Multiply(j.skeletonSpaceMatrix, worldMatrixModel);

			Vector3 pos{
				jointWorld.m[3][0] + debugBoneViewOffset_.x,
				jointWorld.m[3][1] + debugBoneViewOffset_.y,
				jointWorld.m[3][2] + debugBoneViewOffset_.z
			};

			boneMarkers_[i]->SetTranslate(pos);
			boneMarkers_[i]->Update(0.0f);

			if (j.parent && *j.parent >= 0 &&
				static_cast<size_t>(*j.parent) < poseSkeleton.joints.size() &&
				linkIndex < boneLinks_.size()) {
				const auto& parent = poseSkeleton.joints[*j.parent];
				const Matrix4x4 parentWorld =
					Matrix4x4::Multiply(parent.skeletonSpaceMatrix, worldMatrixModel);
				const Vector3 parentPos{
					parentWorld.m[3][0] + debugBoneViewOffset_.x,
					parentWorld.m[3][1] + debugBoneViewOffset_.y,
					parentWorld.m[3][2] + debugBoneViewOffset_.z
				};

				const Vector3 delta{
					pos.x - parentPos.x,
					pos.y - parentPos.y,
					pos.z - parentPos.z
				};
				const float length = std::sqrt(
					delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
				if (length > 0.0001f) {
					const float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
					const float rotateX = std::atan2(delta.z, horizontal);
					const float rotateZ = -std::atan2(delta.x, delta.y);
					const float thickness = debugBoneMarkerScale_ * 0.42f;

					auto& link = boneLinks_[linkIndex];
					link->SetTranslate({
						(parentPos.x + pos.x) * 0.5f,
						(parentPos.y + pos.y) * 0.5f,
						(parentPos.z + pos.z) * 0.5f
					});
					link->SetScale({ thickness, length * 0.5f, thickness });
					link->SetRotate({ rotateX, 0.0f, rotateZ });
					link->Update(0.0f);
				}
				++linkIndex;
			}
		}
	}

	// WVP
	Matrix4x4 wvpModel = worldMatrixModel;
	if (camera_) {
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		wvpModel = Matrix4x4::Multiply(worldMatrixModel, vp);
	}

	transformationMatrixDataModel->WVP = wvpModel;
	transformationMatrixDataModel->World = worldMatrixModel;

	// 4) WorldInverseTranspose
	Matrix4x4 invW = Matrix4x4::Inverse(worldMatrixModel);
	transformationMatrixDataModel->WorldInverseTranspose = Matrix4x4::Transpose(invW);

	if (effectParamData_) {
		effectParamData_->outlineColor = outlineColor_;
		effectParamData_->outlineThickness = outlineThickness_;
		effectParamData_->enableOutline = enableOutline_ ? 1.0f : 0.0f;
		effectParamData_->dissolveThreshold = dissolveThreshold_;
		effectParamData_->enableDissolve = enableDissolve_ ? 1.0f : 0.0f;
		effectParamData_->dissolveEdgeWidth = dissolveEdgeWidth_;
		effectParamData_->dissolveEdgeColor = dissolveEdgeColor_;
		
		effectParamData_->enableRandom = enableRandom_ ? 1.0f : 0.0f;
		effectParamData_->randomTime = randomTime_;
	}
	
	if (!maskTexturePath_.empty()) {
		TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
	}
}



void Object3d::Draw()
{
	if (!isVisible_) {
		return;
	}

	if (!model_) {
		OutputDebugStringA("[Object3d] Draw skipped: model_ is null\n");
		return;
	}

	if (cameraData_ && camera_) {
		cameraData_->worldPosition = camera_->GetTranslate();
	}

	auto* cmd = dx_->GetCommandList();

	// SRV heap
	ID3D12DescriptorHeap* heaps[] = {
		TextureManager::GetInstance()->GetSrvDescriptorHeap()
	};
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	// ------------------------------------------------------------
	// ------------------------------------------------------------
	auto BindEnvironmentMapIfNeeded = [&]() {
		if (!useEnvironmentMap_) {
			return;
		}

		if (environmentTexturePath_.empty()) {
			OutputDebugStringA("[EnvMap] environmentTexturePath_ is empty\n");
			return;
		}

		TextureManager::GetInstance()->LoadTexture(environmentTexturePath_);

		// RootParameter 7 : t2
		cmd->SetGraphicsRootDescriptorTable(
			7,
			TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
		);
		};

	D3D12_GPU_DESCRIPTOR_HANDLE overrideTextureHandle{};
	const D3D12_GPU_DESCRIPTOR_HANDLE* overrideTexture = nullptr;
	if (useOverrideTexture_ && !texturePath_.empty()) {
		if (!TextureManager::GetInstance()->HasTexture(texturePath_)) {
			TextureManager::GetInstance()->LoadTexture(texturePath_);
		}
		overrideTextureHandle = TextureManager::GetInstance()->GetSrvHandleGPU(texturePath_);
		overrideTexture = &overrideTextureHandle;
	}

	if (model_->HasSkinning()) {
		EnsureInstanceMaterial_();
		if (instanceMaterialResource_) {
			PrepareInstanceMeshMaterials_();
            model_->SetMaterialCBVOverride(instanceMaterialResource_->GetGPUVirtualAddress(), instanceMaterialData_, &instanceMeshMaterialCBVs_);
		}
		// =====================================================
		// =====================================================
		if (animator_ && animator_->IsPoseReady()) {
			auto& skinCluster = animator_->GetSkinCluster();
			if (skinCluster.isUavReady) {
				CD3DX12_RESOURCE_BARRIER preBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
					skinCluster.outputVertexResource.Get(),
					D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				);
				cmd->ResourceBarrier(1, &preBarrier);
			} else {
				skinCluster.isUavReady = true;
			}

			cmd->SetPipelineState(skinningCommon_->GetComputePipelineState());
			cmd->SetComputeRootSignature(skinningCommon_->GetComputeRootSignature());

			cmd->SetComputeRootDescriptorTable(0, skinCluster.paletteSrvHandle.second);
			cmd->SetComputeRootDescriptorTable(1, skinCluster.inputVertexSrvHandle.second);
			cmd->SetComputeRootDescriptorTable(2, skinCluster.influenceSrvHandle.second);
			cmd->SetComputeRootDescriptorTable(3, skinCluster.outputVertexUavHandle.second);
			cmd->SetComputeRootConstantBufferView(4, skinCluster.skinningInformationResource->GetGPUVirtualAddress());

			cmd->Dispatch((model_->GetVertexCount() + 1023) / 1024, 1, 1);

			CD3DX12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
				skinCluster.outputVertexResource.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
			);
			cmd->ResourceBarrier(1, &postBarrier);
		}

		// =====================================================
		auto SetNormalPipelineState = [&]() {
			if (primitiveCommon_) {
				if (useEnvironmentMap_) {
					primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
				} else {
					primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
				}
			} else {
				if (useEnvironmentMap_) {
					object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
				} else {
					object3dCommon->SetGraphicsPipelineState(blendMode_);
				}
			}
			cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		};

		SetNormalPipelineState();

		// Transform (Root 1)
		cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

		BindDirectionalShadow_(cmd);

		cmd->SetGraphicsRootConstantBufferView(3, (sceneLight_ ? sceneLight_ : light_.get())->GetDirectionalLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(5, (sceneLight_ ? sceneLight_ : light_.get())->GetPointLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(6, (sceneLight_ ? sceneLight_ : light_.get())->GetSpotLightResource()->GetGPUVirtualAddress());

		BindEnvironmentMapIfNeeded();

		if (!maskTexturePath_.empty()) {
			cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
		}

		if (animator_ && animator_->IsPoseReady()) {
			if (enableOutline_ && object3dCommon) {
				object3dCommon->SetGraphicsPipelineStateOutline();
				cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
				model_->DrawSkinnedCompute(cmd, animator_->GetSkinCluster(), overrideTexture);
				SetNormalPipelineState();
			}
			
			if (!maskTexturePath_.empty()) {
				cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
			}
			cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
			
			model_->DrawSkinnedCompute(cmd, animator_->GetSkinCluster(), overrideTexture);
		}

		// =====================================================
		// =====================================================
		{
			auto SetNormalPipelineState = [&]() {
				if (primitiveCommon_) {
					if (useEnvironmentMap_) {
						primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
					} else {
						primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
					}
				} else {
					if (useEnvironmentMap_) {
						object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
					} else {
						object3dCommon->SetGraphicsPipelineState(blendMode_);
					}
				}
				cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			};

			SetNormalPipelineState();

			BindDirectionalShadow_(cmd);

			cmd->SetGraphicsRootConstantBufferView(3, (sceneLight_ ? sceneLight_ : light_.get())->GetDirectionalLightResource()->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(5, (sceneLight_ ? sceneLight_ : light_.get())->GetPointLightResource()->GetGPUVirtualAddress());
			cmd->SetGraphicsRootConstantBufferView(6, (sceneLight_ ? sceneLight_ : light_.get())->GetSpotLightResource()->GetGPUVirtualAddress());

			// EnvMap (Root 7 : t2)
			BindEnvironmentMapIfNeeded();

		if (!maskTexturePath_.empty()) {
			cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
		}
		cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

			// Material / VB / IB
			cmd->SetGraphicsRootConstantBufferView(
				0,
				instanceMaterialResource_
					? instanceMaterialResource_->GetGPUVirtualAddress()
					: model_->GetMaterialCBV());
			cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
			cmd->IASetIndexBuffer(&model_->GetIBV());

			const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
			const Matrix4x4 originalWorld = transformationMatrixDataModel->World;
			const Matrix4x4 baseWorld = CalculateWorldMatrix();

			// -------------------------------------------------
			// -------------------------------------------------
			const Animation* anim = nullptr;
			float animTime = 0.0f;
			if (animator_ && animator_->HasAnimation()) {
				const auto& anims = model_->GetAnimations();
				if (!animator_->GetPlayingAnimName().empty()) {
					auto itA = anims.find(animator_->GetPlayingAnimName());
					if (itA != anims.end()) {
						anim = &itA->second;
					}
				}
				if (!anim && !anims.empty()) {
					anim = &anims.begin()->second;
				}
				animTime = animator_->GetTime();
			}

			std::vector<Matrix4x4> nodeGlobals;
			model_->ComputeNodeGlobalMatrices(anim, animTime, nodeGlobals);

			for (const auto& inst : model_->GetNodeInstances()) {
				if (model_->IsMeshSkinned(inst.meshIndex)) {
					continue;
				}

				const Matrix4x4 nodeWorld = nodeGlobals[inst.nodeIndex];
				Matrix4x4 world = Matrix4x4::Multiply(nodeWorld, baseWorld);
				Matrix4x4 wvpM = Matrix4x4::Multiply(world, vp);

				transformationMatrixDataModel->World = world;
				transformationMatrixDataModel->WVP = wvpM;
				transformationMatrixDataModel->WorldInverseTranspose =
					Matrix4x4::Transpose(Matrix4x4::Inverse(world));

				cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());
				
				if (enableOutline_ && object3dCommon) {
					object3dCommon->SetGraphicsPipelineStateOutline();
					cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
					model_->DrawOneMesh(cmd, inst.meshIndex, 2, overrideTexture);
					SetNormalPipelineState();
				}
				
				if (!maskTexturePath_.empty()) {
					cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
				}
				cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

				model_->DrawOneMesh(cmd, inst.meshIndex, 2, overrideTexture);
			}

			transformationMatrixDataModel->World = originalWorld;
			transformationMatrixDataModel->WVP = Matrix4x4::Multiply(originalWorld, vp);
			transformationMatrixDataModel->WorldInverseTranspose =
				Matrix4x4::Transpose(Matrix4x4::Inverse(originalWorld));
		}
	} else {
		EnsureInstanceMaterial_();
		if (instanceMaterialResource_) {
			PrepareInstanceMeshMaterials_();
            model_->SetMaterialCBVOverride(instanceMaterialResource_->GetGPUVirtualAddress(), instanceMaterialData_, &instanceMeshMaterialCBVs_);
		}
		auto SetNormalPipelineState = [&]() {
			if (primitiveCommon_) {
				if (useEnvironmentMap_) {
					primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
				} else {
					primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
				}
			} else {
				if (useEnvironmentMap_) {
					object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
				} else {
					object3dCommon->SetGraphicsPipelineState(blendMode_);
				}
			}
			cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		};

		SetNormalPipelineState();

		// light/camera CBV
		BindDirectionalShadow_(cmd);
		cmd->SetGraphicsRootConstantBufferView(3, (sceneLight_ ? sceneLight_ : light_.get())->GetDirectionalLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(5, (sceneLight_ ? sceneLight_ : light_.get())->GetPointLightResource()->GetGPUVirtualAddress());
		cmd->SetGraphicsRootConstantBufferView(6, (sceneLight_ ? sceneLight_ : light_.get())->GetSpotLightResource()->GetGPUVirtualAddress());

		// EnvMap (Root 7 : t2)
		BindEnvironmentMapIfNeeded();

		if (!maskTexturePath_.empty()) {
			cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
		}
		cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

		// VB/IB/Material
		cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
		cmd->IASetIndexBuffer(&model_->GetIBV());
		cmd->SetGraphicsRootConstantBufferView(
			0,
			instanceMaterialResource_
				? instanceMaterialResource_->GetGPUVirtualAddress()
				: model_->GetMaterialCBV());

		if (animator_ && animator_->HasAnimation()) {

			const auto& anims = model_->GetAnimations();
			const Animation* anim = nullptr;
			float animTime = animator_->GetTime();

			if (!animator_->GetPlayingAnimName().empty()) {
				auto itA = anims.find(animator_->GetPlayingAnimName());
				if (itA != anims.end()) {
					anim = &itA->second;
				}
			}
			if (!anim && !anims.empty()) {
				anim = &anims.begin()->second;
			}

			std::vector<Matrix4x4> nodeGlobals;
			model_->ComputeNodeGlobalMatrices(anim, animTime, nodeGlobals);

			const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
			const Matrix4x4 originalWorld = transformationMatrixDataModel->World;
			const Matrix4x4 baseWorld = CalculateWorldMatrix();

			for (const auto& inst : model_->GetNodeInstances()) {
				const Matrix4x4 nodeWorld = nodeGlobals[inst.nodeIndex];

				Matrix4x4 world = Matrix4x4::Multiply(nodeWorld, baseWorld);
				Matrix4x4 wvp = Matrix4x4::Multiply(world, vp);

				transformationMatrixDataModel->World = world;
				transformationMatrixDataModel->WVP = wvp;
				transformationMatrixDataModel->WorldInverseTranspose =
					Matrix4x4::Transpose(Matrix4x4::Inverse(world));

				cmd->SetGraphicsRootConstantBufferView(
					1, transformationMatrixResourceModel->GetGPUVirtualAddress());

				if (enableOutline_ && object3dCommon) {
					object3dCommon->SetGraphicsPipelineStateOutline();
					cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
					model_->DrawOneMesh(cmd, inst.meshIndex, 2, overrideTexture);
					SetNormalPipelineState();
				}

				model_->DrawOneMesh(cmd, inst.meshIndex, 2, overrideTexture);
			}

			transformationMatrixDataModel->World = originalWorld;
			transformationMatrixDataModel->WVP = Matrix4x4::Multiply(originalWorld, vp);
			transformationMatrixDataModel->WorldInverseTranspose =
				Matrix4x4::Transpose(Matrix4x4::Inverse(originalWorld));
		} else {
			cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

			if (video_ && video_->IsReady()) {
				video_->ReadNextFrame();
				video_->UploadToGpu(cmd);

				D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
				model_->Draw(cmd, 1, &vh);

				video_->EndFrame(cmd);
			} else {

				if (useOverrideTexture_) {
					auto handle = TextureManager::GetInstance()->GetSrvHandleGPU(texturePath_);
					
					if (enableOutline_ && object3dCommon) {
						object3dCommon->SetGraphicsPipelineStateOutline();
						cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
						model_->Draw(cmd, 1, &handle);
						SetNormalPipelineState();
					}
					
					model_->Draw(cmd, 1, &handle);
				} else {
					if (enableOutline_ && object3dCommon) {
						object3dCommon->SetGraphicsPipelineStateOutline();
						cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());
						model_->Draw(cmd);
						SetNormalPipelineState();
					}

					model_->Draw(cmd);
				}
			}
		}
	}

	model_->ClearMaterialCBVOverride();

	// debug bones
	if (debugDrawBones_ && !boneMarkers_.empty()) {
		for (auto& link : boneLinks_) {
			link->Draw();
		}
		if (debugDrawBoneJoints_) {
			for (auto& m : boneMarkers_) {
				m->Draw();
			}
		}
	}
}

void Object3d::BindDirectionalShadow_(ID3D12GraphicsCommandList* cmd, UINT rootIndex) {
    const auto* light=sceneLight_ ? sceneLight_ : light_.get();
    auto srv=light->GetDirectionalShadowSrv();
    if (!srv.ptr) srv=TextureManager::GetInstance()->GetSrvHandleGPU("resources/white1x1.png");
    cmd->SetGraphicsRootDescriptorTable(rootIndex,srv);
}

void Object3d::DrawDirectionalShadow(DirectionalShadowMap& shadow, const std::vector<uint32_t>& excludedMeshes) {
    if (!isVisible_ || !model_) return;
    const auto world=CalculateWorldMatrix();
    const auto casts=[&](uint32_t index) { return std::find(excludedMeshes.begin(),excludedMeshes.end(),index)==excludedMeshes.end(); };
    if (animator_ && animator_->HasAnimation() && !model_->HasSkinning()) {
        const auto& animations=model_->GetAnimations();
        const Animation* animation=nullptr;
        const auto found=animations.find(animator_->GetPlayingAnimName());
        if (found!=animations.end()) animation=&found->second;
        if (!animation && !animations.empty()) animation=&animations.at(model_->GetDefaultAnimationName());
        std::vector<Matrix4x4> nodes;
        model_->ComputeNodeGlobalMatrices(animation,animator_->GetTime(),nodes);
        for (const auto& instance : model_->GetNodeInstances())
            if (casts(instance.meshIndex)) shadow.DrawMesh(*model_,instance.meshIndex,Matrix4x4::Multiply(nodes[instance.nodeIndex],world));
    } else {
        for (uint32_t i=0;i<model_->GetMeshCount();++i) if (casts(i)) shadow.DrawMesh(*model_,i,world);
    }
}

void Object3d::DrawWithOverrideSrv(const D3D12_GPU_DESCRIPTOR_HANDLE& srv)
{
	if (!isVisible_) {
		return;
	}

	if (!model_) {
		return;
	}

	auto* cmd = dx_->GetCommandList();

	ID3D12DescriptorHeap* heaps[] = {
		TextureManager::GetInstance()->GetSrvDescriptorHeap()
	};
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	if (primitiveCommon_) {
		if (useEnvironmentMap_) {
			primitiveCommon_->SetGraphicsPipelineStateEnvMap(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
		} else {
			primitiveCommon_->SetGraphicsPipelineState(static_cast<PrimitiveCommon::BlendMode>(blendMode_));
		}
	} else {
		if (useEnvironmentMap_) {
			object3dCommon->SetGraphicsPipelineStateEnvMap(blendMode_);
		} else {
			object3dCommon->SetGraphicsPipelineState(blendMode_);
		}
	}

	BindDirectionalShadow_(cmd);

	cmd->SetGraphicsRootConstantBufferView(3, (sceneLight_ ? sceneLight_ : light_.get())->GetDirectionalLightResource()->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(5, (sceneLight_ ? sceneLight_ : light_.get())->GetPointLightResource()->GetGPUVirtualAddress());
	cmd->SetGraphicsRootConstantBufferView(6, (sceneLight_ ? sceneLight_ : light_.get())->GetSpotLightResource()->GetGPUVirtualAddress());

	// RootParameter 7 : t2
	if (useEnvironmentMap_) {
		if (!environmentTexturePath_.empty()) {
			TextureManager::GetInstance()->LoadTexture(environmentTexturePath_);
			cmd->SetGraphicsRootDescriptorTable(
				7,
				TextureManager::GetInstance()->GetSrvHandleGPU(environmentTexturePath_)
			);
		}
	}

	cmd->IASetVertexBuffers(0, 1, &model_->GetVBV());
	cmd->IASetIndexBuffer(&model_->GetIBV());
	EnsureInstanceMaterial_();
	if (instanceMaterialResource_) {
			PrepareInstanceMeshMaterials_();
            model_->SetMaterialCBVOverride(instanceMaterialResource_->GetGPUVirtualAddress(), instanceMaterialData_, &instanceMeshMaterialCBVs_);
	}
	cmd->SetGraphicsRootConstantBufferView(
		0,
		instanceMaterialResource_
			? instanceMaterialResource_->GetGPUVirtualAddress()
			: model_->GetMaterialCBV());
	cmd->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress());

	if (!maskTexturePath_.empty()) {
		cmd->SetGraphicsRootDescriptorTable(9, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));
	}
	cmd->SetGraphicsRootConstantBufferView(8, effectParamResource_->GetGPUVirtualAddress());

	model_->Draw(cmd, 1, &srv);
	model_->ClearMaterialCBVOverride();
}

void Object3d::SetTexture(const std::string& path)
{
	texturePath_ = path;
	TextureManager::GetInstance()->LoadTexture(path);
	useOverrideTexture_ = true;
}

void Object3d::EnsureBoneDebugObjects_() {
    if(!model_ || !model_->HasSkinning() || !animator_ || !boneMarkers_.empty()) return;
    AssetLoading::Timer timer("object.bone-debug-create");
	if (model_->HasSkinning() && animator_) {
		const auto& skel = animator_->GetPoseSkeleton();
		boneMarkers_.reserve(skel.joints.size());
		boneLinks_.reserve(skel.joints.size());

		for (size_t i = 0; i < skel.joints.size(); ++i) {
			auto marker = std::make_unique<Object3d>();
			marker->Initialize(object3dCommon, dx_, srvManager_,skinningCommon_);
			marker->SetModel(boneMarkerModel_);
			marker->SetScale({
				debugBoneMarkerScale_,
				debugBoneMarkerScale_,
				debugBoneMarkerScale_
			});
			marker->SetRotate({ 0,0,0 });
			marker->SetEnableLighting(0);
			marker->SetMaterialColor({ 1.0f, 0.05f, 0.05f, 1.0f });
			boneMarkers_.push_back(std::move(marker));

			if (skel.joints[i].parent) {
				auto link = std::make_unique<Object3d>();
				link->Initialize(object3dCommon, dx_, srvManager_, skinningCommon_);
				link->SetModel(boneMarkerModel_);
				link->SetScale({ 0.01f, 0.01f, 0.01f });
				link->SetRotate({ 0,0,0 });
				link->SetEnableLighting(0);
				link->SetMaterialColor({ 0.15f, 0.9f, 0.3f, 1.0f });
				boneLinks_.push_back(std::move(link));
			}
		}
	}

    SetDebugSelectedBone(debugSelectedBone_);
}

void Object3d::SetModel(const std::string& filePath) {
    AssetLoading::Timer timer("object.set-model",filePath);
	poseModifier_ = {};
	auto* mgr = ModelManager::GetInstance();

	Model* m = mgr->FindModel(filePath);
	if (!m) {
		mgr->LoadModel(filePath);
		m = mgr->FindModel(filePath);
	}
	model_ = m;

	boneMarkers_.clear();
	boneLinks_.clear();

	if (!model_) { return; }

	if (animator_) {
		animator_->Initialize(model_);
		if (model_->HasSkinning() && srvManager_) {
			animator_->CreateSkinCluster(
				dx_->GetDevice(),
				dx_,
				srvManager_,
				TextureManager::GetInstance()->GetSrvDescriptorHeap(),
				dx_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			);
		}
	}

    if(AssetLoading::Option("YAN_EAGER_BONE_DEBUG",false)) EnsureBoneDebugObjects_();

	swordNodeIndex_ = -1;
	swordMeshIndex_ = 2;

	if (model_) {
		swordNodeIndex_ = model_->FindNodeIndexByName("sword");
	}


}






Matrix4x4 Object3d::GetJointWorldMatrix(const std::string& jointName) const
{
	Matrix4x4 jointWorld = Matrix4x4::MakeIdentity4x4();
	TryGetJointWorldMatrix(jointName, jointWorld);
	return jointWorld;
}

bool Object3d::TryGetJointWorldMatrix(const std::string& jointName, Matrix4x4& out) const
{
	if (!model_ || !model_->HasSkinning() || !animator_ || !animator_->IsPoseReady()) {
		return false;
	}

	const auto& poseSkeleton = animator_->GetPoseSkeleton();
	auto it = poseSkeleton.jointMap.find(jointName);
	if (it == poseSkeleton.jointMap.end()) {
		return false;
	}

	const int32_t jointIndex = it->second;
	Matrix4x4 worldMatrixModel = CalculateWorldMatrix();

	out = Matrix4x4::Multiply(
		poseSkeleton.joints[jointIndex].skeletonSpaceMatrix,
		worldMatrixModel);

	return true;
}

bool Object3d::GetJointWorldPosition(const std::string& jointName, Vector3& out, const Vector3& localOffset) const
{
	Matrix4x4 jointWorld{};
	if (!TryGetJointWorldMatrix(jointName, jointWorld)) {
		return false;
	}

	out = TransformPoint(localOffset, jointWorld);
	return true;
}

bool Object3d::AttachObjectToJoint(Object3d& target, const std::string& jointName, const Vector3& localOffset, const Vector3& rotate, const Vector3& scale) const
{
	Vector3 position{};
	if (!GetJointWorldPosition(jointName, position, localOffset)) {
		return false;
	}

	target.SetTranslate(position);
	target.SetRotate(rotate);
	target.SetScale(scale);
	return true;
}

bool Object3d::HasJoint(const std::string& jointName) const
{
	if (!model_ || !model_->HasSkinning() || !animator_ || !animator_->IsPoseReady()) {
		return false;
	}

	const auto& poseSkeleton = animator_->GetPoseSkeleton();
	return poseSkeleton.jointMap.contains(jointName);
}

void Object3d::SetManualJointTransform(int32_t jointIndex, const Vector3& translate, const Vector3& rotate, const Vector3& scale)
{
	if (!animator_) {
		return;
	}

	Animator::ManualJointTransform transform{};
	transform.translate = translate;
	transform.rotate = rotate;
	transform.scale = scale;
	animator_->SetManualJointTransform(jointIndex, transform);
}

bool Object3d::SetManualJointTransform(const std::string& jointName, const Vector3& translate, const Vector3& rotate, const Vector3& scale)
{
	if (!animator_ || !animator_->IsPoseReady()) {
		return false;
	}

	const auto& poseSkeleton = animator_->GetPoseSkeleton();
	const auto joint = poseSkeleton.jointMap.find(jointName);
	if (joint == poseSkeleton.jointMap.end()) {
		return false;
	}

	SetManualJointTransform(joint->second, translate, rotate, scale);
	return true;
}

void Object3d::ResetManualJointTransforms()
{
	if (animator_) {
		animator_->ResetManualJointTransforms();
	}
}

void Object3d::PrepareInstanceMeshMaterials_() {
    if (!model_ || !instanceMaterialData_) return;
    const auto& materials = model_->GetModelData().materials;
    while (instanceMeshMaterials_.size() < materials.size()) {
        auto resource = dx_->CreateBufferResource(sizeof(Model::Material));
        Model::Material* mapped = nullptr;
        resource->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        instanceMeshMaterialData_.push_back(mapped);
        instanceMeshMaterialCBVs_.push_back(resource->GetGPUVirtualAddress());
        instanceMeshMaterials_.push_back(std::move(resource));
    }
    for (size_t i=0; i<materials.size(); ++i) {
        auto& material = *instanceMeshMaterialData_[i];
        material = *instanceMaterialData_;
        const auto& base = materials[i].baseColor;
        material.color = {material.color.x*base.x, material.color.y*base.y,
            material.color.z*base.z, material.color.w*base.w};
    }
}
