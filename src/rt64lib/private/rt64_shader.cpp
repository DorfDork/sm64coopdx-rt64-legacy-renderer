//
// RT64
//
#ifndef RT64_MINIMAL

#include "rt64_shader.h"

#include "rt64_device.h"
#include "rt64_shader_hlsli.h"

#include <set>

// Private

enum {
	SHADER_0,
	SHADER_INPUT_1,
	SHADER_INPUT_2,
	SHADER_INPUT_3,
	SHADER_INPUT_4,
	SHADER_INPUT_5,
	SHADER_INPUT_6,
	SHADER_INPUT_7,
	SHADER_INPUT_8,
	SHADER_TEXEL0,
	SHADER_TEXEL0A,
	SHADER_TEXEL1,
	SHADER_TEXEL1A,
	SHADER_1,
	SHADER_COMBINED,
	SHADER_COMBINEDA,
	SHADER_NOISE
};

// Four 8-bit operands to a word, in the order the shader's ccCmd() unpacks them.
static unsigned int packCommands(const unsigned char cmd[4]) {
	return ((unsigned int)(cmd[0])      ) |
	       ((unsigned int)(cmd[1]) <<  8) |
	       ((unsigned int)(cmd[2]) << 16) |
	       ((unsigned int)(cmd[3]) << 24);
}

static unsigned int packFlags(const RT64_COMBINER_DESC &desc) {
	unsigned char cmd[16];
	for (int i = 0; i < 4; i++) {
		cmd[i] = desc.rgb1[i];
		cmd[4 + i] = desc.alpha1[i];
		cmd[8 + i] = desc.rgb2[i];
		cmd[12 + i] = desc.alpha2[i];
	}

	const bool use2Cycle = (desc.use2Cycle != 0);
	unsigned int flags = 0;
	if (use2Cycle) { flags |= RT64::CC_FLAG_2CYCLE; }
	if (desc.optAlpha != 0) { flags |= RT64::CC_FLAG_ALPHA; }
	if (desc.optTextureEdge != 0) { flags |= RT64::CC_FLAG_TEXTURE_EDGE; }
	if (desc.optNoise != 0) { flags |= RT64::CC_FLAG_NOISE; }

	unsigned int inputCount = 0;
	const int cmdLength = use2Cycle ? 16 : 8;
	for (int i = 0; i < cmdLength; i++) {
		const unsigned int v = cmd[i];
		if ((v >= SHADER_INPUT_1) && (v <= SHADER_INPUT_8) && (v > inputCount)) {
			inputCount = v;
		}

		if ((v == SHADER_TEXEL0) || (v == SHADER_TEXEL0A)) { flags |= RT64::CC_FLAG_TEX0; }
		if ((v == SHADER_TEXEL1) || (v == SHADER_TEXEL1A)) { flags |= RT64::CC_FLAG_TEX1; }
		if (v == SHADER_NOISE) { flags |= RT64::CC_FLAG_NOISE_INPUT; }
	}

	flags |= (inputCount << RT64::CC_FLAG_INPUT_COUNT_SHIFT) & RT64::CC_FLAG_INPUT_COUNT_MASK;
	return flags;
}

static const unsigned int CustomTextureRegisterBase = 600;
static const unsigned int CustomSamplerRegisterBase = 20;

static void replaceAll(std::string &text, const std::string &from, const std::string &to) {
	if (from.empty()) {
		return;
	}

	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		text.replace(pos, from.length(), to);
		pos += to.length();
	}
}

static void collectStaticNames(const std::string &source, std::set<std::string> &names) {
	const std::string prefix = "static ";
	size_t pos = 0;
	while ((pos = source.find(prefix, pos)) != std::string::npos) {
		const bool atLineStart = (pos == 0) || (source[pos - 1] == '\n');
		pos += prefix.length();
		if (!atLineStart) {
			continue;
		}

		const size_t lineEnd = source.find('\n', pos);
		if (lineEnd == std::string::npos) {
			break;
		}

		std::string line = source.substr(pos, lineEnd - pos);
		const size_t terminator = line.find_first_of("=;");
		if (terminator == std::string::npos) {
			continue;
		}

		line = line.substr(0, terminator);
		const size_t nameEnd = line.find_last_not_of(" \t");
		if (nameEnd == std::string::npos) {
			continue;
		}

		const size_t nameStart = line.find_last_of(" \t", nameEnd);
		if (nameStart == std::string::npos) {
			continue;
		}

		names.insert(line.substr(nameStart + 1, nameEnd - nameStart));
	}
}

