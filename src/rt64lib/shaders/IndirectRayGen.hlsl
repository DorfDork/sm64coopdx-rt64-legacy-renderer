//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Textures.hlsli"
#include "Lights.hlsli"
#include "BgSky.hlsli"
#include "NRD.hlsli"

float3 getCosHemisphereSampleBlueNoise(uint2 pixelPos, uint frameCount, float3 hitNorm) {
	float2 randVal = getBlueNoise(pixelPos, frameCount).rg;

	// Cosine weighted hemisphere sample from RNG
	float3 bitangent = getPerpendicularVector(hitNorm);
	float3 tangent = cross(bitangent, hitNorm);
	float r = sqrt(randVal.x);
	float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(max(0.0, 1.0f - randVal.x));
}

[shader("raygeneration")]
void IndirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if ((instanceId >= 0) && (giSamples > 0)) {
		uint2 launchDims = DispatchRaysDimensions().xy;
		float3 rayOrigin = gShadingPosition[launchIndex].xyz;
		float3 shadingNormal = gShadingNormal[launchIndex].xyz;
		float3 resIndirectSum = float3(0.0f, 0.0f, 0.0f);
		float hitDistSum = 0.0f;

		uint maxSamples = giSamples;
		const uint blueNoiseMult = 64 / giSamples;
		while (maxSamples > 0) {
			float3 rayDirection = getCosHemisphereSampleBlueNoise(launchIndex, frameCount + maxSamples * blueNoiseMult, shadingNormal);

			// Ray differential.
			RayDiff rayDiff;
			rayDiff.dDdx = float3(0.0f, 0.0f, 0.0f);
			rayDiff.dDdy = float3(0.0f, 0.0f, 0.0f);

			// Trace.
			RayDesc ray;
			ray.Origin = rayOrigin;
			ray.Direction = rayDirection;
			ray.TMin = RAY_MIN_DISTANCE;
			ray.TMax = RAY_MAX_DISTANCE;
			HitInfo payload;
			payload.nhits = 0;
			payload.opaqueT = RAY_MAX_DISTANCE;
			payload.rayDiff = rayDiff;
			TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);

			// Mix background and sky color together.
			float3 bgColor = SampleBackgroundAsEnvMap(rayDirection);
			float4 skyColor = SampleSkyPlane(rayDirection);
			bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

			// Process hits.
			float3 resPosition = float3(0.0f, 0.0f, 0.0f);
			float3 resNormal = float3(0.0f, 0.0f, 0.0f);
			float4 resColor = float4(0, 0, 0, 1);
			int resInstanceId = -1;
			for (uint hit = 0; hit < payload.nhits; hit++) {
				uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
				float4 hitColor = gHitColor[hitBufferIndex];
				float alphaContrib = (resColor.a * hitColor.a);
				if (alphaContrib >= EPSILON) {
					uint instanceId = gHitInstanceId[hitBufferIndex];
					float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
					float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
					resColor.rgb += hitColor.rgb * alphaContrib;
					resColor.a *= (1.0 - hitColor.a);
					resPosition = vertexPosition;
					resNormal = vertexNormal;
					resInstanceId = instanceId;
				}

				if (resColor.a <= EPSILON) {
					break;
				}
			}

			// Add diffuse bounce as indirect light.
			float3 resIndirect = ambientBaseColor.rgb;
			float sampleHitDist = RAY_MAX_DISTANCE;
			if (resInstanceId >= 0) {
				float3 directLight = ComputeLightsRandom(launchIndex, resInstanceId, resPosition, resNormal, 1, true) + instanceMaterials[resInstanceId].selfLightColor;
				float3 indirectLight = resColor.rgb * (1.0f - resColor.a) * (ambientBaseColor.rgb + ambientNoGIColor.rgb + directLight) * giDiffuseStrength;
				resIndirect += indirectLight;
				sampleHitDist = length(resPosition - rayOrigin);
			}

			resIndirect += bgColor * giSkyStrength * resColor.a;

			// NRD handles all temporal accumulation
			resIndirectSum += resIndirect / giSamples;
			hitDistSum += sampleHitDist / giSamples;

			maxSamples--;
		}

		float viewZ = gViewZ[launchIndex];
		float normHitDist = REBLUR_FrontEnd_GetNormHitDist(hitDistSum, viewZ, diffuseHitDistParams.xyz, 1.0f);
		gIndirectRadianceHitDist[launchIndex] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(resIndirectSum, normHitDist, true);
	}
	else {
		gIndirectRadianceHitDist[launchIndex] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(ambientBaseColor.rgb + ambientNoGIColor.rgb, 0.0f, true);
	}
}