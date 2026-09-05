//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Denoiser {
	public:
		struct SignalParameters {
			ID3D12Resource *inRadianceHitDist = nullptr;
			ID3D12Resource *outRadiance = nullptr;
		};

		struct DenoiseParameters {
			int rectWidth = 0;
			int rectHeight = 0;
			ID3D12Resource *inMotionVectors = nullptr;  
			ID3D12Resource *inNormalRoughness = nullptr;
			ID3D12Resource *inViewZ = nullptr;
			ID3D12Resource *inHistoryConfidence = nullptr;
			SignalParameters direct;
			SignalParameters indirect;
			XMMATRIX view;
			XMMATRIX viewPrev;
			XMMATRIX projection;
			XMMATRIX projectionPrev;
			float jitterX = 0.0f;
			float jitterY = 0.0f;
			float jitterXPrev = 0.0f;
			float jitterYPrev = 0.0f;
			float deltaTimeMs = 16.6667f;
			uint32_t frameIndex = 0;
			bool resetAccumulation = false;
		};

		struct HitDistanceParams {
			float a = 0.0f;
			float b = 0.0f;
			float c = 0.0f;
		};

	private:
		class Context;
		Context *ctx;
	public:
		Denoiser(Device *device);
		~Denoiser();
		void set(int renderWidth, int renderHeight, int outputWidth, int outputHeight);
		void denoise(const DenoiseParameters &p);
		bool isInitialized() const;
		HitDistanceParams getHitDistanceParams() const;
	};
};
