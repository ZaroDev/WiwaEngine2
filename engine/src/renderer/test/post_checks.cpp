#include <pch.h>
#include "scene_tester.h"

// Checks of the post-processing chain: tone mapping and grading, exposure, bloom, FXAA, ambient occlusion.
namespace wiwa
{
	namespace
	{
		SceneSettings dark()
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.0f;
			settings.BackgroundColor = glm::vec3(0.0f);
			return settings;
		}

		MaterialData emissive_material(glm::vec3 emissive)
		{
			MaterialData material;
			material.BaseColorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			material.EmissiveFactor = emissive;
			return material;
		}

		float srgb_to_linear(int value)
		{
			const float c = static_cast<float>(value) / 255.0f;
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		int linear_to_srgb(float linear)
		{
			linear = std::clamp(linear, 0.0f, 1.0f);
			const float c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
			return static_cast<int>(c * 255.0f + 0.5f);
		}

		// The operators of tonemap.hlsl on a gray value.
		float tonemap_gray(TonemapOperator op, float v)
		{
			switch (op)
			{
			case TonemapOperator::ACES: return std::clamp((v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f), 0.0f, 1.0f);
			case TonemapOperator::Reinhard: return v / (1.0f + v);
			case TonemapOperator::Neutral:
			{
				// below the shoulder the operator only lifts the black level
				const float offset = v < 0.08f ? v - 6.25f * v * v : 0.04f;
				return v - offset;
			}
			default: return std::clamp(v, 0.0f, 1.0f);
			}
		}

		float read_exposure(RenderCheckContext& context, SceneTester& test)
		{
			std::vector<u8> bytes;
			float exposure = -1.0f;
			if (read_buffer_bytes(context.Device, test.renderer().get_viewport().exposure_buffer(), sizeof(float), bytes) && bytes.size() >= sizeof(float))
			{
				std::memcpy(&exposure, bytes.data(), sizeof(float));
			}
			return exposure;
		}

		SceneSettings auto_exposure_settings(float speed = 20.0f)
		{
			SceneSettings settings = dark();
			settings.Exposure.Auto = true;
			settings.Exposure.AdaptSpeed = speed;
			settings.Exposure.MinEv = -8.0f;
			settings.Exposure.MaxEv = 8.0f;
			return settings;
		}

		// Renders an emissive gray cube with the tonemap settings and returns the linear value that was displayed.
		bool render_gray(RenderCheckContext& context, SceneTester& test, const TonemapSettings& tonemap, float exposureEv, int& pixelValue)
		{
			SceneSettings settings = dark();
			settings.Tonemap = tonemap;
			settings.ExposureEv = exposureEv;
			test.scene().set_settings(settings);
			if (!test.render())
			{
				return false;
			}
			const glm::ivec3 p = test.pixel(32, 32);
			pixelValue = p.g;
			return true;
		}
	}

