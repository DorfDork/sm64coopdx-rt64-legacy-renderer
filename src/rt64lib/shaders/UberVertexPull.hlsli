//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
#ifndef UBER_VERTEX_PULL_HLSLI_INCLUDED
#define UBER_VERTEX_PULL_HLSLI_INCLUDED

struct VertexLayout {
	uint vertexSize;
	uint positionOffset;
	uint normalOffset;
	uint uvOffset;
	uint inputOffset;
	uint inputStride;
	uint inputCount;
	bool hasUV;
	bool inputsHaveAlpha;
};

VertexLayout vertexLayoutFromCombiner(uint ccFlags) {
	VertexLayout vl;
	vl.inputCount = ccInputCount(ccFlags);
	vl.inputsHaveAlpha = (ccFlags & RT64_CC_FLAG_ALPHA) != 0;
	vl.hasUV = (ccFlags & (RT64_CC_FLAG_TEX0 | RT64_CC_FLAG_TEX1)) != 0;

	vl.positionOffset = 0;
	vl.normalOffset = 16;
	vl.uvOffset = 28;
	vl.inputOffset = vl.hasUV ? 36 : 28;
	vl.inputStride = vl.inputsHaveAlpha ? 16 : 12;
	vl.vertexSize = vl.inputOffset + vl.inputCount * vl.inputStride;
	return vl;
}

// One combiner input from one vertex.
float4 loadVertexInput(ByteAddressBuffer vertexBuffer, VertexLayout vl, uint vertexIndex, uint inputIndex) {
	const uint address = vertexIndex * vl.vertexSize + vl.inputOffset + inputIndex * vl.inputStride;
	if (vl.inputsHaveAlpha) {
		return asfloat(vertexBuffer.Load4(address));
	}

	return float4(asfloat(vertexBuffer.Load3(address)), 1.0f);
}

void loadInterpolatedInputs(ByteAddressBuffer vertexBuffer, VertexLayout vl, uint3 index3, float3 barycentrics, out float4 inputs[8]) {
	[unroll]
	for (uint j = 0; j < 8; j++) {
		float4 value = float4(0.0f, 0.0f, 0.0f, 1.0f);
		if (j < vl.inputCount) {
			value = loadVertexInput(vertexBuffer, vl, index3[0], j) * barycentrics[0]
			      + loadVertexInput(vertexBuffer, vl, index3[1], j) * barycentrics[1]
			      + loadVertexInput(vertexBuffer, vl, index3[2], j) * barycentrics[2];
		}

		inputs[j] = value;
	}
}

float3 loadVertexPosition(ByteAddressBuffer vertexBuffer, VertexLayout vl, uint vertexIndex) {
	return asfloat(vertexBuffer.Load3(vertexIndex * vl.vertexSize + vl.positionOffset));
}

float4 loadVertexPosition4(ByteAddressBuffer vertexBuffer, VertexLayout vl, uint vertexIndex) {
	return asfloat(vertexBuffer.Load4(vertexIndex * vl.vertexSize + vl.positionOffset));
}

float3 loadVertexNormal(ByteAddressBuffer vertexBuffer, VertexLayout vl, uint vertexIndex) {
	return asfloat(vertexBuffer.Load3(vertexIndex * vl.vertexSize + vl.normalOffset));
}

float2 loadVertexUV(ByteAddressBuffer vertexBuffer, VertexLayout vl, uint vertexIndex) {
	if (!vl.hasUV) {
		return float2(0.0f, 0.0f);
	}

	return asfloat(vertexBuffer.Load2(vertexIndex * vl.vertexSize + vl.uvOffset));
}

#endif
//)raw"
#endif
