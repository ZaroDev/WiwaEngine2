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
	}
}
