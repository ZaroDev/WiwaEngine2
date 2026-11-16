#pragma once

#include <array>
#include <vector>

#include <assets/asset_guid.h>
#include <physics/physics_types.h>
#include <math/math.h>

// How a scene as a whole is lit and shown: its sky, ambient light, background and exposure. Saved with the scene
// (SceneData::Settings), owned by Scene, and sent to the render thread by the SceneExtractor. Quality choices (shadow
// resolution, vsync...) are not here: they belong to the project (RenderQuality).
namespace wiwa
{
	// Where the light around the scene comes from. Only append: the values are saved.
	enum class EnvironmentSource : u32
	{
		None,       // no environment: the hemisphere ambient below
		File,       // an Environment asset (a cooked .hdr)
		Atmosphere, // the procedural atmosphere, with the scene's first directional light as the sun
	};

	// The names the scene file gives the values of an enum; their number is the number of values, so a value appended to
	// the enum must be named here (the static_assert says so) and is then accepted by every reader and writer.
	inline constexpr std::array<const char*, 3> ENVIRONMENT_SOURCE_NAMES{ "none", "file", "atmosphere" };
	static_assert(ENVIRONMENT_SOURCE_NAMES.size() == static_cast<size_t>(EnvironmentSource::Atmosphere) + 1, "name the new EnvironmentSource");

	// Only append: the values are saved.
	enum class BackgroundMode : u32
	{
		Sky,        // the environment or atmosphere is drawn behind the scene
		SolidColor, // BackgroundColor
	};

	inline constexpr std::array<const char*, 2> BACKGROUND_MODE_NAMES{ "sky", "color" };
	static_assert(BACKGROUND_MODE_NAMES.size() == static_cast<size_t>(BackgroundMode::SolidColor) + 1, "name the new BackgroundMode");

	struct AtmosphereParameters
	{
		float RayleighScale = 1.0f;
		float MieScale = 1.0f;
		float OzoneScale = 1.0f;
		float MieAnisotropy = 0.8f;
		float MultipleScattering = 0.6f;
		float SunIlluminance = 3.4f;
		float SkyBrightness = 3.5f;
		float SunDiscBoost = 120.0f;
		glm::vec3 GroundAlbedo{ 0.05f, 0.08f, 0.1f };

		bool operator==(const AtmosphereParameters&) const = default;
	};

	// Only append: the values are saved.
	enum class TonemapOperator : u32
	{
		ACES,
		Reinhard,
		Neutral,
		None,
	};

	inline constexpr std::array<const char*, 4> TONEMAP_OPERATOR_NAMES{ "aces", "reinhard", "neutral", "none" };
	static_assert(TONEMAP_OPERATOR_NAMES.size() == static_cast<size_t>(TonemapOperator::None) + 1, "name the new TonemapOperator");

	// Automatic exposure (eye adaptation). Off: SceneSettings::ExposureEv applies.
	struct ExposureSettings
	{
		bool Auto = false;
		float CompensationEv = 0.0f; // stops added to what the automatic exposure chose
		float Key = 0.3f;            // the brightness the average of the scene is brought to (before tone mapping)
		float MinEv = -4.0f;         // the exposure is kept within 2^MinEv .. 2^MaxEv
		float MaxEv = 4.0f;
		float AdaptSpeed = 1.5f;     // how fast it follows a change of brightness, per second
		float LowPercent = 0.1f;     // the darkest and brightest pixels outside these shares do not count
		float HighPercent = 0.9f;

		bool operator==(const ExposureSettings&) const = default;
	};

	// Off by default, like the other effects: a scene looks as it did until the effect is turned on.
	struct BloomSettings
	{
		bool Enabled = false;
		float Threshold = 1.0f; // luminance above which a pixel starts to glow
		float Knee = 0.5f;      // width of the soft transition below the threshold
		float Intensity = 0.04f;

		bool operator==(const BloomSettings&) const = default;
	};

	// The tone mapping operator and the color grading applied with it. The neutral values change nothing.
	struct TonemapSettings
	{
		TonemapOperator Operator = TonemapOperator::ACES;
		float Contrast = 1.0f;
		float Saturation = 1.0f;
		float Temperature = 0.0f; // -1 cool .. +1 warm
		float Tint = 0.0f;        // -1 green .. +1 magenta
		float Vignette = 0.0f;    // 0 none .. 1 strong darkening towards the corners

		bool operator==(const TonemapSettings&) const = default;
	};

	struct AoSettings
	{
		bool Enabled = false;
		float Radius = 1.0f; // world units: occluders farther than this do not darken a point
		float Power = 1.5f;  // the final contrast: occlusion is raised to this power

		bool operator==(const AoSettings&) const = default;
	};

	// Temporal anti-aliasing: whether it runs is a project choice (RenderQuality::Taa); this is how it looks.
	struct TaaSettings
	{
		float Feedback = 0.9f; // how much of the history is kept at most: higher is smoother, lower reacts faster

