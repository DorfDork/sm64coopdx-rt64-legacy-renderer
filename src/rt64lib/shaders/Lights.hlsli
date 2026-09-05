//
// RT64
//

#include "BlueNoise.hlsli"

// Structures

struct LightInfo {
	float3 position;
	float3 diffuseColor;
	float attenuationRadius;
	float pointRadius;
	float3 specularColor;
	float shadowOffset;
	float attenuationExponent;
	float flickerIntensity;
	uint groupBits;
	uint lightType;
	float pitch, yaw, roll;
	float scaleX, scaleY;
	uint lightShape;
	uint apertureEnabled;
	float aperturePitch, apertureYaw;
	uint volumetricEnabled;
	float volumetricIntensity;
	float intensity;
};

// Root signature

StructuredBuffer<LightInfo> SceneLights : register(t4);

#define MAX_LIGHTS 16
#define RT64_LIGHT_TYPE_POINT 1
#define RT64_LIGHT_SHAPE_SQUARE 1

void ComputePointLightBasis(float pitch, float yaw, float roll, out float3 lightForward, out float3 lightRight, out float3 lightUp) {
	float sinYaw = sin(yaw), cosYaw = cos(yaw);
	float3 forwardYawed = float3(sinYaw, 0.0f, cosYaw);
	float3 rightYawed = float3(cosYaw, 0.0f, -sinYaw);
	const float3 worldUp = float3(0.0f, 1.0f, 0.0f);

	float sinPitch = sin(pitch), cosPitch = cos(pitch);
	float3 forwardPitched = (cosPitch * forwardYawed) + (sinPitch * worldUp);
	float3 upPitched = (-sinPitch * forwardYawed) + (cosPitch * worldUp);

	float sinRoll = sin(roll), cosRoll = cos(roll);
	lightForward = forwardPitched;
	lightRight = (cosRoll * rightYawed) + (sinRoll * upPitched);
	lightUp = (-sinRoll * rightYawed) + (cosRoll * upPitched);
}

void ComputePointLightAperture(uint lightIndex, out float3 beamForward, out float3 planeNormal, out float3 planeRight, out float3 planeUp) {
	float3 beamRight, beamUp;
	ComputePointLightBasis(SceneLights[lightIndex].pitch, SceneLights[lightIndex].yaw, SceneLights[lightIndex].roll, beamForward, beamRight, beamUp);
	if (SceneLights[lightIndex].apertureEnabled != 0) {
		ComputePointLightBasis(SceneLights[lightIndex].aperturePitch, SceneLights[lightIndex].apertureYaw, SceneLights[lightIndex].roll, planeNormal, planeRight, planeUp);
	}
	else {
		planeNormal = beamForward;
		planeRight = beamRight;
		planeUp = beamUp;
	}
}

float ComputePointLightMask(uint lightIndex, float3 lightPosition, float3 samplePosition) {
	float3 beamForward, planeNormal, planeRight, planeUp;
	ComputePointLightAperture(lightIndex, beamForward, planeNormal, planeRight, planeUp);
	float forwardDotNormal = dot(beamForward, planeNormal);
	if (abs(forwardDotNormal) < EPSILON) {
		return 0.0f;
	}

	float3 toSample = samplePosition - lightPosition;
	float travelDist = dot(toSample, planeNormal) / forwardDotNormal;
	if (travelDist <= 0.0f) {
		return 0.0f;
	}

	// Follow the beam back to where this point passed through the opening, and measure the shape there.
	float3 aperturePoint = toSample - (travelDist * beamForward);
	float scaleX = max(SceneLights[lightIndex].scaleX, EPSILON);
	float scaleY = max(SceneLights[lightIndex].scaleY, EPSILON);
	float2 local = float2(dot(aperturePoint, planeRight), dot(aperturePoint, planeUp)) / float2(scaleX, scaleY);

	const float edgeSoftness = 0.05f;
	if (SceneLights[lightIndex].lightShape == RT64_LIGHT_SHAPE_SQUARE) {
		float2 edgeDist = 1.0f - abs(local);
		return saturate(min(edgeDist.x, edgeDist.y) / edgeSoftness);
	}
	else {
		float edgeDist = 1.0f - length(local);
		return saturate(edgeDist / edgeSoftness);
	}
}