static std::string removeDuplicateStatics(const std::string &source, const std::set<std::string> &declaredElsewhere) {
	std::string result;
	result.reserve(source.length());

	size_t lineStart = 0;
	while (lineStart < source.length()) {
		size_t lineEnd = source.find('\n', lineStart);
		if (lineEnd == std::string::npos) {
			lineEnd = source.length();
		}
		else {
			lineEnd++;
		}

		const std::string line = source.substr(lineStart, lineEnd - lineStart);
		lineStart = lineEnd;

		bool drop = false;
		if (line.compare(0, 7, "static ") == 0) {
			for (const std::string &name : declaredElsewhere) {
				const size_t at = line.find(name);
				if (at == std::string::npos) {
					continue;
				}

				const char before = line[at - 1];
				const char after = line[at + name.length()];
				const bool wordStart = (before == ' ') || (before == '\t');
				const bool wordEnd = (after == ';') || (after == ' ') || (after == '\t') || (after == '=');
				if (wordStart && wordEnd) {
					drop = true;
					break;
				}
			}
		}

		if (!drop) {
			result += line;
		}
	}

	return result;
}

static std::string stripResourceDeclarations(const std::string &source) {
	std::string result;
	result.reserve(source.length());

	size_t lineStart = 0;
	while (lineStart < source.length()) {
		size_t lineEnd = source.find('\n', lineStart);
		if (lineEnd == std::string::npos) {
			lineEnd = source.length();
		}
		else {
			lineEnd++;
		}

		const std::string line = source.substr(lineStart, lineEnd - lineStart);
		lineStart = lineEnd;

		const bool isTexture = (line.find(": register(t") != std::string::npos);
		const bool isSampler = (line.find(": register(s") != std::string::npos);
		if (!isTexture && !isSampler) {
			result += line;
		}
	}

	return result;
}

// Moves the shader's texture and sampler registers out of the way of the ray tracer's.
static std::string rewriteResourceRegisters(const std::string &source) {
	std::string result;
	result.reserve(source.length());

	const std::string marker = ": register(";
	size_t pos = 0;
	while (true) {
		const size_t at = source.find(marker, pos);
		if (at == std::string::npos) {
			result.append(source, pos, std::string::npos);
			break;
		}

		// Everything up to and including the register's type letter is copied as is.
		const size_t typeAt = at + marker.length();
		const size_t numberAt = typeAt + 1;
		result.append(source, pos, numberAt - pos);
		pos = numberAt;

		const char registerType = (typeAt < source.length()) ? source[typeAt] : '\0';
		size_t numberEnd = numberAt;
		while ((numberEnd < source.length()) && (source[numberEnd] >= '0') && (source[numberEnd] <= '9')) {
			numberEnd++;
		}

		if (numberEnd == numberAt) {
			continue;
		}

		// Constant buffers are left alone. The caller's start at 1 and the ray tracer only uses 0.
		if ((registerType != 't') && (registerType != 's')) {
			result.append(source, numberAt, numberEnd - numberAt);
			pos = numberEnd;
			continue;
		}

		const unsigned int original = (unsigned int)(strtoul(source.substr(numberAt, numberEnd - numberAt).c_str(), nullptr, 10));
		const unsigned int shifted = original + ((registerType == 't') ? CustomTextureRegisterBase : CustomSamplerRegisterBase);
		result += std::to_string(shifted);
		pos = numberEnd;
	}

	return result;
}

