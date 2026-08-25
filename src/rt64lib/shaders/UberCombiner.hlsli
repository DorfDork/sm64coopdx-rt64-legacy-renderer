//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
#ifndef UBER_COMBINER_HLSLI_INCLUDED
#define UBER_COMBINER_HLSLI_INCLUDED

#define CC_SHADER_0         0
#define CC_SHADER_INPUT_1   1
#define CC_SHADER_INPUT_2   2
#define CC_SHADER_INPUT_3   3
#define CC_SHADER_INPUT_4   4
#define CC_SHADER_INPUT_5   5
#define CC_SHADER_INPUT_6   6
#define CC_SHADER_INPUT_7   7
#define CC_SHADER_INPUT_8   8
#define CC_SHADER_TEXEL0    9
#define CC_SHADER_TEXEL0A   10
#define CC_SHADER_TEXEL1    11
#define CC_SHADER_TEXEL1A   12
#define CC_SHADER_1         13
#define CC_SHADER_COMBINED  14
#define CC_SHADER_COMBINEDA 15
#define CC_SHADER_NOISE     16

struct CombinerOperands {
	float4 texVal0;
	float4 texVal1;
	float4 texel;
	float4 inputs[8];
	float noise;
};

CombinerOperands ccOperandsInit() {
	CombinerOperands ops;
	ops.texVal0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	ops.texVal1 = float4(1.0f, 1.0f, 1.0f, 1.0f);
	ops.texel = float4(0.0f, 0.0f, 0.0f, 0.0f);

	[unroll]
	for (int i = 0; i < 8; i++) {
		ops.inputs[i] = float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	ops.noise = 0.0f;
	return ops;
}

// The 16 operands live four to a word, in the order the CPU packed them (rgb1, alpha1, rgb2, alpha2).
uint ccCmd(uint4 ccWords, uint i) {
	return (ccWords[i >> 2] >> ((i & 3) * 8)) & 0xFFu;
}

uint4 ccWordsOf(MaterialProperties material) {
	return uint4(material.ccRgb1, material.ccAlpha1, material.ccRgb2, material.ccAlpha2);
}

uint ccInputCount(uint ccFlags) {
	return (ccFlags & RT64_CC_FLAG_INPUT_COUNT_MASK) >> RT64_CC_FLAG_INPUT_COUNT_SHIFT;
}

float4 ccInput(CombinerOperands ops, uint cmd) {
	switch (cmd) {
	case CC_SHADER_INPUT_1: return ops.inputs[0];
	case CC_SHADER_INPUT_2: return ops.inputs[1];
	case CC_SHADER_INPUT_3: return ops.inputs[2];
	case CC_SHADER_INPUT_4: return ops.inputs[3];
	case CC_SHADER_INPUT_5: return ops.inputs[4];
	case CC_SHADER_INPUT_6: return ops.inputs[5];
	case CC_SHADER_INPUT_7: return ops.inputs[6];
	case CC_SHADER_INPUT_8: return ops.inputs[7];
	case CC_SHADER_1:
		return float4(1.0f, 1.0f, 1.0f, 1.0f);
	case CC_SHADER_TEXEL0:
		return ops.texVal0;
	case CC_SHADER_TEXEL0A:
		return ops.texVal0.aaaa;
	case CC_SHADER_TEXEL1:
		return ops.texVal1;
	case CC_SHADER_TEXEL1A:
		return ops.texVal1.aaaa;
	case CC_SHADER_COMBINED:
		return ops.texel;
	case CC_SHADER_COMBINEDA:
		return ops.texel.aaaa;
	case CC_SHADER_NOISE:
		return float4(ops.noise, ops.noise, ops.noise, ops.noise);
	}

	// CC_SHADER_0, and anything unrecognized
	return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

void ccEvalCycle(inout CombinerOperands ops, uint4 ccWords, uint cycle, bool optAlpha) {
	const uint b = cycle * 8;
	const float4 rgbPart =
		(ccInput(ops, ccCmd(ccWords, b + 0)) - ccInput(ops, ccCmd(ccWords, b + 1))) *
		 ccInput(ops, ccCmd(ccWords, b + 2)) + ccInput(ops, ccCmd(ccWords, b + 3));
	const float4 alphaPart =
		(ccInput(ops, ccCmd(ccWords, b + 4)) - ccInput(ops, ccCmd(ccWords, b + 5))) *
		 ccInput(ops, ccCmd(ccWords, b + 6)) + ccInput(ops, ccCmd(ccWords, b + 7));

	ops.texel = float4(rgbPart.rgb, optAlpha ? alphaPart.a : 1.0f);
}

void ccEvalCombiner(inout CombinerOperands ops, uint4 ccWords, uint ccFlags) {
	const bool optAlpha = (ccFlags & RT64_CC_FLAG_ALPHA) != 0;

	ccEvalCycle(ops, ccWords, 0, optAlpha);

	if ((ccFlags & RT64_CC_FLAG_2CYCLE) != 0) {
		ccEvalCycle(ops, ccWords, 1, optAlpha);
	}
}

#endif
//)raw"
#endif
