//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
struct MaterialProperties {
	int diffuseTexIndex;
	int normalTexIndex;
	int specularTexIndex;
	int bumpTexIndex;
	float bumpStrength;
	float normalStrength;
	float ignoreNormalFactor;
	float uvDetailScale;
	float reflectionFactor;
	float reflectionFresnelFactor;
	float reflectionShineFactor;
	float refractionFactor;
	float3 specularColor;
	float specularShinyness;
	float solidAlphaMultiplier;
	float shadowAlphaMultiplier;
	float depthBias;
	float shadowRayBias;
	float3 selfLightColor;
	uint lightGroupMaskBits;
	float3 fogColor;
	float4 diffuseColorMix;
	float fogMul;
	float fogOffset;
	uint fogEnabled;
	float lockMask;
	int diffuse2TexIndex;
	int enabledAttributes;
	uint ccRgb1;
	uint ccAlpha1;
	uint ccRgb2;
	uint ccAlpha2;
	uint ccFlags;
	uint ccSamplerIndex;
	uint specularTint;
	uint shadingModel;
	float diffuseIntensity;
	float specularFactor;
	float specularEccentricity;
	uint textureGenEnabled;
	float4 textureGenU;
	float4 textureGenV;
	float3 reflectionColor;
	float specularIntensity;
	float selfLightIntensity;
	uint shadowEnabled;
	uint shadowCenter;
	uint depthWrite;
	uint depthTest;
	uint zmodeXlu;
};

float2 ComputeGeneratedUV(float4 textureGenU, float4 textureGenV, float3 modelSpaceNormal) {
	return float2(dot(modelSpaceNormal, textureGenU.xyz) + textureGenU.w,
		dot(modelSpaceNormal, textureGenV.xyz) + textureGenV.w);
}

#define RT64_SHADING_MODEL_LAMBERT     0
#define RT64_SHADING_MODEL_PHONG       1
#define RT64_SHADING_MODEL_BLINN       2

#define RT64_CC_FLAG_2CYCLE            (1u << 0)
#define RT64_CC_FLAG_ALPHA             (1u << 1)
#define RT64_CC_FLAG_TEXTURE_EDGE      (1u << 2)
#define RT64_CC_FLAG_NOISE             (1u << 3)
#define RT64_CC_FLAG_NOISE_INPUT       (1u << 4)
#define RT64_CC_FLAG_TEX0              (1u << 5)
#define RT64_CC_FLAG_TEX1              (1u << 6)
#define RT64_CC_FLAG_NORMAL_MAP        (1u << 7)
#define RT64_CC_FLAG_SPECULAR_MAP      (1u << 12)
#define RT64_CC_FLAG_BUMP_MAP          (1u << 13)
#define RT64_CC_FLAG_INPUT_COUNT_SHIFT 8
#define RT64_CC_FLAG_INPUT_COUNT_MASK  (0xFu << 8)

bool isTranslucentDepthWriter(MaterialProperties material) {
	return (material.depthWrite != 0);
}
//)raw"
#endif