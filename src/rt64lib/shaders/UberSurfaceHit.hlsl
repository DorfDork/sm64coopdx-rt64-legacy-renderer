//
// RT64
//

ByteAddressBuffer vertexBuffer : register(t2);
ByteAddressBuffer indexBuffer : register(t3);

#include "Materials.hlsli"
#include "Instances.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "GlobalParams.hlsli"
#include "Textures.hlsli"
#include "UberCombiner.hlsli"
#include "UberVertexPull.hlsli"

SamplerState gSamplers[18] : register(s1);

[shader("anyhit")]
void UberSurfaceAnyHit(inout HitInfo payload, Attributes attrib) {
	uint instanceId = InstanceIndex();
	uint triangleIndex = PrimitiveIndex();
	float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);
	uint2 pixelIdx = DispatchRaysIndex().xy;
	uint2 pixelDims = DispatchRaysDimensions().xy;
	uint hitStride = pixelDims.x * pixelDims.y;
	float tval = WithDistanceBias(RayTCurrent(), instanceId);

	// This hit should get get evicted anyway because it's the furthest hit in the buffer.
	if ((payload.nhits >= MAX_HIT_QUERIES) && (tval >= gHitDistAndFlow[getHitBufferIndex(MAX_HIT_QUERIES - 1, pixelIdx, pixelDims)].x)) {
		IgnoreHit();
		return;
	}

	// This hit is beyond a confirmed-opaque surface and is never visible.
	if (RayTCurrent() > (payload.opaqueT + maxDepthBias)) {
		return;
	}

	MaterialProperties material = instanceMaterials[instanceId];
	float4 diffuseColorMix = material.diffuseColorMix;

	const uint ccFlags = material.ccFlags;
	const uint4 ccWords = ccWordsOf(material);
	const VertexLayout vl = vertexLayoutFromCombiner(ccFlags);
	const bool useTexture0 = (ccFlags & RT64_CC_FLAG_TEX0) != 0;
	const bool useTexture1 = (ccFlags & RT64_CC_FLAG_TEX1) != 0;
	SamplerState gTextureSampler = gSamplers[material.ccSamplerIndex];

	// Vertex data.
	uint3 index3 = indexBuffer.Load3((triangleIndex * 3) * 4);

	float3 pos0 = loadVertexPosition(vertexBuffer, vl, index3[0]);
	float3 pos1 = loadVertexPosition(vertexBuffer, vl, index3[1]);
	float3 pos2 = loadVertexPosition(vertexBuffer, vl, index3[2]);
	float3 posW0 = mul(instanceTransforms[instanceId].objectToWorld, float4(pos0, 1.0f)).xyz;
	float3 posW1 = mul(instanceTransforms[instanceId].objectToWorld, float4(pos1, 1.0f)).xyz;
	float3 posW2 = mul(instanceTransforms[instanceId].objectToWorld, float4(pos2, 1.0f)).xyz;
	float3 vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];

	float3 norm0 = loadVertexNormal(vertexBuffer, vl, index3[0]);
	float3 norm1 = loadVertexNormal(vertexBuffer, vl, index3[1]);
	float3 norm2 = loadVertexNormal(vertexBuffer, vl, index3[2]);
	float3 vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];
	float3 triangleNormal = -cross(pos2 - pos0, pos1 - pos0);
	vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : triangleNormal;
	triangleNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(triangleNormal, 0.f)).xyz);

	float2 uv0 = loadVertexUV(vertexBuffer, vl, index3[0]);
	float2 uv1 = loadVertexUV(vertexBuffer, vl, index3[1]);
	float2 uv2 = loadVertexUV(vertexBuffer, vl, index3[2]);
	float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];

	if (instanceMaterials[instanceId].textureGenEnabled != 0) {
		vertexUV = ComputeGeneratedUV(instanceMaterials[instanceId].textureGenU, instanceMaterials[instanceId].textureGenV, vertexNormal);
	}

	CombinerOperands ops = ccOperandsInit();
	loadInterpolatedInputs(vertexBuffer, vl, index3, barycentrics, ops.inputs);

	const bool hasNormalMap = vl.hasUV && ((ccFlags & RT64_CC_FLAG_NORMAL_MAP) != 0);
	const bool hasSpecularMap = vl.hasUV && ((ccFlags & RT64_CC_FLAG_SPECULAR_MAP) != 0);
	const bool hasBumpMap = vl.hasUV && ((ccFlags & RT64_CC_FLAG_BUMP_MAP) != 0);
	float3 vertexTangent = float3(0.0f, 0.0f, 0.0f);
	float3 vertexBinormal = float3(0.0f, 0.0f, 0.0f);

	// Texture differentials.
	float2 ddx = float2(0.0f, 0.0f);
	float2 ddy = float2(0.0f, 0.0f);
	if (useTexture0 || useTexture1) {
		float3 propDOdx, propDOdy;
		propagateRayDiffs(payload.rayDiff, WorldRayDirection(), RayTCurrent(), triangleNormal, propDOdx, propDOdy);
		float2 dBarydx, dBarydy;
		computeBarycentricDifferentials(propDOdx, propDOdy, posW1 - posW0, posW2 - posW0, triangleNormal, dBarydx, dBarydy);
		computeTextureDifferentials(dBarydx, dBarydy, uv0, uv1, uv2, ddx, ddy);
	}

	if (useTexture0) {
		int diffuseTexIndex = material.diffuseTexIndex;
		ops.texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleGrad(gTextureSampler, vertexUV, ddx, ddy);
		ops.texVal0.rgb = lerp(ops.texVal0.rgb, diffuseColorMix.rgb, max(-diffuseColorMix.a, 0.0f));
	}

	if (useTexture1) {
		int diffuse2TexIndex = material.diffuse2TexIndex;
		ops.texVal1 = (diffuse2TexIndex >= 0) ? gTextures[NonUniformResourceIndex(diffuse2TexIndex)].SampleGrad(gTextureSampler, vertexUV, ddx, ddy) : float4(1.0f, 1.0f, 1.0f, 1.0f);
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
	float4 resultColor = ops.texel;

	// Only mix the final diffuse color if the alpha is positive.
	resultColor.rgb = lerp(resultColor.rgb, diffuseColorMix.rgb, max(diffuseColorMix.a, 0.0f));

	// Apply the solid alpha multiplier.
	resultColor.a = clamp(material.solidAlphaMultiplier * resultColor.a, 0.0f, 1.0f);

	if ((ccFlags & RT64_CC_FLAG_TEXTURE_EDGE) != 0) {
		if (resultColor.a > 0.3f) {
			resultColor.a = 1.0f;
		}
		else {
			IgnoreHit();
		}
	}

	if (optNoise) {
		resultColor.a *= round(nextRand(seed));
	}

	if ((resultColor.a >= 1.0f) && (material.zmodeXlu == 0)) {
		payload.opaqueT = min(payload.opaqueT, RayTCurrent());
	}

	if (hasNormalMap || hasBumpMap) {
		float uva = uv1.x - uv0.x;
		float uvb = uv2.x - uv0.x;
		float uvc = uv1.y - uv0.y;
		float uvd = uv2.y - uv0.y;
		float uvk = uvb * uvc - uva * uvd;
		float3 dpos1 = pos1 - pos0;
		float3 dpos2 = pos2 - pos0;
		if (uvk != 0) vertexTangent = normalize((uvc * dpos2 - uvd * dpos1) / uvk);
		else {
			if (uva != 0) vertexTangent = normalize(dpos1 / uva);
			else if (uvb != 0) vertexTangent = normalize(dpos2 / uvb);
			else vertexTangent = 0.0f;
		}

		float2 duv1 = uv1 - uv0;
		float2 duv2 = uv2 - uv1;
		duv1.y = -duv1.y;
		duv2.y = -duv2.y;
		float3 cr = cross(float3(duv1.xy, 0.0f), float3(duv2.xy, 0.0f));
		float binormalMult = (cr.z < 0.0f) ? -1.0f : 1.0f;
		vertexBinormal = cross(vertexTangent, vertexNormal) * binormalMult;
	}

	vertexNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexNormal, 0.f)).xyz);
	float normalSign = (dot(triangleNormal, WorldRayDirection()) <= 0.0f) ? 1.0f : -1.0f;
	vertexNormal *= normalSign;

	if (hasNormalMap || hasBumpMap) {
		vertexTangent = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexTangent, 0.f)).xyz) * normalSign;
		vertexBinormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexBinormal, 0.f)).xyz) * normalSign;
	
		if (hasNormalMap) {
			int normalTexIndex = material.normalTexIndex;
			if (normalTexIndex >= 0) {
				float uvDetailScale = material.uvDetailScale;
				float3 normalColor = gTextures[NonUniformResourceIndex(normalTexIndex)].SampleGrad(gTextureSampler, vertexUV * uvDetailScale, ddx * uvDetailScale, ddy * uvDetailScale).xyz;
				normalColor = (normalColor * 2.0f) - 1.0f;
				normalColor.xy *= material.normalStrength;
				float3 newNormal = normalize(vertexNormal * normalColor.z + vertexTangent * normalColor.x + vertexBinormal * normalColor.y);
				vertexNormal = newNormal;
			}
		}

		int bumpTexIndex = hasBumpMap ? material.bumpTexIndex : -1;
		if (bumpTexIndex >= 0) {
			float bumpDetailScale = material.uvDetailScale;
			float2 bumpUV = vertexUV * bumpDetailScale;
			float2 bumpDdx = ddx * bumpDetailScale;
			float2 bumpDdy = ddy * bumpDetailScale;
			uint bumpWidth, bumpHeight;
			gTextures[NonUniformResourceIndex(bumpTexIndex)].GetDimensions(bumpWidth, bumpHeight);
			float2 texelStep = 1.0f / float2(max(bumpWidth, 1u), max(bumpHeight, 1u));
			float heightCenter = gTextures[NonUniformResourceIndex(bumpTexIndex)].SampleGrad(gTextureSampler, bumpUV, bumpDdx, bumpDdy).r;
			float heightAlongU = gTextures[NonUniformResourceIndex(bumpTexIndex)].SampleGrad(gTextureSampler, bumpUV + float2(texelStep.x, 0.0f), bumpDdx, bumpDdy).r;
			float heightAlongV = gTextures[NonUniformResourceIndex(bumpTexIndex)].SampleGrad(gTextureSampler, bumpUV + float2(0.0f, texelStep.y), bumpDdx, bumpDdy).r;
			float2 slope = float2(heightAlongU - heightCenter, heightAlongV - heightCenter) * material.bumpStrength;
			vertexNormal = normalize(vertexNormal - (vertexTangent * slope.x) - (vertexBinormal * slope.y));
		}
	}

	float3 vertexSpecular = float3(1.0f, 1.0f, 1.0f);
	if (hasSpecularMap) {
		int specularTexIndex = material.specularTexIndex;
		if (specularTexIndex >= 0) {
			float uvDetailScale = material.uvDetailScale;
			vertexSpecular = gTextures[NonUniformResourceIndex(specularTexIndex)].SampleGrad(gTextureSampler, vertexUV * uvDetailScale, ddx * uvDetailScale, ddy * uvDetailScale).rgb;
		}
	}

	uint hi = getHitBufferIndex(min(payload.nhits, MAX_HIT_QUERIES), pixelIdx, pixelDims);
	uint minHi = getHitBufferIndex(0, pixelIdx, pixelDims);
	uint lo = hi - hitStride;
	while ((hi > minHi) && (tval < gHitDistAndFlow[lo].x)) {
		gHitDistAndFlow[hi] = gHitDistAndFlow[lo];
		gHitColor[hi] = gHitColor[lo];
		gHitNormal[hi] = gHitNormal[lo];
		gHitSpecular[hi] = gHitSpecular[lo];
		gHitInstanceId[hi] = gHitInstanceId[lo];
		hi -= hitStride;
		lo -= hitStride;
	}

	uint hitPos = hi / hitStride;
	if (hitPos < MAX_HIT_QUERIES) {
		float3 prevWorldPos = mul(instanceTransforms[instanceId].objectToWorldPrevious, float4(vertexPosition, 1.0f)).xyz;
		float3 curWorldPos = mul(instanceTransforms[instanceId].objectToWorld, float4(vertexPosition, 1.0f)).xyz;
		float3 vertexFlow = curWorldPos - prevWorldPos;
		gHitDistAndFlow[hi] = float4(tval, vertexFlow);
		gHitColor[hi] = resultColor;
		gHitNormal[hi] = float4(vertexNormal, 1.0f);
		gHitSpecular[hi] = float4(vertexSpecular, 1.0f);
		gHitInstanceId[hi] = instanceId;
		++payload.nhits;
		if (hitPos != MAX_HIT_QUERIES - 1) {
			IgnoreHit();
		}
	}
	else {
		IgnoreHit();
	}
}

[shader("closesthit")]
void UberSurfaceClosestHit(inout HitInfo payload, Attributes attrib) { }
