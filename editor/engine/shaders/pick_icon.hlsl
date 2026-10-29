#include "Common.hlsli"

// Picking icons: a camera-facing quad of a fixed size in pixels around a world position, written with the icon's id. Slots match
// PickPass: SRV 0, push constants 1.

// Mirrors PickIconGpu in pick_pass.h.
struct PickIcon
{
	float3 Position;
	float RadiusPx;
	uint Id;
	uint Pad0;
	uint Pad1;
	uint Pad2;
};

StructuredBuffer<PickIcon> uIcons : register(t0);

// Mirrors IconConstants in pick_pass.h.
struct IconData
{
	float4x4 ViewProjection;
	float2 Viewport; // pixels
	float2 Padding;
};

PUSH_CONSTANTS(IconData, Constants, b1);

struct IconOutput
{
	float4 Position : SV_Position;
	nointerpolation uint Id : COLOR0;
};

IconOutput VSMain(uint vertex_id : SV_VertexID)
{
	const PickIcon icon = uIcons[vertex_id / 6];
	const float2 corners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };

	float4 clip = mul(Constants.ViewProjection, float4(icon.Position, 1.0));
	IconOutput output;
	output.Id = icon.Id;
	if (clip.w <= 1e-4)
	{
		output.Position = float4(2.0, 2.0, 2.0, 1.0); // behind the camera: clipped
		return output;
	}
	clip.xy += corners[vertex_id % 6] * icon.RadiusPx * 2.0 / Constants.Viewport * clip.w;
	output.Position = clip;
	return output;
}

uint PSMain(IconOutput input) : SV_Target0
{
	return input.Id;
}
