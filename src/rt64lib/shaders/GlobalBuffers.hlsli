//
// RT64
//

RWTexture2D<float4> gViewDirection : register(u0);
RWTexture2D<float4> gShadingPosition : register(u1);
RWTexture2D<float4> gShadingNormal : register(u2);
RWTexture2D<float4> gShadingSpecular : register(u3);
RWTexture2D<float4> gDiffuse : register(u4);
RWTexture2D<int> gInstanceId : register(u5);
RWTexture2D<float4> gDirectRadianceHitDist : register(u6);
RWTexture2D<float4> gIndirectRadianceHitDist : register(u7);
RWTexture2D<float4> gReflection : register(u8);
RWTexture2D<float4> gRefraction : register(u9);
RWTexture2D<float4> gTransparent : register(u10);
RWTexture2D<float4> gFlow : register(u11);
RWTexture2D<float> gReactiveMask : register(u12);
RWTexture2D<float> gLockMask : register(u13);
RWTexture2D<float4> gNormal : register(u14);
RWTexture2D<float> gDepth : register(u15);
RWTexture2D<float4> gPrevNormal : register(u16);
RWTexture2D<float> gPrevDepth : register(u17);
RWTexture2D<float4> gNormalRoughness : register(u18);
RWTexture2D<float> gViewZ : register(u19);
RWTexture2D<float4> gDenoisedDirect : register(u20);
RWTexture2D<float4> gDenoisedIndirect : register(u21);
RWTexture2D<float4> gVolumetricLight : register(u28);
RWTexture2D<float4> gPrevVolumetricLight : register(u29);
RWTexture2D<float> gHistoryConfidence : register(u30);

Texture2D<float4> gBackground : register(t1);