// Rewrites one kind of sampling call into SampleLevel reading the top mip level.
static std::string rewriteSampleCalls(const std::string &source, const std::string &marker, bool dropLastArgument) {
	std::string result;
	result.reserve(source.length());

	size_t pos = 0;
	while (true) {
		const size_t at = source.find(marker, pos);
		if (at == std::string::npos) {
			result.append(source, pos, std::string::npos);
			break;
		}

		result.append(source, pos, at - pos);
		result += ".SampleLevel(";

		const size_t argsAt = at + marker.length();
		size_t scan = argsAt;
		size_t lastComma = std::string::npos;
		int depth = 1;
		while (scan < source.length()) {
			if (source[scan] == '(') {
				depth++;
			}
			else if (source[scan] == ')') {
				depth--;
				if (depth == 0) {
					break;
				}
			}
			else if ((source[scan] == ',') && (depth == 1)) {
				lastComma = scan;
			}

			scan++;
		}

		const size_t argsEnd = (dropLastArgument && (lastComma != std::string::npos)) ? lastComma : scan;
		result.append(source, argsAt, argsEnd - argsAt);
		result += ", 0.0f)";
		pos = (scan < source.length()) ? (scan + 1) : source.length();
	}

	return result;
}

static std::string rewriteSamplesForRaytracing(const std::string &source) {
	const std::string sampled = rewriteSampleCalls(source, ".Sample(", false);
	return rewriteSampleCalls(sampled, ".SampleBias(", true);
}

static std::string buildCustomShadeGlue(const std::vector<RT64_SHADER_INPUT> &vertexInputs, const std::string &fragmentOutputName) {
	std::string glue =
		"float4 customShade(float2 vertexUV, float3 vertexNormal, float3 vertexPosition, float4 vertexClipPosition, float3 vertexBarycentric, float4 inputs[8], float4 texVal0, float4 texVal1, float noise) {\n";

	for (const RT64_SHADER_INPUT &input : vertexInputs) {
		if (input.name == nullptr) {
			continue;
		}

		const std::string name = input.name;
		std::string value;
		if (name == "aTexCoord0" || name == "aTexCoord1" || name == "aLightMap") {
			value = "vertexUV";
		}
		else if (name == "aNormal") {
			value = "vertexNormal";
		}
		else if (name == "aLocalPos") {
			value = "float4(vertexPosition, 1.0f)";
		}
		else if (name == "aVtxPos") {
			value = "vertexClipPosition";
		}
		else if (name == "aBarycentric") {
			value = "vertexBarycentric";
		}
		else if ((name.compare(0, 6, "aInput") == 0) && (name.length() == 7)) {
			const int index = name[6] - '1';
			if ((index >= 0) && (index < 8)) {
				value = "inputs[" + std::to_string(index) + "]";
			}
		}

		// Every attribute the vertex layout can carry is covered above.
		if (value.empty()) {
			continue;
		}

		// The types line up already: texture coordinates and the light map are two components, the
		// normal three, and a combiner input four.
		glue += "\t" + name + " = " + value + ";\n";
	}

	glue += "\tvert_main();\n";
	glue += "\tfrag_main();\n";

	// Without a name for the fragment shader's output there is nothing to read back out of it, so
	// the surface is left obviously wrong rather than the shader failing to build.
	if (!fragmentOutputName.empty()) {
		glue += "\treturn " + fragmentOutputName + ";\n";
	}
	else {
		glue += "\treturn float4(1.0f, 0.0f, 1.0f, 1.0f);\n";
	}

	glue += "}\n";
	return glue;
}

