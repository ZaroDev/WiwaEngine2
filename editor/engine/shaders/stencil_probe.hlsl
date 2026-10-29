#include "Common.hlsli"

// Used by the render checks only: writes 1 to the target where the stencil of the viewport depth is 1, else 0.

STENCIL_TEXTURE uStencil : register(t0);

// Fullscreen triangle generated from the vertex index: no vertex buffer needed.
float4 VSMain(uint vertex_id : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
	return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

float PSMain(float4 frag_coord : SV_Position) : SV_Target0
{
	return STENCIL_LOAD(uStencil, int2(frag_coord.xy)) == 1u ? 1.0 : 0.0;
}
