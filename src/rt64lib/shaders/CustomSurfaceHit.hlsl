//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
ByteAddressBuffer vertexBuffer : register(t2);
ByteAddressBuffer indexBuffer : register(t3);

SamplerState gSamplers[18] : register(s1);

float4 customShade(float2 vertexUV, float3 vertexNormal, float3 vertexPosition, float4 vertexClipPosition, float3 vertexBarycentric, float4 inputs[8], float4 texVal0, float4 texVal1, float noise);

[shader("anyhit")]
void CustomSurfaceAnyHit(inout HitInfo payload, Attributes attrib) {
	uint instanceId = InstanceIndex();
	uint triangleIndex = PrimitiveIndex();
	float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);

	MaterialProperties material = instanceMaterials[instanceId];
	float4 diffuseColorMix = material.diffuseColorMix;

	const uint ccFlags = material.ccFlags;
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

	float4 shadeInputs[8];
	loadInterpolatedInputs(vertexBuffer, vl, index3, barycentrics, shadeInputs);

	const bool hasNormalMap = vl.hasUV && ((ccFlags & RT64_CC_FLAG_NORMAL_MAP) != 0);
	const bool hasSpecularMap = vl.hasUV && ((ccFlags & RT64_CC_FLAG_SPECULAR_MAP) != 0);
	const bool hasBumpMap = vl.hasUV && ((ccFlags & RT64_CC_FLAG_BUMP_MAP) != 0);
	float3 vertexTangent = float3(0.0f, 0.0f, 0.0f);
	float3 vertexBinormal = float3(0.0f, 0.0f, 0.0f);
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

	// Texture differentials.
	float2 ddx = float2(0.0f, 0.0f);
	float2 ddy = float2(0.0f, 0.0f);
	if (useTexture0 || useTexture1) {
		RayDiff propRayDiff = propagateRayDiffs(payload.rayDiff, WorldRayDirection(), RayTCurrent(), triangleNormal);
		float2 dBarydx, dBarydy;
		computeBarycentricDifferentials(propRayDiff, WorldRayDirection(), posW1 - posW0, posW2 - posW0, triangleNormal, dBarydx, dBarydy);
		computeTextureDifferentials(dBarydx, dBarydy, uv0, uv1, uv2, ddx, ddy);
	}

	float4 texVal0 = float4(1.0f, 1.0f, 1.0f, 1.0f);
	float4 texVal1 = float4(1.0f, 1.0f, 1.0f, 1.0f);
	if (useTexture0) {
		int diffuseTexIndex = material.diffuseTexIndex;
		texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleGrad(gTextureSampler, vertexUV, ddx, ddy);
		texVal0.rgb = lerp(texVal0.rgb, diffuseColorMix.rgb, max(-diffuseColorMix.a, 0.0f));
	}

	if (useTexture1) {
		int diffuse2TexIndex = material.diffuse2TexIndex;
		texVal1 = (diffuse2TexIndex >= 0) ? gTextures[NonUniformResourceIndex(diffuse2TexIndex)].SampleGrad(gTextureSampler, vertexUV, ddx, ddy) : float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	const bool optNoise = (ccFlags & RT64_CC_FLAG_NOISE) != 0;
	const bool usesNoiseInput = (ccFlags & RT64_CC_FLAG_NOISE_INPUT) != 0;
	uint seed = 0;
	if (optNoise || usesNoiseInput) {
		seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frameCount, 16);
	}

	float noise = 0.0f;
	if (usesNoiseInput) {
		noise = round(nextRand(seed));
	}

	float4 vertexClipPosition = float4(0.0f, 0.0f, 0.0f, 1.0f);
#if CUSTOM_USES_CLIP_POSITION
	float3 hitWorldPosition = mul(instanceTransforms[instanceId].objectToWorld, float4(vertexPosition, 1.0f)).xyz;
	vertexClipPosition = mul(viewProj, float4(hitWorldPosition, 1.0f));
#endif

	customDiffuseTexIndex = (uint)(max(material.diffuseTexIndex, 0));
	customDiffuse2TexIndex = (uint)(max(material.diffuse2TexIndex, 0));
	customSamplerIndex = material.ccSamplerIndex;

	float4 resultColor = customShade(vertexUV, vertexNormal, vertexPosition, vertexClipPosition, barycentrics, shadeInputs, texVal0, texVal1, noise);

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
				// Scaling the two sideways components and leaving the one along the normal alone is
				// how far the map is allowed to tilt the surface: 0 keeps the normal it was built
				// with, 1 takes the map as authored.
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

	float3 prevWorldPos = mul(instanceTransforms[instanceId].objectToWorldPrevious, float4(vertexPosition, 1.0f));
	float3 curWorldPos = mul(instanceTransforms[instanceId].objectToWorld, float4(vertexPosition, 1.0f));
	float3 vertexFlow = curWorldPos - prevWorldPos;
	float3 vertexSpecular = float3(1.0f, 1.0f, 1.0f);
	if (hasSpecularMap) {
		int specularTexIndex = material.specularTexIndex;
		if (specularTexIndex >= 0) {
			float uvDetailScale = material.uvDetailScale;
			vertexSpecular = gTextures[NonUniformResourceIndex(specularTexIndex)].SampleGrad(gTextureSampler, vertexUV * uvDetailScale, ddx * uvDetailScale, ddy * uvDetailScale).rgb;
		}
	}

	uint2 pixelIdx = DispatchRaysIndex().xy;
	uint2 pixelDims = DispatchRaysDimensions().xy;
	uint hitStride = pixelDims.x * pixelDims.y;

	float tval = WithDistanceBias(RayTCurrent(), instanceId);
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
void CustomSurfaceClosestHit(inout HitInfo payload, Attributes attrib) { }
//)raw"
#endif
