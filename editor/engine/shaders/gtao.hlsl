#include "Common.hlsli"

// Ambient occlusion from depth and normals: GTAO (Jimenez et al. 2016). The maths is the standalone renderer's (GtaoMath.h there).
//
// For each pixel, a few "slices" (directions on screen) are searched on both sides of the pixel: along each
// direction the highest point of the surroundings, seen from the pixel, gives the horizon, and the part of the
// cosine-weighted hemisphere that lies between the two horizons is open to the sky. The result is noisy on
// purpose (every pixel of a 4x4 block starts at a different angle); gtao_blur.hlsl averages the
// pattern away. The search reads the distances of gtao_depth.hlsl: the farther a sample is from the pixel, the
// coarser the mip level it is read from (cheaper, and a wide search does not alias).

#define GROUP_SIZE WI_GTAO_GROUP_SIZE

// Mirrors AoData in ao_pass.h.
cbuffer AoData : register(b0)
{
	float4x4 View;        // world -> view space (the G-buffer normals are in world space)
	float4 Projection;    // x = P00, y = P11, z = clip-space Y sign, w = last mip level of the distances
	float4 DepthInfo;     // x = near plane, y = far plane, z = width, w = height (pixels)
	float4 Settings;      // x = radius (world units), y = power, z = slices, w = steps
};

Texture2D<float> uLinearDepth : register(t1); // distance along the view direction, mip chain
Texture2D uNormal : register(t2); // rg = octahedral normal
IMAGE_FORMAT("r8") RWTexture2D<float> uOutput : register(u3);

static const float PI = 3.14159265359;
static const float HALF_PI = 1.57079632679;
static const float MAX_PIXEL_RADIUS = 100.0; // keeps the cost bounded when the camera is very close to a surface
static const float MIN_PIXEL_RADIUS = 1.5;   // below this there is nothing to search

// GTAO_NOISE_SLICE in gtao_math.h: the 4x4 Bayer order, each value (b + 0.5) / 16.
static const float NOISE_SLICE[16] =
{
	0.5 / 16.0,  8.5 / 16.0,  2.5 / 16.0,  10.5 / 16.0,
	12.5 / 16.0, 4.5 / 16.0,  14.5 / 16.0, 6.5 / 16.0,
	3.5 / 16.0,  11.5 / 16.0, 1.5 / 16.0,  9.5 / 16.0,
	15.5 / 16.0, 7.5 / 16.0,  13.5 / 16.0, 5.5 / 16.0,
};

