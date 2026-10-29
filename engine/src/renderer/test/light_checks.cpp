#include <pch.h>
#include "scene_tester.h"

// Checks of the clustered lights: what a light reaches, and the cluster lists against a CPU reference.
namespace wiwa
{
	namespace
	{
		// A plane facing +Z at the origin, grey, seen from (0, 0, 5).
		struct PlaneScene
		{
			PlaneScene(SceneTester& test)
			{
				test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.3f)));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(3.0f, 3.0f, 0.05f), material)), glm::mat4(1.0f));
			}
		};

		RenderLight point_light(glm::vec3 position, float range, float intensity)
		{
			RenderLight light;
			light.Kind = LightKind::Point;
			light.Position = position;
			light.Range = range;
			light.Intensity = intensity;
			return light;
		}

		float squared_distance_to_box(glm::vec3 point, glm::vec3 boxMin, glm::vec3 boxMax)
		{
			const glm::vec3 delta = point - glm::clamp(point, boxMin, boxMax);
			return glm::dot(delta, delta);
		}

		// How many of the lights reach each cluster, by the same box test the compute shader makes; rangeScale widens or narrows the lights.
		std::vector<u32> reference_counts(const RenderCamera& camera, const std::vector<RenderLight>& lights, float rangeScale)
		{
			std::vector<u32> counts(CLUSTER_COUNT, 0);
			const float ratio = camera.Far / camera.Near;
			for (u32 z = 0; z < CLUSTERS_Z; z++)
			{
				const float sliceNear = camera.Near * std::pow(ratio, static_cast<float>(z) / CLUSTERS_Z);
				const float sliceFar = camera.Near * std::pow(ratio, static_cast<float>(z + 1) / CLUSTERS_Z);
				for (u32 y = 0; y < CLUSTERS_Y; y++)
				{
					for (u32 x = 0; x < CLUSTERS_X; x++)
					{
						const glm::vec2 ndcMin(static_cast<float>(x) / CLUSTERS_X * 2.0f - 1.0f, 1.0f - static_cast<float>(y + 1) / CLUSTERS_Y * 2.0f);
						const glm::vec2 ndcMax(static_cast<float>(x + 1) / CLUSTERS_X * 2.0f - 1.0f, 1.0f - static_cast<float>(y) / CLUSTERS_Y * 2.0f);

						glm::vec3 boxMin(1e30f);
						glm::vec3 boxMax(-1e30f);
						for (u32 corner = 0; corner < 8; corner++)
						{
							const glm::vec2 ndc((corner & 1) ? ndcMax.x : ndcMin.x, (corner & 2) ? ndcMax.y : ndcMin.y);
							const float depth = (corner & 4) ? sliceFar : sliceNear;
							const glm::vec3 point(ndc.x * depth / camera.Projection[0][0], ndc.y * depth / camera.Projection[1][1], -depth);
							boxMin = glm::min(boxMin, point);
							boxMax = glm::max(boxMax, point);
						}

						u32 count = 0;
						for (const RenderLight& light : lights)
						{
							const glm::vec3 viewPosition = glm::vec3(camera.View * glm::vec4(light.Position, 1.0f));
							const float range = light.Range * rangeScale;
							count += squared_distance_to_box(viewPosition, boxMin, boxMax) <= range * range ? 1 : 0;
						}
						counts[(z * CLUSTERS_Y + y) * CLUSTERS_X + x] = std::min(count, MAX_LIGHTS_PER_CLUSTER);
					}
				}
			}
			return counts;
		}
	}

	void register_light_checks()
	{
		register_render_check("point_light_range", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				PlaneScene plane(test);
				context.check(test.render(), "render without a light");
				const std::vector<u8> ambientOnly = test.pixels();
				const glm::ivec2 centerAt = test.to_pixel(glm::vec3(0.0f, 0.0f, 0.05f));
				const glm::ivec2 farAt = test.to_pixel(glm::vec3(2.5f, 0.0f, 0.05f));
				const int ambient = SceneTester::luma(test.pixel(centerAt));

				test.add_light(point_light(glm::vec3(0.0f, 0.0f, 1.0f), 2.0f, 2.0f));
				context.check(test.render(), "render with the light");
				const int lit = SceneTester::luma(test.pixel(centerAt));
				WI_LOG(Info, "RenderTest", "point_light_range ambient {0} lit {1}", ambient, lit);
				context.check(lit > 3 * ambient, "the pixel under the light is much brighter than with ambient light alone");

				const glm::ivec3 farPixel = test.pixel(farAt);
				const glm::ivec3 farAmbient(ambientOnly[(farAt.y * test.size() + farAt.x) * 4], ambientOnly[(farAt.y * test.size() + farAt.x) * 4 + 1],
					ambientOnly[(farAt.y * test.size() + farAt.x) * 4 + 2]);
				context.check(glm::all(glm::lessThanEqual(glm::abs(farPixel - farAmbient), glm::ivec3(1))), "a pixel beyond the light's range is as dark as before");
			});

		register_render_check("spot_cone", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				PlaneScene plane(test);
				context.check(test.render(), "render without a light");
				const std::vector<u8> ambientOnly = test.pixels();
				const glm::ivec2 centerAt = test.to_pixel(glm::vec3(0.0f, 0.0f, 0.05f));
				const glm::ivec2 outsideAt = test.to_pixel(glm::vec3(1.5f, 0.0f, 0.05f));
				const int ambient = SceneTester::luma(test.pixel(centerAt));

				RenderLight spot;
				spot.Kind = LightKind::Spot;
				spot.Position = glm::vec3(0.0f, 0.0f, 2.0f);
				spot.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
				spot.Range = 8.0f;
				spot.Intensity = 3.0f;
				spot.InnerAngle = 10.0f;
				spot.OuterAngle = 20.0f;
				test.add_light(spot);
				context.check(test.render(), "render with the spot light");

				const int lit = SceneTester::luma(test.pixel(centerAt));
				WI_LOG(Info, "RenderTest", "spot_cone ambient {0} lit {1}", ambient, lit);
				context.check(lit > 2 * ambient, "inside the cone is lit");
				const glm::ivec3 outside = test.pixel(outsideAt);
				const size_t offset = (static_cast<size_t>(outsideAt.y) * test.size() + outsideAt.x) * 4;
				const glm::ivec3 outsideAmbient(ambientOnly[offset], ambientOnly[offset + 1], ambientOnly[offset + 2]);
				context.check(glm::all(glm::lessThanEqual(glm::abs(outside - outsideAmbient), glm::ivec3(1))), "outside the cone is not");
			});

		register_render_check("zero_lights", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				PlaneScene plane(test);
				context.check(test.render(), "render without lights");
				const std::vector<u8> baseline = test.pixels();

				const EntityGuid guid = test.add_light(point_light(glm::vec3(0.0f, 0.0f, 1.0f), 3.0f, 2.0f));
				context.check(test.render(), "render with a light");
				context.check(SceneTester::max_difference(test.pixels(), baseline) > 20, "the light shows");

				SceneDelta removal;
				removal.LightRemovals.push_back(guid);
				test.scene().apply(std::move(removal));
				context.check(test.render(), "render without it again");
				context.check(SceneTester::max_difference(test.pixels(), baseline) == 0, "the stale cluster lists are not used when there are no lights");
			});

		register_render_check("cluster_capacity", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				PlaneScene plane(test);
				u32 seed = 12345;
				auto random = [&seed]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24); };
				for (int i = 0; i < 2000; i++)
				{
					test.add_light(point_light(glm::vec3(random() * 6.0f - 3.0f, random() * 6.0f - 3.0f, random() * 2.0f + 0.5f), 1.5f, 0.05f));
				}

				const u32 errorsBefore = get_error_message_count();
				context.check(test.render(), "render with 2000 lights");
				context.check(test.renderer().get_scene_data().dropped_lights() == 2000 - MAX_LIGHTS, "the lights over the limit are counted");
				context.check(test.renderer().get_scene_data().lights().size() == MAX_LIGHTS, "and the rest are used");
				context.check(get_error_message_count() == errorsBefore, "no GPU error");

				int total = 0;
				for (size_t i = 0; i < test.pixels().size(); i++)
				{
					total += test.pixels()[i];
				}
				context.check(total > 0, "the image is not black");
			});

		register_render_check("light_in_cluster_cpu_reference", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f));

				u32 seed = 777;
				auto random = [&seed]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24); };
				std::vector<RenderLight> lights;
				for (int i = 0; i < 20; i++)
				{
					RenderLight light = point_light(glm::vec3(random() * 10.0f - 5.0f, random() * 6.0f - 3.0f, random() * 14.0f - 6.0f), 1.0f + random() * 3.0f, 1.0f);
					lights.push_back(light);
					test.add_light(light);
				}
				context.check(test.render(2), "render");

				const RenderCamera camera = *test.scene().effective_camera();
				std::vector<u8> bytes;
				context.check(read_buffer_bytes(context.Device, test.renderer().get_scene_data().cluster_counts(), CLUSTER_COUNT * sizeof(u32), bytes), "read the counts back");
				if (bytes.size() != CLUSTER_COUNT * sizeof(u32))
				{
					return;
				}

				const std::vector<u32> narrow = reference_counts(camera, lights, 0.999f);
				const std::vector<u32> wide = reference_counts(camera, lights, 1.001f);
				const u32* gpu = reinterpret_cast<const u32*>(bytes.data());
				u32 mismatches = 0;
				u32 nonEmpty = 0;
				for (u32 cluster = 0; cluster < CLUSTER_COUNT; cluster++)
				{
					mismatches += (gpu[cluster] < narrow[cluster] || gpu[cluster] > wide[cluster]) ? 1 : 0;
					nonEmpty += gpu[cluster] > 0 ? 1 : 0;
				}
				WI_LOG(Info, "RenderTest", "light_in_cluster_cpu_reference: {0} clusters hold lights, {1} differ from the CPU", nonEmpty, mismatches);
				context.check(nonEmpty > 20, "lights reach clusters");
				context.check(mismatches == 0, "every cluster's count equals the CPU's (within the range tolerance)");
			});
	}
}
