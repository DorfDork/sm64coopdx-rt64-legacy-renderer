//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_nrd.h"

#include <algorithm>
#include <vector>

#include "NRD.h"

#include "rt64_common.h"
#include "rt64_device.h"

static const float kWorldUnitsPerMeter = 100.0f;
static const float kDenoisingRange = 2.0e5f;
static const float kDiffuseMaxBlurRadius = 10.0f;
static const float kDiffuseMinBlurRadius = 0.5f;
static const float kDiffusePrepassBlurRadius = 8.0f;
static const float kDiffuseLobeAngleFraction = 0.15f;
static const float kDiffuseMinHitDistanceWeight = 0.10f;
static const uint32_t kDiffuseMaxStabilizedFrameNum = 10;
static const uint32_t kDiffuseMaxFastAccumulatedFrameNum = 5;
static const float kDiffuseFastHistoryClampingSigmaScale = 1.5f;
static const float kHistoryFixPixelStrideBase = 8.0f;
static const float kAntilagLuminanceSensitivity = 2.0f;

namespace {
	DXGI_FORMAT toDxgiFormat(nrd::Format format) {
		switch (format) {
		case nrd::Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
		case nrd::Format::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
		case nrd::Format::R8_UINT: return DXGI_FORMAT_R8_UINT;
		case nrd::Format::R8_SINT: return DXGI_FORMAT_R8_SINT;
		case nrd::Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
		case nrd::Format::RG8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
		case nrd::Format::RG8_UINT: return DXGI_FORMAT_R8G8_UINT;
		case nrd::Format::RG8_SINT: return DXGI_FORMAT_R8G8_SINT;
		case nrd::Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case nrd::Format::RGBA8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
		case nrd::Format::RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
		case nrd::Format::RGBA8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
		case nrd::Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case nrd::Format::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
		case nrd::Format::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
		case nrd::Format::R16_UINT: return DXGI_FORMAT_R16_UINT;
		case nrd::Format::R16_SINT: return DXGI_FORMAT_R16_SINT;
		case nrd::Format::R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;
		case nrd::Format::RG16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
		case nrd::Format::RG16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
		case nrd::Format::RG16_UINT: return DXGI_FORMAT_R16G16_UINT;
		case nrd::Format::RG16_SINT: return DXGI_FORMAT_R16G16_SINT;
		case nrd::Format::RG16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
		case nrd::Format::RGBA16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
		case nrd::Format::RGBA16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
		case nrd::Format::RGBA16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
		case nrd::Format::RGBA16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
		case nrd::Format::RGBA16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case nrd::Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
		case nrd::Format::R32_SINT: return DXGI_FORMAT_R32_SINT;
		case nrd::Format::R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
		case nrd::Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
		case nrd::Format::RG32_SINT: return DXGI_FORMAT_R32G32_SINT;
		case nrd::Format::RG32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
		case nrd::Format::RGB32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
		case nrd::Format::RGB32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
		case nrd::Format::RGB32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
		case nrd::Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
		case nrd::Format::RGBA32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
		case nrd::Format::RGBA32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case nrd::Format::R10_G10_B10_A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
		case nrd::Format::R10_G10_B10_A2_UINT: return DXGI_FORMAT_R10G10B10A2_UINT;
		case nrd::Format::R11_G11_B10_UFLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
		case nrd::Format::R9_G9_B9_E5_UFLOAT: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
		default: return DXGI_FORMAT_UNKNOWN;
		}
	}

	const nrd::Identifier IdentifierDirect = 0;
	const nrd::Identifier IdentifierIndirect = 1;
};

class RT64::Denoiser::Context {
private:
	struct Pool {
		std::vector<AllocatedResource> textures;
		std::vector<D3D12_RESOURCE_STATES> states;
	};