float InterleavedGradientNoise(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// gtao_fast_acos
float FastAcos(float x)
{
	float magnitude = abs(x);
	float result = sqrt(1.0 - magnitude) * (1.5707288 + magnitude * (-0.2121144 + magnitude * (0.0742610 - 0.0187293 * magnitude)));
	return (x >= 0.0) ? result : PI - result;
}

// gtao_project_normal: x = length of the normal inside the slice plane, y = its signed angle from the view vector.
float2 ProjectNormal(float3 slice_direction, float3 view_vector, float3 normal)
{
	float3 axis_raw = cross(slice_direction, view_vector);
	float axis_length = length(axis_raw);
	if (axis_length < 1e-6)
	{
		return float2(0.0, 0.0);
	}

	float3 axis = axis_raw / axis_length;
	float3 projected = normal - axis * dot(normal, axis);
	float length_in_plane = length(projected);
	if (length_in_plane < 1e-6)
	{
		return float2(0.0, 0.0);
	}

	float3 direction = slice_direction - view_vector * dot(slice_direction, view_vector);
	float sign_of_angle = (dot(projected, direction) >= 0.0) ? 1.0 : -1.0;
	float cosine = saturate(dot(projected, view_vector) / length_in_plane);
	return float2(length_in_plane, sign_of_angle * FastAcos(cosine));
}

// gtao_slice_visibility, with sin(n) and cos(n) given (the search needs them as well)
float SliceVisibility(float h1, float h2, float sin_n, float cos_n, float n)
{
	return 0.25 * (-cos(2.0 * h1 - n) + cos_n + 2.0 * h1 * sin_n)
		+ 0.25 * (-cos(2.0 * h2 - n) + cos_n + 2.0 * h2 * sin_n);
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	int2 size = int2(DepthInfo.zw);
	int2 pixel = int2(dispatch_id.xy);
	if (any(pixel >= size))
	{
		return;
	}

	float far_plane = DepthInfo.y;
	float sky_z = far_plane * 0.9999;

	float z = uLinearDepth.Load(int3(pixel, 0));
	if (z >= sky_z)
	{
		uOutput[pixel] = 1.0; // sky: nothing to occlude
		return;
	}

	// View space (the camera looks along -z): x = (2u - 1) z / P00, y = sign (1 - 2v) z / P11, the same clip-space
	// Y convention as UnprojectToWorld. ray_scale turns the NDC of a pixel into that, per unit of distance.
	float2 ray_scale = float2(1.0 / Projection.x, Projection.z / Projection.y);

	float2 ndc = (float2(pixel) + 0.5) * (float2(2.0, -2.0) / DepthInfo.zw) + float2(-1.0, 1.0);
	float3 position = float3(ndc * ray_scale * z, -z);
	float3 normal = normalize(mul((float3x3)View, OctahedralDecode(uNormal.Load(int3(pixel, 0)).rg)));
	float3 view_vector = normalize(-position);

	float radius = Settings.x;
	float power = Settings.y;
	int slice_count = int(Settings.z);
	int max_steps = int(Settings.w);
	int max_mip = int(Projection.w);

	// The world radius as pixels on screen: it shrinks with the distance.
	float pixel_radius = min(radius * Projection.x * DepthInfo.z * 0.5 / z, MAX_PIXEL_RADIUS);
	if (pixel_radius < MIN_PIXEL_RADIUS)
	{
		uOutput[pixel] = 1.0;
		return;
	}

	// A small search area has no use for many steps: the squared spacing would put several on one pixel.
	int step_count = clamp(int(pixel_radius * 0.5) + 1, 2, max_steps);
	float inverse_step_count = 1.0 / float(step_count);

	float falloff_start = 0.6 * radius; // gtao_falloff: nothing is ignored inside it, everything at the radius
	float falloff_scale = 1.0 / (0.4 * radius);

	float slice_noise = NOISE_SLICE[(pixel.y & 3) * 4 + (pixel.x & 3)];
	float step_jitter = InterleavedGradientNoise(float2(pixel));

	float2 centre = float2(pixel) + 0.5;
	float visibility = 0.0;
	for (int slice = 0; slice < slice_count; slice++)
	{
		float phi = (float(slice) + slice_noise) * PI / float(slice_count);
		float2 screen_direction;
		sincos(phi, screen_direction.y, screen_direction.x); // pixel space, y down
		float3 view_direction = float3(screen_direction.x, -Projection.z * screen_direction.y, 0.0);

		float2 slice_normal = ProjectNormal(view_direction, view_vector, normal);
		if (slice_normal.x < 1e-4)
		{
			continue; // the normal is (almost) perpendicular to this slice: it weighs nothing
		}
		float n = slice_normal.y;
		float sin_n;
		float cos_n;
		sincos(n, sin_n, cos_n);

		// The horizons start at the tangent plane (nothing above it blocks anything) and rise with the
		// surroundings: side 0 is towards the slice direction, side 1 away from it.
		// cos(n + pi/2) = -sin(n), cos(n - pi/2) = sin(n)
		float low_horizon_cos[2];
		low_horizon_cos[0] = -sin_n;
		low_horizon_cos[1] = sin_n;
		float horizon_cos[2];
		horizon_cos[0] = low_horizon_cos[0];
		horizon_cos[1] = low_horizon_cos[1];

		[unroll]
		for (int side = 0; side < 2; side++)
		{
			float2 side_direction = ((side == 0) ? 1.0 : -1.0) * screen_direction;
			for (int step_index = 1; step_index <= step_count; step_index++)
			{
				// Denser close to the pixel, where the occluders matter most.
				float k = (float(step_index) + step_jitter) * inverse_step_count;
				k *= k;
				float offset_length = k * pixel_radius;

				float2 sample_position = centre + side_direction * offset_length;
				if (any(sample_position < 0.0) || any(sample_position >= float2(size)))
				{
					continue; // off the screen: nothing is known there, so it blocks nothing
				}

				// The farther from the pixel, the coarser the level (from 12 pixels on).
				int mip = clamp(int(log2(offset_length) - 2.5), 0, max_mip);
				int2 mip_size = max(size >> mip, 1);
				int2 texel = min(int2(sample_position) >> mip, mip_size - 1);
				if (mip == 0 && all(texel == pixel))
				{
					continue;
				}

				float sample_z = uLinearDepth.Load(int3(texel, mip));
				if (sample_z >= sky_z)
				{
					continue; // the sky blocks nothing
				}

				float2 sample_ndc = (float2(texel) + 0.5) * (float2(2.0, -2.0) / float2(mip_size)) + float2(-1.0, 1.0);
				float3 delta = float3(sample_ndc * ray_scale * sample_z, -sample_z) - position;
				float distance_squared = dot(delta, delta);
				if (distance_squared < 1e-8)
				{
					continue;
				}

				// How high the sample is, seen from the pixel, fading out towards the edge of the radius.
				float inverse_distance = rsqrt(distance_squared);
				float candidate = dot(delta, view_vector) * inverse_distance;
				float falloff = saturate((distance_squared * inverse_distance - falloff_start) * falloff_scale);
				candidate = lerp(candidate, low_horizon_cos[side], falloff);
				horizon_cos[side] = max(horizon_cos[side], candidate);
			}
		}

		// Horizon angles from the view vector (h1 on the far side), limited to the normal's hemisphere.
		float h1 = -FastAcos(clamp(horizon_cos[1], -1.0, 1.0));
		float h2 = FastAcos(clamp(horizon_cos[0], -1.0, 1.0));
		h1 = n + max(h1 - n, -HALF_PI);
		h2 = n + min(h2 - n, HALF_PI);

		visibility += slice_normal.x * SliceVisibility(h1, h2, sin_n, cos_n, n);
	}

	float occlusion = saturate(visibility / float(slice_count));
	uOutput[pixel] = pow(occlusion, power);
}
