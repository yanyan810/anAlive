#include "../Utility/AssetLoadProfile.h"
#include "DirectXCommon.h"
#include "LocalLight.h"
#include <cassert>
#include <d3d12shader.h>
#include <dxgidebug.h>
#include <exception>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")


using namespace Logger;
using namespace StringUtility;

namespace {
void EnableDred() {
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings)))) {
		settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}
}

std::string DredName(const char* name, const wchar_t* wideName) {
	if (wideName) return ConvertString(wideName);
	return name ? name : "(unnamed)";
}

void WriteDred(ID3D12Device& device, const char* summary) {
	std::error_code directoryError;
	std::filesystem::create_directories("Dumps", directoryError);
	const auto path = std::format("Dumps/d3d12-device-removed-{}-{}.log", GetCurrentProcessId(), GetTickCount64());
	std::ofstream log(path);
	if (!log) {
		OutputDebugStringA("[DirectXCommon] Could not write the DRED log.\n");
		return;
	}
	log << summary;
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
	if (FAILED(device.QueryInterface(IID_PPV_ARGS(&dred)))) {
		log << "DRED interface unavailable\n";
		return;
	}
	constexpr unsigned maxNodes = 128;
	constexpr unsigned historySize = 65536;
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
	const HRESULT breadcrumbStatus = dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
	log << "Breadcrumb result 0x" << std::hex << static_cast<unsigned>(breadcrumbStatus) << std::dec << '\n';
	if (SUCCEEDED(breadcrumbStatus)) {
		unsigned count = 0;
		for (auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node && count < maxNodes; node = node->pNext, ++count) {
			const unsigned completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
			log << DredName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW)
				<< " / " << DredName(node->pCommandListDebugNameA, node->pCommandListDebugNameW)
				<< " completed " << completed << '/' << node->BreadcrumbCount;
			if (node->pCommandHistory && completed > 0 && completed <= node->BreadcrumbCount) {
				log << ", last completed op " << node->pCommandHistory[(completed - 1) % historySize];
			}
			if (node->pCommandHistory && completed < node->BreadcrumbCount) {
				log << ", next op " << node->pCommandHistory[completed % historySize];
			}
			log << '\n';
		}
	}
	D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
	const HRESULT pageStatus = dred->GetPageFaultAllocationOutput(&fault);
	log << "Page fault result 0x" << std::hex << static_cast<unsigned>(pageStatus) << '\n';
	if (SUCCEEDED(pageStatus)) {
		log << "Faulting GPU virtual address 0x" << fault.PageFaultVA << std::dec << '\n';
		const auto writeAllocations = [&log](const char* label, const D3D12_DRED_ALLOCATION_NODE* node) {
			for (unsigned count = 0; node && count < maxNodes; node = node->pNext, ++count) {
				log << label << ": type " << node->AllocationType << ", "
					<< DredName(node->ObjectNameA, node->ObjectNameW) << '\n';
			}
		};
		writeAllocations("Existing allocation", fault.pHeadExistingAllocationNode);
		writeAllocations("Recently freed allocation", fault.pHeadRecentFreedAllocationNode);
	}
}

void ValidateLocalLightShader(const std::wstring& path, IDxcUtils& utils, IDxcResult& result) {
	const auto name = std::filesystem::path(path).filename();
	if (name != L"Object3d.PS.hlsl" && name != L"Object3dEnvMap.PS.hlsl" && name != L"SkinnedObjectEnvMap.PS.hlsl") return;

	using LocalLighting::Parameters;
	Microsoft::WRL::ComPtr<IDxcBlob> data;
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	const auto fail = [&path] {
		throw std::runtime_error(std::format("{}: expected the b6 local light layout ({} bytes)",
			ConvertString(path), sizeof(Parameters)));
	};
	if (FAILED(result.GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&data), nullptr)) || !data) fail();
	const DxcBuffer buffer{data->GetBufferPointer(), data->GetBufferSize(), 0};
	if (FAILED(utils.CreateReflection(&buffer, IID_PPV_ARGS(&reflection)))) fail();
	D3D12_SHADER_INPUT_BIND_DESC binding{};
	if (FAILED(reflection->GetResourceBindingDescByName("gLocalLight", &binding)) ||
		binding.Type != D3D_SIT_CBUFFER || binding.BindPoint != LocalLighting::kShaderRegister ||
		binding.Space != 0 || binding.BindCount != 1) fail();

	auto* constants = reflection->GetConstantBufferByName("gLocalLight");
	D3D12_SHADER_BUFFER_DESC desc{};
	if (FAILED(constants->GetDesc(&desc)) || desc.Size != sizeof(Parameters)) fail();
	auto* type = constants->GetVariableByIndex(0)->GetType();
	D3D12_SHADER_TYPE_DESC bounces{}, visibility{};
	if (FAILED(type->GetMemberTypeByName("bounces")->GetDesc(&bounces)) ||
		bounces.Offset != offsetof(Parameters, bounces) || bounces.Elements != LocalLighting::kMaxBounceLights ||
		FAILED(type->GetMemberTypeByName("visibility")->GetDesc(&visibility)) ||
		visibility.Offset != offsetof(Parameters, visibility) || visibility.Elements != 1 + LocalLighting::kMaxBounceLights) fail();
}
}

