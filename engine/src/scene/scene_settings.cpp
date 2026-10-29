#include <pch.h>
#include "scene_settings.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		// Replaces a value that is not finite by the fallback and clamps the rest; true when it had to change anything.
		bool fix(float& value, float fallback, float low, float high)
		{
			const float before = value;
			if (!std::isfinite(value))
			{
				value = fallback;
			}
			value = std::clamp(value, low, high);
			return value != before || !std::isfinite(before);
		}

		bool fix(glm::vec3& value, const glm::vec3& fallback, float low, float high)
		{
			bool changed = false;
			for (int i = 0; i < 3; i++)
			{
				changed = fix(value[i], fallback[i], low, high) || changed;
			}
			return changed;
		}
	}

	SceneSettings sanitized(SceneSettings s)
	{
		const SceneSettings defaults{};
		bool changed = false;

		changed = fix(s.Brightness, defaults.Brightness, 0.0f, 64.0f) || changed;
		changed = fix(s.AmbientIntensity, defaults.AmbientIntensity, 0.0f, 16.0f) || changed;
		changed = fix(s.ExposureEv, defaults.ExposureEv, -10.0f, 10.0f) || changed;
		changed = fix(s.HemisphereSky, defaults.HemisphereSky, 0.0f, 64.0f) || changed;
		changed = fix(s.HemisphereGround, defaults.HemisphereGround, 0.0f, 64.0f) || changed;
		changed = fix(s.BackgroundColor, defaults.BackgroundColor, 0.0f, 64.0f) || changed;

		if (!std::isfinite(s.RotationDegrees))
		{
			s.RotationDegrees = 0.0f;
			changed = true;
		}
		else if (s.RotationDegrees < 0.0f || s.RotationDegrees >= 360.0f)
		{
			s.RotationDegrees = std::fmod(s.RotationDegrees, 360.0f);
			if (s.RotationDegrees < 0.0f)
			{
				s.RotationDegrees += 360.0f;
			}
			changed = true;
		}

		AtmosphereParameters& a = s.Atmosphere;
		const AtmosphereParameters& d = defaults.Atmosphere;
		changed = fix(a.RayleighScale, d.RayleighScale, 0.0f, 10.0f) || changed;
		changed = fix(a.MieScale, d.MieScale, 0.0f, 10.0f) || changed;
		changed = fix(a.OzoneScale, d.OzoneScale, 0.0f, 10.0f) || changed;
		changed = fix(a.MieAnisotropy, d.MieAnisotropy, 0.0f, 0.99f) || changed;
		changed = fix(a.MultipleScattering, d.MultipleScattering, 0.0f, 1.0f) || changed;
		changed = fix(a.SunIlluminance, d.SunIlluminance, 0.0f, 64.0f) || changed;
		changed = fix(a.SkyBrightness, d.SkyBrightness, 0.0f, 64.0f) || changed;
		changed = fix(a.SunDiscBoost, d.SunDiscBoost, 0.0f, 10000.0f) || changed;
		changed = fix(a.GroundAlbedo, d.GroundAlbedo, 0.0f, 1.0f) || changed;

		if (static_cast<u32>(s.Source) > static_cast<u32>(EnvironmentSource::Atmosphere))
		{
			s.Source = EnvironmentSource::None;
			changed = true;
		}
		if (static_cast<u32>(s.Background) > static_cast<u32>(BackgroundMode::SolidColor))
		{
			s.Background = BackgroundMode::Sky;
			changed = true;
		}

		if (changed)
		{
			WI_LOG(Warning, "Scene", "scene settings had values that cannot be used: they were corrected");
		}
		return s;
	}
}
