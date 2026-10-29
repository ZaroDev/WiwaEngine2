#include "Common.hlsli"

// FXAA on the tone-mapped image, a simplified version of FXAA 3.11 (Timothy Lottes):
//   1. a pixel whose neighbours (up, down, left, right) are about as bright as it is is not on an edge: unchanged;
//   2. otherwise find which way the edge runs, and walk along it in both directions until the contrast changes
//      (the ends of the edge);
//   3. the closer the pixel is to one end, the further it is moved across the edge, so the staircase turns into a ramp;
//   4. a lone bright or dark pixel (sub-pixel aliasing) is blended with its surroundings as well.
// Brightness is the green channel: cheap, and good enough after tone mapping.

#define EDGE_THRESHOLD 0.125     // contrast (relative to the local maximum) that makes an edge
#define EDGE_THRESHOLD_MIN 0.0312 // ... but nothing darker than this counts
#define SUBPIXEL_QUALITY 0.75
#define SEARCH_STEPS 8

Texture2D uColor : register(t0);
SamplerState uSampler : register(s1);

// Mirrors PushConstants in fxaa_pass.h.
struct FxaaData
{
	float2 RcpFrame; // 1 / size in pixels
	float2 Padding;
};

PUSH_CONSTANTS(FxaaData, Constants, b2);

