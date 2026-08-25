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
void CustomShadowAnyHit(inout ShadowHitInfo payload, Attributes attrib) {
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

	const VertexLayout vl = vertexLayoutFromCombiner(ccFlags);
	const bool useTexture0 = (ccFlags & RT64_CC_FLAG_TEX0) != 0;
	const bool useTexture1 = (ccFlags & RT64_CC_FLAG_TEX1) != 0;
	SamplerState gTextureSampler = gSamplers[material.ccSamplerIndex];

	uint3 index3 = indexBuffer.Load3((triangleIndex * 3) * 4);

	float2 uv0 = loadVertexUV(vertexBuffer, vl, index3[0]);
	float2 uv1 = loadVertexUV(vertexBuffer, vl, index3[1]);
	float2 uv2 = loadVertexUV(vertexBuffer, vl, index3[2]);
	float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];
	float3 vertexNormal = float3(0.0f, 1.0f, 0.0f);
#if CUSTOM_USES_NORMAL
	float3 norm0 = loadVertexNormal(vertexBuffer, vl, index3[0]);
	float3 norm1 = loadVertexNormal(vertexBuffer, vl, index3[1]);
	float3 norm2 = loadVertexNormal(vertexBuffer, vl, index3[2]);
	vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];
	vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : float3(0.0f, 1.0f, 0.0f);
#endif

	float3 vertexPosition = float3(0.0f, 0.0f, 0.0f);
#if CUSTOM_USES_LOCAL_POSITION || CUSTOM_USES_CLIP_POSITION
	float3 pos0 = loadVertexPosition(vertexBuffer, vl, index3[0]);
	float3 pos1 = loadVertexPosition(vertexBuffer, vl, index3[1]);
	float3 pos2 = loadVertexPosition(vertexBuffer, vl, index3[2]);
	vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];
#endif

	float4 shadeInputs[8];
#if CUSTOM_USES_INPUTS
	loadInterpolatedInputs(vertexBuffer, vl, index3, barycentrics, shadeInputs);
#else
	for (int i = 0; i < 8; i++) {
		shadeInputs[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
#endif

	float4 texVal0 = float4(1.0f, 1.0f, 1.0f, 1.0f);
	float4 texVal1 = float4(1.0f, 1.0f, 1.0f, 1.0f);
	if (useTexture0) {
		int diffuseTexIndex = material.diffuseTexIndex;
		texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleLevel(gTextureSampler, vertexUV, 0);
	}

	if (useTexture1) {
		int diffuse2TexIndex = material.diffuse2TexIndex;
		texVal1 = (diffuse2TexIndex >= 0) ? gTextures[NonUniformResourceIndex(diffuse2TexIndex)].SampleLevel(gTextureSampler, vertexUV, 0) : float4(1.0f, 1.0f, 1.0f, 1.0f);
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

	customDiffuseTexIndex = (uint)(max(material.diffuseTexIndex, 0));
	customDiffuse2TexIndex = (uint)(max(material.diffuse2TexIndex, 0));
	customSamplerIndex = material.ccSamplerIndex;

	float4 vertexClipPosition = float4(0.0f, 0.0f, 0.0f, 1.0f);
#if CUSTOM_USES_CLIP_POSITION
	float3 hitWorldPosition = mul(instanceTransforms[instanceId].objectToWorld, float4(vertexPosition, 1.0f)).xyz;
	vertexClipPosition = mul(viewProj, float4(hitWorldPosition, 1.0f));
#endif

	float resultAlpha = customShade(vertexUV, vertexNormal, vertexPosition, vertexClipPosition, barycentrics, shadeInputs, texVal0, texVal1, noise).a;

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
void CustomShadowClosestHit(inout ShadowHitInfo payload, Attributes attrib) { }
//)raw"
#endif
