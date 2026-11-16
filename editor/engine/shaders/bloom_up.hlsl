#include "Common.hlsli"

// Spreads a smaller bloom level back up and adds it to this level's own downsampled image
// (the 9-tap tent of bloom_up_taps() in bloom_math.h, copied here).

#define GROUP_SIZE WI_BLOOM_GROUP_SIZE

Texture2D uSmaller : register(t1); // the level below: read with the tent filter
Texture2D uOwn : register(t2);     // this level, as the downsample made it
IMAGE_FORMAT("rgba16f") RWTexture2D<float4> uOutput : register(u3);
SamplerState uSampler : register(s4);

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width;
	uint height;
	uOutput.GetDimensions(width, height);
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	uint smaller_width;
	uint smaller_height;
	uSmaller.GetDimensions(smaller_width, smaller_height);
	float2 texel = 1.0 / float2(smaller_width, smaller_height);

	float2 uv = (float2(dispatch_id.xy) + 0.5) / float2(width, height);

	float3 spread = uSmaller.SampleLevel(uSampler, uv, 0).rgb * (4.0 / 16.0);
	spread += (uSmaller.SampleLevel(uSampler, uv + float2(-1.0, 0.0) * texel, 0).rgb + uSmaller.SampleLevel(uSampler, uv + float2(1.0, 0.0) * texel, 0).rgb
		+ uSmaller.SampleLevel(uSampler, uv + float2(0.0, -1.0) * texel, 0).rgb + uSmaller.SampleLevel(uSampler, uv + float2(0.0, 1.0) * texel, 0).rgb) * (2.0 / 16.0);
	spread += (uSmaller.SampleLevel(uSampler, uv + float2(-1.0, -1.0) * texel, 0).rgb + uSmaller.SampleLevel(uSampler, uv + float2(1.0, -1.0) * texel, 0).rgb
		+ uSmaller.SampleLevel(uSampler, uv + float2(-1.0, 1.0) * texel, 0).rgb + uSmaller.SampleLevel(uSampler, uv + float2(1.0, 1.0) * texel, 0).rgb) * (1.0 / 16.0);

	uOutput[dispatch_id.xy] = float4(uOwn.Load(int3(dispatch_id.xy, 0)).rgb + spread, 1.0);
}
