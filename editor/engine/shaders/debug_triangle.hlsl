#include "debug_common.hlsli"

// Debug triangles: 3 vertices each, read from a structured buffer.

// Mirrors DebugTriangle in debug_draw.h.
struct DebugTriangle
{
	float3 P0;
	uint Color;
	float3 P1;
	uint Pad0;
	float3 P2;
	uint Pad1;
};

StructuredBuffer<DebugTriangle> uTriangles : register(t1);

VertexOutput VSMain(uint vertex_id : SV_VertexID)
{
	const DebugTriangle shape = uTriangles[Constants.FirstElement + vertex_id / 3];
	const uint corner = vertex_id % 3;
	const float3 position = corner == 0 ? shape.P0 : (corner == 1 ? shape.P1 : shape.P2);

	VertexOutput output;
	output.Position = mul(Constants.ViewProjection, float4(position, 1.0));
	output.Color = shape.Color;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	return ShadeDebug(input);
}
