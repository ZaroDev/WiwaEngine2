#ifndef WI_RENDER_CONSTANTS_H
#define WI_RENDER_CONSTANTS_H

// Numbers the C++ renderer and the HLSL shaders must agree on, written once. Plain #defines so both languages read them: the shaders
// include this file by a relative path (scene_common.hlsli, the compute shaders), the C++ side gets typed constants below.
// Keep it free of anything but #define, so that FXC, DXC and the C++ preprocessor all accept it.

// The cluster grid the lights are sorted into: screen tiles times exponential depth slices.
#define WI_CLUSTERS_X 16
#define WI_CLUSTERS_Y 9
#define WI_CLUSTERS_Z 24
#define WI_MAX_LIGHTS_PER_CLUSTER 64

// [numthreads] of light_cull.hlsl: one thread per cluster. The grid must divide by these.
#define WI_LIGHT_CULL_GROUP_X 4
#define WI_LIGHT_CULL_GROUP_Y 3
#define WI_LIGHT_CULL_GROUP_Z 4

// Shadow maps: the sun's cascades and the atlas the spot lights share (WI_SPOT_ATLAS_SIZE / WI_SPOT_TILE_SIZE tiles per row).
#define WI_MAX_CASCADES 4
#define WI_CASCADE_MAP_SIZE 2048
#define WI_MAX_SPOT_SHADOWS 4
#define WI_SPOT_TILE_SIZE 1024
#define WI_SPOT_ATLAS_SIZE 4096
#define WI_SPOT_GRID (WI_SPOT_ATLAS_SIZE / WI_SPOT_TILE_SIZE)

// GpuInstance::Skin.x of an instance that is not skinned.
#define WI_NO_PALETTE 0xFFFFFFFFu

// The stencil bit the depth prepass sets on meshes that receive no decals (the decal pass skips those pixels).
#define WI_DECAL_STENCIL_BIT 4

// [numthreads] (one side, or the whole group for the 1D ones) of the compute shaders that work on the screen or a list.
#define WI_GTAO_GROUP_SIZE 8
#define WI_GTAO_DEPTH_GROUP_SIZE 16
#define WI_BLOOM_GROUP_SIZE 8
#define WI_MOTION_BLUR_GROUP_SIZE 8
#define WI_TAA_GROUP_SIZE 8
#define WI_EXPOSURE_GROUP_SIZE 16
#define WI_EXPOSURE_BINS 256
#define WI_PARTICLE_GROUP_SIZE 64

#ifdef __cplusplus

#include <math/math.h>

namespace wiwa
{
	constexpr u32 CLUSTERS_X = WI_CLUSTERS_X;
	constexpr u32 CLUSTERS_Y = WI_CLUSTERS_Y;
	constexpr u32 CLUSTERS_Z = WI_CLUSTERS_Z;
	constexpr u32 CLUSTER_COUNT = CLUSTERS_X * CLUSTERS_Y * CLUSTERS_Z;
	constexpr u32 MAX_LIGHTS_PER_CLUSTER = WI_MAX_LIGHTS_PER_CLUSTER;

	constexpr u32 LIGHT_CULL_GROUP_X = WI_LIGHT_CULL_GROUP_X;
	constexpr u32 LIGHT_CULL_GROUP_Y = WI_LIGHT_CULL_GROUP_Y;
	constexpr u32 LIGHT_CULL_GROUP_Z = WI_LIGHT_CULL_GROUP_Z;
	static_assert(CLUSTERS_X % LIGHT_CULL_GROUP_X == 0 && CLUSTERS_Y % LIGHT_CULL_GROUP_Y == 0 && CLUSTERS_Z % LIGHT_CULL_GROUP_Z == 0,
		"the cluster grid must divide by the light culling thread group");

	constexpr u32 MAX_CASCADES = WI_MAX_CASCADES;
	constexpr u32 CASCADE_MAP_SIZE = WI_CASCADE_MAP_SIZE;
	constexpr u32 MAX_SPOT_SHADOWS = WI_MAX_SPOT_SHADOWS;
	constexpr u32 SPOT_TILE_SIZE = WI_SPOT_TILE_SIZE;
	constexpr u32 SPOT_ATLAS_SIZE = WI_SPOT_ATLAS_SIZE;
	constexpr u32 SPOT_GRID = WI_SPOT_GRID;
	static_assert(SPOT_GRID * SPOT_GRID >= MAX_SPOT_SHADOWS, "the spot atlas must hold a tile per shadowed spot light");

	constexpr u32 NO_PALETTE = WI_NO_PALETTE;
	constexpr u8 DECAL_STENCIL_BIT = WI_DECAL_STENCIL_BIT;

	constexpr u32 GTAO_GROUP_SIZE = WI_GTAO_GROUP_SIZE;
	constexpr u32 GTAO_DEPTH_GROUP_SIZE = WI_GTAO_DEPTH_GROUP_SIZE;
	constexpr u32 BLOOM_GROUP_SIZE = WI_BLOOM_GROUP_SIZE;
	constexpr u32 MOTION_BLUR_GROUP_SIZE = WI_MOTION_BLUR_GROUP_SIZE;
	constexpr u32 TAA_GROUP_SIZE = WI_TAA_GROUP_SIZE;
	constexpr u32 EXPOSURE_GROUP_SIZE = WI_EXPOSURE_GROUP_SIZE;
	constexpr u32 EXPOSURE_BINS = WI_EXPOSURE_BINS;
	static_assert(EXPOSURE_GROUP_SIZE * EXPOSURE_GROUP_SIZE == EXPOSURE_BINS, "exposure_histogram.hlsl clears one bin per thread");
	constexpr u32 PARTICLE_GROUP_SIZE = WI_PARTICLE_GROUP_SIZE;

	// How many thread groups of `group` threads cover `count` items.
	constexpr u32 ceil_div(u32 count, u32 group)
	{
		return (count + group - 1) / group;
	}
}

#endif

#endif
