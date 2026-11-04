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

		bool fix_count(u32& value, u32 low, u32 high)
		{
			const u32 before = value;
			value = std::clamp(value, low, high);
			return value != before;
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

		ExposureSettings& e = s.Exposure;
		const ExposureSettings& de = defaults.Exposure;
		changed = fix(e.CompensationEv, de.CompensationEv, -6.0f, 6.0f) || changed;
		changed = fix(e.Key, de.Key, 0.02f, 2.0f) || changed;
		changed = fix(e.MinEv, de.MinEv, -10.0f, 10.0f) || changed;
		changed = fix(e.MaxEv, de.MaxEv, e.MinEv, 10.0f) || changed;
		changed = fix(e.AdaptSpeed, de.AdaptSpeed, 0.0f, 20.0f) || changed;
		changed = fix(e.LowPercent, de.LowPercent, 0.0f, 0.9f) || changed;
		changed = fix(e.HighPercent, de.HighPercent, e.LowPercent + 0.05f, 1.0f) || changed;

		BloomSettings& b = s.Bloom;
		const BloomSettings& db = defaults.Bloom;
		changed = fix(b.Threshold, db.Threshold, 0.0f, 4.0f) || changed;
		changed = fix(b.Knee, db.Knee, 0.0f, 1.0f) || changed;
		changed = fix(b.Intensity, db.Intensity, 0.0f, 0.3f) || changed;

		TonemapSettings& t = s.Tonemap;
		const TonemapSettings& dt = defaults.Tonemap;
		changed = fix(t.Contrast, dt.Contrast, 0.2f, 2.5f) || changed;
		changed = fix(t.Saturation, dt.Saturation, 0.0f, 2.5f) || changed;
		changed = fix(t.Temperature, dt.Temperature, -1.0f, 1.0f) || changed;
		changed = fix(t.Tint, dt.Tint, -1.0f, 1.0f) || changed;
		changed = fix(t.Vignette, dt.Vignette, 0.0f, 1.0f) || changed;
		if (static_cast<u32>(t.Operator) > static_cast<u32>(TonemapOperator::None))
		{
			t.Operator = TonemapOperator::ACES;
			changed = true;
		}

		AoSettings& ao = s.Ao;
		const AoSettings& dao = defaults.Ao;
		changed = fix(ao.Radius, dao.Radius, 0.01f, 10.0f) || changed;
		changed = fix(ao.Power, dao.Power, 0.1f, 8.0f) || changed;

		changed = fix(s.Taa.Feedback, defaults.Taa.Feedback, 0.8f, 0.98f) || changed;

		MotionBlurSettings& mb = s.MotionBlur;
		const MotionBlurSettings& dmb = defaults.MotionBlur;
		changed = fix(mb.ExposureTime, dmb.ExposureTime, 0.0005f, 0.1f) || changed;
		changed = fix(mb.Strength, dmb.Strength, 0.0f, 4.0f) || changed;
		changed = fix(mb.MaxBlurPixels, dmb.MaxBlurPixels, 1.0f, 100.0f) || changed;

		PhysicsSettings& ph = s.Physics;
		const PhysicsSettings& dph = defaults.Physics;
		changed = fix(ph.Gravity, dph.Gravity, -1000.0f, 1000.0f) || changed;
		changed = fix(ph.SleepThreshold, dph.SleepThreshold, 0.0f, 10.0f) || changed;
		changed = fix_count(ph.FixedRateHz, 10, 240) || changed;
		changed = fix_count(ph.MaxStepsPerWake, 1, 16) || changed;
		changed = fix_count(ph.VelocityIterations, 1, 64) || changed;
		changed = fix_count(ph.PositionIterations, 1, 64) || changed;

		const size_t scripts = s.Scripts.size();
		std::erase_if(s.Scripts, [](const AssetRef& script) { return !script.valid(); });
		changed = s.Scripts.size() != scripts || changed;

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
