#include "../Utility/AssetLoadProfile.h"
#include "Animator.h"
#include "AnimationEvaluate.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
Quaternion MultiplyQuaternion(const Quaternion& a, const Quaternion& b)
{
	Quaternion out{};
	out.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	out.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	out.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	out.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return Normalize(out);
}

Quaternion MakeAxisAngleQuaternion(float x, float y, float z, float angle)
{
	const float half = angle * 0.5f;
	const float s = std::sin(half);
	Quaternion out{};
	out.x = x * s;
	out.y = y * s;
	out.z = z * s;
	out.w = std::cos(half);
	return out;
}

Quaternion MakeEulerQuaternion(const Vector3& rotate)
{
	const Quaternion qx = MakeAxisAngleQuaternion(1.0f, 0.0f, 0.0f, rotate.x);
	const Quaternion qy = MakeAxisAngleQuaternion(0.0f, 1.0f, 0.0f, rotate.y);
	const Quaternion qz = MakeAxisAngleQuaternion(0.0f, 0.0f, 1.0f, rotate.z);
	return MultiplyQuaternion(MultiplyQuaternion(qx, qy), qz);
}
}

void Animator::Initialize(Model* model) {
	model_ = model;
	poseReady_ = false;
	manualJointTransforms_.clear();
	if (model_ && model_->HasSkinning()) {
		poseSkeleton_ = model_->GetSkeleton();
		poseReady_ = true;
		Model::UpdateSkeleton(poseSkeleton_);
		manualJointTransforms_.assign(
			poseSkeleton_.joints.size(),
			ManualJointTransform{});
	}
}

// 2つのスケルトンポーズをtでブレンド（t=0:a, t=1:b）
void Animator::BlendSkeletons(Model::Skeleton& dst, const Model::Skeleton& a, const Model::Skeleton& b, float t) {
	assert(a.joints.size() == b.joints.size());
	const size_t count = a.joints.size();
	dst.joints.resize(count);

	for (size_t i = 0; i < count; ++i) {
		dst.joints[i] = a.joints[i]; // name/parentなどはコピー

		// translate: Lerp
		const auto& ta = a.joints[i].transform.translate;
		const auto& tb = b.joints[i].transform.translate;
		dst.joints[i].transform.translate = {
			ta.x + (tb.x - ta.x) * t,
			ta.y + (tb.y - ta.y) * t,
			ta.z + (tb.z - ta.z) * t,
		};

		// scale: Lerp
		const auto& sa = a.joints[i].transform.scale;
		const auto& sb = b.joints[i].transform.scale;
		dst.joints[i].transform.scale = {
			sa.x + (sb.x - sa.x) * t,
			sa.y + (sb.y - sa.y) * t,
			sa.z + (sb.z - sa.z) * t,
		};

		// rotate: Slerp（グローバル関数）
		dst.joints[i].transform.rotate = Slerp(
			a.joints[i].transform.rotate,
			b.joints[i].transform.rotate,
			t
		);
	}
}

void Animator::ApplyAnimation(Model::Skeleton& skeleton, const Animation& animation, float time) {
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

void Animator::ApplyManualJointTransforms(Model::Skeleton& skeleton)
{
	if (manualJointTransforms_.size() != skeleton.joints.size()) {
		manualJointTransforms_.assign(
			skeleton.joints.size(),
			ManualJointTransform{});
	}

	for (size_t i = 0; i < skeleton.joints.size(); ++i) {
		const ManualJointTransform& manual = manualJointTransforms_[i];
		if(manual.translate.x==0 && manual.translate.y==0 && manual.translate.z==0 &&
		   manual.rotate.x==0 && manual.rotate.y==0 && manual.rotate.z==0 &&
		   manual.scale.x==1 && manual.scale.y==1 && manual.scale.z==1) continue;
		auto& transform = skeleton.joints[i].transform;
		transform.translate = transform.translate + manual.translate;
		transform.rotate = MultiplyQuaternion(transform.rotate, MakeEulerQuaternion(manual.rotate));
		transform.scale.x *= manual.scale.x;
		transform.scale.y *= manual.scale.y;
		transform.scale.z *= manual.scale.z;
	}
}

void Animator::SetManualJointTransform(int32_t jointIndex, const ManualJointTransform& transform)
{
	if (!model_ || !model_->HasSkinning() || !poseReady_) {
		return;
	}
	if (manualJointTransforms_.size() != poseSkeleton_.joints.size()) {
		manualJointTransforms_.assign(
			poseSkeleton_.joints.size(),
			ManualJointTransform{});
	}
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(manualJointTransforms_.size())) {
		return;
	}
	manualJointTransforms_[jointIndex] = transform;
}

