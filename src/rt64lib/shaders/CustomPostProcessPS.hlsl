//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);

SamplerState gSampler : register(s0);

#define uPassTex gOutput
#define _uPassTex_sampler gSampler

float4 customPostProcess(float2 uv, float4 pos);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
	return customPostProcess(uv, pos);
}
//)raw"
#endif
