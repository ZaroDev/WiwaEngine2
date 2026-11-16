#include "Common.hlsli"

// The distance along the view direction of every pixel, as a mip chain, for gtao.hlsl and gtao_blur.hlsl. They read
// it instead of the depth buffer: no divide per sample, and the far samples of the horizon search read the coarser
// levels, which are cheaper on the cache and smooth out the noise of a wide search.
// One dispatch builds all levels: a 16x16 group converts its pixels (level 0) and reduces them through groupshared
// memory to levels 1 to 4 (8x8, 4x4, 2x2 and 1 texel per group).

#define GROUP_SIZE WI_GTAO_DEPTH_GROUP_SIZE

// Mirrors AoData in ao_pass.h.
cbuffer AoData : register(b0)
{
	float4x4 View;
	float4 Projection;    // w = last mip level of the distances
	float4 DepthInfo;     // x = near plane, y = far plane, z = width, w = height (pixels)
	float4 Settings;
};

Texture2D<float> uDepth : register(t1);
IMAGE_FORMAT("r32f") RWTexture2D<float> uLevel0 : register(u3);
IMAGE_FORMAT("r32f") RWTexture2D<float> uLevel1 : register(u4);
IMAGE_FORMAT("r32f") RWTexture2D<float> uLevel2 : register(u5);
IMAGE_FORMAT("r32f") RWTexture2D<float> uLevel3 : register(u6);
IMAGE_FORMAT("r32f") RWTexture2D<float> uLevel4 : register(u7);

groupshared float g_Level0[16][16];
groupshared float g_Level1[8][8];
groupshared float g_Level2[4][4];
groupshared float g_Level3[2][2];

// 2x2 texels, averaged but weighted towards the farthest one: a texel much nearer than the rest belongs to a
// different surface, and averaging it in would put a surface in mid-air. The sky (the far plane) is left out
// unless it is all there is.
float Reduce(float a, float b, float c, float d)
{
	float far_plane = DepthInfo.y;
	float sky_z = far_plane * 0.9999;

	float z[4] = { a, b, c, d };
	float farthest = 0.0;
	[unroll]
	for (int i = 0; i < 4; i++)
	{
		if (z[i] < sky_z)
		{
			farthest = max(farthest, z[i]);
		}
	}
	if (farthest == 0.0)
	{
		return far_plane;
	}

	float sum = 0.0;
	float weight_sum = 0.0;
	[unroll]
	for (int j = 0; j < 4; j++)
	{
		if (z[j] < sky_z)
		{
			float weight = saturate(1.0 - (farthest - z[j]) * 10.0 / farthest);
			sum += z[j] * weight;
			weight_sum += weight;
		}
	}
	return sum / weight_sum; // the farthest texel weighs 1
}

bool IsInside(uint2 texel, int level)
{
	int2 level_size = max(int2(DepthInfo.zw) >> level, 1);
	return all(int2(texel) < level_size);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint3 local : SV_GroupThreadID, uint3 group : SV_GroupID)
{
	float far_plane = DepthInfo.y;
	int last_level = int(Projection.w);

	// Level 0. Pixels outside the image count as sky.
	float z = far_plane;
	if (IsInside(id.xy, 0))
	{
		float depth = uDepth.Load(int3(id.xy, 0));
		float n = DepthInfo.x;
		z = (depth >= 1.0) ? far_plane : n * far_plane / (far_plane - depth * (far_plane - n));
		uLevel0[id.xy] = z;
	}
	g_Level0[local.y][local.x] = z;
	GroupMemoryBarrierWithGroupSync();

	if (local.x < 8 && local.y < 8)
	{
		uint2 s = local.xy * 2;
		float value = Reduce(g_Level0[s.y][s.x], g_Level0[s.y][s.x + 1], g_Level0[s.y + 1][s.x], g_Level0[s.y + 1][s.x + 1]);
		g_Level1[local.y][local.x] = value;
		uint2 texel = group.xy * 8 + local.xy;
		if (last_level >= 1 && IsInside(texel, 1))
		{
			uLevel1[texel] = value;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (local.x < 4 && local.y < 4)
	{
		uint2 s = local.xy * 2;
		float value = Reduce(g_Level1[s.y][s.x], g_Level1[s.y][s.x + 1], g_Level1[s.y + 1][s.x], g_Level1[s.y + 1][s.x + 1]);
		g_Level2[local.y][local.x] = value;
		uint2 texel = group.xy * 4 + local.xy;
		if (last_level >= 2 && IsInside(texel, 2))
		{
			uLevel2[texel] = value;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (local.x < 2 && local.y < 2)
	{
		uint2 s = local.xy * 2;
		float value = Reduce(g_Level2[s.y][s.x], g_Level2[s.y][s.x + 1], g_Level2[s.y + 1][s.x], g_Level2[s.y + 1][s.x + 1]);
		g_Level3[local.y][local.x] = value;
		uint2 texel = group.xy * 2 + local.xy;
		if (last_level >= 3 && IsInside(texel, 3))
		{
			uLevel3[texel] = value;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (local.x == 0 && local.y == 0)
	{
		float value = Reduce(g_Level3[0][0], g_Level3[0][1], g_Level3[1][0], g_Level3[1][1]);
		if (last_level >= 4 && IsInside(group.xy, 4))
		{
			uLevel4[group.xy] = value;
		}
	}
}
