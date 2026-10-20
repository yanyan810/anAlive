#pragma once
#include <string>
#include <vector>
#include <map>
#include "Model.h"
#include "SkinningTypes.h"
#include "SrvManager.h"
#include "DirectXCommon.h"

class Animator {
public:
    Animator()=default;
    Animator(const Animator&)=delete;
    Animator& operator=(const Animator&)=delete;
    ~Animator();
	struct ManualJointTransform {
		Vector3 translate{ 0.0f, 0.0f, 0.0f };
		Vector3 rotate{ 0.0f, 0.0f, 0.0f };
		Vector3 scale{ 1.0f, 1.0f, 1.0f };
	};

	void Initialize(Model* model);

	void PlayAnimation(const std::string& animName = "", bool loop = true);
	void CrossFadeTo(const std::string& animName, float fadeSec, bool loop = true); // ★追加
	void StopAnimation() { isPlayAnimation_ = false; }
	void SetAnimationNodeName(const std::string& node) { playingNodeName_ = node; }

	bool IsAnimationFinished() const;
	bool HasAnimation() const;
	bool IsFading() const { return fadeTime_ > 0.0f; } // ★追加
	const std::string& GetPlayingAnimName() const { return playingAnimName_; }
	float GetTime() const { return animationTime_; }

	void Update(float dt);

	void UpdateSkinCluster(DirectXCommon* dx);

	SkinCluster& GetSkinCluster() { return skinCluster_; }
	const SkinCluster& GetSkinCluster() const { return skinCluster_; }

	Model::Skeleton& GetPoseSkeleton() { return poseSkeleton_; }
	const Model::Skeleton& GetPoseSkeleton() const { return poseSkeleton_; }

	bool IsPoseReady() const { return poseReady_; }
	void SetManualJointTransform(int32_t jointIndex, const ManualJointTransform& transform);
	void ResetManualJointTransforms();
	const std::vector<ManualJointTransform>& GetManualJointTransforms() const { return manualJointTransforms_; }

	// スキンクラスター生成
	void CreateSkinCluster(
		ID3D12Device* device,
		DirectXCommon* dx,
		SrvManager* srvManager,
		ID3D12DescriptorHeap* srvHeap,
		uint32_t descriptorSize);

	void SetSrvManager(SrvManager* srv) { srvManager_ = srv; }

private:
    void ReleaseSkinCluster();
    std::array<uint32_t,4> skinDescriptors_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> sourceVertex_;
	void ApplyAnimation(Model::Skeleton& skeleton, const Animation& animation, float time);
	void ApplyManualJointTransforms(Model::Skeleton& skeleton);
	static void BlendSkeletons(Model::Skeleton& dst, const Model::Skeleton& a, const Model::Skeleton& b, float t); // ★追加

private:
	Model* model_ = nullptr;
	SrvManager* srvManager_ = nullptr;

	bool isPlayAnimation_ = false;
	float animationTime_ = 0.0f;
	std::string playingAnimName_;
	std::string playingNodeName_ = "root";
	bool loop_ = true;

	// ★ クロスフェード用
	std::string prevAnimName_;
	float prevAnimTime_ = 0.0f;
	float fadeTime_    = 0.0f;  // 総フェード時間（0=フェードなし）
	float fadeElapsed_ = 0.0f;  // 経過時間
	bool  prevLoop_    = true;
	Model::Skeleton prevSkeleton_; // 前のクリップのポーズ

	bool poseReady_ = false;

	Model::Skeleton poseSkeleton_;
	SkinCluster skinCluster_;
	std::vector<ManualJointTransform> manualJointTransforms_;
};
