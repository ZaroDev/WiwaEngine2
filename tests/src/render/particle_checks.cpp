#include <pch.h>
#include "scene_tester.h"

#include <limits>

#include <renderer/render_quality.h>
#include <renderer/view/view_state.h>

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

		std::vector<ParticleGpu> read_pool(RenderCheckContext& context, ParticlePool& pool)
		{
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

		std::vector<ParticleGpu> read_pool(RenderCheckContext& context, SceneTester& test)
		{
			return read_pool(context, test.renderer().get_particle_pool());
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

		// Renders until the oldest particle is at least `seconds` old (the frames' own time passes; their length is not known) and returns what is alive.
		std::vector<ParticleGpu> age_until(RenderCheckContext& context, SceneTester& test, float seconds)
		{
			std::vector<ParticleGpu> particles;
			for (int round = 0; round < 60; round++)
			{
				test.render(10);
				particles = alive(read_pool(context, test));
				float oldest = 0.0f;
				for (const ParticleGpu& p : particles)
				{
					oldest = std::max(oldest, p.Age);
				}
				if (oldest >= seconds)
				{
					break;
				}
			}
			return particles;
		}

		// A floor in front of a camera that looks down at it from a little above and behind.
		void floor_scene(SceneTester& test)
		{
			test.set_camera(glm::vec3(0.0f, 2.0f, 6.0f), glm::vec3(0.0f, 0.3f, 0.0f));
			const AssetRef grey = test.add_material(test.color_material(glm::vec3(0.4f)));
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(6.0f, 0.1f, 6.0f), grey)), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.1f, 0.0f)));
		}

		// Particles thrown straight down at 20 m/s from 1.5 m, under gravity, that collide with the depth as asked.
		AssetRef falling_effect(SceneTester& test, ParticleCollision collision, ParticleCollisionResponse response, float restitution)
		{
			ParticleEffectData effect;
			ParticleEmitterData emitter = still_emitter(64);
			emitter.Shape = ParticleShape::Cone;
			emitter.ConeAngle = 0.0f;
			emitter.ShapeSize = glm::vec3(0.0f);
			emitter.StartSpeed = ParticleValue::constant(-20.0f);
			emitter.Gravity = glm::vec3(0.0f, -10.0f, 0.0f);
			emitter.Collision = collision;
			emitter.Response = response;
			emitter.Restitution = restitution;
			emitter.bake();
			effect.Emitters = { emitter };
			return test.cache().add_inline_particle_effect(effect);
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

		register_render_check("particle_clear_request", [](RenderCheckContext& context)
			{
				SceneTester test(context, 32);
				test.renderer().get_particle_pool().set_capacity(4096);
				ParticleEffectData effect;
				effect.Emitters = { still_emitter(32) };
				const EntityGuid system = add_system(test, test.cache().add_inline_particle_effect(effect), glm::mat4(1.0f), 20);
				context.check(test.render(3), "render");
				context.check(alive(read_pool(context, test)).size() == 20, "20 alive");

				// the main thread discards the particles (Vfx.stop with clear, Vfx.restart): the counter goes up
				const std::optional<size_t> slot = test.scene().find_particle_system(system);
				context.check(slot.has_value(), "the system is in the scene");
				if (!slot)
				{
					return;
				}
				RenderParticleSystem changed = test.scene().particle_systems()[*slot];
				changed.ClearCount = 1;
				SceneDelta delta;
				delta.ParticleUpserts.emplace_back(system, std::move(changed));
				test.scene().apply(std::move(delta));
				context.check(test.render(3), "render after the clear request");
				context.check(alive(read_pool(context, test)).empty(), "none of its particles is alive");

				// and the system plays on: what is asked for now appears
				spawn_more(test, system, 5);
				context.check(test.render(3), "render with 5 more");
				context.check(alive(read_pool(context, test)).size() == 5, "the system spawns again (5 alive)");
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

		register_render_check("particle_collision_responses", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				test.renderer().get_particle_pool().set_capacity(4096);
				floor_scene(test);
				const glm::mat4 above = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, 0.0f));

				// without collision the particles fall through the floor
				add_system(test, falling_effect(test, ParticleCollision::None, ParticleCollisionResponse::Bounce, 0.0f), above, 20);
				std::vector<ParticleGpu> particles = age_until(context, test, 0.3f);
				bool through = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					through = through && p.Position.y < -1.0f;
				}
				context.check(through, "without collision (the control) particles fall through the floor (" + std::to_string(particles.size()) + " alive)");

				// bounce with no restitution: they stay on the floor
				SceneDelta reset;
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				floor_scene(test);
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Bounce, 0.0f), above, 20);
				particles = age_until(context, test, 0.3f);
				bool onFloor = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					onFloor = onFloor && p.Position.y > -0.2f && p.Position.y < 0.3f;
				}
				context.check(onFloor, "with depth collision they stay on the floor (" + std::to_string(particles.size()) + " alive)");

				// bounce with restitution: they are thrown back up
				reset = SceneDelta{};
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				floor_scene(test);
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Bounce, 0.8f), above, 20);
				particles = age_until(context, test, 0.3f);
				bool bounced = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					bounced = bounced && p.Velocity.y > 0.0f && p.Position.y > 0.3f;
				}
				context.check(bounced, "with a restitution they bounce back up");

				// stick: they stop on the floor and stay
				reset = SceneDelta{};
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				floor_scene(test);
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Stick, 0.0f), above, 20);
				particles = age_until(context, test, 0.3f);
				bool stuck = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					stuck = stuck && (p.Emitter & 0x80000000u) != 0 && p.Velocity == glm::vec3(0.0f) && p.Position.y > -0.2f && p.Position.y < 0.3f;
				}
				context.check(stuck, "sticky particles stop on the floor");

				// kill: they die on touching it
				reset = SceneDelta{};
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				floor_scene(test);
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Kill, 0.0f), above, 20);
				test.render(10);
				std::vector<ParticleGpu> dying = alive(read_pool(context, test));
				for (int round = 0; round < 60 && !dying.empty(); round++)
				{
					test.render(10);
					dying = alive(read_pool(context, test));
				}
				context.check(dying.empty(), "particles that die on touching are gone (" + std::to_string(dying.size()) + " left)");

				// a particle that is not on the screen has no depth to touch: it falls through
				reset = SceneDelta{};
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				floor_scene(test);
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Bounce, 0.0f), glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 1.5f, 0.0f)), 10);
				particles = age_until(context, test, 0.3f);
				bool offscreen = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					offscreen = offscreen && p.Position.y < -1.0f;
				}
				context.check(offscreen, "a particle off the screen passes through (there is nothing to collide with)");
			});

		register_render_check("particle_quality_settings", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				ParticlePool& pool = test.renderer().get_particle_pool();
				pool.set_capacity(4096);
				const auto reset_scene = [&]()
				{
					SceneDelta reset;
					reset.Reset = true;
					test.scene().apply(std::move(reset));
					test.render(2);
				};

				// the project turns collision off: particles that ask for it pass through the floor (the control: with it on they stay on it)
				floor_scene(test);
				const glm::mat4 above = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, 0.0f));
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Bounce, 0.0f), above, 20);
				std::vector<ParticleGpu> particles = age_until(context, test, 0.3f);
				bool onFloor = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					onFloor = onFloor && p.Position.y > -0.2f;
				}
				context.check(onFloor, "the control: with the quality's collision on, the particles stay on the floor");

				reset_scene();
				RenderQuality quality = test.renderer().get_quality();
				quality.ParticleCollision = false;
				test.renderer().set_quality(quality);
				floor_scene(test);
				add_system(test, falling_effect(test, ParticleCollision::Depth, ParticleCollisionResponse::Bounce, 0.0f), above, 20);
				particles = age_until(context, test, 0.3f);
				bool through = !particles.empty();
				for (const ParticleGpu& p : particles)
				{
					through = through && p.Position.y < -1.0f;
				}
				context.check(through, "with the quality's collision off they fall through (" + std::to_string(particles.size()) + " alive)");

				// the most a ring holds: an effect that asks for 500 gets the cap (the control: without a cap it gets its 500)
				reset_scene();
				quality.ParticleCollision = true;
				test.renderer().set_quality(quality);
				ParticleEffectData big;
				big.Emitters = { still_emitter(500) };
				const AssetRef bigRef = test.cache().add_inline_particle_effect(big);
				add_system(test, bigRef, glm::mat4(1.0f), 10);
				test.render(3);
				context.check(pool.used() == 500, "the control: an emitter without a cap gets its 500 slots (" + std::to_string(pool.used()) + ")");

				reset_scene();
				quality.ParticleMaxPerEmitter = 100;
				test.renderer().set_quality(quality);
				add_system(test, bigRef, glm::mat4(1.0f), 10);
				test.render(3);
				context.check(pool.used() == 100, "with a cap of 100 the ring has 100 slots (" + std::to_string(pool.used()) + ")");

				// the pool size is taken from the quality when the project changes it, and left alone when it does not
				reset_scene();
				quality.ParticleMaxPerEmitter = 65536;
				test.renderer().set_quality(quality);
				context.check(pool.capacity() == 4096, "a quality change that leaves the pool size alone keeps a pool made by hand");
				quality.ParticlePoolSize = 8192;
				test.renderer().set_quality(quality);
				context.check(pool.capacity() == 8192, "a new pool size is applied");
				RenderQuality absurd = quality;
				absurd.ParticlePoolSize = 5;
				absurd.ParticleMaxPerEmitter = 1;
				absurd.ParticleRateScale = std::numeric_limits<float>::quiet_NaN();
				const RenderQuality fixed = sanitized(absurd);
				context.check(fixed.ParticlePoolSize == 4096 && fixed.ParticleMaxPerEmitter == 64 && fixed.ParticleRateScale == 1.0f, "the particle settings are sanitized");
			});

		register_render_check("particle_sorting", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				ParticlePool& pool = test.renderer().get_particle_pool();
				pool.set_capacity(4096);
				SpriteScene scene(test);
				test.set_camera(glm::vec3(0.0f, 0.0f, 6.0f), glm::vec3(0.0f));

				const auto make_effect = [&](u32 capacity, bool sorted, ParticleBlend blend)
				{
					ParticleEffectData effect;
					ParticleEmitterData emitter = still_emitter(capacity);
					emitter.Material = scene.Material;
					emitter.Blend = blend;
					emitter.Sorted = sorted;
					emitter.Shape = ParticleShape::Box;
					emitter.ShapeSize = glm::vec3(0.0f, 0.0f, 4.0f); // along the line of sight: depths differ
					emitter.StartSize = ParticleValue::constant(1.0f);
					emitter.StartColor = glm::vec4(1.0f, 0.2f, 0.2f, 0.5f);
					emitter.bake();
					effect.Emitters = { emitter };
					return test.cache().add_inline_particle_effect(effect);
				};

				// 70 particles in a ring of 100 (padded to 128): sorted farthest first, the dead and the padding last
				const EntityGuid system = add_system(test, make_effect(100, true, ParticleBlend::Alpha), glm::mat4(1.0f), 70);
				context.check(test.render(3), "render a sorted emitter");
				context.check(pool.Draws.size() == 1 && pool.Draws[0].SortStart != 0 && pool.Draws[0].SortCount == 128, "the emitter is sorted, over 128 entries");
				if (pool.Draws.size() == 1 && pool.Draws[0].SortStart != 0)
				{
					const ParticleDraw draw = pool.Draws[0];
					std::vector<u8> bytes;
					context.check(read_buffer_bytes(context.Device, pool.sort_buffer(), static_cast<u64>(draw.SortStart - 1 + draw.SortCount) * 8, bytes), "read the sort entries back");
					const std::vector<ParticleGpu> particles = read_pool(context, test);
					if (bytes.size() >= static_cast<size_t>(draw.SortStart - 1 + draw.SortCount) * 8 && particles.size() >= 100)
					{
						std::vector<std::pair<float, u32>> entries(draw.SortCount);
						std::memcpy(entries.data(), bytes.data() + static_cast<size_t>(draw.SortStart - 1) * 8, static_cast<size_t>(draw.SortCount) * 8);
						bool descending = true;
						bool depthsMatch = true;
						std::vector<bool> seen(draw.SortCount, false);
						bool permutation = true;
						u32 live = 0;
						for (u32 i = 0; i < draw.SortCount; i++)
						{
							descending = descending && (i == 0 || entries[i].first <= entries[i - 1].first);
							permutation = permutation && entries[i].second < draw.SortCount && !seen[entries[i].second];
							if (entries[i].second < draw.SortCount)
							{
								seen[entries[i].second] = true;
							}
							if (entries[i].first > 0.0f)
							{
								live++;
								const ParticleGpu& p = particles[draw.Base + entries[i].second];
								depthsMatch = depthsMatch && entries[i].second < 100 && std::abs(entries[i].first - (6.0f - p.Position.z)) < 0.01f;
							}
						}
						context.check(descending, "the entries are in order, the farthest first");
						context.check(permutation, "every entry is a different slot (a permutation)");
						context.check(live == 70, "the 70 live particles come first (" + std::to_string(live) + ")");
						context.check(depthsMatch, "and each key is the view depth of its particle");
						context.check(entries[0].first > entries[69].first + 1.0f, "the depths spread over the box (a real sort)");
					}
				}

				// a sorted draw shows the same as an unsorted one for particles of one color (the order does not matter), so every slot is reached
				context.check(test.render(2), "render the sorted emitter");
				const std::vector<u8> sortedImage = test.pixels();
				SceneDelta removal;
				removal.ParticleRemovals.push_back(system);
				test.scene().apply(std::move(removal));
				context.check(test.render(2), "render without it");
				const std::vector<u8> without = test.pixels();
				context.check(SceneTester::max_difference(sortedImage, without) > 10, "the sorted particles are in the image");

				// additive blending does not sort, whatever the emitter says
				add_system(test, make_effect(64, true, ParticleBlend::Additive), glm::mat4(1.0f), 10);
				context.check(test.render(2), "render an additive emitter that asks to be sorted");
				context.check(pool.Draws.size() == 1 && pool.Draws[0].SortStart == 0, "it is not sorted: the order does not matter for additive blending");
				compare_across_apis(context, "particle_sorting_additive", test.pixels());
			});

		register_render_check("particle_offscreen_view", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				ParticlePool& main = test.renderer().get_particle_pool();
				main.set_capacity(4096);
				SpriteScene scene(test);

				ViewDesc description;
				description.Width = 64;
				description.Height = 64;
				description.Profile = ViewProfile::Preview;
				description.Mode = ViewMode::Continuous;
				const ViewId id = test.renderer().add_view(100, description);
				context.check(id != INVALID_VIEW, "an offscreen view is made");
				ViewState* view = id != INVALID_VIEW ? test.renderer().find_view(id) : nullptr;
				if (view == nullptr)
				{
					return;
				}
				RenderCamera camera;
				camera.Position = glm::vec3(0.0f, 0.0f, 5.0f);
				camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				camera.Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
				view->Scene.set_camera_override(camera);

				ParticleEffectData effect;
				ParticleEmitterData emitter = still_emitter(64);
				emitter.Material = scene.Material;
				emitter.Blend = ParticleBlend::Additive;
				emitter.StartSize = ParticleValue::constant(1.5f);
				emitter.StartColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
				emitter.Intensity = 2.0f;
				emitter.bake();
				effect.Emitters = { emitter };
				const AssetRef ref = test.cache().add_inline_particle_effect(effect);

				// a system of the view only: the view has a pool, the main one is untouched
				RenderParticleSystem system;
				system.Effect = ref;
				system.Seed = 99;
				SceneDelta delta;
				const EntityGuid guid{ 0x7000, 0x7001 };
				delta.ParticleUpserts.emplace_back(guid, std::move(system));
				std::array<u32, PARTICLE_MAX_EMITTERS> spawns{};
				spawns[0] = 40;
				delta.ParticleSpawns.emplace_back(guid, spawns);
				view->Scene.apply(std::move(delta));
				context.check(test.render(6), "render with a system in the view");
				ParticlePool& own = main.for_view(view->Target.uid(), false);
				context.check(own.ready() && alive(read_pool(context, own)).size() == 40, "the view's own pool holds its 40 particles");
				context.check(main.used() == 0, "and the main pool holds none");

				std::vector<u8> pixels;
				u32 width = 0;
				u32 height = 0;
				context.check(read_texture_rgba8(context.Device, view->Target.ldr(), pixels, width, height) && width == 64, "read the view's image");
				if (pixels.size() == static_cast<size_t>(64) * 64 * 4)
				{
					const size_t center = (32 * 64 + 32) * 4;
					const size_t corner = (2 * 64 + 2) * 4;
					context.check(pixels[center + 1] > pixels[corner + 1] + 30 && pixels[center + 1] > pixels[center] + 20, "the particles are drawn in the view (a green sprite in the middle)");
				}

				// a system in the main view does not age or disturb the particles of the view, and is not in its image
				const EntityGuid mainSystem = add_system(test, ref, glm::mat4(1.0f), 10);
				context.check(test.render(4), "render with a system in each view");
				context.check(alive(read_pool(context, own)).size() == 40, "the view's particles are as they were (the main simulation does not touch them)");
				context.check(alive(read_pool(context, main)).size() == 10 && main.used() == 64, "the main pool has its own ring");
				(void)mainSystem;

				// the view goes away with its pool
				test.renderer().remove_view(id);
				context.check(test.render(2), "render after the view is removed");
				context.check(alive(read_pool(context, main)).size() == 10, "the main view is unaffected");
			});
		// the profile of the Asset Browser's thumbnails (no shadows, no bloom, no prepass velocity) on a transparent background: an additive glow shows, with coverage
		register_render_check("particle_thumbnail_view", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				test.renderer().get_particle_pool().set_capacity(4096);
				SpriteScene scene(test);

				for (const bool transparent : { false, true })
				{
					ViewDesc description;
					description.Width = 64;
					description.Height = 64;
					description.Profile = ViewProfile::Thumbnail;
					description.TransparentBackground = transparent;
					description.Mode = ViewMode::Continuous;
					const ViewId id = test.renderer().add_view(transparent ? 102 : 101, description);
					ViewState* view = id != INVALID_VIEW ? test.renderer().find_view(id) : nullptr;
					context.check(view != nullptr, "a thumbnail view is made");
					if (view == nullptr)
					{
						return;
					}
					RenderCamera camera;
					camera.Position = glm::vec3(0.0f, 0.0f, 5.0f);
					camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
					camera.Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
					view->Scene.set_camera_override(camera);

					ParticleEffectData effect;
					ParticleEmitterData emitter = still_emitter(64);
					emitter.Material = scene.Material;
					emitter.Blend = ParticleBlend::Additive;
					emitter.StartSize = ParticleValue::constant(1.5f);
					emitter.StartColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
					emitter.Intensity = 2.0f;
					emitter.SoftDistance = 0.0f;
					emitter.bake();
					effect.Emitters = { emitter };
					RenderParticleSystem system;
					system.Effect = test.cache().add_inline_particle_effect(effect);
					system.Seed = 7;
					SceneDelta delta;
					const EntityGuid guid{ 0x7100 + static_cast<u64>(transparent), 0x7200 };
					delta.ParticleUpserts.emplace_back(guid, std::move(system));
					std::array<u32, PARTICLE_MAX_EMITTERS> spawns{};
					spawns[0] = 40;
					delta.ParticleSpawns.emplace_back(guid, spawns);
					view->Scene.apply(std::move(delta));
					context.check(test.render(6), "render a thumbnail view with a system");

					std::vector<u8> pixels;
					u32 width = 0;
					u32 height = 0;
					const std::string name = transparent ? " (transparent background)" : " (opaque background)";
					const bool read = read_texture_rgba8(context.Device, view->Target.ldr(), pixels, width, height) && pixels.size() == static_cast<size_t>(64) * 64 * 4;
					context.check(read, "read the thumbnail view" + name);
					if (read)
					{
						const size_t center = (32 * 64 + 32) * 4;
						context.check(pixels[center + 1] > 100 && pixels[center + 1] > pixels[center] + 40, "the glow is drawn in the thumbnail view" + name + " (green " + std::to_string(pixels[center + 1]) + ", red " + std::to_string(pixels[center]) + ")");
						if (transparent)
						{
							context.check(pixels[center + 3] > 8, "and it has coverage (alpha " + std::to_string(pixels[center + 3]) + ")");
							context.check(pixels[3] == 0, "where nothing is drawn the alpha is 0");
						}
					}
					test.renderer().remove_view(id);
				}
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