// Fullscreen triangle generated from the vertex index: no vertex buffer needed.
float4 VSMain(uint vertex_id : SV_VertexID) : SV_Position
{
	float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
	return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

float3 SampleColor(float2 uv)
{
	return uColor.SampleLevel(uSampler, uv, 0).rgb;
}

float SampleLuma(float2 uv)
{
	return SampleColor(uv).g;
}

float4 PSMain(float4 frag_coord : SV_Position) : SV_Target0
{
	float2 rcp_frame = Constants.RcpFrame;
	float2 uv = frag_coord.xy * rcp_frame; // frag_coord is at the pixel centre

	float3 color_centre = SampleColor(uv);
	float luma_centre = color_centre.g;
	float luma_down = SampleLuma(uv + float2(0.0, 1.0) * rcp_frame);
	float luma_up = SampleLuma(uv + float2(0.0, -1.0) * rcp_frame);
	float luma_left = SampleLuma(uv + float2(-1.0, 0.0) * rcp_frame);
	float luma_right = SampleLuma(uv + float2(1.0, 0.0) * rcp_frame);

	float luma_min = min(luma_centre, min(min(luma_down, luma_up), min(luma_left, luma_right)));
	float luma_max = max(luma_centre, max(max(luma_down, luma_up), max(luma_left, luma_right)));
	float luma_range = luma_max - luma_min;

	// Not on an edge.
	if (luma_range < max(EDGE_THRESHOLD_MIN, luma_max * EDGE_THRESHOLD))
	{
		return float4(color_centre, 1.0);
	}

	float luma_down_left = SampleLuma(uv + float2(-1.0, 1.0) * rcp_frame);
	float luma_up_right = SampleLuma(uv + float2(1.0, -1.0) * rcp_frame);
	float luma_up_left = SampleLuma(uv + float2(-1.0, -1.0) * rcp_frame);
	float luma_down_right = SampleLuma(uv + float2(1.0, 1.0) * rcp_frame);

	float luma_down_up = luma_down + luma_up;
	float luma_left_right = luma_left + luma_right;
	float luma_left_corners = luma_down_left + luma_up_left;
	float luma_down_corners = luma_down_left + luma_down_right;
	float luma_right_corners = luma_down_right + luma_up_right;
	float luma_up_corners = luma_up_right + luma_up_left;

	// Which way does the edge run: the stronger second derivative says (horizontal edge = brightness changes vertically).
	float edge_horizontal = abs(-2.0 * luma_left + luma_left_corners) + abs(-2.0 * luma_centre + luma_down_up) * 2.0 + abs(-2.0 * luma_right + luma_right_corners);
	float edge_vertical = abs(-2.0 * luma_up + luma_up_corners) + abs(-2.0 * luma_centre + luma_left_right) * 2.0 + abs(-2.0 * luma_down + luma_down_corners);
	bool is_horizontal = edge_horizontal >= edge_vertical;

	// The two neighbours across the edge, on the negative side (up / left, -1) and the positive side (down / right, +1):
	// the one with the steeper contrast is where the edge really is. The step below is negated when luma_1 wins, so
	// it must point towards the winner: uv grows downwards and to the right.
	float luma_1 = is_horizontal ? luma_up : luma_left;
	float luma_2 = is_horizontal ? luma_down : luma_right;
	float gradient_1 = luma_1 - luma_centre;
	float gradient_2 = luma_2 - luma_centre;
	bool is_1_steepest = abs(gradient_1) >= abs(gradient_2);
	float gradient_scaled = 0.25 * max(abs(gradient_1), abs(gradient_2));

	float step_length = is_horizontal ? rcp_frame.y : rcp_frame.x;
	float luma_local_average;
	if (is_1_steepest)
	{
		step_length = -step_length;
		luma_local_average = 0.5 * (luma_1 + luma_centre);
	}
	else
	{
		luma_local_average = 0.5 * (luma_2 + luma_centre);
	}

	// Start on the edge itself: half a pixel across, then walk both ways along it.
	float2 current_uv = uv;
	if (is_horizontal)
	{
		current_uv.y += step_length * 0.5;
	}
	else
	{
		current_uv.x += step_length * 0.5;
	}

	float2 offset = is_horizontal ? float2(rcp_frame.x, 0.0) : float2(0.0, rcp_frame.y);
	float2 uv_1 = current_uv - offset;
	float2 uv_2 = current_uv + offset;

	float luma_end_1 = SampleLuma(uv_1) - luma_local_average;
	float luma_end_2 = SampleLuma(uv_2) - luma_local_average;
	bool reached_1 = abs(luma_end_1) >= gradient_scaled;
	bool reached_2 = abs(luma_end_2) >= gradient_scaled;
	bool reached_both = reached_1 && reached_2;

	if (!reached_1)
	{
		uv_1 -= offset;
	}
	if (!reached_2)
	{
		uv_2 += offset;
	}

	if (!reached_both)
	{
		for (int i = 2; i < SEARCH_STEPS; i++)
		{
			if (!reached_1)
			{
				luma_end_1 = SampleLuma(uv_1) - luma_local_average;
			}
			if (!reached_2)
			{
				luma_end_2 = SampleLuma(uv_2) - luma_local_average;
			}
			reached_1 = abs(luma_end_1) >= gradient_scaled;
			reached_2 = abs(luma_end_2) >= gradient_scaled;
			reached_both = reached_1 && reached_2;

			if (!reached_1)
			{
				uv_1 -= offset;
			}
			if (!reached_2)
			{
				uv_2 += offset;
			}
			if (reached_both)
			{
				break;
			}
		}
	}

	// How far the pixel is from each end of the edge, and so how far across the edge it should be moved.
	float distance_1 = is_horizontal ? (uv.x - uv_1.x) : (uv.y - uv_1.y);
	float distance_2 = is_horizontal ? (uv_2.x - uv.x) : (uv_2.y - uv.y);
	bool is_direction_1 = distance_1 < distance_2;
	float distance_final = min(distance_1, distance_2);
	float edge_thickness = distance_1 + distance_2;
	float pixel_offset = -distance_final / edge_thickness + 0.5;

	// Only when the end of the edge really goes the way that makes this pixel look more like its neighbour.
	bool is_luma_centre_smaller = luma_centre < luma_local_average;
	bool correct_variation = ((is_direction_1 ? luma_end_1 : luma_end_2) < 0.0) != is_luma_centre_smaller;
	float final_offset = correct_variation ? pixel_offset : 0.0;

	// Sub-pixel aliasing: how much the pixel differs from the average of its 3x3 neighbourhood.
	float luma_average = (1.0 / 12.0) * (2.0 * (luma_down_up + luma_left_right) + luma_left_corners + luma_right_corners);
	float sub_pixel_offset_1 = saturate(abs(luma_average - luma_centre) / luma_range);
	float sub_pixel_offset_2 = (-2.0 * sub_pixel_offset_1 + 3.0) * sub_pixel_offset_1 * sub_pixel_offset_1;
	float sub_pixel_offset_final = sub_pixel_offset_2 * sub_pixel_offset_2 * SUBPIXEL_QUALITY;
	final_offset = max(final_offset, sub_pixel_offset_final);

	float2 final_uv = uv;
	if (is_horizontal)
	{
		final_uv.y += final_offset * step_length;
	}
	else
	{
		final_uv.x += final_offset * step_length;
	}

	return float4(SampleColor(final_uv), 1.0);
}