	Device *device;
	nrd::Instance *instance = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
	std::vector<ID3D12PipelineState *> pipelines;
	Pool permanentPool;
	Pool transientPool;
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	UINT descriptorHandleIncrement = 0;
	uint32_t perSetTexturesMaxNum = 0;
	uint32_t perSetStorageTexturesMaxNum = 0;
	uint32_t setsMaxNum = 0;
	AllocatedResource constantBufferUpload;
	uint8_t *constantBufferUploadMapped = nullptr;
	uint32_t constantBufferAlignedSize = 0;
	int renderWidth = 0;
	int renderHeight = 0;
	float blurRadiusScale = 1.0f;
	float verySmoothedFrameTime = 16.6667f;
	nrd::ReblurHitDistanceParameters hitDistanceParameters;

	float getSmoothedFps(float frameTimeMs) {
		if (frameTimeMs <= 0.0f) {
			frameTimeMs = verySmoothedFrameTime;
		}

		float smoothedFpsPrev = 1000.0f / verySmoothedFrameTime;
		float n = smoothedFpsPrev * 0.2f;
		verySmoothedFrameTime = verySmoothedFrameTime + (frameTimeMs - verySmoothedFrameTime) / (1.0f + n);

		return 1000.0f / verySmoothedFrameTime;
	}

	void releaseResources() {
		device->waitForGPU();

		if (instance != nullptr) {
			nrd::DestroyInstance(*instance);
			instance = nullptr;
		}

		ReleaseCom(&rootSignature);

		for (ID3D12PipelineState *&pipeline : pipelines) {
			ReleaseCom(&pipeline);
		}
		pipelines.clear();

		for (AllocatedResource &res : permanentPool.textures) {
			res.Release();
		}
		permanentPool.textures.clear();
		permanentPool.states.clear();

		for (AllocatedResource &res : transientPool.textures) {
			res.Release();
		}
		transientPool.textures.clear();
		transientPool.states.clear();

		ReleaseCom(&descriptorHeap);

		constantBufferUpload.Release();
		constantBufferUploadMapped = nullptr;
	}

	void createRootSignature(const nrd::InstanceDesc &instanceDesc) {
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, instanceDesc.constantBufferRegisterIndex, instanceDesc.constantBufferAndSamplersSpaceIndex);
		rsc.AddHeapRangesParameter({
			{ instanceDesc.resourcesBaseRegisterIndex, perSetTexturesMaxNum, instanceDesc.resourcesSpaceIndex, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 }
		});
		rsc.AddHeapRangesParameter({
			{ instanceDesc.resourcesBaseRegisterIndex, perSetStorageTexturesMaxNum, instanceDesc.resourcesSpaceIndex, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0 }
		});

		std::vector<D3D12_STATIC_SAMPLER_DESC> samplerDescs;
		for (uint32_t i = 0; i < instanceDesc.samplersNum; i++) {
			D3D12_STATIC_SAMPLER_DESC desc = {};
			bool isLinear = (instanceDesc.samplers[i] == nrd::Sampler::LINEAR_CLAMP);
			desc.Filter = isLinear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.MinLOD = 0;
			desc.MaxLOD = D3D12_FLOAT32_MAX;
			desc.MipLODBias = 0.0f;
			desc.MaxAnisotropy = 1;
			desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			desc.ShaderRegister = instanceDesc.samplersBaseRegisterIndex + i;
			desc.RegisterSpace = instanceDesc.constantBufferAndSamplersSpaceIndex;
			samplerDescs.push_back(desc);
		}

