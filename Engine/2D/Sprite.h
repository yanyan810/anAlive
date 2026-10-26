#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "Vector3.h"
#include "Matrix4x4.h"
#include "TextureManager.h"
#include <array>

class SpriteCommon;
class DirectXCommon;

class Sprite {
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct SpriteTransform {
		float pos[2] = { 640.0f, 360.0f }; // 画面中央
		float scale[2] = { 1.0f, 1.0f };
		float rotate = 0.0f;              // ラジアン or 度
	};


public:
	void Initialize(SpriteCommon* spriteCommon, DirectXCommon* dx,std::string textureFilePath);

	// === New: 位置と色（スライド準拠） ===
	const Vector2& GetPosition() const { return position_; }
	void SetPosition(const Vector2& p) { position_ = p; }
	// --- スケール/回転/色の I/F を公開 ---
	const Vector3& GetScale()   const { return scale_; }
	void SetScale(const Vector3& s) { scale_ = s; }

	const Vector3& GetRotation() const { return rotate_; } // ラジアン想定
	void SetRotation(const Vector3& r) { rotate_ = r; }

	const Vector4& GetColor() const { return color_; }
	// Render a monochrome alpha mask using SetColor, ignoring the texture RGB.
	void SetUseTextureAlphaOnly(bool enabled) { if (materialData_) materialData_->useTextureAlphaOnly = enabled ? 1 : 0; }
	void SetColor(const Vector4& c) { color_ = c; if (materialData_) materialData_->color = c; }

	// 便利：Z回転だけ度数で扱いたいとき
	void SetRotationZDegrees(float deg) {
		float rad = deg * (3.1415926535f / 180.0f);
		rotate_.z = rad;
	}
	float GetRotationZDegrees() const {
		return rotate_.z * (180.0f / 3.1415926535f);
	}

	// === 既存：UV, 行列など ===
	void SetUVTransform(const Matrix4x4& m) { if (materialData_) materialData_->uvTransform = m; }

	// === Updateは position_ を反映（座標-反映処理） ===
	void Update(const Matrix4x4& view, const Matrix4x4& proj);
	// Unit-space quad in bottom-left, top-left, bottom-right, top-right order.
	// Default rectangle is unchanged; UI can also tessellate curved masks.
	void SetQuad(const std::array<Vector2,4>& corners) { quad_=corners; }

	// === 引数なし Draw（内部でPSOとSRVをセット） ===
	void Draw();

	//getter
	const Vector2& GetAnchorPoint() const { return anchorPoint; }
	const bool GetFlipX() const { return isFlipX_; }
	const bool GetFlipY() const { return isFlipY_; }
	const Vector2& GetTextureTopLeft() const { return textureTopLeft_; }
	const Vector2& GetTextureCutSize() const { return textureCutSize_; }

	//setter
	void SetAnchorPoint(const Vector2& ap) { this->anchorPoint = ap; }
	void SetFlipX(const bool flipX) { this->isFlipX_ = flipX; }
	void SetFlipY(const bool flipY) { this->isFlipY_ = flipY; }
	void SetTextureTopLeft(const Vector2& ttl) { this->textureTopLeft_ = ttl; }
	void SetTextureCutSize(const Vector2& tcs) { this->textureCutSize_ = tcs; }

	//テクスチャサイズをイメージに合わせる
	void AdjustTextureSize();

	void SetTextureFilePath(const std::string& filePath);
	const std::string& GetTextureFilePath() const { return textureFilePath_; }

private:



private:
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		int32_t useTextureAlphaOnly;
		// HLSL側の定数バッファ配置に合わせ、後続の行列を16バイト境界に保つ。
		float padding[2];
		Matrix4x4 uvTransform;
	};

	// シェーダの入力と合わせた簡易頂点
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	SpriteCommon* spriteCommon_ = nullptr;

	// バッファ類（既存）
	VertexData* vertexData = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW  indexBufferView_{};

	// マテリアル（既存）
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;

	// 変換（既存）
	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	TransformationMatrix* transformData_ = nullptr;

	// === 内部状態 ===
	Vector2 position_{ 0.0f, 0.0f };     // スライドの「座標」メンバ変数
	Vector3 scale_{ 1.0f, 1.0f, 1.0f };  // 必要ならsetter追加してOK
	Vector3 rotate_{ 0.0f, 0.0f, 0.0f }; // 必要ならsetter追加してOK
	Vector4 color_{ 1,1,1,1 };
	std::array<Vector2,4> quad_{{{0,1},{0,0},{1,1},{1,0}}};

	DirectXCommon* dx_ = nullptr;

	std::string textureFilePath_;

	Vector2 anchorPoint{ 0.0f,0.0f };
	Vector2 size_{ 1.0f, 1.0f };

	//左右フリップ
	bool isFlipX_ = false;
    //上下フリップ
	bool isFlipY_ = false;

	//テクスチャ左上座標
	Vector2 textureTopLeft_{ 0.0f,0.0f };
	//テクスチャ切り出しサイズ
	Vector2 textureCutSize_{ 1.0f,1.0f };


};
