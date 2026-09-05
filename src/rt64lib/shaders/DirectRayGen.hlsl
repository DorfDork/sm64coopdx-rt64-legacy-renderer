//
// RT64
//

#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Lights.hlsli"
#include "NRD.hlsli"

[shader("raygeneration")]
void DirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if (instanceId < 0) {
		gDirectRadianceHitDist[launchIndex] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(float3(1.0f, 1.0f, 1.0f), 0.0f, true);
		return;
	}

	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	float4 position = gShadingPosition[launchIndex];
	float3 normal = gShadingNormal[launchIndex].xyz;
	float4 specular = gShadingSpecular[launchIndex];

	float3 lightsSpecularColor;
	float lightsHitDist;
	float3 resDirect = ComputeLightsRandom(launchIndex, instanceId, position.xyz, normal.xyz, maxLights, true, lightsSpecularColor, lightsHitDist);
	resDirect += instanceMaterials[instanceId].selfLightColor;

	const float EyeLightSpecularLift = 0.5f;
	float3 directionToEye = -rayDirection;
	float3 directionToSpecularLight = normalize(directionToEye + float3(0.0f, EyeLightSpecularLift, 0.0f));
	float eyeLightLambertFactor = max(dot(normal.xyz, directionToEye), 0.0f);
	float3 specularColor = eyeLightSpecularColor.rgb + lightsSpecularColor;
	float3 resSpecular = specularColor * ComputeSpecularTerm(instanceId, normal.xyz, directionToSpecularLight, directionToEye, specular.rgb);
	resDirect += (eyeLightDiffuseColor.rgb * eyeLightLambertFactor + resSpecular);
	if (instanceMaterials[instanceId].specularTint == 0) {
		resDirect -= resSpecular;
		gTransparent[launchIndex] += float4(resSpecular, 0.0f);
	}

	float viewZ = gViewZ[launchIndex];
	float normHitDist = REBLUR_FrontEnd_GetNormHitDist(lightsHitDist, viewZ, diffuseHitDistParams.xyz, 1.0f);
	gDirectRadianceHitDist[launchIndex] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(resDirect, normHitDist, true);
}