	void register_post_checks()
	{
		register_render_check("tonemap_operators_ramp", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.5f))))), glm::mat4(1.0f));
				TonemapSettings tonemap;
				tonemap.Operator = TonemapOperator::None;
				int none = 0;
				context.check(render_gray(context, test, tonemap, 0.0f, none), "render with no operator");
				const float shown = srgb_to_linear(none);
				context.check(std::abs(shown - 0.5f) < 0.02f, "the emissive cube shows its value without an operator");

				const TonemapOperator operators[] = { TonemapOperator::ACES, TonemapOperator::Reinhard, TonemapOperator::Neutral };
				int seen[3]{};
				for (int i = 0; i < 3; i++)
				{
					tonemap.Operator = operators[i];
					context.check(render_gray(context, test, tonemap, 0.0f, seen[i]), "render an operator");
					const int expected = linear_to_srgb(tonemap_gray(operators[i], shown));
					WI_LOG(Info, "RenderTest", "tonemap_operators_ramp operator {0}: {1} (expected {2})", i, seen[i], expected);
					context.check(std::abs(seen[i] - expected) <= 4, "operator " + std::to_string(i) + " maps gray as its formula says");
				}
				context.check(seen[0] != seen[1] && seen[1] != seen[2] && seen[0] != seen[2], "the operators differ from each other");
			});

		register_render_check("tonemap_grading_neutral", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.4f, 0.3f, 0.2f))))), glm::mat4(1.0f));
				test.scene().set_settings(dark());
				context.check(test.render(), "render with the default grading");
				const glm::ivec3 plain = test.pixel(32, 32);

				SceneSettings graded = dark();
				graded.Tonemap.Saturation = 0.0f;
				test.scene().set_settings(graded);
				context.check(test.render(), "render without saturation");
				const glm::ivec3 gray = test.pixel(32, 32);
				context.check(std::abs(gray.r - gray.g) <= 1 && std::abs(gray.g - gray.b) <= 1, "saturation 0 gives a gray pixel");
				context.check(std::abs(plain.r - plain.b) > 8, "the neutral grading keeps the color");
			});

		register_render_check("exposure_converges_equal", [](RenderCheckContext& context)
			{
				int luma[2]{};
				const float values[2] = { 0.05f, 2.0f };
				for (int i = 0; i < 2; i++)
				{
					SceneTester test(context);
					test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(values[i]))))), glm::mat4(1.0f));
					test.scene().set_settings(auto_exposure_settings());
					context.check(test.render(90), "render with automatic exposure");
					luma[i] = SceneTester::luma(test.pixel(32, 32));
					WI_LOG(Info, "RenderTest", "exposure_converges_equal {0}: luma {1}, exposure {2}", values[i], luma[i], read_exposure(context, test));
				}
				context.check(luma[0] > 20 && std::abs(luma[0] - luma[1]) <= std::max(luma[0], luma[1]) / 10, "a dark and a bright scene end up equally bright");
			});

		register_render_check("exposure_auto_off_ignores_measurement", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.05f))))), glm::mat4(1.0f));
				test.scene().set_settings(auto_exposure_settings());
				context.check(test.render(60), "render with automatic exposure");
				const int automatic = SceneTester::luma(test.pixel(32, 32));

				test.scene().set_settings(dark());
				context.check(test.render(3), "render with manual exposure");
				const int manual = SceneTester::luma(test.pixel(32, 32));
				context.check(manual < automatic - 20, "switching it off gives the manual exposure back (" + std::to_string(manual) + " < " + std::to_string(automatic) + ")");
			});

		register_render_check("exposure_empty_scene_finite", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.scene().set_settings(auto_exposure_settings());
				context.check(test.render(30), "render an empty scene with automatic exposure");
				const float exposure = read_exposure(context, test);
				context.check(std::isfinite(exposure) && exposure >= 0.0f && exposure <= std::exp2(8.0f), "the exposure is finite and inside its bounds");
				const glm::ivec3 corner = test.pixel(2, 2);
				context.check(corner.r == 0 && corner.g == 0 && corner.b == 0, "the empty image stays black");
			});

		register_render_check("exposure_dt_spike", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.5f))))), glm::mat4(1.0f));
				test.scene().set_settings(auto_exposure_settings(1.5f));
				context.check(test.render(60), "render with automatic exposure");
				test.set_delta(5.0f);
				context.check(test.frame() && test.frame(), "render frames that report five seconds");
				const float exposure = read_exposure(context, test);
				context.check(std::isfinite(exposure) && exposure > std::exp2(-8.0f) && exposure < std::exp2(8.0f), "the exposure stays finite and inside its bounds");
			});

		register_render_check("exposure_scene_swap_resets", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(4.0f))))), glm::mat4(1.0f));
				test.scene().set_settings(auto_exposure_settings(0.0f)); // no adaptation: only a jump can move the exposure
				context.check(test.render(10), "render the bright scene");
				const float bright = read_exposure(context, test);

				SceneDelta reset;
				reset.Reset = true;
				reset.Settings = auto_exposure_settings(0.0f);
				test.scene().apply(std::move(reset));
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.05f))))), glm::mat4(1.0f));
				context.check(test.render(10), "render the dark scene");
				const float dark = read_exposure(context, test);
				WI_LOG(Info, "RenderTest", "exposure_scene_swap_resets {0} -> {1}", bright, dark);
				context.check(dark > bright * 4.0f, "another scene starts from its own measurement, not the old exposure");
			});
	}
}
