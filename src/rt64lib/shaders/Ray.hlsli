//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else

#define RAY_MIN_DISTANCE					0.1f
#define RAY_MAX_DISTANCE					100000.0f
#define RT64_INSTANCE_MASK_DEFAULT			0x01
#define RT64_INSTANCE_MASK_SHADOW_CENTER	0xFE
#define RT64_INSTANCE_MASK_ALL				0xFF
#define RT64_SHADOW_CENTER_GROUP_COUNT		7
uint ShadowCenterGroupBit(uint shadowCenter) {
	return (shadowCenter != 0) ? (1u << (1u + (shadowCenter % RT64_SHADOW_CENTER_GROUP_COUNT))) : 0u;
}

// Structures

struct RayDiff {
    float3 dOdx;
    float3 dOdy;
    float3 dDdx;
    float3 dDdy;
};

struct HitInfo {
	uint nhits;
    RayDiff rayDiff;
};

struct ShadowHitInfo {
	float shadowHit;
	float nearestHitDistance;
    RayDiff rayDiff;
};

struct Attributes {
	float2 bary;
};

RaytracingAccelerationStructure SceneBVH : register(t0);

void computeRayDiffs(float3 nonNormalizedCameraRaydir, float3 cameraRight, float3 cameraUp, float2 viewportDims, out float3 dDdx, out float3 dDdy) {
    // Igehy Equation 8
    float dd = dot(nonNormalizedCameraRaydir, nonNormalizedCameraRaydir);
    float divd = 2.0f / (dd * sqrt(dd));
    float dr = dot(nonNormalizedCameraRaydir, cameraRight);
    float du = dot(nonNormalizedCameraRaydir, cameraUp);
    dDdx = ((dd * cameraRight) - (dr * nonNormalizedCameraRaydir)) * divd / viewportDims.x;
    dDdy = -((dd * cameraUp) - (du * nonNormalizedCameraRaydir)) * divd / viewportDims.y;
}

RayDiff propagateRayDiffs(RayDiff rayDiff, float3 D, float t, float3 N) {
    float3 dodx = rayDiff.dOdx + t * rayDiff.dDdx;    // Part of Igehy Equation 10
    float3 dody = rayDiff.dOdy + t * rayDiff.dDdy;

    float rcpDN = 1.0f / dot(D, N);              // Igehy Equation 10 and 12
    float dtdx = -dot(dodx, N) * rcpDN;
    float dtdy = -dot(dody, N) * rcpDN;
    dodx += D * dtdx;
    dody += D * dtdy;

    RayDiff propRayDiff;
    propRayDiff.dOdx = dodx;
    propRayDiff.dOdy = dody;
    propRayDiff.dDdx = rayDiff.dDdx;
    propRayDiff.dDdy = rayDiff.dDdy;
    return propRayDiff;
}

void computeBarycentricDifferentials(RayDiff rayDiff, float3 rayDir, float3 edge01W, float3 edge02W, float3 faceNormalW, out float2 dBarydx, out float2 dBarydy) {
    float3 Nu = cross(edge02W, faceNormalW);      // Igehy "Normal-Interpolated Triangles", page 182 SIGGRAPH 1999
    float3 Nv = cross(edge01W, faceNormalW);
    float3 Lu = Nu / (dot(Nu, edge01W));          // Plane equations for the triangle edges, scaled in order to make the dot with the opposive vertex = 1
    float3 Lv = Nv / (dot(Nv, edge02W));

    dBarydx.x = dot(Lu, rayDiff.dOdx);     // du / dx
    dBarydx.y = dot(Lv, rayDiff.dOdx);     // dv / dx
    dBarydy.x = dot(Lu, rayDiff.dOdy);     // du / dy
    dBarydy.y = dot(Lv, rayDiff.dOdy);     // dv / dy
}

void computeTextureDifferentials(float2 dBarydx, float2 dBarydy, float2 uv0, float2 uv1, float2 uv2, out float2 dUVdx, out float2 dUVdy) {
    float2 uv01 = uv1 - uv0;
    float2 uv02 = uv2 - uv0;
    dUVdx = dBarydx.x * uv01 + dBarydx.y * uv02;
    dUVdy = dBarydy.x * uv01 + dBarydy.y * uv02;
}

//)raw"
#endif