static std::string buildCustomHitLibrarySource(bool surfaceHit, const std::string &vertexHLSL, const std::string &fragmentHLSL, const std::vector<RT64_SHADER_INPUT> &vertexInputs, const std::string &fragmentOutputName) {
	std::string source;
	auto appendChunk = [&source](const char *chunk) {
		source += chunk;
		source += "\n";
	};

	appendChunk(INCLUDE_HLSLI(MaterialsHLSLI));
	appendChunk(INCLUDE_HLSLI(InstancesHLSLI));
	if (surfaceHit) {
		appendChunk(INCLUDE_HLSLI(GlobalHitBuffersHLSLI));
	}

	appendChunk(INCLUDE_HLSLI(RayHLSLI));
	appendChunk(INCLUDE_HLSLI(RandomHLSLI));
	appendChunk(INCLUDE_HLSLI(GlobalParamsHLSLI));
	appendChunk(INCLUDE_HLSLI(TexturesHLSLI));
	appendChunk(INCLUDE_HLSLI(UberCombinerHLSLI));
	appendChunk(INCLUDE_HLSLI(UberVertexPullHLSLI));

	source +=
		"static uint customDiffuseTexIndex;\n"
		"static uint customDiffuse2TexIndex;\n"
		"static uint customSamplerIndex;\n"
		"#define uTex0 gTextures[NonUniformResourceIndex(customDiffuseTexIndex)]\n"
		"#define uTex1 gTextures[NonUniformResourceIndex(customDiffuse2TexIndex)]\n"
		"#define _uTex0_sampler gSamplers[customSamplerIndex]\n"
		"#define _uTex1_sampler gSamplers[customSamplerIndex]\n";

	bool usesNormal = false;
	bool usesLocalPosition = false;
	bool usesClipPosition = false;
	bool usesInputs = false;
	for (const RT64_SHADER_INPUT &input : vertexInputs) {
		if (input.name == nullptr) {
			continue;
		}

		const std::string name = input.name;
		if (name == "aNormal") { usesNormal = true; }
		else if (name == "aLocalPos") { usesLocalPosition = true; }
		else if (name == "aVtxPos") { usesClipPosition = true; }
		else if ((name.compare(0, 6, "aInput") == 0) && (name.length() == 7)) { usesInputs = true; }
	}

	source += std::string("#define CUSTOM_USES_NORMAL ") + (usesNormal ? "1" : "0") + "\n";
	source += std::string("#define CUSTOM_USES_LOCAL_POSITION ") + (usesLocalPosition ? "1" : "0") + "\n";
	source += std::string("#define CUSTOM_USES_CLIP_POSITION ") + (usesClipPosition ? "1" : "0") + "\n";
	source += std::string("#define CUSTOM_USES_INPUTS ") + (usesInputs ? "1" : "0") + "\n";

	appendChunk(surfaceHit ? INCLUDE_HLSLI(CustomSurfaceHitHLSL) : INCLUDE_HLSLI(CustomShadowHitHLSL));

	std::string vertexSource = rewriteSamplesForRaytracing(stripResourceDeclarations(vertexHLSL));
	std::string fragmentSource = rewriteSamplesForRaytracing(stripResourceDeclarations(fragmentHLSL));

	replaceAll(vertexSource, "SPIRV_Cross_Input", "CustomVertexInput");
	replaceAll(vertexSource, "SPIRV_Cross_Output", "CustomVertexOutput");
	replaceAll(vertexSource, " main(", " customVertexEntry(");
	replaceAll(fragmentSource, "SPIRV_Cross_Input", "CustomFragmentInput");
	replaceAll(fragmentSource, "SPIRV_Cross_Output", "CustomFragmentOutput");
	replaceAll(fragmentSource, " main(", " customFragmentEntry(");

	std::set<std::string> vertexStatics;
	collectStaticNames(vertexSource, vertexStatics);
	fragmentSource = removeDuplicateStatics(fragmentSource, vertexStatics);

	source += vertexSource;
	source += "\n";
	source += fragmentSource;
	source += "\n";
	source += buildCustomShadeGlue(vertexInputs, fragmentOutputName);
	return source;
}

