//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	std::string buildCustomPostProcessSource(const std::string &fragmentHLSL, const std::vector<RT64_SHADER_INPUT> &fragmentInputs, const std::string &fragmentOutputName);

	enum CombinerFlags : unsigned int {
		CC_FLAG_2CYCLE       = (1u << 0),
		CC_FLAG_ALPHA        = (1u << 1),
		CC_FLAG_TEXTURE_EDGE = (1u << 2),
		CC_FLAG_NOISE        = (1u << 3),
		CC_FLAG_NOISE_INPUT  = (1u << 4),
		CC_FLAG_TEX0         = (1u << 5),
		CC_FLAG_TEX1         = (1u << 6),
		CC_FLAG_NORMAL_MAP   = (1u << 7),
		CC_FLAG_SPECULAR_MAP = (1u << 12),
		CC_FLAG_BUMP_MAP     = (1u << 13),
		CC_FLAG_INPUT_COUNT_SHIFT = 8,
		CC_FLAG_INPUT_COUNT_MASK  = (0xFu << 8)
	};

	class Shader {
	public:
		enum class Filter : int {
			Point,
			Linear
		};

		enum class AddressingMode : int {
			Wrap,
			Mirror,
			Clamp
		};

		struct CombinerData {
			unsigned int rgb1 = 0;
			unsigned int alpha1 = 0;
			unsigned int rgb2 = 0;
			unsigned int alpha2 = 0;
			unsigned int flags = 0;
			unsigned int samplerIndex = 0;
		};

		struct HitGroup {
			IDxcBlob *blob = nullptr;
			std::wstring closestHitName;
			std::wstring anyHitName;
			std::wstring hitGroupName;
			void *id = nullptr;
		};
	private:
		Device *device;
		CombinerData combinerData;
		bool rasterEnabled;
		bool raytraceEnabled;
		bool customSourceEnabled = false;
		uint64_t customSourceHash = 0;
		HitGroup surfaceHitGroup;
		HitGroup shadowHitGroup;
	public:
		static unsigned int samplerHeapIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr);

		Shader(Device *device, RT64_COMBINER_DESC cc, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags);

		Shader(Device *device, RT64_COMBINER_DESC cc, const std::string &customVertexHLSL, const std::string &customFragmentHLSL, const std::vector<RT64_SHADER_INPUT> &vertexInputs, const std::string &fragmentOutputName, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags);

		~Shader();
		const CombinerData &getCombinerData() const;
		bool hasRasterGroup() const;
		bool hasHitGroups() const;
		bool hasCustomSource() const;
		uint64_t getCustomSourceHash() const;
		HitGroup &getSurfaceHitGroup();
		HitGroup &getShadowHitGroup();
	};
};
