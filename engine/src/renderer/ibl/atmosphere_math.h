#pragma once

#include <algorithm>
#include <cmath>

#include <math/math.h>

// A procedural sky: single scattering of sunlight (and moonlight) in a planet's atmosphere, plus a cheap stand-in for the
// multiple scattering that single scattering misses. No GPU code here, so everything is tested on its own
// (tests/AtmosphereMathTest.cpp); shaders/Atmosphere.hlsli is the same model in HLSL and has to be kept in step with it.
//
// Distances are in kilometres, with the planet's centre at the origin and the observer straight above it on +Y. The sky is at
// infinity, so the observer's position in the scene does not matter: only its altitude does.
//
// The model is the one of Nishita / Bruneton / Hillaire: along a view ray the air scatters light towards the observer
//   L = sum over the samples p of   T(observer, p) * ( scattering(p) * phase * T(p, sun) ) * E_sun * dx
// where T is the transmittance (Beer-Lambert with the extinction of the air: Rayleigh, Mie and ozone) and E_sun is the sun's
// irradiance outside the atmosphere. The phase functions do not depend on the sample, so the sum is done once per species
// (Rayleigh, Mie) and the phase applied at the end.
namespace wiwa::atmosphere
{
	constexpr float PI = 3.14159265358979f;
	constexpr float PLANET_RADIUS = 6360.0f;        // km
	constexpr float ATMOSPHERE_RADIUS = 6420.0f;    // km, 60 km of air
	constexpr float RAYLEIGH_SCALE_HEIGHT = 8.0f;   // km: the density falls by e every 8 km
	constexpr float MIE_SCALE_HEIGHT = 1.2f;
	constexpr float OZONE_CENTER = 25.0f;           // km: the ozone layer is a tent around this height
	constexpr float OZONE_HALF_WIDTH = 15.0f;
	constexpr float SUN_ANGULAR_RADIUS = 0.00465f;  // radians (0.267 degrees)
	constexpr float MOON_ANGULAR_RADIUS = 0.0045f;

	// Per kilometre, at sea level (Hillaire 2020).
	constexpr float RAYLEIGH_R = 5.802e-3f;
	constexpr float RAYLEIGH_G = 13.558e-3f;
	constexpr float RAYLEIGH_B = 33.100e-3f;
	constexpr float MIE_SCATTERING = 3.996e-3f;
	constexpr float MIE_EXTINCTION = 4.440e-3f;
	constexpr float OZONE_R = 0.650e-3f;
	constexpr float OZONE_G = 1.881e-3f;
	constexpr float OZONE_B = 0.085e-3f;

	struct SkyParameters
	{
		float RayleighScale{ 1.0f };    // 1 = Earth. More: a bluer, denser sky; 0: no blue sky at all
		float MieScale{ 1.0f };         // haze and aerosols: more makes the sky whiter and the sun's glow wider
		float OzoneScale{ 1.0f };
		float MieAnisotropy{ 0.8f };    // g of the Mie phase function: how strongly it favours the forward direction
		float MultipleScattering{ 0.6f };
		glm::vec3 GroundAlbedo{ 0.3f };
		float ObserverAltitude{ 0.05f }; // km above the sea level
	};

	struct LightSource
	{
		glm::vec3 Direction{ 0.0f, 1.0f, 0.0f }; // unit vector towards the light
		glm::vec3 Irradiance{ 0.0f };            // what arrives outside the atmosphere (rgb)
	};

	inline float saturate(float x) { return std::min(std::max(x, 0.0f), 1.0f); }
	inline float smoothstep(float low, float high, float x)
	{
		const float t = saturate((x - low) / (high - low));
		return t * t * (3.0f - 2.0f * t);
	}

	inline glm::vec3 observer_position(const SkyParameters& parameters)
	{
		return glm::vec3(0.0f, PLANET_RADIUS + parameters.ObserverAltitude, 0.0f);
	}