const uint32_t DirectXCommon::kMaxSRVCount = 512;

/// <summary>
/// </summary>
Microsoft::WRL::ComPtr <ID3D12DescriptorHeap>   DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescripters, bool shaderVisible) {

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescripters;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device_->CreateDescriptorHeap(
		&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	CheckGpuResult(hr, "CreateDescriptorHeap");
	return descriptorHeap;
}

/// <summary>
/// </summary>
Microsoft::WRL::ComPtr<ID3D12Resource>
DirectXCommon::CreateDepthStencilResource(int32_t width, int32_t height) {
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&resource));
	CheckGpuResult(hr, "CreateCommittedResource (DepthStencil)");

	resource->SetName(L"DepthStencil");

	return resource;
}


D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriporSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr < ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriporSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriporSize * index);
	return handleGPU;
}



D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV, index);
}


D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(dsvDescriptorHeap_.Get(), descriptorSizeDSV, index);
}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompilesSharder(
	const std::wstring& filePath,
	const wchar_t* profile) {
	Log(ConvertString(std::format(L"Compile Shader: {}\n", filePath)));

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
	if (FAILED(dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource))) {
		throw std::runtime_error("Failed to load shader: " + ConvertString(filePath));
	}

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;
	LPCWSTR arguments[] = {
	filePath.c_str(),
	L"-E", L"main",
	L"-T", profile,
	L"-Zi",L"-Qembed_debug",
	L"-Od",
	L"-Zpr",
	};

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
	HRESULT hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler,
		IID_PPV_ARGS(&shaderResult));
	if (FAILED(hr)) throw std::runtime_error("DXC invocation failed: " + ConvertString(filePath));

	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
	}

	HRESULT status = E_FAIL;
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	if (FAILED(shaderResult->GetStatus(&status)) || FAILED(status) ||
		FAILED(shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr)) || !shaderBlob) {
		throw std::runtime_error("Failed to compile shader: " + ConvertString(filePath));
	}
	ValidateLocalLightShader(filePath, *dxcUtils, *shaderResult.Get());
	Log(ConvertString(std::format(L"Compile succeeded: {} ({})\n", filePath, profile)));
	return shaderBlob;

}

Microsoft::WRL::ComPtr<ID3D12Resource>DirectXCommon::CreateBufferResource(size_t sizeInBytes) {
	if (gpuFailed_) throw FatalError{};
	if (sizeInBytes == 0) {
		OutputDebugStringA("[DirectXCommon] CreateBufferResource requested zero bytes. Using 256 bytes instead.\n");
		sizeInBytes = 256;
	}
	sizeInBytes = (sizeInBytes + 0xff) & ~0xff;

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer));
	if (FAILED(hr) || !buffer) {
		const HRESULT reason = device_->GetDeviceRemovedReason();
		char msg[256]{};
		sprintf_s(msg, "[DirectXCommon] CreateBufferResource failed. size=%zu hr=0x%08X reason=0x%08X\n",
			sizeInBytes, static_cast<unsigned>(hr), static_cast<unsigned>(reason));
		OutputDebugStringA(msg);
		if (FAILED(reason) || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
			hr == DXGI_ERROR_DEVICE_HUNG) FailGpu(hr, reason, "CreateBufferResource");
		return nullptr;
	}
	buffer->SetName(L"GenericUploadBuffer");

	return buffer;
}

