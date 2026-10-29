#ifndef ATMOSPHERE_HLSLI
#define ATMOSPHERE_HLSLI

#include "scene_common.hlsli"

// The sky model of ibl/atmosphere_math.h in HLSL (single scattering of Rayleigh and Mie, ozone absorption, a stand-in for multiple
// scattering); keep the two in step. Distances are in kilometres with the planet's centre at the origin and the observer straight above
// it. The parameters come from the AtmosphereA/B/Ground/Sun/Irradiance fields of the frame constants.

static const float PLANET_RADIUS = 6360.0;
static const float ATMOSPHERE_RADIUS = 6420.0;
static const float RAYLEIGH_SCALE_HEIGHT = 8.0;
static const float MIE_SCALE_HEIGHT = 1.2;
static const float OZONE_CENTER = 25.0;
static const float OZONE_HALF_WIDTH = 15.0;
static const float SUN_ANGULAR_RADIUS = 0.00465;
static const float3 RAYLEIGH_SCATTERING = float3(5.802e-3, 13.558e-3, 33.1e-3);
static const float MIE_SCATTERING = 3.996e-3;
static const float MIE_EXTINCTION = 4.44e-3;
static const float3 OZONE_ABSORPTION = float3(0.65e-3, 1.881e-3, 0.085e-3);

float3 ObserverPosition()
{
	return float3(0.0, PLANET_RADIUS + AtmosphereB.y, 0.0);
}

bool RaySphere(float3 origin, float3 direction, float radius, out float nearDistance, out float farDistance)
{
	float b = dot(origin, direction);
	float c = dot(origin, origin) - radius * radius;
	float discriminant = b * b - c;
	nearDistance = 0.0;
	farDistance = 0.0;
	if (discriminant < 0.0)
	{
		return false;
	}
	float root = sqrt(discriminant);
	nearDistance = -b - root;
	farDistance = -b + root;
	return true;
}

float3 ExtinctionAt(float height)
{
	float rayleigh = exp(-height / RAYLEIGH_SCALE_HEIGHT) * AtmosphereA.x;
	float mie = exp(-height / MIE_SCALE_HEIGHT) * AtmosphereA.y;
	float ozone = max(0.0, 1.0 - abs(height - OZONE_CENTER) / OZONE_HALF_WIDTH) * AtmosphereA.z;
	return RAYLEIGH_SCATTERING * rayleigh + float3(MIE_EXTINCTION, MIE_EXTINCTION, MIE_EXTINCTION) * mie + OZONE_ABSORPTION * ozone;
}

// What of the light of the top of the atmosphere reaches a point along a direction towards the light (0 when the planet is in the way).
float3 TransmittanceToTop(float3 position, float3 direction, int steps)
{
	float nearDistance;
	float farDistance;
	if (RaySphere(position, direction, PLANET_RADIUS, nearDistance, farDistance) && farDistance > 0.0 && nearDistance > 0.0)
	{
		return float3(0.0, 0.0, 0.0);
	}
	if (!RaySphere(position, direction, ATMOSPHERE_RADIUS, nearDistance, farDistance) || farDistance <= 0.0)
	{
		return float3(1.0, 1.0, 1.0);
	}

	float stepLength = farDistance / float(steps);
	float3 depth = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < steps; i++)
	{
		float3 samplePosition = position + direction * ((float(i) + 0.5) * stepLength);
		depth += ExtinctionAt(length(samplePosition) - PLANET_RADIUS) * stepLength;
	}
	return exp(-depth);
}

float RayleighPhase(float cosine)
{
	return 3.0 / (16.0 * PI) * (1.0 + cosine * cosine);
}

float MiePhase(float cosine, float g)
{
	float g2 = g * g;
	float denominator = pow(max(1.0 + g2 - 2.0 * g * cosine, 1e-4), 1.5);
	return 3.0 / (8.0 * PI) * (1.0 - g2) * (1.0 + cosine * cosine) / ((2.0 + g2) * denominator);
}

