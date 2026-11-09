#include <pch.h>
#include "scene_tester.h"

#include "cross_backend.h"

// Checks of the particles (vfx.md): the pool's rings, the spawn and simulation on the GPU (read back from the pool), and the sprites that are drawn.
namespace wiwa
{
	namespace
	{
		// A sprite emitter whose particles stay where they are born and live a long time, unless a test changes it.
		ParticleEmitterData still_emitter(u32 capacity)
		{
			ParticleEmitterData e;
			e.Name = "E";
			e.Renderer = ParticleRenderer::Sprite;
			e.MaxParticles = capacity;
			e.Shape = ParticleShape::Point;
			e.StartSpeed = ParticleValue::constant(0.0f);
			e.Lifetime = ParticleValue::constant(100.0f);
			e.StartSize = ParticleValue::constant(0.1f);
			e.SoftDistance = 0.0f;
			e.bake();
			return e;
		}

		EntityGuid add_system(SceneTester& test, const AssetRef& effect, const glm::mat4& world, u32 spawn)
		{
			static u64 counter = 0x5000;
			const EntityGuid guid{ ++counter, counter * 3 };
			RenderParticleSystem system;
			system.World = world;
			system.Effect = effect;
			system.Seed = 12345;
			SceneDelta delta;
			delta.ParticleUpserts.emplace_back(guid, std::move(system));
			if (spawn > 0)
			{
				std::array<u32, PARTICLE_MAX_EMITTERS> spawns{};
				spawns[0] = spawn;
				delta.ParticleSpawns.emplace_back(guid, spawns);
			}
			test.scene().apply(std::move(delta));
			return guid;
		}

		void spawn_more(SceneTester& test, EntityGuid guid, u32 count)
		{
			std::array<u32, PARTICLE_MAX_EMITTERS> spawns{};
			spawns[0] = count;
			SceneDelta delta;
			delta.ParticleSpawns.emplace_back(guid, spawns);
			test.scene().apply(std::move(delta));
		}

		std::vector<ParticleGpu> read_pool(RenderCheckContext& context, SceneTester& test)
		{
			ParticlePool& pool = test.renderer().get_particle_pool();
			std::vector<ParticleGpu> particles;
			if (!pool.ready() || pool.slot_end() == 0)
			{
				return particles;
			}
			std::vector<u8> bytes;
			if (read_buffer_bytes(context.Device, pool.particles(), static_cast<u64>(pool.slot_end()) * sizeof(ParticleGpu), bytes) && bytes.size() == static_cast<size_t>(pool.slot_end()) * sizeof(ParticleGpu))
			{
				particles.resize(pool.slot_end());
				std::memcpy(particles.data(), bytes.data(), bytes.size());
			}
			return particles;
		}

		std::vector<ParticleGpu> alive(const std::vector<ParticleGpu>& all)
		{
			std::vector<ParticleGpu> result;
			for (const ParticleGpu& p : all)
			{
				if (p.Lifetime > 0.0f && p.Age < p.Lifetime)
				{
					result.push_back(p);
				}
			}
			return result;
		}

		// The test's scene: the camera at (0, 0, 5) looking at the origin, and a white material.
		struct SpriteScene
		{
			explicit SpriteScene(SceneTester& test)
			{
				test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
				Material = test.add_material(test.color_material(glm::vec3(1.0f)));
			}

			AssetRef Material{};
		};
	}