Microsoft::WRL::ComPtr<ID3D12Resource>DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metadata) {

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);;
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);;
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&resource));
	CheckGpuResult(hr, "CreateCommittedResource (TextureResource)");

	resource->SetName(L"TextureResource");

	return resource;
}

// DirectXCommon.cpp
void DirectXCommon::UploadTextureData(
	const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	const DirectX::ScratchImage& mipImages)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	CheckGpuResult(DirectX::PrepareUpload(device_.Get(),
		mipImages.GetImages(),
		mipImages.GetImageCount(),
		mipImages.GetMetadata(),
		subresources), "Prepare texture upload");

	const UINT numSubresources = UINT(subresources.size());
	const UINT64 intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, numSubresources);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediate = CreateBufferResource(intermediateSize);
	if (!intermediate) throw std::runtime_error("Failed to create a texture upload buffer");

	if (!textureUploadList_) {
		CheckGpuResult(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&textureUploadAllocator_)), "Create texture upload allocator");
		CheckGpuResult(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			textureUploadAllocator_.Get(), nullptr, IID_PPV_ARGS(&textureUploadList_)), "Create texture upload list");
		textureUploadList_->SetName(L"Texture upload");
	}
	UpdateSubresources(textureUploadList_.Get(),
		texture.Get(),
		intermediate.Get(),
		0, 0,
		numSubresources,
		subresources.data());

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	textureUploadList_->ResourceBarrier(1, &barrier);

    textureUploadStaging_.push_back(std::move(intermediate));
    textureUploadBytes_+=static_cast<size_t>(intermediateSize);
    // Bound temporary memory even for large models, retaining staging until the fence completes.
    if(textureUploadBatchDepth_==0 || !AssetLoading::Option("YAN_TEXTURE_BATCH") || textureUploadBytes_>=128*1024*1024)
        FlushTextureUploads();
}

DirectXCommon::TextureUploadBatch::~TextureUploadBatch() noexcept(false) {
	if (std::uncaught_exceptions() == 0) {
		dx_->EndTextureUploadBatch();
		return;
	}
	try { dx_->EndTextureUploadBatch(); }
	catch (const FatalError&) { /* The next device check will rethrow the latched failure. */ }
}

void DirectXCommon::BeginTextureUploadBatch() { ++textureUploadBatchDepth_; }
void DirectXCommon::EndTextureUploadBatch() {
    assert(textureUploadBatchDepth_>0);
    if (--textureUploadBatchDepth_ == 0 && !gpuFailed_) FlushTextureUploads();
}
void DirectXCommon::FlushTextureUploads() {
    if(textureUploadStaging_.empty()) return;
    AssetLoading::Timer timer("texture.gpu-submit-wait");
    CheckGpuResult(textureUploadList_->Close(), "Close texture upload list");
    ID3D12CommandList* lists[]{textureUploadList_.Get()}; commandQueue->ExecuteCommandLists(1,lists);
    ++textureUploadSubmissions_;
    WaitForGPU();
    textureUploadStaging_.clear(); textureUploadBytes_=0;
    CheckGpuResult(textureUploadAllocator_->Reset(), "Reset texture upload allocator");
    CheckGpuResult(textureUploadList_->Reset(textureUploadAllocator_.Get(), nullptr), "Reset texture upload list");
}

void DirectXCommon::SetDescriptorHeaps(ID3D12DescriptorHeap* srvHeap)
{
	ID3D12DescriptorHeap* heaps[] = { srvHeap };
	commandList->SetDescriptorHeaps(1, heaps);
}


void DirectXCommon::Initialize(WinApp* winApp) {

	assert(winApp);

	this->winApp_ = winApp;
	InitializeFixFPS();

	DeviceInitialize();
	CommandInitialize();
	commandQueue->SetName(L"Main direct queue");
	commandList->SetName(L"Frame graphics");
	computeCommandList->SetName(L"Frame compute");
	SwapChainSpawn();
	DepthBufferSpawn();
	DethCriptorHeapSpawn();
	RenderTargetViewInitialize();
	DepthStencilViewInitialize();
	FanceInitialize();
	ViewPortInitialize();
	SizeringInitialize();
	DXCCompilierSpawn();
//	ImGuiInitialize();

	HRESULT hr = commandList->Close();
	hr = commandAllocator->Reset();
	hr = commandList->Reset(commandAllocator.Get(), nullptr);

	hr = computeCommandList->Close();
	hr = computeCommandAllocator->Reset();
	hr = computeCommandList->Reset(computeCommandAllocator.Get(), nullptr);
}

