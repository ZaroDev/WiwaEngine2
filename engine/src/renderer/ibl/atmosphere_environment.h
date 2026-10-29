#pragma once

#include <cmath>

#include <scene/scene_settings.h>

#include "atmosphere_math.h"
#include "ibl_math.h"

// The sky of the atmosphere as an environment: what the scene reflects and the ambient light it receives. It is built the way an
// environment file is (a cube, then the prefiltered mips and the SH, see ibl_math.h) on a job thread whenever the sun or the sky
// parameters change enough to matter, while the sky that is seen is drawn every frame by the sky pass.
namespace wiwa
{
	struct AtmosphereDescription
	{
		atmosphere::SkyParameters Sky{};
		glm::vec3 SunDirection{ 0.0f, 1.0f, 0.0f };   // unit vector towards the sun
		glm::vec3 SunIrradiance{ 3.4f };              // outside the atmosphere
		float Brightness = 3.5f;                      // scales the sky seen and the light it makes
		float DiscBoost = 120.0f;                     // the sun disc is drawn with this much of its real brightness
	};

	// The sun direction is the direction towards the sun; zero, NaN or infinite input gives a defined default (high in the sky).
	inline AtmosphereDescription make_atmosphere_description(const AtmosphereParameters& parameters, glm::vec3 towardsTheSun)
	{
		AtmosphereDescription description;
		description.Sky.RayleighScale = parameters.RayleighScale;
		description.Sky.MieScale = parameters.MieScale;
		description.Sky.OzoneScale = parameters.OzoneScale;
		description.Sky.MieAnisotropy = parameters.MieAnisotropy;
		description.Sky.MultipleScattering = parameters.MultipleScattering;
		description.Sky.GroundAlbedo = parameters.GroundAlbedo;
		description.SunIrradiance = glm::vec3(parameters.SunIlluminance);
		description.Brightness = parameters.SkyBrightness;
		description.DiscBoost = parameters.SunDiscBoost;

		const float length = glm::length(towardsTheSun);
		if (!std::isfinite(length) || length < 1e-4f)
		{
			towardsTheSun = glm::vec3(0.3f, 0.8f, 0.5f);
		}
		description.SunDirection = glm::normalize(towardsTheSun);
		return description;
	}

	// The radiance of the sky towards a direction (no sun disc: the analytic sun light is the sun's highlight).
	inline glm::vec3 atmosphere_radiance(const AtmosphereDescription& description, const glm::vec3& direction)
	{
		const atmosphere::LightSource sun{ description.SunDirection, description.SunIrradiance };
		return atmosphere::sky_radiance(direction, description.Sky, &sun, 1, 16, 6) * description.Brightness;
	}

	// The cube of the sky. The sky is smooth, so a small face size is plenty.
	inline CubeMap make_atmosphere_cube(const AtmosphereDescription& description, u32 size)
	{
		return make_cube(size, [&description](const glm::vec3& direction) { return atmosphere_radiance(description, direction); });
	}

	// Is the environment built for `built` still good for `wanted`? The ambient light is rebuilt when the sun has moved by half a degree
	// or a parameter has changed (the sky that is seen is exact all the time).
	inline bool atmosphere_is_close(const AtmosphereDescription& built, const AtmosphereDescription& wanted)
	{
		auto same = [](float a, float b) { return std::abs(a - b) <= 1e-4f * std::max(1.0f, std::abs(a)); };
		auto same3 = [&](const glm::vec3& a, const glm::vec3& b) { return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z); };
		return glm::dot(built.SunDirection, wanted.SunDirection) >= std::cos(glm::radians(0.5f))
			&& same(built.Sky.RayleighScale, wanted.Sky.RayleighScale) && same(built.Sky.MieScale, wanted.Sky.MieScale)
			&& same(built.Sky.OzoneScale, wanted.Sky.OzoneScale) && same(built.Sky.MieAnisotropy, wanted.Sky.MieAnisotropy)
			&& same(built.Sky.MultipleScattering, wanted.Sky.MultipleScattering) && same3(built.Sky.GroundAlbedo, wanted.Sky.GroundAlbedo)
			&& same3(built.SunIrradiance, wanted.SunIrradiance) && same(built.Brightness, wanted.Brightness);
	}
}