	// The two distances at which a ray (origin inside or outside, unit direction) crosses a sphere around the origin; false if it misses.
	inline bool intersect_sphere(const glm::vec3& origin, const glm::vec3& direction, float radius, float& nearDistance, float& farDistance)
	{
		const float b = glm::dot(origin, direction);
		const float c = glm::dot(origin, origin) - radius * radius;
		const float discriminant = b * b - c;
		if (discriminant < 0.0f)
		{
			return false;
		}
		const float root = std::sqrt(discriminant);
		nearDistance = -b - root;
		farDistance = -b + root;
		return true;
	}

	inline float rayleigh_density(float height) { return std::exp(-height / RAYLEIGH_SCALE_HEIGHT); }
	inline float mie_density(float height) { return std::exp(-height / MIE_SCALE_HEIGHT); }
	inline float ozone_density(float height) { return std::max(0.0f, 1.0f - std::abs(height - OZONE_CENTER) / OZONE_HALF_WIDTH); }

	// How much light the air removes per kilometre at a height.
	inline glm::vec3 extinction(float height, const SkyParameters& parameters)
	{
		const float rayleigh = rayleigh_density(height) * parameters.RayleighScale;
		const float mie = mie_density(height) * parameters.MieScale;
		const float ozone = ozone_density(height) * parameters.OzoneScale;
		return glm::vec3(RAYLEIGH_R, RAYLEIGH_G, RAYLEIGH_B) * rayleigh + glm::vec3(MIE_EXTINCTION) * mie + glm::vec3(OZONE_R, OZONE_G, OZONE_B) * ozone;
	}

	// The fraction of light that gets from the top of the atmosphere to a point along a direction (towards the light): 0 when the
	// planet is in the way.
	inline glm::vec3 transmittance_to_top(const glm::vec3& position, const glm::vec3& direction, const SkyParameters& parameters, int steps = 8)
	{
		float nearDistance = 0.0f;
		float farDistance = 0.0f;
		if (intersect_sphere(position, direction, PLANET_RADIUS, nearDistance, farDistance) && farDistance > 0.0f && nearDistance > 0.0f)
		{
			return glm::vec3(0.0f);
		}
		if (!intersect_sphere(position, direction, ATMOSPHERE_RADIUS, nearDistance, farDistance) || farDistance <= 0.0f)
		{
			return glm::vec3(1.0f);
		}

		const float step = farDistance / static_cast<float>(steps);
		glm::vec3 depth(0.0f);
		for (int i = 0; i < steps; i++)
		{
			const glm::vec3 sample = position + direction * ((static_cast<float>(i) + 0.5f) * step);
			depth += extinction(glm::length(sample) - PLANET_RADIUS, parameters) * step;
		}
		return glm::exp(-depth);
	}

	inline float rayleigh_phase(float cosine) { return 3.0f / (16.0f * PI) * (1.0f + cosine * cosine); }

	// Cornette-Shanks: Henyey-Greenstein with the right Rayleigh-like back lobe.
	inline float mie_phase(float cosine, float g)
	{
		const float g2 = g * g;
		const float denominator = std::pow(std::max(1.0f + g2 - 2.0f * g * cosine, 1e-4f), 1.5f);
		return 3.0f / (8.0f * PI) * (1.0f - g2) * (1.0f + cosine * cosine) / ((2.0f + g2) * denominator);
	}