DirectXCommon::~DirectXCommon() {
	if (fenceEvent) {
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}
}


void DirectXCommon::DeviceInitialize() {

	EnableDred();
	HRESULT hr;


#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
	//	debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif

	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr < IDXGIAdapter4> useAdapter = nullptr;
	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			Log(ConvertString(std::format(L"Use Adapter: {}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr;
	}
	assert(useAdapter != nullptr);


	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };
	for (size_t i = 0; i < _countof(featureLevels); i++) {
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		if (SUCCEEDED(hr)) {
			Log(std::format("Use Feature Level: {}\n", featureLevelStrings[i]));
			break;
		}
	}
	assert(device_ != nullptr);
	Log("Complete create D3D12Device!!!\n");

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		//infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		infoQueue->PushStorageFilter(&filter);
	}
#endif

}

void DirectXCommon::CommandInitialize() {

	HRESULT hr;


	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));

	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&computeCommandAllocator));
	assert(SUCCEEDED(hr));


	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));

	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		computeCommandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&computeCommandList));
	assert(SUCCEEDED(hr));


	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue));
	assert(SUCCEEDED(hr));
}

void DirectXCommon::SwapChainSpawn() {

	HRESULT hr;

	swapChainDesc.Width = WinApp::kClientWidth;
	swapChainDesc.Height = WinApp::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> tempSwapChain;
	hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue.Get(), winApp_->GetHwnd(), &swapChainDesc, nullptr, nullptr, &tempSwapChain);
	assert(SUCCEEDED(hr));

	hr = tempSwapChain.As(&swapChain);
	assert(SUCCEEDED(hr));


}


void DirectXCommon::DepthBufferSpawn() {

	depthStencilResource_ = CreateDepthStencilResource(
		WinApp::kClientWidth, WinApp::kClientHeight);

	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	Log("Depth buffer and DSV created successfully.\n");
}


void DirectXCommon::DethCriptorHeapSpawn() {


	descriptorSizeRTV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 12, false);

	
}

void DirectXCommon::RenderTargetViewInitialize() {
	HRESULT hr;

	for (UINT i = 0; i < 2; i++) {
		hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
		assert(SUCCEEDED(hr));
	}

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	for (UINT i = 0; i < 2; ++i) {
		rtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, i);
		device_->CreateRenderTargetView(
			swapChainResources[i].Get(),
			&rtvDesc,
			rtvHandles[i]
		);
	}

	Log("RenderTargetViewInitialize: created RTV for both buffers.\n");
}


void DirectXCommon::DepthStencilViewInitialize() {

	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV, 0);

	//commandList->OMSetRenderTargets(1, &rtvHandles[0], FALSE, &dsvHandle);

	Log("Depth Stencil View initialized successfully.\n");
}

void DirectXCommon::FanceInitialize() {

	HRESULT hr;

	fenceValue = 0;
	hr = device_->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);

}

void DirectXCommon::ViewPortInitialize() {

	viewport.Width = WinApp::kClientWidth;
	viewport.Height = WinApp::kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

}

void DirectXCommon::SizeringInitialize() {

	scissorRect.left = 0;
	scissorRect.right = WinApp::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WinApp::kClientHeight;

}

void DirectXCommon::DXCCompilierSpawn() {

	HRESULT hr;

	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));


}

void DirectXCommon::DXCCompilerSpawn() {
	DXCCompilierSpawn();
}
//
//void DirectXCommon::ImGuiInitialize() {
//
//
//	IMGUI_CHECKVERSION();
//	ImGui::CreateContext();
//	ImGui::StyleColorsDark();
//	ImGui_ImplWin32_Init(winApp_->GetHwnd());
//	ImGui_ImplDX12_Init(device_.Get(),
//		swapChainDesc.BufferCount,
//		rtvDesc.Format,
//		srvDescriptorHeap.Get(),
//		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
//		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
//
//}

void DirectXCommon::InitializeFixFPS() {

	reference_ = std::chrono::steady_clock::now();
	fps_ = 0.0f;

}