		bool operator==(const TaaSettings&) const = default;
	};

	// Smears the image along the motion of every pixel, as a camera shutter open for ExposureTime seconds would.
	struct MotionBlurSettings
	{
		bool Enabled = false;
		float ExposureTime = 0.012f; // seconds: the longer, the longer the streaks
		float Strength = 1.0f;
		float MaxBlurPixels = 32.0f;

		bool operator==(const MotionBlurSettings&) const = default;
	};

	struct SceneSettings
	{
		EnvironmentSource Source = EnvironmentSource::None;
		AssetRef EnvironmentAsset{};      // for File
		float RotationDegrees = 0.0f;     // turns the environment about the vertical axis
		float Brightness = 1.0f;          // scales the environment (what is seen and what it lights)
		float AmbientIntensity = 0.3f;    // scales the ambient light

		// The ambient light when there is no environment to use.
		glm::vec3 HemisphereSky{ 0.5f, 0.6f, 0.8f };
		glm::vec3 HemisphereGround{ 0.15f };

		AtmosphereParameters Atmosphere{};

		BackgroundMode Background = BackgroundMode::Sky;
		glm::vec3 BackgroundColor{ 0.05f, 0.06f, 0.08f };

		float ExposureEv = 0.0f;          // the image is multiplied by 2^ExposureEv

		ExposureSettings Exposure{};
		BloomSettings Bloom{};
		TonemapSettings Tonemap{};
		AoSettings Ao{};
		TaaSettings Taa{};
		MotionBlurSettings MotionBlur{};

		// The step settings of the physics world of this scene (gravity, rate, solver iterations, sleeping).
		PhysicsSettings Physics{};

		// Scene-level scripts (Script assets): they run while the scene is active, in this order, and have no entity.
		std::vector<AssetRef> Scripts{};

		bool operator==(const SceneSettings&) const = default;
	};