float TraceShadow(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint instanceMask, out float nearestHitDistance) {
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	ShadowHitInfo shadowPayload;
	shadowPayload.shadowHit = 1.0f;
	shadowPayload.nearestHitDistance = rayMaxDist;

	uint flags = RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
	TraceRay(SceneBVH, flags, instanceMask, 1, 0, 1, ray, shadowPayload);
	nearestHitDistance = shadowPayload.nearestHitDistance;
	return shadowPayload.shadowHit;
}

// For everything that only wants to know whether the ray got through.
float TraceShadow(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint instanceMask) {
	float unusedHitDistance;
	return TraceShadow(rayOrigin, rayDirection, rayMinDist, rayMaxDist, instanceMask, unusedHitDistance);
}

float TraceCenteredShadow(uint instanceId, float3 position, float3 normal, uint instanceMask) {
	if (normal.y <= 0.0f) {
		return 1.0f;
	}

	const float3 rayDirection = float3(0.0f, 1.0f, 0.0f);
	const float shadowRayBias = instanceMaterials[instanceId].shadowRayBias;
	const float rayMinDistance = RAY_MIN_DISTANCE + shadowRayBias;
	const float shadowNormalBiasScale = 3.0f;
	const float grazing = sqrt(saturate(1.0f - normal.y * normal.y));
	const float3 rayOrigin = position + normal * (shadowRayBias * shadowNormalBiasScale * grazing);

	// What is overhead, out of the surfaces asking for their shadow dropped straight down.
	float casterDistance;
	float shadowFactor = TraceShadow(rayOrigin, rayDirection, rayMinDistance, RAY_MAX_DISTANCE, instanceMask, casterDistance);
	if (shadowFactor >= 1.0f) {
		return 1.0f;
	}

	// And whether anything stands between here and it.
	float unblockedFactor = TraceShadow(rayOrigin, rayDirection, rayMinDistance, casterDistance, RT64_INSTANCE_MASK_DEFAULT);
	return lerp(1.0f, shadowFactor, unblockedFactor);
}

// How much of a light a surface takes up before anything is done about visibility
float ComputeLambertTerm(uint instanceId, float3 normal, float3 directionToLight) {
	float ignoreNormalFactor = instanceMaterials[instanceId].ignoreNormalFactor;
	float NdotL = max(dot(normal, directionToLight), 0.0f);
	return lerp(NdotL, 1.0f, ignoreNormalFactor) * instanceMaterials[instanceId].diffuseIntensity;
}

float3 ComputeSpecularTerm(uint instanceId, float3 normal, float3 directionToLight, float3 directionToEye, float3 specular) {
	uint shadingModel = instanceMaterials[instanceId].shadingModel;
	if (shadingModel == RT64_SHADING_MODEL_LAMBERT) {
		return float3(0.0f, 0.0f, 0.0f);
	}

	if (dot(normal, directionToLight) <= 0.0f) {
		return float3(0.0f, 0.0f, 0.0f);
	}

	float specularFactor;
	if (shadingModel == RT64_SHADING_MODEL_BLINN) {
		const float MinimumEccentricity = 0.02f;
		const float MinimumExponent = 1.0f;
		float specularEccentricity = clamp(instanceMaterials[instanceId].specularEccentricity, MinimumEccentricity, 1.0f);
		float blinnExponent = max((2.0f / (specularEccentricity * specularEccentricity)) - 2.0f, MinimumExponent);
		float3 halfwayVector = normalize(directionToLight + directionToEye);
		specularFactor = pow(max(dot(normal, halfwayVector), 0.0f), blinnExponent);
	}
	else {
		float specularShinyness = instanceMaterials[instanceId].specularShinyness;
		float3 reflectedLight = reflect(-directionToLight, normal);
		specularFactor = pow(max(dot(reflectedLight, directionToEye), 0.0f), specularShinyness);
	}

	return specular * specularFactor * instanceMaterials[instanceId].specularFactor;
}

