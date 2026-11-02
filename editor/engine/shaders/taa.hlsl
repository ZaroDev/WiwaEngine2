#include "Common.hlsli"

// Temporal anti-aliasing resolve (taa_math.h has the same formulas on the CPU: keep them in step). For every pixel:
//   1. where was this surface in the previous frame? The velocity of the nearest-depth pixel of its 3x3 neighbourhood (so the edge of
//      a moving object follows the object); pixels with no geometry (the sky) are moved by the camera alone;
//   2. was the same surface there? The depth of the previous frame at that place has to match the depth now, else something was
//      uncovered and the history is not used;
//   3. the colors of the 3x3 neighbourhood now give a box (mean +- 1.25 standard deviations, in YCoCg); the history (read with a
//      5-tap Catmull-Rom filter, so it stays sharp) is pulled into that box: it cannot show anything that is no longer there;
//   4. the new color and the history are blended with a weight of 1 / (1 + luma), so a bright pixel cannot swamp the result; the
//      history keeps less the faster the pixel moves and the further it had to be pulled.
// Writes the resolved color and this frame's depth (the next frame's disocclusion test) into the textures of this frame.

#define GROUP_SIZE 8
#define CLIP_GAMMA 1.25
#define DEPTH_TOLERANCE 0.02

// Mirrors TaaData in taa_pass.h.
cbuffer TaaData : register(b0)
{
	float4x4 InverseViewProjection;  // this frame's, unjittered
	float4x4 PreviousViewProjection; // the previous frame's, unjittered
	float4 Parameters; // x = feedback, y = 1 when the history can be used, z = width, w = height (pixels)
	float4 DepthInfo;  // x = near plane, y = far plane, z = 1 for an orthographic camera
};

Texture2D uCurrent : register(t1);
Texture2D<float> uDepth : register(t2);
Texture2D uHistory : register(t3);
Texture2D<float2> uVelocity : register(t4);
Texture2D<float> uDepthHistory : register(t5);
SamplerState uSampler : register(s6);
IMAGE_FORMAT("rgba16f") RWTexture2D<float4> uOutput : register(u7);
IMAGE_FORMAT("r32f") RWTexture2D<float> uDepthOutput : register(u8);

float Luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 RgbToYCoCg(float3 rgb)
{
	return float3(0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b, 0.5 * rgb.r - 0.5 * rgb.b, -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b);
}

