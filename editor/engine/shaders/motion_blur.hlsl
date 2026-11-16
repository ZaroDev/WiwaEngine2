#include "Common.hlsli"

// Motion blur: smears every pixel along the way it moved since the previous frame (the velocity target; pixels with no geometry, the
// sky, move with the camera alone), as a camera shutter that stays open for a while would. 16 taps are spread over the smear, shifted
// by a per-pixel noise so that the steps do not show. A pixel beside a moving object takes the object's motion, so the smear also covers the
// background next to it. A pixel that moved less than a pixel is copied. In the velocity debug mode the
// output is the velocity itself (absolute value, 50 times) instead.

#define GROUP_SIZE WI_MOTION_BLUR_GROUP_SIZE
#define TAP_COUNT 16

// Mirrors MotionBlurData in motion_blur_pass.h.
cbuffer MotionBlurData : register(b0)
{
	float4x4 InverseViewProjection;  // this frame, unjittered
	float4x4 PreviousViewProjection; // the previous frame, unjittered
	float4 Parameters; // x = velocity scale (exposure time / frame time * strength), y = longest smear in pixels, z = width, w = height (pixels)
	float4 Mode;       // x = 1 for the velocity debug view
};

Texture2D uColor : register(t1);
Texture2D<float2> uVelocity : register(t2);
Texture2D<float> uDepth : register(t3);
SamplerState uSampler : register(s4);
IMAGE_FORMAT("rgba16f") RWTexture2D<float4> uOutput : register(u5);

// Interleaved gradient noise in [0, 1).
float Noise(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// How far the surface at a pixel moved on the screen since the previous frame, as a uv delta.
float2 PixelVelocity(int2 pixel, int2 size)
{
	if (uDepth.Load(int3(pixel, 0)).r >= 1.0)
	{
		// Nothing was drawn here: the camera alone moved it. The point of the far plane behind this pixel, seen from the previous frame.
		float2 uv = (float2(pixel) + 0.5) / float2(size);
		float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
		float4 world = mul(InverseViewProjection, clip);
		float4 previous = mul(PreviousViewProjection, float4(world.xyz / world.w, 1.0));
		if (previous.w > 0.0)
		{
			return uv - float2(previous.x / previous.w * 0.5 + 0.5, 0.5 - previous.y / previous.w * 0.5);
		}
		return float2(0.0, 0.0);
	}
	return uVelocity.Load(int3(pixel, 0)).rg;
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

	float4 center = uColor.Load(int3(pixel, 0));
	float2 uv = (float2(pixel) + 0.5) / float2(size);

	float2 velocity = PixelVelocity(pixel, size);
	if (Mode.x < 0.5)
	{
		// A pixel next to a moving object takes the fastest motion around it (within half the longest smear, at most 8 pixels), so that the
		// smear also reaches over the background beside the object.
		int step = (int)clamp(Parameters.y * 0.5, 1.0, 8.0);
		float best = dot(velocity * float2(size), velocity * float2(size));
		for (int dy = -1; dy <= 1; dy++)
		{
			for (int dx = -1; dx <= 1; dx++)
			{
				int2 tap = clamp(pixel + int2(dx, dy) * step, int2(0, 0), size - 1);
				float2 candidate = PixelVelocity(tap, size);
				float speed = dot(candidate * float2(size), candidate * float2(size));
				if (speed > best)
				{
					best = speed;
					velocity = candidate;
				}
			}
		}
	}

	if (Mode.x > 0.5)
	{
		uOutput[pixel] = float4(abs(velocity) * 50.0, 0.0, 1.0);
		return;
	}

	float2 smear = velocity * float2(size) * Parameters.x; // pixels
	float length_pixels = length(smear);
	if (!(length_pixels >= 1.0)) // also catches NaN
	{
		uOutput[pixel] = center;
		return;
	}
	smear *= min(1.0, Parameters.y / length_pixels);
	float2 smear_uv = smear / float2(size);

	float offset = Noise(float2(pixel));
	float3 sum = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < TAP_COUNT; i++)
	{
		float t = (float(i) + offset) / float(TAP_COUNT) - 0.5;
		float3 tap = uColor.SampleLevel(uSampler, clamp(uv + smear_uv * t, 0.0, 1.0), 0).rgb;
		sum += (any(isnan(tap)) || any(isinf(tap))) ? center.rgb : tap;
	}
	uOutput[pixel] = float4(sum / float(TAP_COUNT), center.a);
}