float CalculateLightIntensitySimple(uint l, float3 position, float3 normal, float ignoreNormalFactor) {
	float3 lightPosition = SceneLights[l].position;
	float lightRadius = SceneLights[l].attenuationRadius;
	float lightAttenuation = SceneLights[l].attenuationExponent;
	float lightDistance = length(position - lightPosition);
	float3 lightDirection = normalize(lightPosition - position);
	float NdotL = dot(normal, lightDirection);
	const float surfaceBiasDotOffset = 0.707106f;
	float surfaceBias = max(lerp(NdotL, 1.0f, ignoreNormalFactor) + surfaceBiasDotOffset, 0.0f);
	float sampleIntensityFactor = pow(max(1.0f - (lightDistance / lightRadius), 0.0f), lightAttenuation) * surfaceBias;
	if (SceneLights[l].lightType == RT64_LIGHT_TYPE_POINT) {
		sampleIntensityFactor *= ComputePointLightMask(l, lightPosition, position);
	}

	return sampleIntensityFactor * dot(SceneLights[l].diffuseColor, float3(1.0f, 1.0f, 1.0f));
}
float3 ComputeLight(uint2 launchIndex, uint lightIndex, uint instanceId, float3 position, float3 normal, const bool checkShadows, uint shadowInstanceMask, out float3 outSpecularColor, out float outHitDist) {
	float shadowRayBias = instanceMaterials[instanceId].shadowRayBias;
	float3 lightPosition = SceneLights[lightIndex].position;
	float3 lightDirection = normalize(lightPosition - position);
	float lightRadius = SceneLights[lightIndex].attenuationRadius;
	float lightAttenuation = SceneLights[lightIndex].attenuationExponent;
	const float ShadowOutlineRadiusFraction = 0.1f;
	float lightPointRadius = (diSamples > 0) ? (SceneLights[lightIndex].pointRadius * ShadowOutlineRadiusFraction) : 0.0f;
	float3 perpX = cross(-lightDirection, float3(0.f, 1.0f, 0.f));
	if (all(perpX == 0.0f)) {
		perpX.x = 1.0;
	}

	float3 perpY = cross(perpX, -lightDirection);
	float shadowOffset = SceneLights[lightIndex].shadowOffset;

	float lightMask = (SceneLights[lightIndex].lightType == RT64_LIGHT_TYPE_POINT) ? ComputePointLightMask(lightIndex, lightPosition, position) : 1.0f;

	const uint maxSamples = max(diSamples, 1);
	uint samples = maxSamples;
	float lLambertFactor = 0.0f;
	float lShadowFactor = 0.0f;
	float lSpecularReach = 0.0f;
	float lHitDist = 0.0f;
	while ((samples > 0) && (lightMask > 0.0f)) {
		float2 sampleCoordinate = getBlueNoise(launchIndex, frameCount + samples).rg * 2.0f - 1.0f;
		sampleCoordinate = normalize(sampleCoordinate) * saturate(length(sampleCoordinate));

		float3 samplePosition = lightPosition + perpX * sampleCoordinate.x * lightPointRadius + perpY * sampleCoordinate.y * lightPointRadius;
		float sampleDistance = length(position - samplePosition);
		float3 sampleDirection = normalize(samplePosition - position);
		float sampleIntensityFactor = pow(max(1.0f - (sampleDistance / lightRadius), 0.0f), lightAttenuation);
		float NdotL = max(dot(normal, sampleDirection), 0.0f);
		float sampleLambertFactor = ComputeLambertTerm(instanceId, normal, sampleDirection) * sampleIntensityFactor;
		float sampleShadowFactor = 1.0f;
		float sampleHitDist = sampleDistance;
		if (checkShadows) {
			const float shadowNormalBiasScale = 3.0f;
			const float grazing = sqrt(saturate(1.0f - NdotL * NdotL));
			float3 shadowOrigin = position + normal * (shadowRayBias * shadowNormalBiasScale * grazing);
			sampleShadowFactor = TraceShadow(shadowOrigin, sampleDirection, RAY_MIN_DISTANCE + shadowRayBias, (sampleDistance - shadowOffset), shadowInstanceMask, sampleHitDist);
		}

		lLambertFactor += sampleLambertFactor / maxSamples;
		lShadowFactor += sampleShadowFactor / maxSamples;
		lSpecularReach += (NdotL * sampleIntensityFactor) / maxSamples;
		lHitDist += sampleHitDist / maxSamples;

		samples--;
	}

	outSpecularColor = SceneLights[lightIndex].specularColor * lSpecularReach * lShadowFactor * lightMask;
	outHitDist = lHitDist;
	return SceneLights[lightIndex].diffuseColor * lLambertFactor * lShadowFactor * lightMask;
}

