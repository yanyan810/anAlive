#pragma once
#include "Matrix4x4.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <numbers>
class Camera
{

public:

	Camera();

	void Update();

	//setter
	void SetRotate(const Vector3& r) { transform_.rotate = r; }
	void SetTranslate(const Vector3& t) { transform_.translate = t; }
	void SetFovY(float fovY) { if (std::isfinite(fovY) && fovY > 0 && fovY < std::numbers::pi_v<float>) fovY_ = fovY; }
	void SetAspect(float aspect) { if (std::isfinite(aspect) && aspect > 0) aspect_ = aspect; }
	void SetNearZ(float nearZ) { if (std::isfinite(nearZ) && nearZ > 0 && nearZ < farZ_) nearZ_ = nearZ; }
	void SetFarClip(float farZ) { if (std::isfinite(farZ) && farZ > nearZ_) farZ_ = farZ; }
    void SetOrthographic(bool enabled,float height=30) {
        if (!std::isfinite(height)) return;
        orthographic_=enabled; orthographicHeight_=std::max(.1f,height);
    }
	//getter
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
	const Vector3& GetRotate() const { return transform_.rotate; }
	const Vector3& GetTranslate() const { return transform_.translate; }
	float GetFovY() const { return fovY_; }
	const Transform GetTransform() const { return transform_; }

private:

	Transform transform_;
	Matrix4x4 worldMatrix_;
	Matrix4x4 viewMatrix_;

	float fovY_;
	float aspect_;
	float nearZ_;
	float farZ_;
	Matrix4x4 projectionMatrix_;
	Matrix4x4 viewProjectionMatrix_;
    bool orthographic_=false;
    float orthographicHeight_=30;

};