float3 YCoCgToRgb(float3 ycocg)
{
	return float3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

float3 Compress(float3 color)
{
	return color / (1.0 + Luma(color));
}

float3 Decompress(float3 color)
{
	return color / max(1.0 - Luma(color), 1e-4);
}

// The scene is HDR and may hold NaN or infinity: keep that out of the history.
float3 Sanitize(float3 color)
{
	if (any(isnan(color)) || any(isinf(color)))
	{
		return float3(0.0, 0.0, 0.0);
	}
	return clamp(color, 0.0, 65000.0);
}

// taa_clip_to_box in taa_math.h
float3 ClipToBox(float3 history, float3 box_min, float3 box_max)
{
	float3 centre = (box_min + box_max) * 0.5;
	float3 half_extent = max((box_max - box_min) * 0.5, 1e-6);
	float3 offset = history - centre;
	float3 scaled = abs(offset / half_extent);
	float t = max(scaled.x, max(scaled.y, scaled.z));
	return t > 1.0 ? centre + offset / t : history;
}

// The distance along the view direction of a depth buffer value.
float LinearDepth(float depth)
{
	if (DepthInfo.z > 0.5)
	{
		return DepthInfo.x + depth * (DepthInfo.y - DepthInfo.x);
	}
	return DepthInfo.x * DepthInfo.y / (DepthInfo.y - depth * (DepthInfo.y - DepthInfo.x));
}

// Catmull-Rom with five bilinear taps (the four corner taps of the full filter are dropped: their weights are tiny, and negative).
// Returns the weighted color divided by the weights of the taps used.
float3 SampleHistory(float2 uv, float2 size)
{
	float2 sample_position = uv * size;
	float2 tex_position_1 = floor(sample_position - 0.5) + 0.5;
	float2 f = sample_position - tex_position_1;

	float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);
	float2 w12 = w1 + w2;
	float2 offset_12 = w2 / w12;

	float2 tex_position_0 = (tex_position_1 - 1.0) / size;
	float2 tex_position_3 = (tex_position_1 + 2.0) / size;
	float2 tex_position_12 = (tex_position_1 + offset_12) / size;

	float3 result = float3(0.0, 0.0, 0.0);
	result += Sanitize(uHistory.SampleLevel(uSampler, float2(tex_position_12.x, tex_position_0.y), 0).rgb) * (w12.x * w0.y);
	result += Sanitize(uHistory.SampleLevel(uSampler, float2(tex_position_0.x, tex_position_12.y), 0).rgb) * (w0.x * w12.y);
	result += Sanitize(uHistory.SampleLevel(uSampler, float2(tex_position_12.x, tex_position_12.y), 0).rgb) * (w12.x * w12.y);
	result += Sanitize(uHistory.SampleLevel(uSampler, float2(tex_position_3.x, tex_position_12.y), 0).rgb) * (w3.x * w12.y);
	result += Sanitize(uHistory.SampleLevel(uSampler, float2(tex_position_12.x, tex_position_3.y), 0).rgb) * (w12.x * w3.y);

	float weight_sum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return max(result / weight_sum, 0.0);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	int2 size = int2(Parameters.zw);
	int2 pixel = int2(dispatch_id.xy);
	if (any(pixel >= size))
	{
		return;
	}

	float3 current = Sanitize(uCurrent.Load(int3(pixel, 0)).rgb);
	float depth = uDepth.Load(int3(pixel, 0)).r;
	uDepthOutput[pixel] = depth;

	if (Parameters.y < 0.5)
	{
		uOutput[pixel] = float4(current, 1.0); // no history: this frame alone
		return;
	}

	// The nearest surface of the neighbourhood decides how the pixel moved (an edge pixel that is not covered by the moving object
	// still belongs to its edge), and the colors of the neighbourhood give the box.
	float2 uv = (float2(pixel) + 0.5) / float2(size);
	int2 closest_pixel = pixel;
	float closest_depth = depth;
	float3 mean = float3(0.0, 0.0, 0.0);
	float3 squares = float3(0.0, 0.0, 0.0);
	for (int dy = -1; dy <= 1; dy++)
	{
		for (int dx = -1; dx <= 1; dx++)
		{
			int2 tap = clamp(pixel + int2(dx, dy), int2(0, 0), size - 1);
			float tap_depth = uDepth.Load(int3(tap, 0)).r;
			if (tap_depth < closest_depth)
			{
				closest_depth = tap_depth;
				closest_pixel = tap;
			}
			float3 neighbour = RgbToYCoCg(Compress(Sanitize(uCurrent.Load(int3(tap, 0)).rgb)));
			mean += neighbour;
			squares += neighbour * neighbour;
		}
	}
	mean /= 9.0;
	squares /= 9.0;

	float2 velocity;
	if (closest_depth >= 1.0)
	{
		// Nothing was drawn here: the camera alone moved it. The point of the far plane behind this pixel, seen from the previous frame.
		float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
		float4 world = mul(InverseViewProjection, clip);
		float4 previous = mul(PreviousViewProjection, float4(world.xyz / world.w, 1.0));
		if (previous.w <= 0.0)
		{
			uOutput[pixel] = float4(current, 1.0);
			return;
		}
		velocity = uv - float2(previous.x / previous.w * 0.5 + 0.5, 0.5 - previous.y / previous.w * 0.5);
	}
	else
	{
		velocity = uVelocity.Load(int3(closest_pixel, 0)).rg;
	}

	float2 history_uv = uv - velocity;
	if (any(history_uv < 0.0) || any(history_uv > 1.0))
	{
		uOutput[pixel] = float4(current, 1.0); // the surface was off the screen
		return;
	}

	// Was the same surface there? A different depth means something moved in front of it or away from it. The jitter flips the
	// coverage of edge pixels from frame to frame, so the surface counts as found when any pixel next to the place has its depth.
	int2 history_pixel = clamp(int2(history_uv * float2(size)), int2(0, 0), size - 1);
	float linear_now = LinearDepth(closest_depth);
	bool same_surface = false;
	for (int hy = -1; hy <= 1; hy++)
	{
		for (int hx = -1; hx <= 1; hx++)
		{
			int2 history_tap = clamp(history_pixel + int2(hx, hy), int2(0, 0), size - 1);
			float linear_before = LinearDepth(uDepthHistory.Load(int3(history_tap, 0)).r);
			same_surface = same_surface || abs(linear_now - linear_before) <= DEPTH_TOLERANCE * max(linear_now, linear_before);
		}
	}
	if (!same_surface)
	{
		uOutput[pixel] = float4(current, 1.0);
		return;
	}

	float3 sigma = sqrt(max(squares - mean * mean, 0.0));
	float3 box_min = mean - CLIP_GAMMA * sigma;
	float3 box_max = mean + CLIP_GAMMA * sigma;

	float3 history = SampleHistory(history_uv, float2(size));
	float3 history_ycocg = RgbToYCoCg(Compress(history));
	float3 clipped_ycocg = ClipToBox(history_ycocg, box_min, box_max);

	// taa_feedback: the further the history had to be pulled and the faster the pixel moves, the less of it is kept.
	float moved = length(clipped_ycocg - history_ycocg) / max(length(history_ycocg), 1e-4);
	float speed = length(velocity * float2(size));
	float feedback = Parameters.x * (1.0 - 0.5 * saturate(moved)) * (1.0 - 0.5 * saturate(speed / 16.0));

	// taa_blend, in linear color.
	float3 history_clipped = Decompress(YCoCgToRgb(clipped_ycocg));
	float current_weight = (1.0 - feedback) / (1.0 + Luma(current));
	float history_weight = feedback / (1.0 + Luma(history_clipped));
	float3 result = (current * current_weight + history_clipped * history_weight) / (current_weight + history_weight);

	if (any(isnan(result)) || any(isinf(result)))
	{
		result = current;
	}
	uOutput[pixel] = float4(clamp(result, 0.0, 65000.0), 1.0);
}
