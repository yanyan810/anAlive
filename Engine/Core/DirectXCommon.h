#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include"Logger.h"
#include "StringUtility.h"
#include "WinApp.h"
#include <array>
#include <dxcapi.h>
#include <chrono>
#include <thread>
#include <vector>
#include "MathStruct.h"

class DirectXCommon
{

public:
	void	Initialize(WinApp* winApp);

	//描画前処理
	void PreDraw(bool clearDepth = true);
	//描画後処理
	void PostDraw();
	void WaitForGPU();

	~DirectXCommon(); // デストラクタを宣言

	/// <summary>
	/// 指定番号のCPUでスクリプタハンドルを取得する
	/// </summary>
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVCPUDescriptorHandle(uint32_t index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVCPUDescriptorHandle(uint32_t index);

	/// <summary>
	/// 指定番号のGPUでスクリプタハンドルを取得する
	/// </summary>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetRTVGPUDescriptorHandle(uint32_t index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetDSVGPUDescriptorHandle(uint32_t index);

	/// <summary>
/// SRVディスクリプタヒープをコマンドリストにセットする
/// </summary>
	void SetDescriptorHeaps(ID3D12DescriptorHeap* srvHeap);

	//getter
	ID3D12Device* GetDevice() const { return device_.Get(); }
	ID3D12CommandQueue* GetCommandQueue() const { return commandQueue.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList.Get(); }
	ID3D12GraphicsCommandList* GetComputeCommandList() const { return computeCommandList.Get(); }
	ID3D12Resource* GetDepthStencilResource() const { return depthStencilResource_.Get(); }



	void ReportLiveObjects();

	Microsoft::WRL::ComPtr<IDxcBlob> CompilesSharder(
		//CompilerするSharderファイルへのパス
		const std::wstring& filePath,
		//Compilerにする使用するProfile
		const wchar_t* profile);

	Microsoft::WRL::ComPtr<ID3D12Resource>CreateBufferResource(size_t sizeInBytes);

	Microsoft::WRL::ComPtr<ID3D12Resource>CreateTextureResource(const DirectX::TexMetadata& metadata);

	void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);
	void BeginTextureUploadBatch();
	void EndTextureUploadBatch();
	void FlushTextureUploads();
	size_t GetTextureUploadSubmissions() const { return textureUploadSubmissions_; }
	// Nestable, render-thread-only scope. A dedicated list never resets pending draw commands.
	class TextureUploadBatch {
		DirectXCommon* dx_;
	public:
		explicit TextureUploadBatch(DirectXCommon* dx) : dx_(dx) { dx_->BeginTextureUploadBatch(); }
		~TextureUploadBatch() { dx_->EndTextureUploadBatch(); }
		TextureUploadBatch(const TextureUploadBatch&) = delete;
		TextureUploadBatch& operator=(const TextureUploadBatch&) = delete;
	};

	// 現在のFPSを取得
	float GetFPS() const { return fps_; }

	//最大SRV数(最大テクスチャ枚数)
	static const uint32_t kMaxSRVCount;

	// ====== PSO 用 Getter ======
	D3D12_BLEND_DESC GetBlendDesc() const;
	D3D12_RASTERIZER_DESC GetRasterizerDesc() const;
	D3D12_DEPTH_STENCIL_DESC GetDepthStencilDesc() const;

	DXGI_FORMAT GetRTVFormat() const { return rtvDesc.Format; }
	DXGI_FORMAT GetDSVFormat() const { return dsvDesc.Format; }

	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap>CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		UINT numDescripters, bool shaderVisible);

	Microsoft::WRL::ComPtr<ID3D12Resource>
		CreateDepthStencilResource(int32_t width, int32_t height);

	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile
	);

	/// <summary>
/// 指定番号のCPUでスクリプタハンドルを取得する
/// </summary>
	static	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index);

	/// <summary>
	/// 指定番号のGPUでスクリプタハンドルを取得する
	/// </summary>
	static	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index);

	//===========================
	//レンダー用
	//===========================

	//RenderTextureの関数
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTextureResource(
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		const Vector4& clearColor
	);

	// RenderTexture用RTVの作成
	void CreateRenderTextureRTV(
		ID3D12Resource* resource,
		uint32_t rtvIndex,
		DXGI_FORMAT format);

	// RenderTextureをPostEffect用に設定（DSVなし、クリアなし）
	void PreDrawPostEffectBuffer(uint32_t rtvIndex);
	void PreDrawPostEffectBuffer(uint32_t rtvIndex, const Vector4& clearColor);

	// BackBufferをPostEffect用に設定（DSVなし）
	void SetBackBufferRenderTargetForPostEffect();

	void PreDrawRenderTexture(uint32_t rtvIndex, const Vector4& clearColor);
	void PreDrawRenderTextureNoDepthClear(uint32_t rtvIndex, const Vector4& clearColor);
	void SetBackBufferRenderTarget();

	// RenderTexture用DSVの作成
	void TransitionResource(
		ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after);

private:

	void DeviceInitialize();
	void CommandInitialize();
	void SwapChainSpawn();
	void DepthBufferSpawn();
	void DethCriptorHeapSpawn();
	void RenderTargetViewInitialize();
	void DepthStencilViewInitialize();
	void FanceInitialize();
	void ViewPortInitialize();
	void SizeringInitialize();
	void DXCCompilerSpawn();   // ★正しい綴り（リンクエラー側）
	void DXCCompilierSpawn();  // ★今あるやつ（残す）
	//void ImGuiInitialize();

	//FPS固定初期化
	void InitializeFixFPS();
	//FPS固定更新
	void UpdateFixFPS();


private:

#ifdef _DEBUG
	friend void RunClothRuntimeTests(class GameApp& app);
#endif

	//WindowsApi
	WinApp* winApp_ = nullptr;

	//DXGIファクトリーの生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;

	//コマンド関連の変数
	Microsoft::WRL::ComPtr < ID3D12CommandQueue> commandQueue = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	Microsoft::WRL::ComPtr < ID3D12CommandAllocator> commandAllocator = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> textureUploadList_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> textureUploadAllocator_;
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textureUploadStaging_;
	size_t textureUploadBytes_=0,textureUploadSubmissions_=0;
	int textureUploadBatchDepth_=0;

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> computeCommandList = nullptr;
	Microsoft::WRL::ComPtr < ID3D12CommandAllocator> computeCommandAllocator = nullptr;

	//スワップチェーン
	Microsoft::WRL::ComPtr <IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
	//スワップチェーンリソース
	std::array<Microsoft::WRL::ComPtr < ID3D12Resource>, 2 > swapChainResources;

	//各種デスクリプタヒープの生成
	uint32_t descriptorSizeRTV;
	uint32_t descriptorSizeDSV;

	//アインドバッファの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

	//RTV用のヒープでディスクリプタの数は2。RTVはshader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	//SRV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue

	//DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;

	//設定するRTV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvHandles;

	//フェンスの設定
	Microsoft::WRL::ComPtr < ID3D12Fence> fence = nullptr;
	UINT64 fenceValue = 0;
	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	//ビューポート
	D3D12_VIEWPORT viewport{};

	//シザー
	D3D12_RECT scissorRect{};

	//DXCの初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	IDxcIncludeHandler* includeHandler = nullptr;

	//記録時間
	std::chrono::steady_clock::time_point reference_;

	// 現在のFPS
	float fps_ = 0.0f;

};