std::string RT64::buildCustomPostProcessSource(const std::string &fragmentHLSL, const std::vector<RT64_SHADER_INPUT> &fragmentInputs, const std::string &fragmentOutputName) {
	std::string source = INCLUDE_HLSLI(CustomPostProcessPSHLSL);
	source += "\n";

	std::string fragmentSource = stripResourceDeclarations(fragmentHLSL);
	replaceAll(fragmentSource, "SPIRV_Cross_Input", "CustomPostProcessInput");
	replaceAll(fragmentSource, "SPIRV_Cross_Output", "CustomPostProcessOutput");
	replaceAll(fragmentSource, " main(", " customPostProcessEntry(");

	source += fragmentSource;
	source += "\n";

	std::string glue = "float4 customPostProcess(float2 uv, float4 pos) {\n";
	for (const RT64_SHADER_INPUT &input : fragmentInputs) {
		if (input.name == nullptr) {
			continue;
		}

		if (input.size == 2) {
			glue += std::string("\t") + input.name + " = uv;\n";
		}
		else if (input.size == 4) {
			glue += std::string("\t") + input.name + " = pos;\n";
		}
	}

	if (fragmentSource.find("gl_FragCoord") != std::string::npos) {
		glue += "\tgl_FragCoord = pos;\n";
	}

	glue += "\tfrag_main();\n";
	if (!fragmentOutputName.empty()) {
		glue += "\treturn " + fragmentOutputName + ";\n";
	}
	else {
		glue += "\treturn float4(1.0f, 0.0f, 1.0f, 1.0f);\n";
	}

	glue += "}\n";
	source += glue;
	return source;
}

// Public

unsigned int RT64::Shader::samplerHeapIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr) {
	return (unsigned int)(filter) * 9 + (unsigned int)(hAddr) * 3 + (unsigned int)(vAddr);
}

RT64::Shader::Shader(Device *device, RT64_COMBINER_DESC cc, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags) {
	assert(device != nullptr);
	this->device = device;

	rasterEnabled = (flags & RT64_SHADER_RASTER_ENABLED) != 0;
	raytraceEnabled = (flags & RT64_SHADER_RAYTRACE_ENABLED) != 0;

	combinerData.rgb1 = packCommands(cc.rgb1);
	combinerData.alpha1 = packCommands(cc.alpha1);
	combinerData.rgb2 = packCommands(cc.rgb2);
	combinerData.alpha2 = packCommands(cc.alpha2);
	combinerData.flags = packFlags(cc);
	combinerData.samplerIndex = samplerHeapIndex(filter, hAddr, vAddr);

	if (flags & RT64_SHADER_NORMAL_MAP_ENABLED) { combinerData.flags |= RT64::CC_FLAG_NORMAL_MAP; }
	if (flags & RT64_SHADER_SPECULAR_MAP_ENABLED) { combinerData.flags |= RT64::CC_FLAG_SPECULAR_MAP; }
	if (flags & RT64_SHADER_BUMP_MAP_ENABLED) { combinerData.flags |= RT64::CC_FLAG_BUMP_MAP; }
}

// FNV-1a over the concatenated vertex+fragment HLSL. If the custom shader is the same, it will hit the device's PSO cache instead of recompiling.
static uint64_t hashCustomSource(const std::string &vertexHLSL, const std::string &fragmentHLSL) {
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](const std::string &s) {
		for (unsigned char c : s) {
			h ^= c;
			h *= 1099511628211ull;
		}
	};

	mix(vertexHLSL);
	mix(fragmentHLSL);
	return h;
}

