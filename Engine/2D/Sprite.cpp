#include "Sprite.h"
#include "SpriteCommon.h"
#include "DirectXCommon.h"
#include <cassert>

void Sprite::Initialize(SpriteCommon* spriteCommon, DirectXCommon* dx, std::string textureFilePath) {
    spriteCommon_ = spriteCommon;
    dx_ = dx;

    //単位行列を書き込んでおく
    textureFilePath_ = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);


    // === 頂点/インデックス ===
    vertexResource_ = dx->CreateBufferResource(sizeof(VertexData) * 4);
    indexResource_ = dx->CreateBufferResource(sizeof(uint32_t) * 6);

    size_ = { 128.0f, 128.0f };// デフォルトサイズ

    // 頂点データ（画面ピクセル座標のまま。WVPは main 側で正射影にする想定）
    VertexData* v = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&v));
    v[0] = { {0.0f, 300.0f, 0.0f, 1.0f}, {0.0f,1.0f}, {0,0,-1} };
    v[1] = { {0.0f,   0.0f, 0.0f, 1.0f}, {0.0f,0.0f}, {0,0,-1} };
    v[2] = { {640.f, 300.0f,0.0f, 1.0f}, {1.0f,1.0f}, {0,0,-1} };
    v[3] = { {640.f,   0.0f,0.0f, 1.0f}, {1.0f,0.0f}, {0,0,-1} };
    // UnmapしなくてOK（永続Mapでも可）

    uint32_t* idx = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&idx));
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    idx[3] = 1; idx[4] = 3; idx[5] = 2;

    // ビュー
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    // === マテリアル（※重複を削除して1回だけ作る）
    materialResource_ = dx->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = color_;
    materialData_->enableLighting = false;
    materialData_->useTextureAlphaOnly = 0;
    materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();

    // === 変換（※重複を削除して1回だけ作る）
    transformResource_ = dx->CreateBufferResource(sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    transformData_->WVP = Matrix4x4::MakeIdentity4x4();
    transformData_->World = Matrix4x4::MakeIdentity4x4();

    AdjustTextureSize();

}

// 座標-反映処理：position_ → transform.translate
void Sprite::Update(const Matrix4x4& view, const Matrix4x4& proj) {
    // --- 1) アンカー反映で頂点を書き換え ---
    VertexData* v = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&v));

    const DirectX::TexMetadata& metadata =
        TextureManager::GetInstance()->GetMetaData(textureFilePath_);

    // (0,0)〜(1,1) の単位矩形を、anchor だけ平行移動
    float texL = textureTopLeft_.x / metadata.width;
    float texT = textureTopLeft_.y / metadata.height;
    float texR = (textureTopLeft_.x + textureCutSize_.x) / metadata.width;
    float texB = (textureTopLeft_.y + textureCutSize_.y) / metadata.height;

    const Vector2 uv[]={{texL,texB},{texL,texT},{texR,texB},{texR,texT}};
    for (size_t i=0;i<quad_.size();++i) {
        const float x=(quad_[i].x-anchorPoint.x)*(isFlipX_ ? -1.0f : 1.0f);
        const float y=(quad_[i].y-anchorPoint.y)*(isFlipY_ ? -1.0f : 1.0f);
        v[i]={{x,y,0,1},uv[i],{0,0,-1}};
    }

    // --- 2) 変換：サイズ→ユーザースケール→回転→配置 ---
    // 頂点は単位矩形なので、サイズは行列のスケールで与える
    Matrix4x4 Ssize = Matrix4x4::Scale({ size_.x, size_.y, 1.0f });
    Matrix4x4 Suser = Matrix4x4::Scale(scale_);
    Matrix4x4 R = Matrix4x4::RotateXYZ(rotate_.x, rotate_.y, rotate_.z);
    Matrix4x4 T = Matrix4x4::Translation({ position_.x, position_.y, 0.0f });

    Matrix4x4 world = Matrix4x4::Multiply(Matrix4x4::Multiply(Matrix4x4::Multiply(Ssize, Suser), R), T);
    Matrix4x4 vp = Matrix4x4::Multiply(view, proj);
    Matrix4x4 wvp = Matrix4x4::Multiply(world, vp);

    transformData_->World = world;
    transformData_->WVP = wvp;
}



void Sprite::Draw() {
    assert(spriteCommon_ && "Sprite not initialized (spriteCommon_ is null)");
    assert(dx_ && "Sprite not initialized (dx_ is null)");

    // ★ SRV の heap をこのコマンドリストにセット（これが無いと DescriptorTable が無効）
    ID3D12DescriptorHeap* heaps[] = { TextureManager::GetInstance()->GetSrvDescriptorHeap() };
    dx_->GetCommandList()->SetDescriptorHeaps(1, heaps);

    D3D12_GPU_DESCRIPTOR_HANDLE texSrv{};

    // slot は一旦使わない（混在防止）
    texSrv = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);

    assert(texSrv.ptr != 0 && "texture SRV is null");


    auto* cmd = dx_->GetCommandList();

    // 呼び出し側不要：ここでPSO/RootSigをセット
    spriteCommon_->SetGraphicsPipelineState();

    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->IASetIndexBuffer(&indexBufferView_);

    // Root 0=Material, 1=Transform, 2=Texture SRV（プロジェクトの順に合わせて）
    cmd->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(2,texSrv);

    cmd->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

// テクスチャサイズをイメージに合わせる
void Sprite::AdjustTextureSize() {
	const DirectX::TexMetadata& metadata =
		TextureManager::GetInstance()->GetMetaData(textureFilePath_);
    textureCutSize_.x = static_cast<float>(metadata.width);
    textureCutSize_.y = static_cast<float>(metadata.height);

    size_ = textureCutSize_;

}

void Sprite::SetTextureFilePath(const std::string& filePath) {
    textureFilePath_ = filePath;

    // 念のためロード（ロード済みなら内部で無視される想定）
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);

    // 画像サイズに合わせたいならこれ（数字は小さいのでON推奨）
    AdjustTextureSize();
}
