//
// RT64
//

#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Textures.hlsli"
#include "UberCombiner.hlsli"

SamplerState gSamplers[18] : register(s1);

int instanceId : register(b0);

void UberRasterPS(
	in float4 vertexPosition : SV_POSITION,
	in float3 vertexNormal : NORMAL,
	in float2 vertexUV : TEXCOORD,
	in float4 input1 : COLOR0,
	in float4 input2 : COLOR1,
	in float4 input3 : COLOR2,
	in float4 input4 : COLOR3,
	in float4 input5 : COLOR4,
	in float4 input6 : COLOR5,
	in float4 input7 : COLOR6,
	in float4 input8 : COLOR7,
	out float4 resultColor : SV_TARGET)
{
	MaterialProperties material = instanceMaterials[instanceId];
	const uint ccFlags = material.ccFlags;
	const uint4 ccWords = ccWordsOf(material);
	SamplerState gTextureSampler = gSamplers[material.ccSamplerIndex];

	CombinerOperands ops = ccOperandsInit();
	ops.inputs[0] = input1;
	ops.inputs[1] = input2;
	ops.inputs[2] = input3;
	ops.inputs[3] = input4;
	ops.inputs[4] = input5;
	ops.inputs[5] = input6;
	ops.inputs[6] = input7;
	ops.inputs[7] = input8;

	if ((ccFlags & RT64_CC_FLAG_TEX0) != 0) {
		int diffuseTexIndex = material.diffuseTexIndex;
		ops.texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].Sample(gTextureSampler, vertexUV);
	}

	if ((ccFlags & RT64_CC_FLAG_TEX1) != 0) {
		int diffuse2TexIndex = material.diffuse2TexIndex;
		ops.texVal1 = (diffuse2TexIndex >= 0) ? gTextures[NonUniformResourceIndex(diffuse2TexIndex)].Sample(gTextureSampler, vertexUV) : float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	ccEvalCombiner(ops, ccWords, ccFlags);
	resultColor = ops.texel;
}