RT64::Shader::Shader(Device *device, RT64_COMBINER_DESC cc, const std::string &customVertexHLSL, const std::string &customFragmentHLSL, const std::vector<RT64_SHADER_INPUT> &vertexInputs, const std::string &fragmentOutputName, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags) {
	assert(device != nullptr);
	this->device = device;

	rasterEnabled = (flags & RT64_SHADER_RASTER_ENABLED) != 0;
	raytraceEnabled = (flags & RT64_SHADER_RAYTRACE_ENABLED) != 0;
	customSourceEnabled = true;

	combinerData.rgb1 = packCommands(cc.rgb1);
	combinerData.alpha1 = packCommands(cc.alpha1);
	combinerData.rgb2 = packCommands(cc.rgb2);
	combinerData.alpha2 = packCommands(cc.alpha2);
	combinerData.flags = packFlags(cc);
	combinerData.samplerIndex = samplerHeapIndex(filter, hAddr, vAddr);

	customSourceHash = hashCustomSource(customVertexHLSL, customFragmentHLSL);

	if (rasterEnabled) {
		device->getOrCreateCustomRasterPipeline(customSourceHash, customVertexHLSL, customFragmentHLSL, vertexInputs.data(), (unsigned int)(vertexInputs.size()));
	}

	if (raytraceEnabled) {
		wchar_t hashSuffix[32];
		swprintf_s(hashSuffix, L"_%016llx", (unsigned long long)(customSourceHash));

		surfaceHitGroup.closestHitName = std::wstring(L"CustomSurfaceClosestHit") + hashSuffix;
		surfaceHitGroup.anyHitName = std::wstring(L"CustomSurfaceAnyHit") + hashSuffix;
		surfaceHitGroup.hitGroupName = std::wstring(L"CustomSurfaceHitGroup") + hashSuffix;
		shadowHitGroup.closestHitName = std::wstring(L"CustomShadowClosestHit") + hashSuffix;
		shadowHitGroup.anyHitName = std::wstring(L"CustomShadowAnyHit") + hashSuffix;
		shadowHitGroup.hitGroupName = std::wstring(L"CustomShadowHitGroup") + hashSuffix;

		char hashSuffixNarrow[32];
		snprintf(hashSuffixNarrow, sizeof(hashSuffixNarrow), "_%016llx", (unsigned long long)(customSourceHash));

		auto renameEntryPoints = [&hashSuffixNarrow](const char *closestHit, const char *anyHit) {
			std::string defines;
			defines += std::string("#define ") + closestHit + " " + closestHit + hashSuffixNarrow + "\n";
			defines += std::string("#define ") + anyHit + " " + anyHit + hashSuffixNarrow + "\n";
			return defines;
		};

		const bool librariesAreNew = !device->getOrCreateCustomHitLibraries(customSourceHash, &surfaceHitGroup.blob, &shadowHitGroup.blob);
		if (librariesAreNew) {
			const std::string surfaceSource = renameEntryPoints("CustomSurfaceClosestHit", "CustomSurfaceAnyHit") +
				buildCustomHitLibrarySource(true, customVertexHLSL, customFragmentHLSL, vertexInputs, fragmentOutputName);
			const std::string shadowSource = renameEntryPoints("CustomShadowClosestHit", "CustomShadowAnyHit") +
				buildCustomHitLibrarySource(false, customVertexHLSL, customFragmentHLSL, vertexInputs, fragmentOutputName);

			surfaceHitGroup.blob = device->compileHlslBlob(surfaceSource, L"", L"lib_6_3");
			shadowHitGroup.blob = device->compileHlslBlob(shadowSource, L"", L"lib_6_3");
			device->storeCustomHitLibraries(customSourceHash, surfaceHitGroup.blob, shadowHitGroup.blob);
		}

		device->addCustomShader(this, librariesAreNew);
	}
}

RT64::Shader::~Shader() {
	if (customSourceEnabled) {
		device->removeCustomShader(this);
	}
}

const RT64::Shader::CombinerData &RT64::Shader::getCombinerData() const {
	return combinerData;
}

bool RT64::Shader::hasRasterGroup() const {
	return rasterEnabled;
}

bool RT64::Shader::hasHitGroups() const {
	return raytraceEnabled;
}

bool RT64::Shader::hasCustomSource() const {
	return customSourceEnabled;
}

uint64_t RT64::Shader::getCustomSourceHash() const {
	return customSourceHash;
}

RT64::Shader::HitGroup &RT64::Shader::getSurfaceHitGroup() {
	return surfaceHitGroup;
}

RT64::Shader::HitGroup &RT64::Shader::getShadowHitGroup() {
	return shadowHitGroup;
}

RT64::Shader::Filter convertFilter(unsigned int filter) {
	switch (filter) {
	case RT64_SHADER_FILTER_LINEAR:
		return RT64::Shader::Filter::Linear;
	case RT64_SHADER_FILTER_POINT:
	default:
		return RT64::Shader::Filter::Point;
	}
}