	void register_particle_checks()
	{
		register_render_check("particle_pool_rings", [](RenderCheckContext& context)
			{
				ParticlePool pool;
				pool.set_capacity(1000);
				context.check(pool.ensure(context.Device) && pool.ready() && pool.capacity() == 1000, "the pool is made");
				const std::optional<ParticleAllocation> a = pool.allocate(400);
				const std::optional<ParticleAllocation> b = pool.allocate(400);
				const std::optional<ParticleAllocation> c = pool.allocate(200);
				context.check(a && b && c, "three rings fit");
				if (!a || !b || !c)
				{
					return;
				}
				context.check(a->Id != b->Id && b->Id != c->Id && a->Id != c->Id, "each ring has its own emitter id");
				context.check(a->Base + a->Capacity <= b->Base && b->Base + b->Capacity <= c->Base, "and its own slots");
				context.check(!pool.allocate(1).has_value(), "a full pool gives no ring");
				context.check(pool.used() == 1000 && pool.slot_end() == 1000 && pool.id_end() == std::max({ a->Id, b->Id, c->Id }) + 1, "used, slot end and id end follow");

				pool.release(*b);
				const std::optional<ParticleAllocation> d = pool.allocate(300);
				context.check(d && d->Base == b->Base && d->Id == b->Id, "a released ring is reused (first fit, same id)");
				pool.release(*a);
				pool.release(*d);
				context.check(pool.allocate(750).has_value(), "released rings next to each other join (coalescing)");
				pool.release(*c);

				ParticlePool small;
				small.set_capacity(ParticlePool::MAX_EMITTERS * 2);
				context.check(small.ensure(context.Device), "a pool for many tiny rings");
				u32 made = 0;
				while (small.allocate(1) && made <= ParticlePool::MAX_EMITTERS + 1)
				{
					made++;
				}
				context.check(made == ParticlePool::MAX_EMITTERS, "there are at most MAX_EMITTERS rings at a time, then none");
			});

		register_render_check("particle_spawn_and_age", [](RenderCheckContext& context)
			{
				SceneTester test(context, 32);
				ParticlePool& pool = test.renderer().get_particle_pool();
				pool.set_capacity(4096);

				ParticleEffectData effect;
				ParticleEmitterData emitter = still_emitter(64);
				emitter.Gravity = glm::vec3(0.0f, -10.0f, 0.0f);
				effect.Emitters = { emitter };
				const AssetRef ref = test.cache().add_inline_particle_effect(effect);
				const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
				const EntityGuid system = add_system(test, ref, world, 40);
				context.check(test.render(8), "render with a system that spawns 40");
				std::vector<ParticleGpu> particles = alive(read_pool(context, test));
				context.check(particles.size() == 40, "40 particles are alive (" + std::to_string(particles.size()) + ")");
				if (particles.size() == 40)
				{
					bool atSystem = true;
					bool aged = true;
					bool falling = true;
					for (const ParticleGpu& p : particles)
					{
						atSystem = atSystem && std::abs(p.Position.x - 1.0f) < 1e-3f && std::abs(p.Position.z - 3.0f) < 1e-3f && p.Position.y <= 2.0f + 1e-3f;
						aged = aged && p.Age >= 0.0f && p.Age < 100.0f && p.Emitter == 0 && p.Lifetime == 100.0f;
						falling = falling && p.Velocity.y < -1e-4f;
					}
					context.check(atSystem, "they were born at the system");
					context.check(aged, "with the lifetime and emitter the effect says");
					context.check(falling, "and gravity pulls them down (the simulation ran)");
				}

				// the ring: 40 more into a ring of 64 leaves 64 alive, the oldest overwritten
				spawn_more(test, system, 40);
				context.check(test.render(2), "render with 40 more");
				context.check(alive(read_pool(context, test)).size() == 64, "a full ring overwrites its oldest particles (64 alive)");

				// nothing more is spawned without a request
				context.check(test.render(4), "render without spawns");
				context.check(alive(read_pool(context, test)).size() == 64, "no spawn without a request");

				// a system that goes away gives its ring back
				SceneDelta removal;
				removal.ParticleRemovals.push_back(system);
				test.scene().apply(std::move(removal));
				context.check(test.render(2), "render after the system is removed");
				context.check(pool.used() == 0, "its ring is released");
			});

		register_render_check("particle_cone_shape", [](RenderCheckContext& context)
			{
				SceneTester test(context, 32);
				test.renderer().get_particle_pool().set_capacity(4096);
				ParticleEffectData effect;
				ParticleEmitterData emitter = still_emitter(256);
				emitter.Shape = ParticleShape::Cone;
				emitter.ConeAngle = 20.0f;
				emitter.ShapeSize = glm::vec3(0.5f);
				emitter.StartSpeed = ParticleValue::constant(5.0f);
				emitter.bake();
				effect.Emitters = { emitter };
				add_system(test, test.cache().add_inline_particle_effect(effect), glm::mat4(1.0f), 200);
				context.check(test.render(2), "render a cone emitter");
				const std::vector<ParticleGpu> particles = alive(read_pool(context, test));
				context.check(particles.size() == 200, "200 particles");
				bool inCone = !particles.empty();
				bool speedOk = !particles.empty();
				bool spread = false;
				for (const ParticleGpu& p : particles)
				{
					const float speed = glm::length(p.Velocity);
					speedOk = speedOk && std::abs(speed - 5.0f) < 0.2f; // a step of gravity-free motion does not change a velocity
					inCone = inCone && p.Velocity.y / std::max(speed, 1e-4f) >= std::cos(glm::radians(20.0f)) - 1e-3f;
					spread = spread || std::abs(p.Velocity.x) > 0.3f;
				}
				context.check(speedOk, "they leave at the start speed");
				context.check(inCone, "inside the cone of 20 degrees about +Y");
				context.check(spread, "and spread across it");
			});

		register_render_check("particle_effect_reload", [](RenderCheckContext& context)
			{
				SceneTester test(context, 32);
				ParticlePool& pool = test.renderer().get_particle_pool();
				pool.set_capacity(4096);
				ParticleEffectData effect;
				effect.Emitters = { still_emitter(32) };
				const AssetRef ref = test.cache().add_inline_particle_effect(effect);
				add_system(test, ref, glm::mat4(1.0f), 20);
				context.check(test.render(3), "render");
				context.check(alive(read_pool(context, test)).size() == 20, "20 alive");

				// the effect is cooked again with a bigger ring: the system starts over on a new ring
				effect.Emitters = { still_emitter(128) };
				test.cache().set_inline_particle_effect(ref, effect);
				context.check(test.render(3), "render after the effect changed");
				context.check(pool.used() == 128, "the ring has the new size and the old one is gone");
				context.check(alive(read_pool(context, test)).empty(), "its particles started over (none alive until they are asked for)");
			});

		register_render_check("particle_sprites_draw", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				SpriteScene scene(test);
				test.renderer().get_particle_pool().set_capacity(4096);
				context.check(test.render(), "render the empty scene");
				const std::vector<u8> empty = test.pixels();
				const glm::ivec2 center = test.to_pixel(glm::vec3(0.0f));
				const glm::ivec3 background = test.pixel(center);

				ParticleEffectData effect;
				ParticleEmitterData emitter = still_emitter(8);
				emitter.Material = scene.Material;
				emitter.Blend = ParticleBlend::Additive;
				emitter.StartSize = ParticleValue::constant(1.5f);
				emitter.StartColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
				emitter.Intensity = 2.0f;
				emitter.bake();
				effect.Emitters = { emitter };
				const AssetRef ref = test.cache().add_inline_particle_effect(effect);

				// a system that has not spawned yet draws nothing, and leaves the image as it was
				const EntityGuid system = add_system(test, ref, glm::mat4(1.0f), 0);
				context.check(test.render(), "render a system that spawned nothing");
				context.check(SceneTester::max_difference(empty, test.pixels()) == 0, "an idle system leaves every pixel as it was, exactly");

				spawn_more(test, system, 1);
				context.check(test.render(), "render with one sprite");
				const glm::ivec3 lit = test.pixel(center);
				WI_LOG(Info, "RenderTest", "particle_sprites_draw center {0},{1},{2} background {3},{4},{5}", lit.r, lit.g, lit.b, background.r, background.g, background.b);
				context.check(lit.g > background.g + 30 && lit.g > lit.r + 20, "the green sprite lights the pixel under it");
				context.check(SceneTester::luma(test.pixel(glm::ivec2(2, 2))) == SceneTester::luma(glm::ivec3(empty[(2 * 64 + 2) * 4], empty[(2 * 64 + 2) * 4 + 1], empty[(2 * 64 + 2) * 4 + 2])), "a pixel far from it is as it was");
				compare_across_apis(context, "particle_sprites_additive", test.pixels());
			});

