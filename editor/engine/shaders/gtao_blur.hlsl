#include "Common.hlsli"

// Averages the noise of gtao.hlsl away. Every pixel of a 4x4 block searched a different set of directions and
// offsets, so a window of that size holds all of them; each tap counts less the farther its distance is from the
// centre's, so the occlusion does not bleed across the edge of an object.

#define GROUP_SIZE 8

// Mirrors AoData in ao_pass.h (only the depth range is used here).
cbuffer AoData : register(b0)
{
	float4x4 View;
	float4 Projection;
	float4 DepthInfo;     // x = near plane, y = far plane, z = width, w = height (pixels)
	float4 Settings;
};

Texture2D<float> uLinearDepth : register(t1); // level 0 of gtao_depth.hlsl's chain
Texture2D<float> uRaw : register(t2);
IMAGE_FORMAT("r8") RWTexture2D<float> uOutput : register(u3);

// The group's 8x8 pixels plus the margin the window needs (1 before, 2 after): 11x11 texels, loaded once.
#define TILE_SIZE (GROUP_SIZE + 3)
groupshared float g_Depth[TILE_SIZE * TILE_SIZE];
groupshared float g_Raw[TILE_SIZE * TILE_SIZE];

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
	int2 size = int2(DepthInfo.zw);
	int2 pixel = int2(dispatch_id.xy);
	int2 tile_origin = int2(group_id.xy) * GROUP_SIZE - 1;

	for (uint i = group_index; i < TILE_SIZE * TILE_SIZE; i += GROUP_SIZE * GROUP_SIZE)
	{
		int2 tap = clamp(tile_origin + int2(i % TILE_SIZE, i / TILE_SIZE), int2(0, 0), size - 1);
		g_Depth[i] = uLinearDepth.Load(int3(tap, 0));
		g_Raw[i] = uRaw.Load(int3(tap, 0));
	}
	GroupMemoryBarrierWithGroupSync();

	if (any(pixel >= size))
	{
		return;
	}

	float sky_z = DepthInfo.y * 0.9999;
	int2 local = pixel - tile_origin; // the pixel's place in the tile, (1..8, 1..8)
	float center_z = g_Depth[local.y * TILE_SIZE + local.x];
	if (center_z >= sky_z)
	{
		uOutput[pixel] = 1.0; // sky
		return;
	}

	// The slope of the surface under the pixel (per pixel step): of the two one-sided differences on each axis the
	// smaller one, so that an edge of the object on one side does not count as a slope.
	int center_index = local.y * TILE_SIZE + local.x;
	float dz_left = center_z - g_Depth[center_index - 1];
	float dz_right = g_Depth[center_index + 1] - center_z;
	float dz_up = center_z - g_Depth[center_index - TILE_SIZE];
	float dz_down = g_Depth[center_index + TILE_SIZE] - center_z;
	float2 slope = float2(
		(abs(dz_left) < abs(dz_right)) ? dz_left : dz_right,
		(abs(dz_up) < abs(dz_down)) ? dz_up : dz_down);

	// A 4x4 window: offsets -1 to +2, the size of the noise tile. A tap counts less the farther its distance is from
	// the plane of the pixel's surface; zero weight at a 5 % relative difference in distance.
	float depth_scale = 20.0 / center_z;
	float sum = 0.0;
	float weight_sum = 0.0;
	[unroll]
	for (int dy = -1; dy <= 2; dy++)
	{
		[unroll]
		for (int dx = -1; dx <= 2; dx++)
		{
			int index = (local.y + dy) * TILE_SIZE + local.x + dx;
			float tap_z = g_Depth[index];
			if (tap_z >= sky_z)
			{
				continue;
			}

			float weight = saturate(1.0 - abs(tap_z - (center_z + dot(slope, float2(dx, dy)))) * depth_scale);
			sum += g_Raw[index] * weight;
			weight_sum += weight;
		}
	}

	// The centre tap always counts fully, so weight_sum is at least 1.
	uOutput[pixel] = sum / weight_sum;
}