RT64::Shader::AddressingMode convertAddressingMode(unsigned int mode) {
	switch (mode) {
	case RT64_SHADER_ADDRESSING_CLAMP:
		return RT64::Shader::AddressingMode::Clamp;
	case RT64_SHADER_ADDRESSING_MIRROR:
		return RT64::Shader::AddressingMode::Mirror;
	case RT64_SHADER_ADDRESSING_WRAP:
	default:
		return RT64::Shader::AddressingMode::Wrap;
	}
}

DLLEXPORT RT64_SHADER *RT64_CreateShader(RT64_DEVICE *devicePtr, RT64_COMBINER_DESC cc, unsigned int filter, unsigned int hAddr, unsigned int vAddr, int flags) {
	try {
		RT64::Device *device = (RT64::Device *)(devicePtr);
		RT64::Shader::Filter sFilter = convertFilter(filter);
		RT64::Shader::AddressingMode sHAddr = convertAddressingMode(hAddr);
		RT64::Shader::AddressingMode sVAddr = convertAddressingMode(vAddr);
		return (RT64_SHADER *)(new RT64::Shader(device, cc, sFilter, sHAddr, sVAddr, flags));
	}
	RT64_CATCH_EXCEPTION();
	return nullptr;
}

DLLEXPORT RT64_SHADER *RT64_CreateShaderFromSource(RT64_DEVICE *devicePtr, RT64_COMBINER_DESC cc, const char *customVertexHLSL, const char *customFragmentHLSL, const RT64_SHADER_INPUT *vertexInputs, unsigned int vertexInputCount, const char *fragmentOutputName, unsigned int filter, unsigned int hAddr, unsigned int vAddr, int flags) {
	try {
		RT64::Device *device = (RT64::Device *)(devicePtr);
		RT64::Shader::Filter sFilter = convertFilter(filter);
		RT64::Shader::AddressingMode sHAddr = convertAddressingMode(hAddr);
		RT64::Shader::AddressingMode sVAddr = convertAddressingMode(vAddr);
		std::vector<RT64_SHADER_INPUT> inputs(vertexInputs, vertexInputs + vertexInputCount);
		const std::string outputName = (fragmentOutputName != nullptr) ? fragmentOutputName : "";
		return (RT64_SHADER *)(new RT64::Shader(device, cc, customVertexHLSL, customFragmentHLSL, inputs, outputName, sFilter, sHAddr, sVAddr, flags));
	}
	RT64_CATCH_EXCEPTION();
	return nullptr;
}

DLLEXPORT void RT64_SetPostProcessShader(RT64_DEVICE *devicePtr, const char *fragmentHLSL, const RT64_SHADER_INPUT *fragmentInputs, unsigned int fragmentInputCount, const char *fragmentOutputName, int targetWidth, int targetHeight) {
	try {
		RT64::Device *device = (RT64::Device *)(devicePtr);
		std::vector<RT64_SHADER_INPUT> inputs;
		if ((fragmentInputs != nullptr) && (fragmentInputCount > 0)) {
			inputs.assign(fragmentInputs, fragmentInputs + fragmentInputCount);
		}

		const std::string source = (fragmentHLSL != nullptr) ? fragmentHLSL : "";
		const std::string outputName = (fragmentOutputName != nullptr) ? fragmentOutputName : "";
		device->setCustomPostProcessShader(source, inputs, outputName, targetWidth, targetHeight);
	}
	RT64_CATCH_EXCEPTION();
}

DLLEXPORT void RT64_SetPostProcessUniforms(RT64_DEVICE *devicePtr, const RT64_SHADER_UNIFORM_BLOCK *blocks, unsigned int blockCount) {
	try {
		RT64::Device *device = (RT64::Device *)(devicePtr);
		device->setCustomPostProcessUniforms(blocks, blockCount);
	}
	RT64_CATCH_EXCEPTION();
}

DLLEXPORT void RT64_DestroyShader(RT64_SHADER *shaderPtr) {
	delete (RT64::Shader *)(shaderPtr);
}

#endif
