#ifndef DEBUG_COMMON_HLSLI
#define DEBUG_COMMON_HLSLI

#include "Common.hlsli"

// Shared by debug_line.hlsl and debug_triangle.hlsl (the debug draw pass). Slots match DebugPass: SRV 0 lines, SRV 1 triangles,
// push constants 2. Colors are RGBA8 in the low-to-high bytes, written as they are: the target already holds sRGB-encoded values.

// Mirrors PushConstants in debug_pass.h.
struct DebugData
{
	float4x4 ViewProjection;
	float2 Viewport;      // pixels
	uint FirstElement;    // the first line / triangle of this draw in its buffer
	uint Padding;
};

PUSH_CONSTANTS(DebugData, Constants, b2);

struct VertexOutput
{
	float4 Position : SV_Position;
	nointerpolation uint Color : COLOR0;
};

float4 UnpackColor(uint color)
{
	return float4((color & 0xFFu), ((color >> 8) & 0xFFu), ((color >> 16) & 0xFFu), ((color >> 24) & 0xFFu)) / 255.0;
}

float4 ShadeDebug(VertexOutput input)
{
	return UnpackColor(input.Color);
}

#endif