		rootSignature = rsc.Generate(device->getD3D12Device(), false, false, samplerDescs.data(), (unsigned int)(samplerDescs.size()));
	}

	void createPipelines(const nrd::InstanceDesc &instanceDesc) {
		for (uint32_t i = 0; i < instanceDesc.pipelinesNum; i++) {
			const nrd::PipelineDesc &pipelineDesc = instanceDesc.pipelines[i];
			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = rootSignature;
			psoDesc.CS.pShaderBytecode = pipelineDesc.computeShaderDXIL.bytecode;
			psoDesc.CS.BytecodeLength = pipelineDesc.computeShaderDXIL.size;

			ID3D12PipelineState *pso = nullptr;
			D3D12_CHECK(device->getD3D12Device()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
			pipelines.push_back(pso);
		}
	}

	void createPoolTextures(Pool &pool, const nrd::TextureDesc *descs, uint32_t descsNum) {
		for (uint32_t i = 0; i < descsNum; i++) {
			const nrd::TextureDesc &texDesc = descs[i];
			uint16_t downsample = (texDesc.downsampleFactor > 0) ? texDesc.downsampleFactor : 1;
			UINT width = (UINT)((renderWidth + downsample - 1) / downsample);
			UINT height = (UINT)((renderHeight + downsample - 1) / downsample);

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			resDesc.Format = toDxgiFormat(texDesc.format);
			resDesc.Width = width;
			resDesc.Height = height;

			AllocatedResource res = device->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
#ifndef NDEBUG
			res.SetName(L"NRD Pool Texture");
#endif
			pool.textures.push_back(res);
			pool.states.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
	}

	ID3D12Resource *resolveResource(const nrd::ResourceDesc &resourceDesc, const DenoiseParameters &p, const SignalParameters &signal, std::vector<D3D12_RESOURCE_BARRIER> &barriers) {
		using namespace nrd;

		switch (resourceDesc.type) {
		case ResourceType::IN_MV: return p.inMotionVectors;
		case ResourceType::IN_NORMAL_ROUGHNESS: return p.inNormalRoughness;
		case ResourceType::IN_VIEWZ: return p.inViewZ;
		case ResourceType::IN_DIFF_CONFIDENCE: return p.inHistoryConfidence;
		case ResourceType::IN_DIFF_RADIANCE_HITDIST: return signal.inRadianceHitDist;
		case ResourceType::OUT_DIFF_RADIANCE_HITDIST: return signal.outRadiance;
		case ResourceType::TRANSIENT_POOL:
		case ResourceType::PERMANENT_POOL: {
			D3D12_RESOURCE_STATES requiredState = (resourceDesc.descriptorType == DescriptorType::STORAGE_TEXTURE) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			Pool &pool = (resourceDesc.type == ResourceType::TRANSIENT_POOL) ? transientPool : permanentPool;
			AllocatedResource &res = pool.textures[resourceDesc.indexInPool];
			D3D12_RESOURCE_STATES &state = pool.states[resourceDesc.indexInPool];
			if (state != requiredState) {
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(res.Get(), state, requiredState));
				state = requiredState;
			}
			return res.Get();
		}
		default: return nullptr;
		}
	}

	void createSRV(ID3D12Resource *resource, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		device->getD3D12Device()->CreateShaderResourceView(resource, &srvDesc, handle);
	}

	void createUAV(ID3D12Resource *resource, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device->getD3D12Device()->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handle);
	}