		register_render_check("particle_sprites_behind_surface", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				SpriteScene scene(test);
				test.renderer().get_particle_pool().set_capacity(4096);
				// a wall in front of where the sprite is, covering the middle of the view
				const AssetRef grey = test.add_material(test.color_material(glm::vec3(0.4f)));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(3.0f, 3.0f, 0.05f), grey)), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 2.0f)));
				test.add_light(test.sun(glm::vec3(0.0f, -0.3f, -1.0f), 2.0f));
				context.check(test.render(), "render the wall alone");
				const std::vector<u8> wall = test.pixels();

				ParticleEffectData effect;
				ParticleEmitterData emitter = still_emitter(8);
				emitter.Material = scene.Material;
				emitter.Blend = ParticleBlend::Additive;
				emitter.StartSize = ParticleValue::constant(1.5f);
				emitter.Intensity = 3.0f;
				emitter.bake();
				effect.Emitters = { emitter };
				add_system(test, test.cache().add_inline_particle_effect(effect), glm::mat4(1.0f), 1); // at the origin, behind the wall (z = 2 is nearer the camera)
				context.check(test.render(), "render with a sprite behind the wall");
				context.check(SceneTester::max_difference(wall, test.pixels()) == 0, "a sprite behind a surface is not drawn through it, exactly");
			});

		register_render_check("particle_sprites_soft_and_blend", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				SpriteScene scene(test);
				test.renderer().get_particle_pool().set_capacity(4096);
				const glm::ivec2 center = test.to_pixel(glm::vec3(0.0f));
				context.check(test.render(), "render the empty scene");
				const glm::ivec3 background = test.pixel(center);

				const auto draw_with = [&](ParticleBlend blend, const glm::vec4& color) -> glm::ivec3
				{
					SceneDelta reset;
					reset.Reset = true;
					test.scene().apply(std::move(reset));
					test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
					ParticleEffectData effect;
					ParticleEmitterData emitter = still_emitter(8);
					emitter.Material = scene.Material;
					emitter.Blend = blend;
					emitter.StartSize = ParticleValue::constant(1.5f);
					emitter.StartColor = color;
					emitter.bake();
					effect.Emitters = { emitter };
					add_system(test, test.cache().add_inline_particle_effect(effect), glm::mat4(1.0f), 1);
					test.render();
					return test.pixel(center);
				};

				const glm::ivec3 alpha = draw_with(ParticleBlend::Alpha, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
				context.check(alpha.r > background.r + 30 && alpha.r > alpha.g + 20, "an opaque red alpha sprite covers the pixel with red");
				const glm::ivec3 half = draw_with(ParticleBlend::Alpha, glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
				context.check(half.r < alpha.r && half.r > background.r, "a half transparent one covers it less");
				const glm::ivec3 multiply = draw_with(ParticleBlend::Multiply, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
				context.check(SceneTester::luma(multiply) < SceneTester::luma(background), "a dark multiply sprite darkens what is behind it");
				const glm::ivec3 additive = draw_with(ParticleBlend::Additive, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
				context.check(additive.b >= background.b && additive.b > additive.r, "an additive blue sprite adds blue");
			});
	}
}
