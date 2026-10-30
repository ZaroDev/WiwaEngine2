#include <pch.h>

#include "../passes/taa_math.h"
#include "../render_pick.h"
#include "half_float.h"
#include "scene_tester.h"

// Checks of the temporal chain: the TAA maths, the velocity target, TAA and motion blur.
namespace wiwa
{
	namespace
	{
		// Where a point lands in an image of size x size pixels (origin top left), fractions included.
		glm::vec2 project_pixel(const glm::mat4& projection, const glm::mat4& view, glm::vec3 point, u32 size)
		{
			const glm::vec4 clip = projection * view * glm::vec4(point, 1.0f);
			const glm::vec2 ndc = glm::vec2(clip) / clip.w;
			return glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(size), (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(size));
		}
	}

	namespace
	{
		void set_taa(SceneTester& test, bool on)
		{
			RenderQuality quality = test.renderer().get_quality();
			quality.Taa = on;
			test.renderer().set_quality(quality);
		}

		const FrameConstants& frame_constants(SceneTester& test) { return test.renderer().get_scene_data().constants(); }

		// How far, in pixels, the jittered matrices move a point away from the unjittered ones (x right, y down).
		glm::vec2 jitter_offset(const FrameConstants& constants, glm::vec3 point, u32 size)
		{
			const glm::vec4 jittered = constants.ViewProjection * glm::vec4(point, 1.0f);
			const glm::vec4 plain = constants.UnjitteredViewProjection * glm::vec4(point, 1.0f);
			const glm::vec2 difference = glm::vec2(jittered) / jittered.w - glm::vec2(plain) / plain.w;
			return glm::vec2(difference.x * 0.5f * static_cast<float>(size), -difference.y * 0.5f * static_cast<float>(size));
		}

		AssetRef cube_mesh(SceneTester& test, float half = 0.5f)
		{
			return test.add_mesh(make_test_box(glm::vec3(half), test.add_material(test.color_material(glm::vec3(0.8f)))));
		}

		// The camera SceneTester::set_camera builds (60 degrees, near 0.1, far 100, square), for working out what a pixel should read.
		struct LookCamera
		{
			glm::mat4 View;
			glm::mat4 Projection;
		};

