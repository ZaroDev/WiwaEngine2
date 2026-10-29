#include "debug_common.hlsli"

// Debug lines: every line is a quad of two triangles (6 vertices), expanded in clip space so its width is in pixels.

// Mirrors DebugLine in debug_draw.h.
struct DebugLine
{
	float3 A;
	float Width; // pixels
	float3 B;
	uint Color;
};

StructuredBuffer<DebugLine> uLines : register(t0);

VertexOutput VSMain(uint vertex_id : SV_VertexID)
{
	const DebugLine segment = uLines[Constants.FirstElement + vertex_id / 6];
	const uint corner = vertex_id % 6;

	float4 a = mul(Constants.ViewProjection, float4(segment.A, 1.0));
	float4 b = mul(Constants.ViewProjection, float4(segment.B, 1.0));

	VertexOutput output;
	output.Color = segment.Color;

	// Behind the camera: cut the line where it reaches the eye plane (everything with w <= 0 is behind it).
	const float eye = 1e-4;
	if (a.w < eye && b.w < eye)
	{
		output.Position = float4(2.0, 2.0, 2.0, 1.0); // off screen: the whole quad is clipped
		return output;
	}
	if (a.w < eye)
	{
		a = lerp(a, b, (eye - a.w) / (b.w - a.w));
	}
	else if (b.w < eye)
	{
		b = lerp(b, a, (eye - b.w) / (a.w - b.w));
	}

	float2 direction = (b.xy / b.w - a.xy / a.w) * Constants.Viewport * 0.5; // in pixels
	const float length_px = length(direction);
	direction = length_px > 1e-6 ? direction / length_px : float2(1.0, 0.0);
	const float2 offset = float2(-direction.y, direction.x) * segment.Width / Constants.Viewport; // half the width, in clip units per w

	// two triangles: (A-, A+, B-) and (B-, A+, B+)
	const bool at_b = corner == 2 || corner == 3 || corner == 5;
	const float side = (corner == 0 || corner == 2 || corner == 3) ? -1.0 : 1.0;
	float4 position = at_b ? b : a;
	position.xy += side * offset * position.w;
	output.Position = position;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	return ShadeDebug(input);
}
