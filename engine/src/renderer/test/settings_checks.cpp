#include <pch.h>
#include "render_test.h"

#include <filesystem>
#include <fstream>

#include <assets/wasset_io.h>
#include <scene/scene.h>

#include "../render_world.h"
#include "../renderer.h"
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
			s.Exposure = { true, 0.5f, 0.4f, -3.0f, 5.0f, 2.0f, 0.2f, 0.8f };
			s.Bloom = { true, 2.0f, 0.25f, 0.1f };
			s.Tonemap = { TonemapOperator::Neutral, 1.2f, 0.8f, 0.3f, -0.3f, 0.5f };
			s.Ao = { true, 2.5f, 3.0f };
			return s;
		}
	}

	void register_settings_checks()
	{
		register_render_check("quality_roundtrip", [](RenderCheckContext& context)
			{
				RenderQuality quality;
				quality.ShadowDistance = 123.0f;
				quality.ShadowCascades = 3;
				quality.ShadowSplitLambda = 0.5f;
				quality.ShadowDepthBias = 0.002f;
				quality.ShadowNormalBias = 2.5f;
				quality.SpotShadowBias = 0.001f;
				quality.Vsync = false;
				quality.CopyQueue = false;
				const std::filesystem::path path = "Library/quality_roundtrip.json";
				std::filesystem::create_directories("Library");
				context.check(save_render_quality(path, quality), "save");
				std::string error;
				const std::optional<RenderQuality> back = load_render_quality(path, error);
				context.check(back && *back == quality && error.empty(), "load gives what was saved");
				std::filesystem::remove(path);
			});

		register_render_check("quality_post_fields", [](RenderCheckContext& context)
			{
				RenderQuality quality;
				quality.BloomMips = 3;
				quality.AoSlices = 4;
				quality.AoSteps = 12;
				quality.Fxaa = false;
				const std::filesystem::path path = "Library/quality_post.json";
				std::filesystem::create_directories("Library");
				context.check(save_render_quality(path, quality), "save");
				std::string error;
				const std::optional<RenderQuality> back = load_render_quality(path, error);
				context.check(back && *back == quality, "the post fields survive");

				{
					std::ofstream file(path, std::ios::trunc);
					file << "{\"vsync\": false}";
				}
				const std::optional<RenderQuality> legacy = load_render_quality(path, error);
				context.check(legacy && legacy->BloomMips == 6 && legacy->AoSlices == 2 && legacy->AoSteps == 8 && legacy->Fxaa && !legacy->Vsync, "an old file keeps the defaults of the new keys");

				RenderQuality wild;
				wild.BloomMips = 99;
				wild.AoSlices = 0;
				wild.AoSteps = 0;
				const RenderQuality fixed = sanitized(wild);
				context.check(fixed.BloomMips == 6 && fixed.AoSlices == 1 && fixed.AoSteps == 4, "values out of range are clamped");
				std::filesystem::remove(path);
			});

		register_render_check("settings_post_sanitized", [](RenderCheckContext& context)
			{
				const float nan = std::numeric_limits<float>::quiet_NaN();
				SceneSettings bad;
				bad.Bloom.Threshold = nan;
				bad.Exposure.MinEv = 5.0f;
				bad.Exposure.MaxEv = -5.0f;
				bad.Exposure.HighPercent = 0.0f;
				bad.Tonemap.Operator = static_cast<TonemapOperator>(99);
				bad.Tonemap.Contrast = 100.0f;
				bad.Ao.Radius = -1.0f;
				const SceneSettings fixed = sanitized(bad);
				context.check(fixed.Bloom.Threshold == 1.0f, "NaN threshold becomes the default");
				context.check(fixed.Exposure.MaxEv >= fixed.Exposure.MinEv, "max EV is not below min EV");
				context.check(fixed.Exposure.HighPercent > fixed.Exposure.LowPercent, "the percentile range is ordered");
				context.check(fixed.Tonemap.Operator == TonemapOperator::ACES && fixed.Tonemap.Contrast == 2.5f, "unknown operator and contrast");
				context.check(fixed.Ao.Radius == 0.01f, "negative radius is clamped");
			});

		register_render_check("quality_missing_file", [](RenderCheckContext& context)
			{
				std::string error = "stale";
				const std::optional<RenderQuality> loaded = load_render_quality("Library/no_such_quality.json", error);
				context.check(!loaded && error.empty(), "a missing file is not an error: the defaults are used");
			});

		register_render_check("quality_garbage_file", [](RenderCheckContext& context)
			{
				std::filesystem::create_directories("Library");
				const std::filesystem::path path = "Library/quality_garbage.json";
				{
					std::ofstream file(path);
					file << "{nope";
				}
				std::string error;
				context.check(!load_render_quality(path, error) && !error.empty(), "text that is not JSON gives an error");
				{
					std::ofstream file(path, std::ios::trunc);
					file << "{\"shadowDistance\": 1e9, \"shadowCascades\": 17, \"somethingNew\": true}";
				}
				const std::optional<RenderQuality> loaded = load_render_quality(path, error);
				context.check(loaded && loaded->ShadowDistance == 1000.0f && loaded->ShadowCascades == 4, "absurd values are clamped and unknown keys ignored");
				{
					std::ofstream file(path, std::ios::trunc);
					file << "{\"vsync\": false}";
				}
				const std::optional<RenderQuality> partial = load_render_quality(path, error);
				const RenderQuality defaults;
				context.check(partial && !partial->Vsync && partial->ShadowDistance == defaults.ShadowDistance && partial->CopyQueue == defaults.CopyQueue, "a missing key keeps its default");
				std::filesystem::remove(path);
			});

		register_render_check("quality_applies", [](RenderCheckContext& context)
			{
				Renderer& renderer = context.World.get_renderer();
				const RenderQuality before = renderer.get_quality();
				RenderQuality changed = before;
				changed.ShadowDistance = 6.0f;
				renderer.set_quality(changed);
				context.check(renderer.get_scene_data().quality().ShadowDistance == 6.0f && renderer.get_quality().ShadowDistance == 6.0f, "the shadow reach reaches the scene data");
				changed.BloomMips = 3;
				changed.Fxaa = false;
				renderer.set_quality(changed);
				context.check(renderer.get_scene_data().quality().BloomMips == 3 && !renderer.get_scene_data().quality().Fxaa, "the post quality reaches the scene data");
				changed.ShadowCascades = 99;
				renderer.set_quality(changed);
				context.check(renderer.get_quality().ShadowCascades == 4, "a value out of range is clamped on the way in");
				renderer.set_quality(before);
			});

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
