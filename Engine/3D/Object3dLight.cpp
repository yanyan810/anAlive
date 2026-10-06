#include "Object3dLight.h"

void Object3dLight::Initialize(DirectXCommon* dx) {
	dx_ = dx;

	// 平行光源
	directionalLightResource_ = dx_->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
	// 初期化
	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // ライトの色
	directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData_->intensity = 0.0f; // ライトの強度

	// ポイントライト
	pointLightResource_ = dx_->CreateBufferResource(sizeof(PointLight));
	pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData_));
	// 初期値
	pointLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	pointLightData_->position = { 0.0f, 3.0f, 0.0f };
	pointLightData_->intensity = 1.0f;
	pointLightData_->radius = 10.0f;
	pointLightData_->decay = 2.0f;

	// スポットライト
	spotLightResource_ = dx_->CreateBufferResource(sizeof(SpotLights));
	spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightArrayData_));
	*spotLightArrayData_ = {};
	spotLightData_ = &(*spotLightArrayData_)[0];
	// 初期値
	spotLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	spotLightData_->position = { 0.0f, 5.0f, 0.0f };
	spotLightData_->intensity = 1.0f;
	spotLightData_->direction = { 0.0f, -1.0f, 0.0f };
	spotLightData_->distance = 15.0f;
	spotLightData_->decay = 2.0f;
	spotLightData_->cosAngle = std::cos(30.0f * (3.14159265f / 180.0f));
	spotLightData_->cosFalloffStart = std::cos(15.0f * (3.14159265f / 180.0f));
	spotLightData_->specularStrength = 1.0f; // Preserve the legacy single-light appearance.
}
