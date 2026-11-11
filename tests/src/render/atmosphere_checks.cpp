#include <pch.h>
#include "scene_tester.h"

#include <renderer/environment/atmosphere_builder.h>
#include <renderer/environment/environment_cache.h>

// Checks of the procedural atmosphere: the math, the builder thread, the cache and the sky drawn from it.
namespace wiwa
{
	namespace
	{
		AtmosphereDescription sun_at(float elevationDegrees, float azimuthDegrees = 0.0f)
		{
			const float e = glm::radians(elevationDegrees);
			const float a = glm::radians(azimuthDegrees);
			return make_atmosphere_description(AtmosphereParameters{}, glm::vec3(std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a)));
		}

		bool finite(const glm::vec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		// The scene's sun (a directional light travelling away from the sun), for the atmosphere to follow.
		void atmosphere_scene(SceneTester& test, glm::vec3 towardsTheSun)
		{
			SceneSettings settings;
			settings.Source = EnvironmentSource::Atmosphere;
			test.scene().set_settings(settings);
			if (glm::length(towardsTheSun) > 0.0f)
			{
				test.add_light(test.sun(-towardsTheSun, 1.0f, false));
			}
		}
	}

	void register_atmosphere_checks()
	{
		register_render_check("atmosphere_cpu", [](RenderCheckContext& context)
			{
				const AtmosphereDescription desc = sun_at(30.0f);
				const glm::vec3 towards = atmosphere_radiance(desc, glm::normalize(desc.SunDirection + glm::vec3(0.0f, 0.05f, 0.0f)));
				const glm::vec3 away = atmosphere_radiance(desc, glm::normalize(glm::vec3(-desc.SunDirection.x, desc.SunDirection.y, -desc.SunDirection.z)));
				WI_LOG(Info, "RenderTest", "atmosphere_cpu toward the sun ({0}, {1}, {2}), opposite ({3}, {4}, {5})", towards.x, towards.y, towards.z, away.x, away.y, away.z);
				context.check(towards.x + towards.y + towards.z > 2.0f * (away.x + away.y + away.z), "the sky near the sun is more than twice as bright as the sky opposite it");

				u32 seed = 3;
				auto random = [&seed]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24) * 2.0f - 1.0f; };
				bool ok = true;
				for (int i = 0; i < 200; i++)
				{
					glm::vec3 direction(random(), random(), random());
					if (i == 0) direction = glm::vec3(0, 1, 0);
					if (i == 1) direction = glm::vec3(0, -1, 0);
					if (glm::length(direction) < 1e-3f) continue;
					const glm::vec3 radiance = atmosphere_radiance(desc, glm::normalize(direction));
					ok = ok && finite(radiance) && radiance.x >= 0.0f && radiance.y >= 0.0f && radiance.z >= 0.0f;
				}
				context.check(ok, "the sky is finite and not negative in every direction, up and down included");

				const float nan = std::numeric_limits<float>::quiet_NaN();
				const AtmosphereDescription fromZero = make_atmosphere_description(AtmosphereParameters{}, glm::vec3(0.0f));
				const AtmosphereDescription fromNan = make_atmosphere_description(AtmosphereParameters{}, glm::vec3(nan, 1.0f, 0.0f));
				context.check(finite(fromZero.SunDirection) && std::abs(glm::length(fromZero.SunDirection) - 1.0f) < 1e-4f && fromZero.SunDirection.y > 0.0f, "a zero sun gets a default above the horizon");
				context.check(finite(fromNan.SunDirection) && fromNan.SunDirection == fromZero.SunDirection, "so does a NaN one");
				const AtmosphereDescription straightUp = make_atmosphere_description(AtmosphereParameters{}, glm::vec3(0.0f, 1.0f, 0.0f));
				context.check(finite(atmosphere_radiance(straightUp, glm::vec3(0, 1, 0))), "a sun straight up gives a finite sky");

				context.check(atmosphere_is_close(desc, desc), "the same description is close");
				context.check(!atmosphere_is_close(desc, sun_at(31.0f)), "a sun one degree away is not");
				context.check(atmosphere_is_close(desc, sun_at(30.2f)), "one a fifth of a degree away is");
				AtmosphereDescription denser = desc;
				denser.Sky.RayleighScale = 2.0f;
				context.check(!atmosphere_is_close(desc, denser), "a changed Rayleigh scale is not");
			});

		register_render_check("atmosphere_build_async", [](RenderCheckContext& context)
			{
				JobSystem jobs(1);
				AtmosphereBuilder builder(&jobs);
				builder.request(sun_at(40.0f));
				context.check(builder.building(), "a request starts a build");

				std::optional<AtmosphereBuilder::Build> first;
				for (int i = 0; i < 4000 && !first; i++)
				{
					first = builder.poll();
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				context.check(first.has_value(), "the build finishes within 20 s");
				if (!first)
				{
					return;
				}
				bool finiteSh = true;
				for (const glm::vec4& coefficient : first->Data.SH)
				{
					finiteSh = finiteSh && finite(glm::vec3(coefficient));
				}
				context.check(first->Data.FaceSize == 64 && finiteSh && first->Data.Mips.size() == ENV_MIP_COUNT, "a 64 face environment with finite SH");

				// A request while one runs waits; two waiting requests leave only the last.
				builder.request(sun_at(10.0f));
				builder.request(sun_at(20.0f));
				builder.request(sun_at(60.0f));
				std::vector<AtmosphereBuilder::Build> results;
				for (int i = 0; i < 8000 && builder.building(); i++)
				{
					if (std::optional<AtmosphereBuilder::Build> build = builder.poll())
					{
						results.push_back(std::move(*build));
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				if (std::optional<AtmosphereBuilder::Build> build = builder.poll())
				{
					results.push_back(std::move(*build));
				}
				context.check(results.size() == 2, "the running build finished and the last waiting request ran (" + std::to_string(results.size()) + " results)");
				if (results.size() == 2)
				{
					context.check(glm::dot(results[0].Description.SunDirection, sun_at(10.0f).SunDirection) > 0.999f, "the one that was running is not lost");
					context.check(glm::dot(results[1].Description.SunDirection, sun_at(60.0f).SunDirection) > 0.999f, "and the waiting request that was replaced did not run");
				}
				context.check(builder.builds() == 3 && builder.error().empty(), "three builds in all, no error");
			});

		register_render_check("atmosphere_cache_uploads", [](RenderCheckContext& context)
			{
				UploadQueue queue;
				queue.initialize(context.Device, context.HasCopyQueue, 8 * 1024 * 1024);
				JobSystem jobs(1);
				EnvironmentCache cache(context.Device, context.World.get_renderer().get_assets(), &queue, &jobs);

				AtmosphereDescription desc = sun_at(45.0f);
				auto frame = [&](const AtmosphereDescription* wanted)
				{
					queue.begin_frame();
					cache.set_atmosphere_wanted(wanted);
					cache.update();
					queue.submit();
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				};
				for (int i = 0; i < 4000 && cache.atmosphere() == nullptr; i++)
				{
					frame(&desc);
				}
				context.check(cache.atmosphere() != nullptr, "the atmosphere is uploaded within 20 s");
				const GpuEnvironment* first = cache.atmosphere();
				if (first == nullptr)
				{
					return;
				}
				const std::array<glm::vec4, 9> firstSh = first->SH;
				context.check(cache.atmosphere_status() == "ready", "the status says ready: " + cache.atmosphere_status());

				desc = sun_at(55.0f); // ten degrees: a new build
				frame(&desc);
				context.check(cache.atmosphere() != nullptr && cache.atmosphere_status() != "ready", "the old environment is in use while the new one builds");
				for (int i = 0; i < 4000 && cache.get_stats().AtmosphereBuilds < 2; i++)
				{
					frame(&desc);
				}
				for (int i = 0; i < 10; i++)
				{
					frame(&desc);
				}
				context.check(cache.get_stats().AtmosphereBuilds == 2, "a second build ran");
				context.check(cache.atmosphere() != nullptr && cache.atmosphere()->SH != firstSh, "and its light replaced the first");

				frame(&desc);
				const u32 builds = cache.get_stats().AtmosphereBuilds;
				frame(&desc);
				context.check(cache.get_stats().AtmosphereBuilds == builds && !cache.atmosphere_builder().building(), "a sun that did not move starts no build");
			});

		register_render_check("sky_atmosphere_renders", [](RenderCheckContext& context)
			{
				auto view = [&](glm::vec3 sunDirection, glm::vec3 look)
				{
					SceneTester test(context);
					atmosphere_scene(test, glm::normalize(sunDirection));
					test.set_camera(glm::vec3(0.0f), look);
					test.render(2);
					return test.pixel(32, 32);
				};
				const glm::ivec3 zenith = view(glm::vec3(0.3f, 0.9f, 0.2f), glm::vec3(0.01f, 1.0f, 0.0f));
				WI_LOG(Info, "RenderTest", "sky_atmosphere noon zenith ({0}, {1}, {2})", zenith.r, zenith.g, zenith.b);
				context.check(zenith.b > zenith.r && zenith.b > 40, "the sky overhead with the sun high is blue");

				const glm::ivec3 sunset = view(glm::vec3(1.0f, 0.06f, 0.0f), glm::vec3(1.0f, 0.12f, 0.0f));
				const glm::ivec3 sunsetZenith = view(glm::vec3(1.0f, 0.06f, 0.0f), glm::vec3(0.01f, 1.0f, 0.0f));
				WI_LOG(Info, "RenderTest", "sky_atmosphere sunset toward ({0}, {1}, {2}) zenith ({3}, {4}, {5})", sunset.r, sunset.g, sunset.b, sunsetZenith.r, sunsetZenith.g, sunsetZenith.b);
				const float towardsRatio = static_cast<float>(sunset.r + 1) / static_cast<float>(sunset.b + 1);
				const float zenithRatio = static_cast<float>(sunsetZenith.r + 1) / static_cast<float>(sunsetZenith.b + 1);
				context.check(towardsRatio > 2.0f * zenithRatio, "toward a low sun the sky is much redder than overhead");
			});

		register_render_check("atmosphere_lights_scene", [](RenderCheckContext& context)
			{
				auto top_luma = [&](glm::vec3 sunDirection)
				{
					SceneTester test(context);
					atmosphere_scene(test, glm::normalize(sunDirection));
					SceneSettings settings = test.scene().settings();
					settings.AmbientIntensity = 1.0f;
					test.scene().set_settings(settings);
					test.set_camera(glm::vec3(3.0f, 4.0f, 5.0f), glm::vec3(0.0f));
					test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f), 1.0f, 0.0f)))), glm::mat4(1.0f));
					test.render();
					return SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(0.0f, 1.0f, 0.0f))));
				};
				const int day = top_luma(glm::vec3(0.3f, 0.9f, 0.2f));
				const int night = top_luma(glm::vec3(0.3f, -0.9f, 0.2f));
				WI_LOG(Info, "RenderTest", "atmosphere_lights_scene top face: sun high {0}, sun below the horizon {1}", day, night);
				context.check(day > night + 15, "the top of a cube is lit by a bright sky and left dark when the sun is below the horizon");
			});

		register_render_check("atmosphere_no_sun_default", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				atmosphere_scene(test, glm::vec3(0.0f)); // no directional light at all
				test.set_camera(glm::vec3(0.0f), glm::vec3(0.0f, 0.2f, 1.0f));
				context.check(test.render(2), "render");
				const int luma = SceneTester::luma(test.pixel(32, 32));
				context.check(luma > 5 && luma < 250, "a defined sun gives a finite sky");
			});
	}
}
