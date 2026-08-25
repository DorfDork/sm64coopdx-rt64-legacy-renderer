//
// RT64
//

#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Lights.hlsli"

static const uint VOLUMETRIC_STEPS = 24;
static const float VOLUMETRIC_PARALLEL_EPSILON = 1e-12f;

[shader("raygeneration")]
void VolumetricRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;

	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float3 rayDirection = normalize(mul(viewI, float4(target.xyz, 0)).xyz);

	int instanceId = gInstanceId[launchIndex];
	float3 shadingPosition = gShadingPosition[launchIndex].xyz;
	float maxDist = (instanceId >= 0) ? length(shadingPosition - rayOrigin) : RAY_MAX_DISTANCE;

	float3 result = float3(0.0f, 0.0f, 0.0f);
	uint lightCount, lightStride;
	SceneLights.GetDimensions(lightCount, lightStride);
	for (uint l = 0; l < lightCount; l++) {
		if ((SceneLights[l].lightType != RT64_LIGHT_TYPE_POINT) || (SceneLights[l].volumetricEnabled == 0)) {
			continue;
		}

		float3 lightPosition = SceneLights[l].position;
		float beamLength = SceneLights[l].attenuationRadius;
		float scaleX = max(SceneLights[l].scaleX, EPSILON);
		float scaleY = max(SceneLights[l].scaleY, EPSILON);
		if (beamLength <= EPSILON) {
			continue;
		}

		float3 beamForward, planeNormal, planeRight, planeUp;
		ComputePointLightAperture(l, beamForward, planeNormal, planeRight, planeUp);
		float forwardDotNormal = dot(beamForward, planeNormal);
		if (abs(forwardDotNormal) < EPSILON) {
			continue;
		}

		float3 toRayOrigin = rayOrigin - lightPosition;
		float axialOrigin = dot(toRayOrigin, planeNormal) / forwardDotNormal;
		float axialDirection = dot(rayDirection, planeNormal) / forwardDotNormal;
		float3 aperturePoint = toRayOrigin - (axialOrigin * beamForward);
		float3 apertureDirection = rayDirection - (axialDirection * beamForward);
		float2 rayOrigin2D = float2(dot(aperturePoint, planeRight) / scaleX, dot(aperturePoint, planeUp) / scaleY);
		float2 rayDirection2D = float2(dot(apertureDirection, planeRight) / scaleX, dot(apertureDirection, planeUp) / scaleY);
		float boundRadius = (SceneLights[l].lightShape == RT64_LIGHT_SHAPE_SQUARE) ? 1.41421356f : 1.0f;

		float tNear = 0.0f;
		float tFar = maxDist;
		float a = dot(rayDirection2D, rayDirection2D);
		float b = 2.0f * dot(rayOrigin2D, rayDirection2D);
		float c = dot(rayOrigin2D, rayOrigin2D) - (boundRadius * boundRadius);
		if (a > VOLUMETRIC_PARALLEL_EPSILON) {
			float disc = (b * b) - (4.0f * a * c);
			if (disc <= 0.0f) {
				// Misses the beam entirely, which is most of the screen most of the time.
				continue;
			}

			float discRoot = sqrt(disc);
			tNear = max(tNear, (-b - discRoot) / (2.0f * a));
			tFar = min(tFar, (-b + discRoot) / (2.0f * a));
		}
		else if (c > 0.0f) {
			continue;
		}

		if (abs(axialDirection) > EPSILON) {
			float tAxisStart = -axialOrigin / axialDirection;
			float tAxisEnd = (beamLength - axialOrigin) / axialDirection;
			tNear = max(tNear, min(tAxisStart, tAxisEnd));
			tFar = min(tFar, max(tAxisStart, tAxisEnd));
		}
		else if ((axialOrigin < 0.0f) || (axialOrigin > beamLength)) {
			continue;
		}

		if (tFar <= tNear) {
			continue;
		}

		if (tNear > RAY_MIN_DISTANCE) {
			if (TraceShadow(rayOrigin, rayDirection, RAY_MIN_DISTANCE, tNear, RT64_INSTANCE_MASK_ALL) <= EPSILON) {
				continue;
			}
		}

		float jitter = getBlueNoise(launchIndex, frameCount + l).r;
		float stepSize = (tFar - tNear) / VOLUMETRIC_STEPS;
		float3 lightContribution = float3(0.0f, 0.0f, 0.0f);
		for (uint s = 0; s < VOLUMETRIC_STEPS; s++) {
			float travelDist = tNear + ((s + jitter) * stepSize);

			float axialDist = axialOrigin + (travelDist * axialDirection);
			float taper = saturate(1.0f - (axialDist / beamLength));
			if (taper <= EPSILON) {
				continue;
			}

			float2 crossSection = rayOrigin2D + (travelDist * rayDirection2D);
			float radial = (SceneLights[l].lightShape == RT64_LIGHT_SHAPE_SQUARE) ? max(abs(crossSection.x), abs(crossSection.y)) : length(crossSection);
			float profile = saturate(1.0f - radial);
			profile *= profile;
			if (profile <= EPSILON) {
				continue;
			}

			float3 samplePosition = rayOrigin + (rayDirection * travelDist);
			float lightDistance = length(samplePosition - lightPosition);
			float3 lightDirection = (lightPosition - samplePosition) / max(lightDistance, EPSILON);
			float visibility = TraceShadow(samplePosition, lightDirection, RAY_MIN_DISTANCE, lightDistance, RT64_INSTANCE_MASK_ALL);
			if (visibility <= EPSILON) {
				continue;
			}

			lightContribution += SceneLights[l].diffuseColor * (taper * profile * visibility);
		}

		float beamRadius = max((scaleX + scaleY) * 0.5f, EPSILON);
		result += lightContribution * (stepSize * SceneLights[l].volumetricIntensity / beamRadius);
	}

	float3 newVolumetric = result;

	float3 normal = gNormal[launchIndex].xyz;
	float2 flow = gFlow[launchIndex].xy;
	int2 prevIndex = int2(launchIndex + float2(0.5f, 0.5f) + flow);
	float depth = gDepth[launchIndex];
	float prevDepth = gPrevDepth[prevIndex];
	float3 prevNormal = gPrevNormal[prevIndex].xyz;
	const float WeightNormalExponent = 128.0f;
	float weightDepth = abs(depth - prevDepth) / 0.01f;
	float weightNormal = pow(max(0.0f, dot(prevNormal, normal)), WeightNormalExponent);
	float historyWeight = exp(-weightDepth) * weightNormal;
	float4 prevAccum = gPrevVolumetricLight[prevIndex];
	float historyLength = min(prevAccum.a * historyWeight + 1.0f, 64.0f);
	float3 accumVolumetric = lerp(prevAccum.rgb, newVolumetric, 1.0f / historyLength);

	gVolumetricLight[launchIndex] = float4(accumVolumetric, historyLength);
}