void DirectXCommon::UpdateFixFPS() {

	using namespace std::chrono;

	const microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
	const microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

	auto now = steady_clock::now();
	auto elapsed = duration_cast<microseconds>(now - reference_);

	if (elapsed < kMinCheckTime) {
		while (steady_clock::now() - reference_ < kMinTime) {
			std::this_thread::sleep_for(microseconds(1));
		}
		now = steady_clock::now();
		elapsed = duration_cast<microseconds>(now - reference_);
	}

	reference_ = now;

	if (elapsed.count() > 0) {
		float elapsedSec = static_cast<float>(elapsed.count()) / 1'000'000.0f;
		fps_ = 1.0f / elapsedSec;
	}
}


void DirectXCommon::PreDraw(bool clearDepth) {
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);

	const float clearColor[4] = { 0.10f, 0.25f, 0.50f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
	if (clearDepth) {
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	// VP/Scissor
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

}


void DirectXCommon::PostDraw() {
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	// OutputDebugStringA(std::format("[PreDraw] backBufferIndex = {}\n", backBufferIndex).c_str());

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	CheckGpuResult(commandList->Close(), "Close graphics command list");
	CheckGpuResult(computeCommandList->Close(), "Close compute command list");

	ID3D12CommandList* lists[] = { computeCommandList.Get(), commandList.Get() };
	commandQueue->ExecuteCommandLists(2, lists);

	UpdateFixFPS();

	const HRESULT hr = swapChain->Present(1, 0);
	CheckGpuResult(hr, "Present");
	if (hr == DXGI_STATUS_OCCLUDED) Sleep(16);

	WaitForGPU();
	CheckGpuResult(commandAllocator->Reset(), "Reset graphics allocator");
	CheckGpuResult(commandList->Reset(commandAllocator.Get(), nullptr), "Reset graphics command list");
	CheckGpuResult(computeCommandAllocator->Reset(), "Reset compute allocator");
	CheckGpuResult(computeCommandList->Reset(computeCommandAllocator.Get(), nullptr), "Reset compute command list");
}

void DirectXCommon::WaitForGPU() {
	if (deviceRemoved_) throw FatalError{};
	if (!commandQueue || !fence) return;
	if (!fenceEvent) CheckGpuResult(E_HANDLE, "Wait for GPU fence");

	const UINT64 target = ++fenceValue;
	CheckGpuResult(commandQueue->Signal(fence.Get(), target), "Signal GPU fence");
	UINT64 completed = fence->GetCompletedValue();
	if (completed == UINT64_MAX) CheckGpuResult(DXGI_ERROR_DEVICE_REMOVED, "Read GPU fence");
	if (completed >= target) return;

	CheckGpuResult(fence->SetEventOnCompletion(target, fenceEvent), "Register GPU fence event");
	for (;;) {
		const DWORD wait = WaitForSingleObject(fenceEvent, 1000);
		if (wait == WAIT_FAILED) CheckGpuResult(HRESULT_FROM_WIN32(GetLastError()), "Wait for GPU fence");
		completed = fence->GetCompletedValue();
		if (completed == UINT64_MAX) CheckGpuResult(DXGI_ERROR_DEVICE_REMOVED, "Read GPU fence after wait");
		if (completed >= target) return;
		if (wait == WAIT_TIMEOUT) CheckDevice();
	}
}

void DirectXCommon::CheckDevice() {
	if (gpuFailed_) throw FatalError{};
	if (!device_) return;
	const HRESULT reason = device_->GetDeviceRemovedReason();
	if (FAILED(reason)) FailGpu(reason, reason, "Check D3D12 device");
}

void DirectXCommon::CheckGpuResult(HRESULT hr, const char* operation) {
	if (SUCCEEDED(hr)) return;
	const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : S_OK;
	FailGpu(hr, reason, operation);
}

void DirectXCommon::FailGpu(HRESULT hr, HRESULT reason, const char* operation) {
	gpuFailed_ = true;
	char summary[512]{};
	sprintf_s(summary, "[DirectXCommon] %s failed, hr=0x%08X, removal reason=0x%08X\n", operation,
		static_cast<unsigned>(hr), static_cast<unsigned>(reason));
	OutputDebugStringA(summary);
	if (!deviceRemoved_ && (FAILED(reason) || hr == DXGI_ERROR_DEVICE_REMOVED ||
		hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG)) {
		deviceRemoved_ = true;
		if (device_) {
			try { WriteDred(*device_.Get(), summary); }
			catch (...) { OutputDebugStringA("[DirectXCommon] DRED logging failed.\n"); }
		}
	}
	throw FatalError{};
}

void DirectXCommon::ReportLiveObjects()
{
#if _DEBUG
	Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
	if (SUCCEEDED(device_.As(&debugDevice))) {
		debugDevice->ReportLiveDeviceObjects(
			D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL
		);
	}
#endif
}

// === BlendDesc ===
D3D12_BLEND_DESC DirectXCommon::GetBlendDesc() const {
	D3D12_BLEND_DESC desc{};
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;

	const D3D12_RENDER_TARGET_BLEND_DESC defaultBlend = {
		TRUE, FALSE,
		D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};

	for (int i = 0; i < 8; ++i) {
		desc.RenderTarget[i] = defaultBlend;
	}
	return desc;
}

// === Rasterizer ===
D3D12_RASTERIZER_DESC DirectXCommon::GetRasterizerDesc() const {
	D3D12_RASTERIZER_DESC desc{};
	desc.FillMode = D3D12_FILL_MODE_SOLID;
	desc.CullMode = D3D12_CULL_MODE_BACK;
	desc.FrontCounterClockwise = FALSE;
	desc.DepthClipEnable = TRUE;
	return desc;
}

// === DepthStencil ===
D3D12_DEPTH_STENCIL_DESC DirectXCommon::GetDepthStencilDesc() const {
	D3D12_DEPTH_STENCIL_DESC desc{};
	desc.DepthEnable = TRUE;
	desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.StencilEnable = FALSE;
	return desc;
}

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile
) {
	return CompilesSharder(filePath, profile);
}

