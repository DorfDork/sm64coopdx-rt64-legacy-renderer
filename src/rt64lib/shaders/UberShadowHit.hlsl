//
// RT64
//

ByteAddressBuffer vertexBuffer : register(t2);
ByteAddressBuffer indexBuffer : register(t3);

#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "GlobalParams.hlsli"
#include "Textures.hlsli"
#include "UberCombiner.hlsli"
#include "UberVertexPull.hlsli"

SamplerState gSamplers[18] : register(s1);

[shader("anyhit")]
void UberShadowAnyHit(inout ShadowHitInfo payload, Attributes attrib) {
	uint instanceId = InstanceIndex();
	MaterialProperties material = instanceMaterials[instanceId];
	const uint ccFlags = material.ccFlags;

	if ((ccFlags & RT64_CC_FLAG_ALPHA) == 0) {
		const float opaqueAlpha = clamp(material.shadowAlphaMultiplier, 0.0f, 1.0f);
		if (opaqueAlpha > 0.0f) {
			payload.nearestHitDistance = min(payload.nearestHitDistance, RayTCurrent());
		}

		payload.shadowHit = max(payload.shadowHit - opaqueAlpha, 0.0f);
		if (payload.shadowHit > 0.0f) {
			IgnoreHit();
		}

		return;
	}

	uint triangleIndex = PrimitiveIndex();
	float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);

	const uint4 ccWords = ccWordsOf(material);
	const VertexLayout vl = vertexLayoutFromCombiner(ccFlags);
	const bool useTexture0 = (ccFlags & RT64_CC_FLAG_TEX0) != 0;
	const bool useTexture1 = (ccFlags & RT64_CC_FLAG_TEX1) != 0;
	SamplerState gTextureSampler = gSamplers[material.ccSamplerIndex];

	uint3 index3 = indexBuffer.Load3((triangleIndex * 3) * 4);

	float2 uv0 = loadVertexUV(vertexBuffer, vl, index3[0]);
	float2 uv1 = loadVertexUV(vertexBuffer, vl, index3[1]);
	float2 uv2 = loadVertexUV(vertexBuffer, vl, index3[2]);
	float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];

	CombinerOperands ops = ccOperandsInit();
	loadInterpolatedInputs(vertexBuffer, vl, index3, barycentrics, ops.inputs);

	if (useTexture0) {
		int diffuseTexIndex = material.diffuseTexIndex;
		ops.texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleLevel(gTextureSampler, vertexUV, 0);
	}

	if (useTexture1) {
		int diffuse2TexIndex = material.diffuse2TexIndex;
		ops.texVal1 = (diffuse2TexIndex >= 0) ? gTextures[NonUniformResourceIndex(diffuse2TexIndex)].SampleLevel(gTextureSampler, vertexUV, 0) : float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	const bool optNoise = (ccFlags & RT64_CC_FLAG_NOISE) != 0;
	const bool usesNoiseInput = (ccFlags & RT64_CC_FLAG_NOISE_INPUT) != 0;
	uint seed = 0;
	if (optNoise || usesNoiseInput) {
		seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frameCount, 16);
	}

	if (usesNoiseInput) {
		ops.noise = round(nextRand(seed));
	}

	ccEvalCombiner(ops, ccWords, ccFlags);
	float resultAlpha = ops.texel.a;

	resultAlpha = clamp(resultAlpha * material.shadowAlphaMultiplier, 0.0f, 1.0f);

	if ((ccFlags & RT64_CC_FLAG_TEXTURE_EDGE) != 0) {
		if (resultAlpha > 0.3f) {
			resultAlpha = 1.0f;
		}
		else {
			IgnoreHit();
		}
	}

	if (optNoise) {
		resultAlpha *= round(nextRand(seed));
	}

	if (resultAlpha > 0.0f) {
		payload.nearestHitDistance = min(payload.nearestHitDistance, RayTCurrent());
	}

	payload.shadowHit = max(payload.shadowHit - resultAlpha, 0.0f);
	if (payload.shadowHit > 0.0f) {
		IgnoreHit();
	}
}

[shader("closesthit")]
void UberShadowClosestHit(inout ShadowHitInfo payload, Attributes attrib) { }