public:
	Context(Device *device) {
		this->device = device;
	}

	~Context() {
		releaseResources();
	}

	void set(int renderWidth, int renderHeight, int outputWidth, int outputHeight) {
		releaseResources();

		this->renderWidth = renderWidth;
		this->renderHeight = renderHeight;

		blurRadiusScale = ((outputWidth > 0) && (renderWidth < outputWidth)) ? ((float)(renderWidth) / (float)(outputWidth)) : 1.0f;

		nrd::DenoiserDesc denoiserDescs[] = {
			{ IdentifierDirect, nrd::Denoiser::REBLUR_DIFFUSE },
			{ IdentifierIndirect, nrd::Denoiser::REBLUR_DIFFUSE }
		};

		nrd::InstanceCreationDesc creationDesc = {};
		creationDesc.denoisers = denoiserDescs;
		creationDesc.denoisersNum = _countof(denoiserDescs);

		nrd::Result result = nrd::CreateInstance(creationDesc, instance);
		if (result != nrd::Result::SUCCESS) {
			RT64_LOG_PRINTF("nrd::CreateInstance failed with code %d", (int)(result));
			instance = nullptr;
			return;
		}

		const nrd::InstanceDesc &instanceDesc = *nrd::GetInstanceDesc(*instance);
		perSetTexturesMaxNum = instanceDesc.descriptorPoolDesc.perSetTexturesMaxNum;
		perSetStorageTexturesMaxNum = instanceDesc.descriptorPoolDesc.perSetStorageTexturesMaxNum;
		setsMaxNum = instanceDesc.descriptorPoolDesc.setsMaxNum;

		createRootSignature(instanceDesc);
		createPipelines(instanceDesc);
		createPoolTextures(permanentPool, instanceDesc.permanentPool, instanceDesc.permanentPoolSize);
		createPoolTextures(transientPool, instanceDesc.transientPool, instanceDesc.transientPoolSize);

		descriptorHandleIncrement = device->getD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		UINT descriptorsPerSet = perSetTexturesMaxNum + perSetStorageTexturesMaxNum;
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NumDescriptors = descriptorsPerSet * setsMaxNum;
		D3D12_CHECK(device->getD3D12Device()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap)));

		constantBufferAlignedSize = ROUND_UP(instanceDesc.constantBufferMaxDataSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		if (constantBufferAlignedSize > 0) {
			constantBufferUpload = device->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, (uint64_t)(constantBufferAlignedSize) * setsMaxNum, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
			D3D12_CHECK(constantBufferUpload.Get()->Map(0, nullptr, (void **)(&constantBufferUploadMapped)));
		}

		hitDistanceParameters = {};
		hitDistanceParameters.A *= kWorldUnitsPerMeter;
	}

	void updateSettings(const DenoiseParameters &p) {
		nrd::CommonSettings commonSettings = {};
		memcpy(commonSettings.viewToClipMatrix, &p.projection, sizeof(float) * 16);
		memcpy(commonSettings.viewToClipMatrixPrev, &p.projectionPrev, sizeof(float) * 16);
		memcpy(commonSettings.worldToViewMatrix, &p.view, sizeof(float) * 16);
		memcpy(commonSettings.worldToViewMatrixPrev, &p.viewPrev, sizeof(float) * 16);
		commonSettings.motionVectorScale[0] = 1.0f / (float)(p.rectWidth);
		commonSettings.motionVectorScale[1] = 1.0f / (float)(p.rectHeight);
		commonSettings.motionVectorScale[2] = 1.0f;
		commonSettings.cameraJitter[0] = p.jitterX;
		commonSettings.cameraJitter[1] = p.jitterY;
		commonSettings.cameraJitterPrev[0] = p.jitterXPrev;
		commonSettings.cameraJitterPrev[1] = p.jitterYPrev;
		commonSettings.resourceSize[0] = commonSettings.resourceSizePrev[0] = (uint16_t)(p.rectWidth);
		commonSettings.resourceSize[1] = commonSettings.resourceSizePrev[1] = (uint16_t)(p.rectHeight);
		commonSettings.rectSize[0] = commonSettings.rectSizePrev[0] = (uint16_t)(p.rectWidth);
		commonSettings.rectSize[1] = commonSettings.rectSizePrev[1] = (uint16_t)(p.rectHeight);
		commonSettings.denoisingRange = kDenoisingRange;
		commonSettings.timeDeltaBetweenFrames = p.deltaTimeMs;
		commonSettings.frameIndex = p.frameIndex;
		commonSettings.accumulationMode = p.resetAccumulation ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
		commonSettings.isMotionVectorInWorldSpace = false;
		commonSettings.isHistoryConfidenceAvailable = (p.inHistoryConfidence != nullptr);

		nrd::SetCommonSettings(*instance, commonSettings);

		uint32_t maxAccumulatedFrameNum = nrd::GetMaxAccumulatedFrameNum(nrd::REBLUR_DEFAULT_ACCUMULATION_TIME, getSmoothedFps(p.deltaTimeMs));
		if (maxAccumulatedFrameNum > nrd::REBLUR_MAX_HISTORY_FRAME_NUM) {
			maxAccumulatedFrameNum = nrd::REBLUR_MAX_HISTORY_FRAME_NUM;
		}

		uint32_t historyFixPixelStride = (uint32_t)(std::max(3.0f, kHistoryFixPixelStrideBase * blurRadiusScale));

		nrd::ReblurSettings settings = {};
		settings.hitDistanceParameters = hitDistanceParameters;
		settings.enableAntiFirefly = true;
		settings.maxAccumulatedFrameNum = maxAccumulatedFrameNum;
		settings.maxBlurRadius = kDiffuseMaxBlurRadius * blurRadiusScale;
		settings.minBlurRadius = kDiffuseMinBlurRadius * blurRadiusScale;
		settings.diffusePrepassBlurRadius = kDiffusePrepassBlurRadius * blurRadiusScale;
		settings.historyFixBasePixelStride = historyFixPixelStride;
		settings.historyFixAlternatePixelStride = historyFixPixelStride;
		settings.lobeAngleFraction = kDiffuseLobeAngleFraction;
		settings.minHitDistanceWeight = kDiffuseMinHitDistanceWeight;
		settings.maxStabilizedFrameNum = std::min(kDiffuseMaxStabilizedFrameNum, maxAccumulatedFrameNum);
		settings.maxFastAccumulatedFrameNum = std::min(kDiffuseMaxFastAccumulatedFrameNum, maxAccumulatedFrameNum);
		settings.fastHistoryClampingSigmaScale = kDiffuseFastHistoryClampingSigmaScale;
		settings.antilagSettings.luminanceSensitivity = kAntilagLuminanceSensitivity;

		nrd::SetDenoiserSettings(*instance, IdentifierDirect, &settings);
		nrd::SetDenoiserSettings(*instance, IdentifierIndirect, &settings);
	}

	void recordDispatches(ID3D12GraphicsCommandList4 *cmdList, const nrd::DispatchDesc *dispatches, uint32_t dispatchesNum, const DenoiseParameters &p, const SignalParameters &signal, uint32_t &setIndex) {
		UINT descriptorsPerSet = perSetTexturesMaxNum + perSetStorageTexturesMaxNum;
		for (uint32_t dispatchIndex = 0; (dispatchIndex < dispatchesNum) && (setIndex < setsMaxNum); dispatchIndex++, setIndex++) {
			const nrd::DispatchDesc &dispatch = dispatches[dispatchIndex];

			D3D12_CPU_DESCRIPTOR_HANDLE setStartCpu = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
			setStartCpu.ptr += (size_t)(setIndex * descriptorsPerSet) * descriptorHandleIncrement;
			D3D12_GPU_DESCRIPTOR_HANDLE setStartGpu = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
			setStartGpu.ptr += (UINT64)(setIndex * descriptorsPerSet) * descriptorHandleIncrement;

			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = setStartCpu;
			D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = setStartCpu;
			uavHandle.ptr += (size_t)(perSetTexturesMaxNum) * descriptorHandleIncrement;

			for (uint32_t r = 0; r < dispatch.resourcesNum; r++) {
				const nrd::ResourceDesc &resourceDesc = dispatch.resources[r];
				ID3D12Resource *resource = resolveResource(resourceDesc, p, signal, barriers);
				if (resource == nullptr) {
					continue;
				}

				if (resourceDesc.descriptorType == nrd::DescriptorType::TEXTURE) {
					createSRV(resource, srvHandle);
					srvHandle.ptr += descriptorHandleIncrement;
				}
				else {
					createUAV(resource, uavHandle);
					uavHandle.ptr += descriptorHandleIncrement;
				}
			}

			if (!barriers.empty()) {
				cmdList->ResourceBarrier((UINT)(barriers.size()), barriers.data());
			}

			if ((constantBufferUploadMapped != nullptr) && (dispatch.constantBufferDataSize > 0)) {
				memcpy(constantBufferUploadMapped + (size_t)(setIndex) * constantBufferAlignedSize, dispatch.constantBufferData, dispatch.constantBufferDataSize);
			}

			cmdList->SetDescriptorHeaps(1, &descriptorHeap);
			cmdList->SetComputeRootSignature(rootSignature);

			if ((constantBufferUploadMapped != nullptr) && (dispatch.constantBufferDataSize > 0)) {
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = constantBufferUpload.Get()->GetGPUVirtualAddress() + (UINT64)(setIndex) * constantBufferAlignedSize;
				cmdList->SetComputeRootConstantBufferView(0, cbAddress);
			}

			D3D12_GPU_DESCRIPTOR_HANDLE srvTableStart = setStartGpu;
			D3D12_GPU_DESCRIPTOR_HANDLE uavTableStart = setStartGpu;
			uavTableStart.ptr += (UINT64)(perSetTexturesMaxNum) * descriptorHandleIncrement;
			cmdList->SetComputeRootDescriptorTable(1, srvTableStart);
			cmdList->SetComputeRootDescriptorTable(2, uavTableStart);

			cmdList->SetPipelineState(pipelines[dispatch.pipelineIndex]);
			cmdList->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
		}
	}

	void denoise(const DenoiseParameters &p) {
		if (instance == nullptr) {
			return;
		}

		updateSettings(p);

		const struct {
			nrd::Identifier identifier;
			const SignalParameters *signal;
		} signals[] = {
			{ IdentifierDirect, &p.direct },
			{ IdentifierIndirect, &p.indirect }
		};

		ID3D12GraphicsCommandList4 *cmdList = device->getD3D12CommandList();
		uint32_t setIndex = 0;
		for (uint32_t s = 0; s < _countof(signals); s++) {
			const SignalParameters &signal = *signals[s].signal;
			if ((signal.inRadianceHitDist == nullptr) || (signal.outRadiance == nullptr)) {
				continue;
			}

			const nrd::Identifier identifiers[] = { signals[s].identifier };
			const nrd::DispatchDesc *dispatches = nullptr;
			uint32_t dispatchesNum = 0;
			nrd::Result result = nrd::GetComputeDispatches(*instance, identifiers, _countof(identifiers), dispatches, dispatchesNum);
			if (result != nrd::Result::SUCCESS) {
				continue;
			}

			recordDispatches(cmdList, dispatches, dispatchesNum, p, signal, setIndex);
		}
	}

	bool isInitialized() const {
		return instance != nullptr;
	}

	HitDistanceParams getHitDistanceParams() const {
		HitDistanceParams result;
		result.a = hitDistanceParameters.A;
		result.b = hitDistanceParameters.B;
		result.c = hitDistanceParameters.C;
		return result;
	}
};

RT64::Denoiser::Denoiser(Device *device) {
	ctx = new Context(device);
}

RT64::Denoiser::~Denoiser() {
	delete ctx;
}

void RT64::Denoiser::set(int renderWidth, int renderHeight, int outputWidth, int outputHeight) {
	ctx->set(renderWidth, renderHeight, outputWidth, outputHeight);
}

void RT64::Denoiser::denoise(const DenoiseParameters &p) {
	ctx->denoise(p);
}

bool RT64::Denoiser::isInitialized() const {
	return ctx->isInitialized();
}

RT64::Denoiser::HitDistanceParams RT64::Denoiser::getHitDistanceParams() const {
	return ctx->getHitDistanceParams();
}

#endif
