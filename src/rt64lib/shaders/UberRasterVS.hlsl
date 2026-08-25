//
// RT64
//

ByteAddressBuffer vertexBuffer : register(t2);

#include "Materials.hlsli"
#include "Instances.hlsli"
#include "UberCombiner.hlsli"
#include "UberVertexPull.hlsli"

int instanceId : register(b0);

void UberRasterVS(
	in uint vertexId : SV_VertexID,
	out float4 oPosition : SV_POSITION,
	out float3 oNormal : NORMAL,
	out float2 oUV : TEXCOORD,
	out float4 oInput1 : COLOR0,
	out float4 oInput2 : COLOR1,
	out float4 oInput3 : COLOR2,
	out float4 oInput4 : COLOR3,
	out float4 oInput5 : COLOR4,
	out float4 oInput6 : COLOR5,
	out float4 oInput7 : COLOR6,
	out float4 oInput8 : COLOR7)
{
	const uint ccFlags = instanceMaterials[instanceId].ccFlags;
	const VertexLayout vl = vertexLayoutFromCombiner(ccFlags);

	oPosition = loadVertexPosition4(vertexBuffer, vl, vertexId);
	oNormal = loadVertexNormal(vertexBuffer, vl, vertexId);
	oUV = loadVertexUV(vertexBuffer, vl, vertexId);

	float4 inputs[8];

	[unroll]
	for (uint j = 0; j < 8; j++) {
		inputs[j] = (j < vl.inputCount) ? loadVertexInput(vertexBuffer, vl, vertexId, j) : float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	oInput1 = inputs[0];
	oInput2 = inputs[1];
	oInput3 = inputs[2];
	oInput4 = inputs[3];
	oInput5 = inputs[4];
	oInput6 = inputs[5];
	oInput7 = inputs[6];
	oInput8 = inputs[7];
}