float3 ComputeLightsRandom(uint2 launchIndex, uint instanceId, float3 position, float3 normal, uint maxLightCount, const bool checkShadows, out float3 outSpecularColor, out float outHitDist) {
	float3 resultLight = float3(0.0f, 0.0f, 0.0f);
	outSpecularColor = float3(0.0f, 0.0f, 0.0f);
	outHitDist = 0.0f;
	uint lightGroupMaskBits = instanceMaterials[instanceId].lightGroupMaskBits;
	float ignoreNormalFactor = instanceMaterials[instanceId].ignoreNormalFactor;
	bool traceShadows = checkShadows && (instanceMaterials[instanceId].shadowEnabled != 0);
	uint shadowGroupBit = ShadowCenterGroupBit(instanceMaterials[instanceId].shadowCenter);
	uint shadowInstanceMask = RT64_INSTANCE_MASK_DEFAULT | shadowGroupBit;
	uint centeredInstanceMask = RT64_INSTANCE_MASK_SHADOW_CENTER & ~shadowGroupBit;
	float centeredShadowFactor = traceShadows ? TraceCenteredShadow(instanceId, position, normal, centeredInstanceMask) : 1.0f;
	if (lightGroupMaskBits > 0) {
		uint sLightCount = 0;
		uint gLightCount, gLightStride;
		uint sLightIndices[MAX_LIGHTS + 1];
		float sLightIntensities[MAX_LIGHTS + 1];
		float totalLightIntensity = 0.0f;
		SceneLights.GetDimensions(gLightCount, gLightStride);
		for (uint l = 0; (l < gLightCount) && (sLightCount < MAX_LIGHTS); l++) {
			if (lightGroupMaskBits & SceneLights[l].groupBits) {
				float lightIntensity = CalculateLightIntensitySimple(l, position, normal, ignoreNormalFactor);
				if (lightIntensity > EPSILON) {
					sLightIntensities[sLightCount] = lightIntensity;
					sLightIndices[sLightCount] = l;
					totalLightIntensity += lightIntensity;
					sLightCount++;
				}
			}
		}

		float randomRange = totalLightIntensity;
		uint lLightCount = min(sLightCount, maxLightCount);

		// TODO: Probability is disabled when more than one light is sampled because it's
		// not trivial to calculate the probability of the dependent events without replacement.
		// In any case, it is likely more won't be needed when a temporally stable denoiser is
		// implemented.
		bool useProbability = lLightCount == 1;
		for (uint s = 0; s < lLightCount; s++) {
			float r = getBlueNoise(launchIndex, frameCount + s).r * randomRange;
			uint chosen = 0;
			float rLightIntensity = sLightIntensities[chosen];
			while ((chosen < (sLightCount - 1)) && (r >= rLightIntensity)) {
				chosen++;
				rLightIntensity += sLightIntensities[chosen];
			}

			// Store and clear the light intensity from the array.
			float cLightIntensity = sLightIntensities[chosen];
			uint cLightIndex = sLightIndices[chosen];
			float invProbability = useProbability ? (randomRange / cLightIntensity) : 1.0f;
			sLightIntensities[chosen] = 0.0f;
			randomRange -= cLightIntensity;

			// Compute and add the light.
			float3 lightSpecularColor;
			float lightHitDist;
			resultLight += ComputeLight(launchIndex, cLightIndex, instanceId, position, normal, traceShadows, shadowInstanceMask, lightSpecularColor, lightHitDist) * invProbability;
			outSpecularColor += lightSpecularColor * invProbability;
			outHitDist += lightHitDist / lLightCount;
		}
	}

	outSpecularColor *= centeredShadowFactor;
	return resultLight * centeredShadowFactor;
}

float3 ComputeLightsRandom(uint2 launchIndex, uint instanceId, float3 position, float3 normal, uint maxLightCount, const bool checkShadows) {
	float3 unusedSpecularColor;
	float unusedHitDist;
	return ComputeLightsRandom(launchIndex, instanceId, position, normal, maxLightCount, checkShadows, unusedSpecularColor, unusedHitDist);
}