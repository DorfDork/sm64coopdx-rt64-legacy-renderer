//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_fsr.h"

#include "FidelityFX-SDK/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.hpp"
#include "FidelityFX-SDK/Kits/FidelityFX/upscalers/include/ffx_upscale.hpp"

#include "rt64_device.h"

// FSR::Context

class RT64::FSR::Context {
private:
    Device *device;
    bool initialized;
    ffx::Context fsrContext;
    bool fsrFilled;
public:
    Context(Device *device) {
        assert(device != nullptr);

        this->device = device;
        fsrContext = nullptr;
        fsrFilled = false;
        initialized = (device->getD3D12Device() != nullptr);
    }

    ~Context() {
        release();
    }

    ffx::CreateBackendDX12Desc backendDesc() const {
        ffx::CreateBackendDX12Desc desc{};
        desc.device = device->getD3D12Device();
        return desc;
    }

    uint32_t toFSRQuality(QualityMode q) {
        switch (q) {
        case QualityMode::UltraPerformance:
            return FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
        case QualityMode::Performance:
            return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
        case QualityMode::Balanced:
            return FFX_UPSCALE_QUALITY_MODE_BALANCED;
        case QualityMode::Quality:
        case QualityMode::UltraQuality:
        case QualityMode::Native:
            return FFX_UPSCALE_QUALITY_MODE_QUALITY;
        default:
            return FFX_UPSCALE_QUALITY_MODE_BALANCED;
        }
    }

    bool set(QualityMode quality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
        if (quality == QualityMode::Auto) {
            quality = getQualityAuto(displayWidth, displayHeight);
        }

        release();

        ffx::CreateContextDescUpscale createDesc{};
        createDesc.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        createDesc.maxRenderSize = { (uint32_t)(renderWidth), (uint32_t)(renderHeight) };
        createDesc.maxUpscaleSize = { (uint32_t)(displayWidth), (uint32_t)(displayHeight) };

        ffx::CreateBackendDX12Desc backend = backendDesc();
        ffx::ReturnCode retCode = ffx::CreateContext(fsrContext, nullptr, createDesc, backend);
        if (retCode != ffx::ReturnCode::Ok) {
            RT64_LOG_PRINTF("ffx::CreateContext failed: %d\n", (uint32_t)(retCode));
            return false;
        }

        fsrFilled = true;

        return true;
    }

    void release() {
        device->waitForGPU();

        if (fsrFilled) {
            ffx::DestroyContext(fsrContext);
            fsrFilled = false;
        }
    }

    bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
        if (quality == QualityMode::Auto) {
            quality = getQualityAuto(displayWidth, displayHeight);
        }

        // FSR doesn't provide these quality settings, so we force them instead.
        if (quality == QualityMode::Native) {
            renderWidth = displayWidth;
            renderHeight = displayHeight;
        }
        else if (quality == QualityMode::UltraQuality) {
            renderWidth = (displayWidth * 77) / 100;
            renderHeight = (displayHeight * 77) / 100;
        }
        else {
            uint32_t fsrRenderWidth = 0;
            uint32_t fsrRenderHeight = 0;
            ffx::QueryDescUpscaleGetRenderResolutionFromQualityMode getRes{};
            getRes.displayWidth = displayWidth;
            getRes.displayHeight = displayHeight;
            getRes.qualityMode = toFSRQuality(quality);
            getRes.pOutRenderWidth = &fsrRenderWidth;
            getRes.pOutRenderHeight = &fsrRenderHeight;

            ffx::CreateBackendDX12Desc backend = backendDesc();
            ffx::ReturnCode retCode = ffx::Query(getRes, backend);
            if (retCode != ffx::ReturnCode::Ok) {
                RT64_LOG_PRINTF("ffx::Query (GetRenderResolutionFromQualityMode) failed: %d\n", (uint32_t)(retCode));
                return false;
            }

            renderWidth = fsrRenderWidth;
            renderHeight = fsrRenderHeight;
        }

        return true;
    }

    int getJitterPhaseCount(int renderWidth, int displayWidth) {
        int32_t phaseCount = 0;
        ffx::QueryDescUpscaleGetJitterPhaseCount getPhase{};
        getPhase.renderWidth = renderWidth;
        getPhase.displayWidth = displayWidth;
        getPhase.pOutPhaseCount = &phaseCount;

        ffx::CreateBackendDX12Desc backend = backendDesc();
        ffx::ReturnCode retCode = ffx::Query(getPhase, backend);
        if (retCode != ffx::ReturnCode::Ok) {
            RT64_LOG_PRINTF("ffx::Query (GetJitterPhaseCount) failed: %d\n", (uint32_t)(retCode));
        }

        return phaseCount;
    }

    void upscale(const UpscaleParameters &p) {
        ffx::DispatchDescUpscale dispatchDesc{};
        dispatchDesc.commandList = device->getD3D12CommandList();
        dispatchDesc.color = ffxApiGetResourceDX12(p.inColor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatchDesc.depth = ffxApiGetResourceDX12(p.inDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatchDesc.motionVectors = ffxApiGetResourceDX12(p.inFlow, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatchDesc.exposure = ffxApiGetResourceDX12(nullptr);
        dispatchDesc.reactive = ffxApiGetResourceDX12(p.inReactiveMask, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatchDesc.transparencyAndComposition = ffxApiGetResourceDX12(p.inLockMask, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatchDesc.output = ffxApiGetResourceDX12(p.outColor, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatchDesc.jitterOffset = { p.jitterX, p.jitterY };
        dispatchDesc.motionVectorScale = { 1.0f, 1.0f };
        dispatchDesc.reset = p.resetAccumulation;
        dispatchDesc.renderSize = { (uint32_t)(p.inRect.w), (uint32_t)(p.inRect.h) };
        dispatchDesc.enableSharpening = (p.sharpness > 0.0f);
        dispatchDesc.sharpness = p.sharpness;
        dispatchDesc.frameTimeDelta = p.deltaTime;
        dispatchDesc.preExposure = 1.0f;
        dispatchDesc.cameraNear = p.nearPlane;
        dispatchDesc.cameraFar = p.farPlane;
        dispatchDesc.cameraFovAngleVertical = p.fovY;
        dispatchDesc.viewSpaceToMetersFactor = 1.0f;
        dispatchDesc.flags = 0;

        ffx::ReturnCode retCode = ffx::Dispatch(fsrContext, dispatchDesc);
        if (retCode != ffx::ReturnCode::Ok) {
            RT64_LOG_PRINTF("ffx::Dispatch failed: %d\n", (uint32_t)(retCode));
        }
    }

    bool isInitialized() const {
        return initialized;
    }
};

// FSR

RT64::FSR::FSR(Device *device) {
    ctx = new Context(device);
}

RT64::FSR::~FSR() {
    delete ctx;
}

void RT64::FSR::set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
    ctx->set(inQuality, renderWidth, renderHeight, displayWidth, displayHeight);
}

bool RT64::FSR::getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
    return ctx->getQualityInformation(quality, displayWidth, displayHeight, renderWidth, renderHeight);
}

int RT64::FSR::getJitterPhaseCount(int renderWidth, int displayWidth) {
    return ctx->getJitterPhaseCount(renderWidth, displayWidth);
}

void RT64::FSR::upscale(const UpscaleParameters &p) {
    ctx->upscale(p);
}

bool RT64::FSR::isInitialized() const {
    return ctx->isInitialized();
}

bool RT64::FSR::requiresNonShaderResourceInputs() const {
    return false;
}

#endif
