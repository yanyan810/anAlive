#pragma once
#include "MathStruct.h"
#include "DirectXCommon.h"
#include <wrl.h>
#include <array>

class Object3dLight {
public:
	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	struct PointLight {
		Vector4 color;
		Vector3 position;
		float intensity;
		float radius;
		float decay;
		float padding[2];
	};

	struct SpotLight {
		Vector4 color;
		Vector3 position;
		float intensity;
		Vector3 direction;
		float distance;
		float decay;
		float cosAngle;
		float cosFalloffStart;
		float specularStrength;
	};
	static constexpr size_t kMaxSpotLights = 3;
	static_assert(sizeof(SpotLight) == 64, "SpotLight must match the HLSL array stride");
	using SpotLights = std::array<SpotLight, kMaxSpotLights>;

public:
	void Initialize(DirectXCommon* dx);

	// ===== Directional Light =====
	void SetDirectionalLightColor(const Vector4& color) { if (directionalLightData_) directionalLightData_->color = color; }
	void SetDirectionalLightDirection(const Vector3& dir) { if (directionalLightData_) directionalLightData_->direction = dir; }
	void SetDirectionalLightIntensity(float intensity) { if (directionalLightData_) directionalLightData_->intensity = intensity; }

	const Vector4& GetDirectionalLightColor() const { return directionalLightData_->color; }
	const Vector3& GetDirectionalLightDirection() const { return directionalLightData_->direction; }
	float GetDirectionalLightIntensity() const { return directionalLightData_->intensity; }

	ID3D12Resource* GetDirectionalLightResource() const { return directionalLightResource_.Get(); }

	// ===== Point Light =====
	void SetPointLightColor(const Vector4& c) { if (pointLightData_) pointLightData_->color = c; }
	void SetPointLightPos(const Vector3& p) { if (pointLightData_) pointLightData_->position = p; }
	void SetPointLightIntensity(float i) { if (pointLightData_) pointLightData_->intensity = i; }
	void SetPointLightRadius(float r) { if (pointLightData_) pointLightData_->radius = r; }
	void SetPointLightDecay(float d) { if (pointLightData_) pointLightData_->decay = d; }

	ID3D12Resource* GetPointLightResource() const { return pointLightResource_.Get(); }

	// ===== Spot Light =====
	void SetSpotLightColor(const Vector4& c) { if (spotLightData_) spotLightData_->color = c; }
	void SetSpotLightPos(const Vector3& p) { if (spotLightData_) spotLightData_->position = p; }
	void SetSpotLightIntensity(float i) { if (spotLightData_) spotLightData_->intensity = i; }
	void SetSpotLightDirection(const Vector3& d) { if (spotLightData_) spotLightData_->direction = d; } // 正規化推奨
	void SetSpotLightDistance(float d) { if (spotLightData_) spotLightData_->distance = d; }
	void SetSpotLightDecay(float d) { if (spotLightData_) spotLightData_->decay = d; }
	void SetSpotLightCosAngle(float c) { if (spotLightData_) spotLightData_->cosAngle = c; } // cos(角度)
	void SetSpotLightCosFalloffStart(float c) { if (spotLightData_) spotLightData_->cosFalloffStart = c; }

	ID3D12Resource* GetSpotLightResource() const { return spotLightResource_.Get(); }
	// The legacy setters above continue to edit slot 0; unused slots remain off.
	void SetSpotLights(const SpotLights& lights) { if (spotLightData_) *spotLightArrayData_ = lights; }

private:
	DirectXCommon* dx_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
	PointLight* pointLightData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
	SpotLight* spotLightData_ = nullptr;
	SpotLights* spotLightArrayData_ = nullptr;
};
