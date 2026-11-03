#pragma once

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

	// Only append: the values are saved.
	enum class BackgroundMode : u32
	{
		Sky,        // the environment or atmosphere is drawn behind the scene
		SolidColor, // BackgroundColor
	};

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

	// The settings with every value made usable: finite numbers inside sane ranges, the rotation wrapped to 0..360, enums that
	// name something. A value that was changed is logged once per call.
	SceneSettings sanitized(SceneSettings settings);
}