// The sky along a view direction (no sun disc), lit by the sun; the ground where the ray meets the planet. Not yet scaled by the sky
// brightness.
float3 SkyRadiance(float3 viewDirection)
{
	float3 origin = ObserverPosition();
	float nearDistance;
	float farDistance;
	RaySphere(origin, viewDirection, ATMOSPHERE_RADIUS, nearDistance, farDistance);
	float groundNear;
	float groundFar;
	bool hitsGround = RaySphere(origin, viewDirection, PLANET_RADIUS, groundNear, groundFar) && groundNear > 0.0;

	float path = hitsGround ? groundNear : farDistance;
	int steps = int(AtmosphereB.z);
	int lightSteps = int(AtmosphereB.w);
	float stepLength = path / float(steps);

	float3 rayleighSum = float3(0.0, 0.0, 0.0);
	float3 mieSum = float3(0.0, 0.0, 0.0);
	float3 opticalDepth = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < steps; i++)
	{
		float3 position = origin + viewDirection * ((float(i) + 0.5) * stepLength);
		float height = max(length(position) - PLANET_RADIUS, 0.0);
		float rayleigh = exp(-height / RAYLEIGH_SCALE_HEIGHT) * AtmosphereA.x;
		float mie = exp(-height / MIE_SCALE_HEIGHT) * AtmosphereA.y;
		float3 segment = ExtinctionAt(height) * stepLength;
		float3 toObserver = exp(-(opticalDepth + segment * 0.5));
		opticalDepth += segment;

		float3 arriving = TransmittanceToTop(position, AtmosphereSun.xyz, lightSteps);
		rayleighSum += toObserver * arriving * (rayleigh * stepLength);
		mieSum += toObserver * arriving * (mie * stepLength);
	}

	float cosine = dot(viewDirection, AtmosphereSun.xyz);
	float3 single = RAYLEIGH_SCATTERING * RayleighPhase(cosine) * rayleighSum + MIE_SCATTERING * MiePhase(cosine, AtmosphereA.w) * mieSum;
	float3 isotropic = (RAYLEIGH_SCATTERING * rayleighSum + MIE_SCATTERING * mieSum) / (4.0 * PI);
	float3 radiance = AtmosphereIrradiance.rgb * (single + isotropic * AtmosphereB.x);

	if (hitsGround)
	{
		float3 pointOnGround = origin + viewDirection * groundNear;
		float3 normal = normalize(pointOnGround);
		float3 toObserver = exp(-opticalDepth);
		float3 sunArriving = TransmittanceToTop(pointOnGround + normal * 0.001, AtmosphereSun.xyz, lightSteps);
		radiance += AtmosphereIrradiance.rgb * sunArriving * AtmosphereGround.rgb * (max(dot(normal, AtmosphereSun.xyz), 0.0) / PI) * toObserver;
	}
	return radiance;
}

// The sun's disc: limb-darkened, with a soft edge.
float3 SunDisc(float3 direction)
{
	float cosine = dot(direction, AtmosphereSun.xyz);
	if (cosine < cos(SUN_ANGULAR_RADIUS * 1.6))
	{
		return float3(0.0, 0.0, 0.0);
	}

	float angle = acos(clamp(cosine, -1.0, 1.0));
	float edge = saturate(1.0 - (angle - SUN_ANGULAR_RADIUS * 0.85) / (SUN_ANGULAR_RADIUS * 0.3));
	float r = saturate(angle / SUN_ANGULAR_RADIUS);
	float limb = 1.0 - 0.55 * (1.0 - sqrt(max(1.0 - r * r, 0.0)));
	float3 transmittance = TransmittanceToTop(ObserverPosition(), direction, 12);
	return AtmosphereIrradiance.rgb * transmittance * (AtmosphereGround.w * limb * edge);
}

#endif // ATMOSPHERE_HLSLI