		LookCamera look(glm::vec3 position, glm::vec3 target)
		{
			return { glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f)), glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) };
		}

		// uv of a world point, origin top left.
		glm::vec2 project_uv(const LookCamera& camera, glm::vec3 point)
		{
			const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(point, 1.0f);
			return glm::vec2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
		}

		// The velocity the prepass wrote at a pixel.
		bool read_velocity(RenderCheckContext& context, SceneTester& test, glm::ivec2 pixel, glm::vec2& velocity)
		{
			std::vector<u8> bytes;
			u32 width = 0;
			u32 height = 0;
			if (!read_texture_slice_bytes(context.Device, test.renderer().get_viewport().velocity(), 0, 0, bytes, width, height, 4))
			{
				return false;
			}
			const size_t offset = (static_cast<size_t>(pixel.y) * width + static_cast<size_t>(pixel.x)) * 4;
			u16 raw[2];
			std::memcpy(raw, bytes.data() + offset, sizeof(raw));
			velocity = glm::vec2(half_to_float(raw[0]), half_to_float(raw[1]));
			return true;
		}
	}

	void register_temporal_checks()
	{
		register_render_check("taa_halton_sequence", [](RenderCheckContext& context)
			{
				context.check(halton(1, 2) == 0.5f && halton(2, 2) == 0.25f && halton(3, 2) == 0.75f, "base 2 radical inverse");
				context.check(std::abs(halton(1, 3) - 1.0f / 3.0f) < 1e-6f && std::abs(halton(2, 3) - 2.0f / 3.0f) < 1e-6f, "base 3 radical inverse");

				bool inside = true;
				bool distinct = true;
				for (u64 i = 0; i < 8; i++)
				{
					const glm::vec2 jitter = taa_jitter(i);
					inside = inside && std::abs(jitter.x) <= 0.5f && std::abs(jitter.y) <= 0.5f;
					for (u64 j = i + 1; j < 8; j++)
					{
						distinct = distinct && taa_jitter(j) != jitter;
					}
				}
				context.check(inside, "every jitter is inside the pixel");
				context.check(distinct, "the eight jitters differ");
				context.check(taa_jitter(8) == taa_jitter(0) && taa_jitter(13) == taa_jitter(5), "the sequence repeats after eight frames");
			});

		register_render_check("taa_jitter_projects_to_offset", [](RenderCheckContext& context)
			{
				constexpr u32 SIZE = 64;
				const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::mat4 perspective = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
				const glm::mat4 orthographic = glm::orthoRH_ZO(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 100.0f);
				const glm::vec2 jitter(0.5f, -0.25f);

				for (const glm::mat4& projection : { perspective, orthographic })
				{
					const glm::mat4 jittered = apply_jitter(projection, jitter, SIZE, SIZE);
					for (const float z : { 1.0f, -26.0f })
					{
						const glm::vec3 point(0.3f, -0.2f, z);
						const glm::vec2 offset = project_pixel(jittered, view, point, SIZE) - project_pixel(projection, view, point, SIZE);
						context.check(std::abs(offset.x - jitter.x) < 1e-3f && std::abs(offset.y - jitter.y) < 1e-3f, "a jittered projection moves a point by the jitter (z " + std::to_string(z) + ", " + (projection[3][3] == 1.0f ? "orthographic" : "perspective") + ")");
					}
				}
			});

		register_render_check("taa_clip_and_blend", [](RenderCheckContext& context)
			{
				glm::vec3 neighbourhood[9];
				for (int i = 0; i < 9; i++)
				{
					neighbourhood[i] = glm::vec3(0.4f + 0.02f * static_cast<float>(i), 0.1f, 0.0f);
				}
				const ColorBox box = taa_variance_box(neighbourhood, 1.25f);
				const glm::vec3 inside = (box.Min + box.Max) * 0.5f;
				context.check(taa_clip_to_box(inside, box) == inside, "a history inside the box is kept");

				const glm::vec3 outside(box.Max.x + 1.0f, inside.y, inside.z);
				const glm::vec3 clipped = taa_clip_to_box(outside, box);
				context.check(std::abs(clipped.x - box.Max.x) < 1e-4f, "a history outside the box is moved onto its surface");

				const glm::vec3 current(0.2f, 0.4f, 0.6f);
				const glm::vec3 history(0.6f, 0.4f, 0.2f);
				const glm::vec3 none = taa_blend(current, history, 0.0f);
				const glm::vec3 all = taa_blend(current, history, 1.0f);
				context.check(glm::distance(none, current) < 1e-5f && glm::distance(all, history) < 1e-5f, "feedback 0 gives the new color, 1 the history");

				const glm::vec3 mixed = taa_blend(glm::vec3(100.0f), glm::vec3(0.1f), 0.9f);
				context.check(mixed.x < 5.0f, "a very bright new pixel cannot swamp the history");
				context.check(std::abs(taa_luma(glm::vec3(1.0f)) - 1.0f) < 1e-5f && glm::distance(ycocg_to_rgb(rgb_to_ycocg(current)), current) < 1e-5f, "luma and the YCoCg round trip");
			});

		register_render_check("taa_feedback_falls_with_motion", [](RenderCheckContext& context)
			{
				context.check(std::abs(taa_feedback(0.9f, 0.0f, 0.0f) - 0.9f) < 1e-6f, "no motion keeps the feedback");
				context.check(std::abs(taa_feedback(0.9f, 0.0f, 16.0f) - 0.45f) < 1e-6f, "16 pixels per frame halves it");
				context.check(std::abs(taa_feedback(0.9f, 1.0f, 0.0f) - 0.45f) < 1e-6f, "a history clipped all the way halves it");
				context.check(std::abs(taa_feedback(0.9f, 1.0f, 16.0f) - 0.225f) < 1e-6f, "both quarter it");
				context.check(std::abs(taa_feedback(0.9f, 5.0f, 100.0f) - 0.225f) < 1e-6f, "the reductions are capped");
			});

		register_render_check("prev_world_new_instance", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
				const EntityGuid guid = test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f))))), world);
				const std::optional<size_t> slot = test.scene().find_instance(guid);
				context.check(slot.has_value() && test.scene().instances()[*slot].PrevWorld == world, "a new instance has no motion: PrevWorld is its World");
			});

		register_render_check("prev_world_follows_end_frame", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const glm::mat4 first = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
				const glm::mat4 second = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
				const EntityGuid guid = test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f))))), first);
				const size_t slot = *test.scene().find_instance(guid);

				test.set_instance_world(guid, second);
				context.check(test.scene().instances()[slot].World == second && test.scene().instances()[slot].PrevWorld == first, "moving an instance keeps the matrix of the last frame");
				test.set_instance_world(guid, glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)));
				context.check(test.scene().instances()[slot].PrevWorld == first, "several moves in one frame keep the matrix of the last frame");

				test.scene().end_frame();
				context.check(test.scene().instances()[slot].PrevWorld == test.scene().instances()[slot].World && test.scene().previous_stale(), "end_frame makes the matrix of this frame the previous one and says the buffer is stale");
				test.scene().end_frame();
				context.check(!test.scene().previous_stale(), "a frame without motion is not stale");
			});

		register_render_check("prev_world_in_instance_buffer", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const glm::mat4 first = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.0f, 0.0f));
				const glm::mat4 second = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f));
				const EntityGuid guid = test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f))))), first);
				context.check(test.render(), "render the still scene");
				const size_t slot = *test.scene().find_instance(guid);

				// x of the translation column: World at byte 0, Normal at 64, PrevWorld at 128, 48 bytes into a matrix
				auto read_x = [&](u32 matrixOffset, float& x)
				{
					std::vector<u8> bytes;
					const u64 end = (slot + 1) * sizeof(GpuInstance);
					if (!read_buffer_bytes(context.Device, test.renderer().get_scene_data().instance_buffer(), end, bytes) || bytes.size() < end)
					{
						return false;
					}
					std::memcpy(&x, bytes.data() + slot * sizeof(GpuInstance) + matrixOffset + 48, sizeof(float));
					return true;
				};

				test.set_instance_world(guid, second);
				context.check(test.frame(), "render the moving frame");
				float world = 0.0f;
				float previous = 0.0f;
				context.check(read_x(0, world) && read_x(128, previous), "read the instance buffer");
				context.check(std::abs(world - 0.5f) < 1e-6f && std::abs(previous + 0.5f) < 1e-6f, "the frame of the move sees the old matrix as the previous one");

				context.check(test.frame(), "render the frame after the move");
				context.check(read_x(0, world) && read_x(128, previous), "read the instance buffer again");
				context.check(std::abs(world - 0.5f) < 1e-6f && std::abs(previous - 0.5f) < 1e-6f, "the frame after it sees no motion");
			});

		register_render_check("jitter_off_equals_unjittered", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				context.check(test.render(), "render with TAA off");
				const FrameConstants& constants = frame_constants(test);
				context.check(constants.ViewProjection == constants.UnjitteredViewProjection, "without TAA the matrices are not jittered");
				context.check(constants.JitterInfo.z == 0.0f && constants.JitterInfo.x == 0.0f && constants.JitterInfo.y == 0.0f, "and the jitter says so");
				context.check(constants.InverseViewProjection == glm::inverse(constants.UnjitteredViewProjection), "the inverse is the plain one");
			});

		register_render_check("jitter_on_constants", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				set_taa(test, true);
				context.check(test.render(), "render with TAA on");

				std::vector<glm::vec2> offsets;
				for (int i = 0; i < 9; i++)
				{
					context.check(test.frame(), "render a frame");
					const FrameConstants& constants = frame_constants(test);
					const glm::vec2 offset = jitter_offset(constants, glm::vec3(0.3f, -0.2f, 1.0f), test.size());
					const glm::vec2 distant = jitter_offset(constants, glm::vec3(-1.0f, 0.5f, -20.0f), test.size());
					const glm::vec2 jitter = test.renderer().get_scene_data().jitter();
					context.check(constants.JitterInfo.z == 1.0f && constants.JitterInfo.x == jitter.x && constants.JitterInfo.y == jitter.y, "the constants carry the jitter");
					context.check(glm::distance(offset, jitter) < 1e-3f && glm::distance(distant, jitter) < 1e-3f, "a point moves by exactly the jitter, near or far");
					context.check(glm::distance(constants.InverseViewProjection * constants.ViewProjection * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f), glm::vec4(1.0f, 2.0f, 3.0f, 1.0f)) < 1e-3f, "the inverse matches the jittered matrix");
					offsets.push_back(jitter);
				}
				context.check(offsets[0] != offsets[1] && offsets[1] != offsets[2], "the jitter changes from frame to frame");
				context.check(offsets[0] == offsets[8], "and repeats after eight frames");
			});

		register_render_check("jitter_keeps_culling_and_picks", [](RenderCheckContext& context)
			{
				// A box that sits on the edge of the view is culled by the unjittered frustum, not by whatever the jitter makes of it.
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				test.add_instance(cube_mesh(test, 0.4f), glm::translate(glm::mat4(1.0f), glm::vec3(2.35f, 0.0f, 0.0f)));
				test.add_instance(cube_mesh(test, 0.4f), glm::translate(glm::mat4(1.0f), glm::vec3(-3.2f, 0.0f, 0.0f)));
				context.check(test.render(), "render with TAA off");
				const u32 visibleOff = test.renderer().get_scene_data().visible_instances();

				set_taa(test, true);
				bool same = true;
				for (int i = 0; i < 8; i++)
				{
					context.check(test.frame(), "render a frame with TAA on");
					same = same && test.renderer().get_scene_data().visible_instances() == visibleOff;
				}
				context.check(same, "the same instances are drawn with and without the jitter");

				// the pick of every pixel of a row across the cube edges is the same with the jitter moving
				PickChannel channel;
				test.renderer().set_pick_channel(&channel);
				u64 id = 0;
				// every pixel is picked in eight consecutive frames: one with each jitter of the sequence
				auto row = [&](u32 y, u32 repeats)
				{
					std::vector<i32> picks;
					for (u32 x = 0; x < test.size(); x++)
					{
						for (u32 r = 0; r < repeats; r++)
						{
							test.renderer().set_pick_request(PickRequest{ ++id, x, y });
							test.frame();
							const std::optional<PickResult> result = channel.take();
							picks.push_back(result && result->RequestId == id && result->Guid ? static_cast<i32>(result->Guid->Lo) : -1);
						}
					}
					return picks;
				};
				const std::vector<i32> with = row(32, 8);
				set_taa(test, false);
				test.frame();
				const std::vector<i32> without = row(32, 1);
				test.renderer().set_pick_channel(nullptr);
				u32 differing = 0;
				for (size_t i = 0; i < with.size(); i++)
				{
					if (with[i] != without[i / 8])
					{
						if (differing++ == 0)
						{
							WI_LOG(Info, "RenderTest", "jitter_keeps_culling_and_picks: pixel {0} picks {1} with the jitter and {2} without", i / 8, with[i], without[i / 8]);
						}
					}
				}
				context.check(differing == 0, "picking ignores the jitter");
				context.check(std::count(without.begin(), without.end(), -1) < static_cast<std::ptrdiff_t>(test.size()), "and the row picks something");
			});

		register_render_check("jitter_orthographic_camera", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				RenderCamera camera;
				camera.View = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				camera.Projection = glm::orthoRH_ZO(-3.0f, 3.0f, -3.0f, 3.0f, 0.1f, 100.0f);
				camera.Position = glm::vec3(0.0f, 0.0f, 4.0f);
				test.scene().set_camera_override(camera);
				set_taa(test, true);
				context.check(test.render(), "render an orthographic scene with TAA on");
				for (int i = 0; i < 4; i++)
				{
					context.check(test.frame(), "render a frame");
					const FrameConstants& constants = frame_constants(test);
					context.check(constants.Projection[3][3] == 1.0f, "the jittered projection is still orthographic");
					context.check(glm::distance(jitter_offset(constants, glm::vec3(0.5f, 0.5f, 0.0f), test.size()), test.renderer().get_scene_data().jitter()) < 1e-3f, "a point moves by the jitter");
				}
			});

		register_render_check("history_resets", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				SceneDelta delta;
				RenderCamera sceneCamera;
				sceneCamera.View = glm::lookAt(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				sceneCamera.Projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
				sceneCamera.Position = glm::vec3(0.0f, 1.0f, 5.0f);
				delta.Camera = sceneCamera;
				test.scene().apply(std::move(delta));
				context.check(test.render(), "render with the override camera");
				SceneRenderData& data = test.renderer().get_scene_data();

				auto step = [&]() { return test.frame(); };
				step();
				context.check(data.history_valid(), "two still frames have a history");
				const u32 resets = data.history_reset_count();

				test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
				step();
				context.check(data.history_valid() && data.history_reset_count() == resets, "a small camera move keeps it");

				test.set_size(48);
				step();
				context.check(!data.history_valid(), "a resize drops it");
				step();
				context.check(data.history_valid(), "and it is back one frame later");

				test.scene().set_camera_override(std::nullopt); // the scene's camera takes over
				step();
				context.check(!data.history_valid(), "leaving the override camera drops it");
				step();
				context.check(data.history_valid(), "back one frame later");
				test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
				step();
				context.check(!data.history_valid(), "entering the override camera drops it");
				step();

				test.set_camera(glm::vec3(0.0f, 0.0f, 65.0f), glm::vec3(0.0f, 0.0f, 60.0f));
				step();
				context.check(!data.history_valid(), "a camera jump of 60 units drops it");
				step();
				context.check(data.history_valid(), "and it is back one frame later");

				data.request_history_reset();
				step();
				context.check(!data.history_valid(), "an explicit request drops it");
				step();
				context.check(data.history_valid() && data.history_reset_count() > resets, "and it is back one frame later; every drop was counted");
			});

		register_render_check("velocity_still_is_zero", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				context.check(test.render(), "render the still scene");
				context.check(test.frame(), "render one more frame");
				glm::vec2 velocity(1.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.0f, 0.0f, 0.5f)), velocity), "read the velocity");
				context.check(glm::length(velocity) < 1e-4f, "nothing moves: the velocity is zero");
			});

		register_render_check("velocity_moving_object", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const EntityGuid box = test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				context.check(test.render(), "render the still scene");

				test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)));
				context.check(test.frame(), "render the frame in which the box moves");
				const LookCamera camera = look(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));
				const glm::vec2 expected = project_uv(camera, glm::vec3(0.5f, 0.0f, 0.5f)) - project_uv(camera, glm::vec3(0.0f, 0.0f, 0.5f));
				glm::vec2 velocity(0.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.5f, 0.0f, 0.5f)), velocity), "read the velocity");
				context.check(glm::distance(velocity, expected) < 1.5e-3f && expected.x > 0.1f, "the box has its own velocity: current uv minus previous uv");
				const FrameConstants& constants = frame_constants(test);
				context.check(constants.PreviousViewProjection == constants.UnjitteredViewProjection, "and none of it comes from the camera, which stood still");

				glm::vec2 background(1.0f);
				context.check(read_velocity(context, test, glm::ivec2(1, 1), background) && glm::length(background) < 1e-4f, "pixels with no geometry read zero");
			});

		register_render_check("velocity_moving_camera", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				context.check(test.render(), "render the still scene");

				test.set_camera(glm::vec3(0.3f, 0.0f, 4.0f), glm::vec3(0.3f, 0.0f, 0.0f));
				context.check(test.frame(), "render the frame in which the camera moves");
				const glm::vec2 expected = project_uv(look(glm::vec3(0.3f, 0.0f, 4.0f), glm::vec3(0.3f, 0.0f, 0.0f)), glm::vec3(0.0f, 0.0f, 0.5f))
					- project_uv(look(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f)), glm::vec3(0.0f, 0.0f, 0.5f));
				glm::vec2 velocity(0.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.0f, 0.0f, 0.5f)), velocity), "read the velocity");
				context.check(glm::distance(velocity, expected) < 1.5e-3f && expected.x < -0.05f, "a static box moves on the screen with the camera");
			});

		register_render_check("velocity_zero_after_stop", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const EntityGuid box = test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				context.check(test.render(), "render the still scene");

				test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)));
				context.check(test.frame(), "render the frame in which the box moves");
				const glm::ivec2 pixel = test.to_pixel(glm::vec3(0.5f, 0.0f, 0.5f));
				glm::vec2 moving(0.0f);
				context.check(read_velocity(context, test, pixel, moving) && moving.x > 0.05f, "it has a velocity while it moves");
				context.check(test.frame(), "render the frame after it stopped");
				glm::vec2 stopped(1.0f);
				context.check(read_velocity(context, test, pixel, stopped) && glm::length(stopped) < 1e-4f, "and none in the frame after it stopped");
			});

		register_render_check("velocity_masked_material", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				MaterialData masked = test.color_material(glm::vec3(0.8f));
				masked.Alpha = AlphaMode::Mask;
				masked.AlphaCutoff = 0.5f;
				const EntityGuid box = test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.5f), test.add_material(masked))), glm::mat4(1.0f));
				context.check(test.render(), "render the still scene");

				test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)));
				context.check(test.frame(), "render the frame in which the box moves");
				glm::vec2 velocity(0.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.5f, 0.0f, 0.5f)), velocity) && velocity.x > 0.1f, "an alpha-masked material writes its velocity too");
			});
	}
}