	// The radiance that reaches the observer along a view direction: the sky, or the ground where the ray meets the planet.
	// Rayleigh and Mie scattering of every light in `lights`, no sun disc, no stars.
	inline glm::vec3 sky_radiance(const glm::vec3& viewDirection, const SkyParameters& parameters, const LightSource* lights, int lightCount,
		int steps = 16, int lightSteps = 6)
	{
		const glm::vec3 origin = observer_position(parameters);
		float nearDistance = 0.0f;
		float farDistance = 0.0f;
		intersect_sphere(origin, viewDirection, ATMOSPHERE_RADIUS, nearDistance, farDistance);
		float groundNear = 0.0f;
		float groundFar = 0.0f;
		const bool hitsGround = intersect_sphere(origin, viewDirection, PLANET_RADIUS, groundNear, groundFar) && groundNear > 0.0f;
		const float length = hitsGround ? groundNear : farDistance;

		const float step = length / static_cast<float>(steps);
		glm::vec3 rayleighSum[2] = { glm::vec3(0.0f), glm::vec3(0.0f) };
		glm::vec3 mieSum[2] = { glm::vec3(0.0f), glm::vec3(0.0f) };
		glm::vec3 opticalDepth(0.0f);
		const int usedLights = std::min(lightCount, 2);

		for (int i = 0; i < steps; i++)
		{
			const glm::vec3 position = origin + viewDirection * ((static_cast<float>(i) + 0.5f) * step);
			const float height = std::max(glm::length(position) - PLANET_RADIUS, 0.0f);
			const float rayleigh = rayleigh_density(height) * parameters.RayleighScale;
			const float mie = mie_density(height) * parameters.MieScale;

			const glm::vec3 segment = extinction(height, parameters) * step;
			const glm::vec3 toObserver = glm::exp(-(opticalDepth + segment * 0.5f));
			opticalDepth += segment;

			for (int light = 0; light < usedLights; light++)
			{
				const glm::vec3 arriving = transmittance_to_top(position, lights[light].Direction, parameters, lightSteps);
				rayleighSum[light] += toObserver * arriving * (rayleigh * step);
				mieSum[light] += toObserver * arriving * (mie * step);
			}
		}

		const glm::vec3 rayleighScattering = glm::vec3(RAYLEIGH_R, RAYLEIGH_G, RAYLEIGH_B);
		glm::vec3 radiance(0.0f);
		for (int light = 0; light < usedLights; light++)
		{
			const float cosine = glm::dot(viewDirection, lights[light].Direction);
			const glm::vec3 single = rayleighScattering * rayleigh_phase(cosine) * rayleighSum[light]
				+ glm::vec3(MIE_SCATTERING) * mie_phase(cosine, parameters.MieAnisotropy) * mieSum[light];
			// Multiple scattering, approximately: light that has been scattered several times has lost its direction.
			const glm::vec3 isotropic = (rayleighScattering * rayleighSum[light] + glm::vec3(MIE_SCATTERING) * mieSum[light]) / (4.0f * PI);
			radiance += lights[light].Irradiance * (single + isotropic * parameters.MultipleScattering);
		}

		if (hitsGround)
		{
			const glm::vec3 point = origin + viewDirection * groundNear;
			const glm::vec3 normal = glm::normalize(point);
			const glm::vec3 toObserver = glm::exp(-opticalDepth);
			for (int light = 0; light < usedLights; light++)
			{
				const float lit = std::max(glm::dot(normal, lights[light].Direction), 0.0f);
				const glm::vec3 arriving = transmittance_to_top(point + normal * 0.001f, lights[light].Direction, parameters, lightSteps);
				radiance += lights[light].Irradiance * arriving * parameters.GroundAlbedo * (lit / PI) * toObserver;
			}
		}
		return radiance;
	}

	// The irradiance of a light on a surface at the observer's altitude that faces it: the light's own irradiance, reddened and dimmed
	// by the air it crosses, and faded out as it goes down behind the horizon (a disc, not a point).
	inline glm::vec3 light_at_observer(const glm::vec3& direction, const glm::vec3& irradiance, const SkyParameters& parameters)
	{
		// The path through the air at the horizon is already very long: the light below it is the horizon's, faded.
		const glm::vec3 clamped = glm::normalize(glm::vec3(direction.x, std::max(direction.y, 0.0f), direction.z) + glm::vec3(0.0f, 1e-4f, 0.0f));
		const glm::vec3 arriving = transmittance_to_top(observer_position(parameters), clamped, parameters, 16);
		const float visible = smoothstep(-0.03f, 0.02f, direction.y);
		return irradiance * arriving * visible;
	}
}
