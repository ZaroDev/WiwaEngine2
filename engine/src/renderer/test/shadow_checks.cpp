#include <pch.h>
#include "scene_tester.h"

#include "../scene/cascade_math.h"

// Checks of the shadow maps: the cascade math, then shadows in rendered scenes.
namespace wiwa
{
	namespace
	{
		RenderCamera make_camera(glm::vec3 position, glm::vec3 target, float nearPlane = 0.1f, float farPlane = 100.0f)
		{
			RenderCamera camera;
			camera.View = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
			camera.Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, nearPlane, farPlane);
			camera.Position = position;
			camera.Near = nearPlane;
			camera.Far = farPlane;
			return camera;
		}

		// A big plane (top at y = 0.05), a small cube above it, seen from above and in front.
		struct ShadowScene
		{
			explicit ShadowScene(SceneTester& test, glm::vec3 cubePosition = glm::vec3(0.0f, 1.5f, 0.0f))
			{
				test.set_camera(glm::vec3(0.0f, 7.0f, 7.0f), glm::vec3(0.0f));
				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.8f)));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(8.0f, 0.05f, 8.0f), material)), glm::mat4(1.0f));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.5f), material)), glm::translate(glm::mat4(1.0f), cubePosition));
			}
		};

		// Little ambient light, so that a pixel the light does not reach is clearly darker than one it does.
		SceneSettings dim_ambient()
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.08f;
			return settings;
		}

		// Where a ray from `from` along `direction` meets the plane y = 0.05.
		glm::vec3 hit_plane(glm::vec3 from, glm::vec3 direction)
		{
			const float t = (0.05f - from.y) / direction.y;
			return from + direction * t;
		}

		bool corner_inside(const glm::mat4& viewProjection, glm::vec3 point)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			constexpr float slack = 1e-3f;
			return std::abs(ndc.x) <= 1.0f + slack && std::abs(ndc.y) <= 1.0f + slack && ndc.z >= -slack && ndc.z <= 1.0f + slack;
		}
	}

	void register_shadow_checks()
	{
		register_render_check("cascade_math", [](RenderCheckContext& context)
			{
				const RenderCamera camera = make_camera(glm::vec3(3.0f, 2.0f, 8.0f), glm::vec3(0.0f));
				const glm::vec3 direction = glm::normalize(glm::vec3(0.4f, -1.0f, 0.3f));
				const CascadeSet set = compute_cascades(camera, direction, 4, 50.0f, 0.7f);

				bool monotonic = set.SplitDepths[0] > camera.Near;
				for (u32 i = 1; i < 4; i++)
				{
					monotonic = monotonic && set.SplitDepths[i] > set.SplitDepths[i - 1];
				}
				context.check(monotonic, "the splits increase");
				context.check(std::abs(set.SplitDepths[3] - 50.0f) < 1e-4f, "the last split is the shadow distance");

				float previous = camera.Near;
				bool covered = true;
				for (u32 i = 0; i < 4; i++)
				{
					for (const glm::vec3& corner : frustum_slice_corners(camera, previous, set.SplitDepths[i]))
					{
						covered = covered && corner_inside(set.ViewProjection[i], corner);
					}
					previous = set.SplitDepths[i];
				}
				context.check(covered, "every corner of each camera slice is inside its cascade");

				// The world origin stays on a texel corner of every cascade, wherever the camera is.
				bool snapped = true;
				for (const glm::vec3 position : { glm::vec3(3.0f, 2.0f, 8.0f), glm::vec3(3.001f, 2.0f, 8.0f), glm::vec3(3.0f, 2.003f, 7.9993f), glm::vec3(-5.0f, 4.0f, 6.0f) })
				{
					const CascadeSet moved = compute_cascades(make_camera(position, glm::vec3(0.0f)), direction, 4, 50.0f, 0.7f);
					for (u32 i = 0; i < 4; i++)
					{
						const glm::vec4 origin = moved.ViewProjection[i] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
						const glm::vec2 texels = glm::vec2(origin) * (static_cast<float>(CASCADE_MAP_SIZE) * 0.5f);
						snapped = snapped && std::abs(texels.x - std::round(texels.x)) < 0.02f && std::abs(texels.y - std::round(texels.y)) < 0.02f;
					}
				}
				context.check(snapped, "the cascades are texel-snapped");

				const CascadeSet single = compute_cascades(camera, direction, 1, 30.0f, 0.5f);
				context.check(std::abs(single.SplitDepths[0] - 30.0f) < 1e-4f, "one cascade covers the whole distance");
				const CascadeSet degenerate = compute_cascades(make_camera(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 5.0f, -1.0f)), glm::vec3(0.0f, -1.0f, 0.0f), 4, 0.01f, 5.0f);
				bool finite = true;
				for (u32 i = 0; i < 4; i++)
				{
					finite = finite && std::isfinite(degenerate.ViewProjection[i][0][0]) && std::isfinite(degenerate.ViewProjection[i][3][2]) && std::isfinite(degenerate.SplitDepths[i]);
				}
				context.check(finite, "a light straight down and a tiny distance still give finite matrices");
			});

		register_render_check("directional_shadow", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				ShadowScene scene(test);
				test.scene().set_settings(dim_ambient());
				const glm::vec3 direction = glm::normalize(glm::vec3(0.4f, -1.0f, 0.2f));
				const glm::ivec2 shadowAt = test.to_pixel(hit_plane(glm::vec3(0.0f, 1.5f, 0.0f), direction));
				const glm::ivec2 litAt = test.to_pixel(glm::vec3(-4.0f, 0.05f, 2.0f));

				test.add_light(test.sun(direction, 3.0f, true));
				context.check(test.render(), "render with the shadow");
				const int shadowed = SceneTester::luma(test.pixel(shadowAt));
				const int lit = SceneTester::luma(test.pixel(litAt));
				WI_LOG(Info, "RenderTest", "directional_shadow shadowed {0} lit {1}", shadowed, lit);
				context.check(lit > 100 && shadowed * 2 < lit, "the plane in the cube's shadow is less than half as bright as the lit plane");

				// The same scene with a light that casts no shadow.
				SceneTester again(context);
				ShadowScene second(again);
				again.scene().set_settings(dim_ambient());
				again.add_light(again.sun(direction, 3.0f, false));
				context.check(again.render(), "render without the shadow");
				const int unshadowed = SceneTester::luma(again.pixel(shadowAt));
				context.check(unshadowed * 10 >= lit * 9, "without CastsShadows the same pixel is as bright as the lit plane");
			});

		register_render_check("spot_shadow", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				ShadowScene scene(test, glm::vec3(0.0f, 2.5f, 0.0f));
				test.scene().set_settings(dim_ambient());

				RenderLight spot;
				spot.Kind = LightKind::Spot;
				spot.Position = glm::vec3(0.0f, 5.0f, 0.0f);
				spot.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
				spot.Range = 14.0f;
				spot.Intensity = 60.0f;
				spot.InnerAngle = 30.0f;
				spot.OuterAngle = 45.0f;
				spot.CastsShadows = true;
				test.add_light(spot);
				context.check(test.render(), "render with the shadow");

				const glm::ivec2 shadowAt = test.to_pixel(glm::vec3(0.0f, 0.05f, 0.0f));
				const glm::ivec2 litAt = test.to_pixel(glm::vec3(2.0f, 0.05f, 0.0f));
				const int shadowed = SceneTester::luma(test.pixel(shadowAt));
				const int lit = SceneTester::luma(test.pixel(litAt));
				WI_LOG(Info, "RenderTest", "spot_shadow shadowed {0} lit {1}", shadowed, lit);
				context.check(lit > 100 && shadowed * 2 < lit, "under the cube the plane is less than half as bright as beside it");

				SceneTester again(context);
				ShadowScene second(again, glm::vec3(0.0f, 2.5f, 0.0f));
				again.scene().set_settings(dim_ambient());
				spot.CastsShadows = false;
				again.add_light(spot);
				context.check(again.render(), "render without the shadow");
				context.check(SceneTester::luma(again.pixel(shadowAt)) * 10 >= lit * 9, "without CastsShadows it is not darker");
			});

		register_render_check("shadow_no_acne", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(0.0f, 6.0f, 4.0f), glm::vec3(0.0f));
				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.8f)));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(8.0f, 0.05f, 8.0f), material)), glm::mat4(1.0f));
				test.scene().set_settings(dim_ambient());
				test.add_light(test.sun(glm::normalize(glm::vec3(0.8f, -0.6f, 0.3f)), 1.2f, true)); // a low sun: the worst case for acne
				context.check(test.render(), "render a lit plane with shadows on");

				double sum = 0.0;
				double sumSquares = 0.0;
				int count = 0;
				for (int y = 16; y < 48; y++)
				{
					for (int x = 16; x < 48; x++)
					{
						const double value = SceneTester::luma(test.pixel(x, y));
						sum += value;
						sumSquares += value * value;
						count++;
					}
				}
				const double mean = sum / count;
				const double deviation = std::sqrt(std::max(sumSquares / count - mean * mean, 0.0));
				WI_LOG(Info, "RenderTest", "shadow_no_acne mean {0} deviation {1}", mean, deviation);
				context.check(mean > 60.0 && mean < 235.0 && deviation < 2.0, "the lit plane is evenly bright (no shadow acne stripes)");
			});

		register_render_check("shadow_distance", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				ShadowScene scene(test);
				test.scene().set_settings(dim_ambient());
				const glm::vec3 direction = glm::normalize(glm::vec3(0.4f, -1.0f, 0.2f));
				const glm::ivec2 shadowAt = test.to_pixel(hit_plane(glm::vec3(0.0f, 1.5f, 0.0f), direction));
				const glm::ivec2 litAt = test.to_pixel(glm::vec3(-4.0f, 0.05f, 2.0f));
				test.add_light(test.sun(direction, 3.0f, true));
				context.check(test.render(), "render");
				const int shadowedNear = SceneTester::luma(test.pixel(shadowAt));
				const int lit = SceneTester::luma(test.pixel(litAt));

				test.scene().set_settings(dim_ambient());
				RenderQuality quality = test.renderer().get_quality();
				quality.ShadowDistance = 6.0f; // the shadow is about 10 units from the camera
				test.renderer().set_quality(quality);
				context.check(test.render(), "render with a short shadow distance");
				const int shadowedFar = SceneTester::luma(test.pixel(shadowAt));
				WI_LOG(Info, "RenderTest", "shadow_distance inside {0} beyond {1} lit {2}", shadowedNear, shadowedFar, lit);
				context.check(shadowedNear * 2 < lit, "inside the distance the shadow is there");
				context.check(shadowedFar * 10 >= lit * 9, "beyond the distance it is gone");
			});
	}
}
