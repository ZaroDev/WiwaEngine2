#pragma once

#include <assets/asset_guid.h>
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

		bool operator==(const SceneSettings&) const = default;
	};

	// The settings with every value made usable: finite numbers inside sane ranges, the rotation wrapped to 0..360, enums that
	// name something. A value that was changed is logged once per call.
	SceneSettings sanitized(SceneSettings settings);
}