//=======================
//=======================

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateRenderTextureResource(
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format,
	const Vector4& clearColor)
{
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;
	clearValue.Color[0] = clearColor.x;
	clearValue.Color[1] = clearColor.y;
	clearValue.Color[2] = clearColor.z;
	clearValue.Color[3] = clearColor.w;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearValue,
		IID_PPV_ARGS(&resource));
	CheckGpuResult(hr, "CreateCommittedResource (RenderTexture)");

	resource->SetName(L"RenderTexture");

	return resource;
}

void DirectXCommon::CreateRenderTextureRTV(
	ID3D12Resource* resource,
	uint32_t rtvIndex,
	DXGI_FORMAT format)
{
	assert(resource);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	device_->CreateRenderTargetView(resource, &rtvDesc, handle);
}

void DirectXCommon::PreDrawRenderTexture(uint32_t rtvIndex, const Vector4& clearColor)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	commandList->OMSetRenderTargets(1, &handle, FALSE, &dsvHandle);

	float color[4] = {
		clearColor.x,
		clearColor.y,
		clearColor.z,
		clearColor.w
	};

	commandList->ClearRenderTargetView(handle, color, 0, nullptr);
	commandList->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PreDrawPostEffectBuffer(uint32_t rtvIndex)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	commandList->OMSetRenderTargets(1, &handle, FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PreDrawPostEffectBuffer(uint32_t rtvIndex, const Vector4& clearColor)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	commandList->OMSetRenderTargets(1, &handle, FALSE, nullptr);

	float color[4] = {
		clearColor.x,
		clearColor.y,
		clearColor.z,
		clearColor.w
	};
	commandList->ClearRenderTargetView(handle, color, 0, nullptr);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PreDrawRenderTextureNoDepthClear(uint32_t rtvIndex, const Vector4& clearColor)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, rtvIndex);

	commandList->OMSetRenderTargets(1, &handle, FALSE, &dsvHandle);

	float color[4] = {
		clearColor.x,
		clearColor.y,
		clearColor.z,
		clearColor.w
	};
	commandList->ClearRenderTargetView(handle, color, 0, nullptr);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::SetBackBufferRenderTarget()
{
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::SetBackBufferRenderTargetForPostEffect()
{
	const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::TransitionResource(
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES before,
	D3D12_RESOURCE_STATES after)
{
	assert(resource);

	if (before == after) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;

	commandList->ResourceBarrier(1, &barrier);
}
