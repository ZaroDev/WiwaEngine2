#include <pch.h>
#include "render_test.h"

#include <assets/wasset_io.h>
#include <scene/scene.h>

#include "../scene/scene_extractor.h"

// Checks of the scene settings (and later the quality settings): values, formats and delivery. Plain logic, no GPU.
namespace wiwa
{
	namespace
	{
		SceneSettings non_default_settings()
		{
			SceneSettings s;
			s.Source = EnvironmentSource::File;
			s.EnvironmentAsset = AssetRef{ AssetGuid{ 11, 22 }, 3 };
			s.RotationDegrees = 123.0f;
			s.Brightness = 2.5f;
			s.AmbientIntensity = 0.7f;
			s.HemisphereSky = glm::vec3(0.1f, 0.2f, 0.3f);
			s.HemisphereGround = glm::vec3(0.4f, 0.5f, 0.6f);
			s.Atmosphere.RayleighScale = 1.5f;
			s.Atmosphere.MieScale = 0.5f;
			s.Atmosphere.OzoneScale = 2.0f;
			s.Atmosphere.MieAnisotropy = 0.7f;
			s.Atmosphere.MultipleScattering = 0.3f;
			s.Atmosphere.SunIlluminance = 5.0f;
			s.Atmosphere.SkyBrightness = 2.0f;
			s.Atmosphere.SunDiscBoost = 50.0f;
			s.Atmosphere.GroundAlbedo = glm::vec3(0.2f, 0.3f, 0.4f);
			s.Background = BackgroundMode::SolidColor;
			s.BackgroundColor = glm::vec3(0.9f, 0.8f, 0.7f);
			s.ExposureEv = -1.5f;
			return s;
		}
	}

	void register_settings_checks()
	{
		register_render_check("settings_defaults_equal_old_look", [](RenderCheckContext& context)
			{
				const SceneSettings s;
				context.check(s.HemisphereSky == glm::vec3(0.5f, 0.6f, 0.8f) && s.HemisphereGround == glm::vec3(0.15f), "the hemisphere colors");
				context.check(s.AmbientIntensity == 0.3f && s.BackgroundColor == glm::vec3(0.05f, 0.06f, 0.08f) && s.ExposureEv == 0.0f, "ambient, background and exposure");
				context.check(s.Source == EnvironmentSource::None && s.Background == BackgroundMode::Sky, "no environment, the sky as background");
			});

		register_render_check("settings_sanitized", [](RenderCheckContext& context)
			{
				const float nan = std::numeric_limits<float>::quiet_NaN();
				const float inf = std::numeric_limits<float>::infinity();
				SceneSettings bad;
				bad.Brightness = nan;
				bad.ExposureEv = 50.0f;
				bad.RotationDegrees = -90.0f;
				bad.AmbientIntensity = -1.0f;
				bad.BackgroundColor = glm::vec3(inf, 0.0f, nan);
				bad.Atmosphere.MieAnisotropy = 5.0f;
				bad.Source = static_cast<EnvironmentSource>(77);
				const SceneSettings fixed = sanitized(bad);
				context.check(fixed.Brightness == 1.0f, "NaN brightness becomes the default");
				context.check(fixed.ExposureEv == 10.0f, "exposure is clamped");
				context.check(std::abs(fixed.RotationDegrees - 270.0f) < 1e-4f, "rotation is wrapped");
				context.check(fixed.AmbientIntensity == 0.0f, "negative ambient is clamped");
				context.check(std::isfinite(fixed.BackgroundColor.x) && std::isfinite(fixed.BackgroundColor.z), "non-finite colors are replaced");
				context.check(fixed.Atmosphere.MieAnisotropy < 1.0f && fixed.Source == EnvironmentSource::None, "anisotropy and unknown enums");

				const SceneSettings good = non_default_settings();
				context.check(sanitized(good) == good, "a good value is untouched");
			});

		register_render_check("settings_binary_roundtrip", [](RenderCheckContext& context)
			{
				SceneData data;
				data.Name = "roundtrip";
				data.Settings = non_default_settings();
				const std::vector<std::byte> bytes = serialize(data);
				const std::optional<SceneData> back = deserialize<SceneData>(bytes);
				context.check(back && back->Settings == data.Settings && back->Name == "roundtrip", "every settings field survives");

				const std::vector<std::byte> cut(bytes.begin(), bytes.end() - 10);
				context.check(!deserialize<SceneData>(cut).has_value(), "a payload cut inside the settings block is refused");

				SceneData empty;
				const std::optional<SceneData> defaults = deserialize<SceneData>(serialize(empty));
				context.check(defaults && defaults->Settings == SceneSettings{}, "default settings round-trip");
			});

		register_render_check("extractor_sends_settings", [](RenderCheckContext& context)
			{
				Ref<Scene> scene = CreateRef<Scene>();
				SceneExtractor extractor;
				SceneDelta first = extractor.extract(scene, 1.0f);
				context.check(first.Reset && first.Settings.has_value() && *first.Settings == SceneSettings{}, "the first delta resets and carries the settings");
				context.check(!extractor.extract(scene, 1.0f).Settings.has_value(), "unchanged settings are not sent again");

				const u64 revision = scene->settings_revision();
				scene->set_settings(scene->settings());
				context.check(scene->settings_revision() == revision, "setting an equal value does not bump the revision");

				scene->set_settings(non_default_settings());
				const SceneDelta changed = extractor.extract(scene, 1.0f);
				context.check(!changed.Reset && changed.Settings.has_value() && *changed.Settings == non_default_settings(), "a change is sent once");
				context.check(!extractor.extract(scene, 1.0f).Settings.has_value(), "and not again");

				Ref<Scene> other = CreateRef<Scene>();
				other->set_settings(non_default_settings());
				const SceneDelta swapped = extractor.extract(other, 1.0f);
				context.check(swapped.Reset && swapped.Settings.has_value(), "another scene resets with its own settings");

				// The render thread's mirror applies them.
				RenderScene mirror;
				SceneDelta delta;
				delta.Settings = non_default_settings();
				mirror.apply(std::move(delta));
				context.check(mirror.settings() == non_default_settings(), "RenderScene holds what it was sent");
			});
	}
}
