#include "Common.hlsli"

// The selection outline: draws a ring of pixels around what the stencil marks. The stencil holds bit 0 where the selected entity's
// silhouette is and bit 1 where that part of it is not hidden by other geometry (SelectionOutlinePass writes both). A pixel outside the
// silhouette within Width pixels of it becomes the outline color, dimmed to OccludedAlpha when every silhouette pixel it sees is hidden.
// Slots match SelectionOutlinePass: SRV 0 (the stencil plane of the viewport depth), push constants 1.

STENCIL_TEXTURE uStencil : register(t0);

// Mirrors PushConstants in selection_outline_pass.h.
struct OutlineData
{
	float3 Color;
	float Width;          // pixels
	float OccludedAlpha;
	float3 Padding;
};

PUSH_CONSTANTS(OutlineData, Constants, b1);

// Fullscreen triangle generated from the vertex index: no vertex buffer needed.
float4 VSMain(uint vertex_id : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
	return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

float4 PSMain(float4 frag_coord : SV_Position) : SV_Target0
{
	uint width_px, height_px;
	uStencil.GetDimensions(width_px, height_px);
	const int2 size = int2(width_px, height_px);
	const int2 pixel = int2(frag_coord.xy);

	if ((STENCIL_LOAD(uStencil, pixel) & 1u) != 0u)
	{
		discard; // inside the silhouette
	}

	const int reach = (int)ceil(Constants.Width);
	const float limit = Constants.Width * Constants.Width + 0.001;
	bool found = false;
	bool visible = false;
	for (int dy = -reach; dy <= reach; dy++)
	{
		for (int dx = -reach; dx <= reach; dx++)
		{
			const int2 other = pixel + int2(dx, dy);
			if (float(dx * dx + dy * dy) > limit || any(other < 0) || any(other >= size))
			{
				continue;
			}
			const uint stencil = STENCIL_LOAD(uStencil, other);
			if ((stencil & 1u) != 0u)
			{
				found = true;
				visible = visible || (stencil & 2u) != 0u;
			}
		}
	}

	if (!found)
	{
		discard;
	}
	return float4(Constants.Color, visible ? 1.0 : Constants.OccludedAlpha);
}