void Animator::ResetManualJointTransforms()
{
	for (auto& transform : manualJointTransforms_) {
		transform = ManualJointTransform{};
	}
}

void Animator::PlayAnimation(const std::string& animName, bool loop) {
	if (!model_ || model_->GetAnimations().empty()) return;

	if (animName.empty() || !model_->GetAnimations().contains(animName)) {
		playingAnimName_ = model_->GetDefaultAnimationName();
	}
	else {
		playingAnimName_ = animName;
	}

	loop_ = loop;
	animationTime_ = 0.0f;
	isPlayAnimation_ = true;

	// フェード状態もリセット
	fadeTime_    = 0.0f;
	fadeElapsed_ = 0.0f;
	prevAnimName_ = "";
}

void Animator::CrossFadeTo(const std::string& animName, float fadeSec, bool loop) {
	if (!model_ || model_->GetAnimations().empty()) return;
	const std::string nextAnimName =
		(animName.empty() || !model_->GetAnimations().contains(animName))
		? model_->GetDefaultAnimationName()
		: animName;
	if (nextAnimName == playingAnimName_) return;
	if (animName == playingAnimName_) return; // 同じアニメならスキップ
	if (!isPlayAnimation_) { PlayAnimation(animName, loop); return; }

	// 現在の状態を「前」として保存
	prevAnimName_ = playingAnimName_;
	prevAnimTime_ = animationTime_;
	prevLoop_     = loop_;
	prevSkeleton_ = poseSkeleton_;  // 現在のポーズを保存

	// 次のアニメへ切り替え
	playingAnimName_ = nextAnimName;
	animationTime_   = 0.0f;
	loop_            = loop;
	isPlayAnimation_ = true;

	// フェード設定
	fadeTime_    = (fadeSec > 0.0f) ? fadeSec : 0.001f;
	fadeElapsed_ = 0.0f;
}

bool Animator::IsAnimationFinished() const {
	if (!isPlayAnimation_ || !model_) return true;

	auto it = model_->GetAnimations().find(playingAnimName_);
	if (it != model_->GetAnimations().end()) {
		return (!loop_ && animationTime_ >= it->second.duration);
	}
	return true;
}

bool Animator::HasAnimation() const {
	return model_ && !model_->GetAnimations().empty();
}

void Animator::Update(float dt) {
	if (!model_ || !poseReady_) return;

	// Topology and names are immutable. Restore only pose data, not strings/maps.
	const auto& bind=model_->GetSkeleton();
	for(size_t i=0;i<poseSkeleton_.joints.size();++i) {
		poseSkeleton_.joints[i].transform=bind.joints[i].transform;
	}

	if (!isPlayAnimation_) {
		ApplyManualJointTransforms(poseSkeleton_);
		Model::UpdateSkeleton(poseSkeleton_);
		return;
	}

	auto it = model_->GetAnimations().find(playingAnimName_);
	if (it == model_->GetAnimations().end()) {
		ApplyManualJointTransforms(poseSkeleton_);
		Model::UpdateSkeleton(poseSkeleton_);
		return;
	}

	const Animation& clip = it->second;
	animationTime_ += dt;

	if (loop_) {
		animationTime_ = std::fmod(animationTime_, clip.duration);
	} else {
		if (animationTime_ > clip.duration) {
			animationTime_ = clip.duration;
		}
	}

	// クロスフェード中
	if (fadeTime_ > 0.0f) {
		fadeElapsed_ += dt;
		float blend = fadeElapsed_ / fadeTime_; // 0→1

		if (blend >= 1.0f) {
			// フェード完了 → そのまま次のクリップだけ使う
			fadeTime_    = 0.0f;
			fadeElapsed_ = 0.0f;
			prevAnimName_ = "";
			ApplyAnimation(poseSkeleton_, clip, animationTime_);
		} else {
			// 前クリップのポーズを進める
			auto prevIt = model_->GetAnimations().find(prevAnimName_);
			if (prevIt != model_->GetAnimations().end()) {
				prevAnimTime_ += dt;
				if (prevLoop_) {
					prevAnimTime_ = std::fmod(prevAnimTime_, prevIt->second.duration);
				} else {
					prevAnimTime_ = std::min(prevAnimTime_, prevIt->second.duration);
				}
				ApplyAnimation(prevSkeleton_, prevIt->second, prevAnimTime_);
			}

			// 次クリップも計算
			Model::Skeleton nextSkeleton = model_->GetSkeleton();
			ApplyAnimation(nextSkeleton, clip, animationTime_);

			// 2つをブレンド
			BlendSkeletons(poseSkeleton_, prevSkeleton_, nextSkeleton, blend);
		}
	} else {
		// 通常再生
		ApplyAnimation(poseSkeleton_, clip, animationTime_);
	}

	ApplyManualJointTransforms(poseSkeleton_);
	Model::UpdateSkeleton(poseSkeleton_);
}