	// Every field of SceneSettings, once: in the order of the binary file, with the key of the JSON file, the default and the
	// range a value may have. The binary archive (scene_data_io.cpp), the JSON reader and writer (scene_json.cpp), the sanitizer
	// (scene_settings.cpp) and the ranges the panels offer (settings_range) are visitors of this one list: a field is added here
	// (and to the struct), nowhere else. (The JSON file has `scripts` just before `exposure` instead of at its place here.)
	//
	// A visitor has: choice(key, enum, default, names), asset(key, ref), angle(key, float, default), number(key, float, default,
	// min, max), vec3(key, vec3, default, min, max) (min and max apply to each component), flag(key, bool, default), count(key, u32,
	// default, min, max), scripts(key, list) and group(key, fn), which calls fn for the fields of an object of the JSON file.
	// S is SceneSettings, or const SceneSettings for a visitor that only reads.
	template<typename S, typename V>
	void visit_scene_settings(S& s, const SceneSettings& d, V&& v)
	{
		v.choice("source", s.Source, d.Source, ENVIRONMENT_SOURCE_NAMES);
		v.asset("environment", s.EnvironmentAsset);
		v.angle("rotation", s.RotationDegrees, d.RotationDegrees); // 0..360, wrapped rather than clamped
		v.number("brightness", s.Brightness, d.Brightness, 0.0f, 64.0f);
		v.number("ambient", s.AmbientIntensity, d.AmbientIntensity, 0.0f, 16.0f);
		v.vec3("hemisphereSky", s.HemisphereSky, d.HemisphereSky, 0.0f, 64.0f);
		v.vec3("hemisphereGround", s.HemisphereGround, d.HemisphereGround, 0.0f, 64.0f);
		v.group("atmosphere", [&]()
			{
				auto& a = s.Atmosphere;
				const AtmosphereParameters& da = d.Atmosphere;
				v.number("rayleigh", a.RayleighScale, da.RayleighScale, 0.0f, 10.0f);
				v.number("mie", a.MieScale, da.MieScale, 0.0f, 10.0f);
				v.number("ozone", a.OzoneScale, da.OzoneScale, 0.0f, 10.0f);
				v.number("mieAnisotropy", a.MieAnisotropy, da.MieAnisotropy, 0.0f, 0.99f);
				v.number("multipleScattering", a.MultipleScattering, da.MultipleScattering, 0.0f, 1.0f);
				v.number("sunIlluminance", a.SunIlluminance, da.SunIlluminance, 0.0f, 64.0f);
				v.number("skyBrightness", a.SkyBrightness, da.SkyBrightness, 0.0f, 64.0f);
				v.number("sunDiscBoost", a.SunDiscBoost, da.SunDiscBoost, 0.0f, 10000.0f);
				v.vec3("groundAlbedo", a.GroundAlbedo, da.GroundAlbedo, 0.0f, 1.0f);
			});
		v.choice("background", s.Background, d.Background, BACKGROUND_MODE_NAMES);
		v.vec3("backgroundColor", s.BackgroundColor, d.BackgroundColor, 0.0f, 64.0f);
		v.number("exposureEv", s.ExposureEv, d.ExposureEv, -10.0f, 10.0f);
		v.group("exposure", [&]()
			{
				auto& e = s.Exposure;
				const ExposureSettings& de = d.Exposure;
				v.flag("auto", e.Auto, de.Auto);
				v.number("compensation", e.CompensationEv, de.CompensationEv, -6.0f, 6.0f);
				v.number("key", e.Key, de.Key, 0.02f, 2.0f);
				v.number("minEv", e.MinEv, de.MinEv, -10.0f, 10.0f);
				v.number("maxEv", e.MaxEv, de.MaxEv, -10.0f, 10.0f); // sanitized() also keeps it above MinEv
				v.number("adaptSpeed", e.AdaptSpeed, de.AdaptSpeed, 0.0f, 20.0f);
				v.number("lowPercent", e.LowPercent, de.LowPercent, 0.0f, 0.9f);
				v.number("highPercent", e.HighPercent, de.HighPercent, 0.0f, 1.0f); // sanitized() also keeps it above LowPercent + 0.05
			});
		v.group("bloom", [&]()
			{
				auto& b = s.Bloom;
				const BloomSettings& db = d.Bloom;
				v.flag("enabled", b.Enabled, db.Enabled);
				v.number("threshold", b.Threshold, db.Threshold, 0.0f, 4.0f);
				v.number("knee", b.Knee, db.Knee, 0.0f, 1.0f);
				v.number("intensity", b.Intensity, db.Intensity, 0.0f, 0.3f);
			});
		v.group("tonemap", [&]()
			{
				auto& t = s.Tonemap;
				const TonemapSettings& dt = d.Tonemap;
				v.choice("operator", t.Operator, dt.Operator, TONEMAP_OPERATOR_NAMES);
				v.number("contrast", t.Contrast, dt.Contrast, 0.2f, 2.5f);
				v.number("saturation", t.Saturation, dt.Saturation, 0.0f, 2.5f);
				v.number("temperature", t.Temperature, dt.Temperature, -1.0f, 1.0f);
				v.number("tint", t.Tint, dt.Tint, -1.0f, 1.0f);
				v.number("vignette", t.Vignette, dt.Vignette, 0.0f, 1.0f);
			});
		v.group("ao", [&]()
			{
				auto& ao = s.Ao;
				const AoSettings& dao = d.Ao;
				v.flag("enabled", ao.Enabled, dao.Enabled);
				v.number("radius", ao.Radius, dao.Radius, 0.01f, 10.0f);
				v.number("power", ao.Power, dao.Power, 0.1f, 8.0f);
			});
		v.group("taa", [&]() { v.number("feedback", s.Taa.Feedback, d.Taa.Feedback, 0.8f, 0.98f); });
		v.group("motionBlur", [&]()
			{
				auto& mb = s.MotionBlur;
				const MotionBlurSettings& dmb = d.MotionBlur;
				v.flag("enabled", mb.Enabled, dmb.Enabled);
				v.number("exposureTime", mb.ExposureTime, dmb.ExposureTime, 0.0005f, 0.1f);
				v.number("strength", mb.Strength, dmb.Strength, 0.0f, 4.0f);
				v.number("maxBlurPixels", mb.MaxBlurPixels, dmb.MaxBlurPixels, 1.0f, 100.0f);
			});
		v.scripts("scripts", s.Scripts);
		v.group("physics", [&]()
			{
				auto& ph = s.Physics;
				const PhysicsSettings& dph = d.Physics;
				v.vec3("gravity", ph.Gravity, dph.Gravity, -1000.0f, 1000.0f);
				v.count("fixedRateHz", ph.FixedRateHz, dph.FixedRateHz, 10, 240);
				v.count("maxStepsPerWake", ph.MaxStepsPerWake, dph.MaxStepsPerWake, 1, 16);
				v.count("velocityIterations", ph.VelocityIterations, dph.VelocityIterations, 1, 64);
				v.count("positionIterations", ph.PositionIterations, dph.PositionIterations, 1, 64);
				v.number("sleepThreshold", ph.SleepThreshold, dph.SleepThreshold, 0.0f, 10.0f);
			});
	}

	// The range the sanitizer allows the numeric field of `settings` that `field` is (a member of `settings` itself): what the
	// panels offer, so that a slider never reaches a value the sanitizer would change.
	struct SettingRange
	{
		float Min = 0.0f;
		float Max = 0.0f;
	};
	SettingRange settings_range(const SceneSettings& settings, const float& field);
	SettingRange settings_range(const SceneSettings& settings, const u32& field);

	// The settings with every value made usable: finite numbers inside sane ranges, the rotation wrapped to 0..360, enums that
	// name something. A value that was changed is logged once per call.
	SceneSettings sanitized(SceneSettings settings);
}
