#include "Common.hlsli"

// Automatic exposure, step 1: counts the scene's luminance into a 256-bin histogram on a log2 scale (bin 0 = black,
// ignored; bins 1..255 cover [MinLog2, MinLog2 + RangeLog2]). Each group counts its own 16x16 pixels in groupshared
// memory first, then adds the result to the global histogram: 256 global atomics per group instead of 256 per pixel.
// Mirrors luminance_to_bin in ExposureMath.h.

#define GROUP_SIZE 16
#define BINS 256

// Slots match ExposurePass: SRV 0, UAV 1, push constants 2.
Texture2D uHdr : register(t0);
RWStructuredBuffer<uint> uHistogram : register(u1);

struct HistogramData
{
	float MinLog2;
	float RangeLog2;
	float2 Padding;
};

PUSH_CONSTANTS(HistogramData, Constants, b2);

groupshared uint gs_bins[BINS];

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
	gs_bins[group_index] = 0; // GROUP_SIZE * GROUP_SIZE == BINS
	GroupMemoryBarrierWithGroupSync();

	uint width;
	uint height;
	uHdr.GetDimensions(width, height);

	if (id.x < width && id.y < height)
	{
		float3 color = uHdr.Load(int3(id.xy, 0)).rgb;
		float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));

		uint bin = 0;
		if (luminance > 1e-4 && luminance < 1e10) // not black, not NaN or infinite
		{
			float position = saturate((log2(luminance) - Constants.MinLog2) / Constants.RangeLog2);
			bin = 1 + uint(position * 254.0 + 0.5);
		}
		InterlockedAdd(gs_bins[bin], 1);
	}
	GroupMemoryBarrierWithGroupSync();

	if (gs_bins[group_index] > 0)
	{
		InterlockedAdd(uHistogram[group_index], gs_bins[group_index]);
	}
}