void Animator::UpdateSkinCluster(DirectXCommon* dx) {
	if (!poseReady_ || !model_ || !model_->HasSkinning()) return;

	for (size_t i = 0; i < poseSkeleton_.joints.size(); ++i) {
		assert(i < skinCluster_.inverseBindPoseMatrices.size());
		// Upload heaps are write-combined memory. Reading a matrix back from the
		// mapped palette for its inverse is extremely slow on discrete GPUs.
		// Finish both calculations in ordinary CPU memory before writing them.
		const auto skinMatrix=Matrix4x4::Multiply(skinCluster_.inverseBindPoseMatrices[i],poseSkeleton_.joints[i].skeletonSpaceMatrix);
		const auto normalMatrix=Matrix4x4::Transpose(Matrix4x4::Inverse(skinMatrix));
		skinCluster_.mappedPalette[i].skeletonSpaceMatrix=skinMatrix;
		skinCluster_.mappedPalette[i].skeletonSpaceInverseTransposeMatrix=normalMatrix;
	}
}

void Animator::CreateSkinCluster(
	ID3D12Device* device,
	DirectXCommon* dx,
	SrvManager* srvManager,
	ID3D12DescriptorHeap* srvHeap,
	uint32_t descriptorSize)
{
	if (!model_ || !model_->HasSkinning()) return;
    AssetLoading::Timer timer("object.skin-cluster");

	const auto& skeleton = model_->GetSkeleton();
	const auto& skinData = model_->GetSkinClusterData();
	uint32_t vertexCount = model_->GetVertexCount();

	skinCluster_ = SkinCluster{};

	// ========= palette (StructuredBuffer<WellForGPU>) =========
	skinCluster_.paletteResource = dx->CreateBufferResource(sizeof(WellForGPU) * skeleton.joints.size());

	WellForGPU* mappedPalette = nullptr;
	skinCluster_.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
	skinCluster_.mappedPalette = { mappedPalette, skeleton.joints.size() };

	uint32_t srvIndex = srvManager->Allocate();
	skinCluster_.paletteSrvHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, srvIndex);
	skinCluster_.paletteSrvHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC paletteSrvDesc{};
	paletteSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	paletteSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	paletteSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	paletteSrvDesc.Buffer.FirstElement = 0;
	paletteSrvDesc.Buffer.NumElements = static_cast<UINT>(skeleton.joints.size());
	paletteSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	paletteSrvDesc.Buffer.StructureByteStride = sizeof(WellForGPU);
	device->CreateShaderResourceView(skinCluster_.paletteResource.Get(), &paletteSrvDesc, skinCluster_.paletteSrvHandle.first);

	// ========= influence (VertexCount 分の VB) =========
	skinCluster_.influenceResource = dx->CreateBufferResource(sizeof(VertexInfluence) * vertexCount);

	VertexInfluence* mappedInfluence = nullptr;
	skinCluster_.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
	std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * vertexCount);
	skinCluster_.mappedInfluence = { mappedInfluence, vertexCount };

	skinCluster_.influenceBufferView.BufferLocation = skinCluster_.influenceResource->GetGPUVirtualAddress();
	skinCluster_.influenceBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexInfluence) * vertexCount);
	skinCluster_.influenceBufferView.StrideInBytes = sizeof(VertexInfluence);

	// --- influence用SRV作成 ---
	uint32_t influenceSrvIndex = srvManager->Allocate();
	skinCluster_.influenceSrvHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, influenceSrvIndex);
	skinCluster_.influenceSrvHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, influenceSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC influenceSrvDesc{};
	influenceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	influenceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	influenceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	influenceSrvDesc.Buffer.FirstElement = 0;
	influenceSrvDesc.Buffer.NumElements = vertexCount;
	influenceSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	influenceSrvDesc.Buffer.StructureByteStride = sizeof(VertexInfluence);
	device->CreateShaderResourceView(skinCluster_.influenceResource.Get(), &influenceSrvDesc, skinCluster_.influenceSrvHandle.first);

	// --- 入力頂点(ModelのvertexResource)用SRV作成 ---
	uint32_t inputVertexSrvIndex = srvManager->Allocate();
	skinCluster_.inputVertexSrvHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, inputVertexSrvIndex);
	skinCluster_.inputVertexSrvHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, inputVertexSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC inputVertexSrvDesc{};
	inputVertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	inputVertexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	inputVertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	inputVertexSrvDesc.Buffer.FirstElement = 0;
	inputVertexSrvDesc.Buffer.NumElements = vertexCount;
	inputVertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	inputVertexSrvDesc.Buffer.StructureByteStride = sizeof(Model::VertexData);
	device->CreateShaderResourceView(model_->GetVertexResource().Get(), &inputVertexSrvDesc, skinCluster_.inputVertexSrvHandle.first);

	// --- スキニング結果書き込み用UAV/Resource作成 ---
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Model::VertexData) * vertexCount);
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	HRESULT hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&skinCluster_.outputVertexResource)
	);

	uint32_t uavIndex = srvManager->Allocate();
	skinCluster_.outputVertexUavHandle.first = dx->GetCPUDescriptorHandle(srvHeap, descriptorSize, uavIndex);
	skinCluster_.outputVertexUavHandle.second = dx->GetGPUDescriptorHandle(srvHeap, descriptorSize, uavIndex);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = vertexCount;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.StructureByteStride = sizeof(Model::VertexData);
	device->CreateUnorderedAccessView(skinCluster_.outputVertexResource.Get(), nullptr, &uavDesc, skinCluster_.outputVertexUavHandle.first);

	skinCluster_.skinnedVertexBufferView.BufferLocation = skinCluster_.outputVertexResource->GetGPUVirtualAddress();
	skinCluster_.skinnedVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Model::VertexData) * vertexCount);
	skinCluster_.skinnedVertexBufferView.StrideInBytes = sizeof(Model::VertexData);

	// --- 定数バッファ (SkinningInformation) の作成 ---
	skinCluster_.skinningInformationResource = dx->CreateBufferResource(sizeof(SkinningInformation));
	skinCluster_.skinningInformationResource->Map(0, nullptr, reinterpret_cast<void**>(&skinCluster_.mappedSkinningInformation));
	skinCluster_.mappedSkinningInformation->numVertices = vertexCount;

	// ========= inverse bind pose =========
	skinCluster_.inverseBindPoseMatrices.resize(skeleton.joints.size());
	std::generate(skinCluster_.inverseBindPoseMatrices.begin(), skinCluster_.inverseBindPoseMatrices.end(), Matrix4x4::MakeIdentity4x4);

	for (const auto& jw : skinData) {
		auto it = skeleton.jointMap.find(jw.first);
		if (it == skeleton.jointMap.end()) continue;

		const int32_t jointIndex = it->second;
		skinCluster_.inverseBindPoseMatrices[jointIndex] = jw.second.inverseBindPoseMatrix;

		for (const auto& vw : jw.second.vertexWeights) {
			if (vw.vertexIndex >= vertexCount) continue;
			auto& inf = skinCluster_.mappedInfluence[vw.vertexIndex];
			for (uint32_t k = 0; k < kNumMaxInfluence; ++k) {
				if (inf.weights[k] == 0.0f) {
					inf.weights[k] = vw.weight;
					inf.jointIndices[k] = jointIndex;
					break;
				}
			}
		}
	}
}
