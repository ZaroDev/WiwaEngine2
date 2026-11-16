#include <pch.h>

#include <renderer/passes/taa_math.h>
#include <renderer/render_pick.h>
#include <functional>

#include "cross_backend.h"
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

		AssetRef cube_mesh(SceneTester& test, float half = 0.5f, float emissive = 0.0f)
		{
			MaterialData material = test.color_material(glm::vec3(0.8f));
			material.EmissiveFactor = glm::vec3(emissive);
			return test.add_mesh(make_test_box(glm::vec3(half), test.add_material(material)));
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

	namespace
	{
		void set_feedback(SceneTester& test, float feedback)
		{
			SceneSettings settings;
			settings.Taa.Feedback = feedback;
			test.scene().set_settings(settings);
		}

		void advance_to(SceneTester& test, u64 frame)
		{
			while (test.frame_count() < frame)
			{
				test.frame();
			}
		}

		int luma_of(const SceneTester& test, int x, int y) { return SceneTester::luma(test.pixel(x, y)); }

		// The RGBA16F texture as floats (four per pixel).
		bool read_half4(RenderCheckContext& context, nvrhi::ITexture* texture, std::vector<float>& values)
		{
			std::vector<u8> bytes;
			u32 width = 0;
			u32 height = 0;
			if (!read_texture_slice_bytes(context.Device, texture, 0, 0, bytes, width, height, 8))
			{
				return false;
			}
			values.resize(bytes.size() / 2);
			for (size_t i = 0; i < values.size(); i++)
			{
				u16 raw;
				std::memcpy(&raw, bytes.data() + i * 2, sizeof(raw));
				values[i] = half_to_float(raw);
			}
			return true;
		}

		bool all_finite(const std::vector<float>& values)
		{
			return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
		}

		// The renderer counts frames across testers: renders empty frames until the next one starts the jitter sequence, so that runs
		// that must match start at the same point of it.
		void align_jitter(RenderCheckContext& context)
		{
			SceneTester pad(context);
			while (pad.renderer().get_frame_index() % 8 != 0)
			{
				pad.frame();
			}
		}

		// How a scenario (a still camera over a box, plus something done at frame 16) ends, with the real feedback or with none (the
		// current frame alone, a reference that the same jitter makes comparable).
		using Scenario = std::function<std::vector<u8>(SceneTester&, bool reference)>;

		std::vector<u8> run_scenario(RenderCheckContext& context, const Scenario& scenario, bool reference, u32& size)
		{
			align_jitter(context);
			SceneTester test(context);
			set_taa(test, true);
			set_feedback(test, reference ? 0.0f : 0.9f);
			std::vector<u8> pixels = scenario(test, reference);
			size = test.size();
			return pixels;
		}

		// The box scene that the history checks start from; returns after frame 15.
		void still_scene(SceneTester& test)
		{
			const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.0f, 0.0f, 1.0f));
			test.add_instance(cube_mesh(test, 0.6f, 2.0f), turn);
			test.add_instance(cube_mesh(test, 0.6f, 2.0f), glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 0.0f, 0.0f)) * turn);
			test.render();
			advance_to(test, 16);
		}
	}

	namespace
	{
		void set_motion_blur(SceneTester& test, bool enabled, float exposure = 0.0167f, float maxPixels = 32.0f)
		{
			SceneSettings settings;
			settings.MotionBlur.Enabled = enabled;
			settings.MotionBlur.ExposureTime = exposure;
			settings.MotionBlur.MaxBlurPixels = maxPixels;
			test.scene().set_settings(settings);
		}

		// A bright box that moves along x by speed units per second, one step per frame at the tester's frame time; the image of the frame
		// in which it moved last.
		std::vector<u8> render_moving_box(RenderCheckContext& context, bool blur, float exposure, float maxPixels, float speed, float delta)
		{
			SceneTester test(context);
			set_motion_blur(test, blur, exposure, maxPixels);
			test.set_delta(delta);
			const EntityGuid box = test.add_instance(cube_mesh(test, 0.5f, 2.0f), glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)));
			test.render();
			for (int i = 1; i <= 3; i++)
			{
				test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f + speed * delta * static_cast<float>(i), 0.0f, 0.0f)));
				test.frame();
			}
			test.read();
			return test.pixels();
		}

		// The columns, in the middle row, that a bright thing covers (luma well above the corner's) in an image of 64 x 64.
		glm::ivec2 bright_span(const std::vector<u8>& pixels)
		{
			const auto luma = [&](int x, int y)
			{
				const size_t offset = (static_cast<size_t>(y) * 64 + static_cast<size_t>(x)) * 4;
				return (pixels[offset] * 3 + pixels[offset + 1] * 6 + pixels[offset + 2]) / 10;
			};
			const int background = luma(1, 1);
			int first = 64;
			int last = -1;
			for (int x = 0; x < 64; x++)
			{
				if (luma(x, 32) > background + 60)
				{
					first = std::min(first, x);
					last = std::max(last, x);
				}
			}
			return glm::ivec2(first, last);
		}

		// How many pixels of the middle row, outside the span, differ between two images.
		int changed_outside(const std::vector<u8>& a, const std::vector<u8>& b, glm::ivec2 span)
		{
			int count = 0;
			for (int x = 0; x < 64; x++)
			{
				if (x >= span.x && x <= span.y)
				{
					continue;
				}
				const size_t offset = (static_cast<size_t>(32) * 64 + static_cast<size_t>(x)) * 4;
				count += std::abs(static_cast<int>(a[offset]) - static_cast<int>(b[offset])) > 3 ? 1 : 0;
			}
			return count;
		}
	}

	namespace
	{
		// The post chain of the scene checks (ambient occlusion, bloom, automatic exposure, a neutral tonemap) with the temporal effects.
		SceneSettings chain_scene_settings(bool motionBlur, bool autoExposure)
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.6f;
			settings.Ao.Enabled = true;
			settings.Bloom.Enabled = true;
			settings.Bloom.Threshold = 0.8f;
			settings.Bloom.Intensity = 0.2f;
			settings.Exposure.Auto = autoExposure;
			settings.Exposure.AdaptSpeed = 20.0f;
			settings.Exposure.MinEv = -4.0f;
			settings.Exposure.MaxEv = 4.0f;
			settings.Tonemap.Operator = TonemapOperator::Neutral;
			settings.Tonemap.Saturation = 1.2f;
			settings.Tonemap.Vignette = 0.3f;
			settings.MotionBlur.Enabled = motionBlur;
			settings.MotionBlur.ExposureTime = 0.03f;
			return settings;
		}

		glm::mat4 chain_box_world(float x)
		{
			return glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(x, -0.5f, 0.0f)), glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		// A wall, a bright lamp and a sunlit box; returns the box.
		EntityGuid add_chain_scene(SceneTester& test)
		{
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(4.0f, 4.0f, 0.1f), test.add_material(test.color_material(glm::vec3(0.6f))))), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.5f)));
			MaterialData lamp = test.color_material(glm::vec3(1.0f));
			lamp.EmissiveFactor = glm::vec3(6.0f, 5.0f, 3.0f);
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(lamp))), glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, -0.6f, 0.0f)));
			test.add_light(test.sun(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), 2.0f));
			return test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.4f), test.add_material(test.color_material(glm::vec3(0.9f, 0.2f, 0.1f), 0.4f)))), chain_box_world(-1.0f));
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

		// A static opaque box and a small blended box (alpha as given) at the given depth that moves 0.3 to the right between two frames;
		// reads the velocity at the front face of the blended box.
		register_render_check("velocity_transparent_writes_own_motion", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				MaterialData glass = test.color_material(glm::vec3(0.2f, 0.4f, 1.0f));
				glass.Alpha = AlphaMode::Blend;
				glass.BaseColorFactor.a = 0.8f;
				const EntityGuid pane = test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(glass))), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.5f)));
				context.check(test.render(), "render the still scene");

				test.set_instance_world(pane, glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, 1.5f)));
				context.check(test.frame(), "render the frame in which the pane moves");
				const LookCamera camera = look(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));
				const glm::vec2 expected = project_uv(camera, glm::vec3(0.3f, 0.0f, 1.8f)) - project_uv(camera, glm::vec3(0.0f, 0.0f, 1.8f));
				glm::vec2 velocity(0.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.3f, 0.0f, 1.8f)), velocity), "read the velocity");
				context.check(glm::distance(velocity, expected) < 1.5e-3f && expected.x > 0.05f, "a pane with alpha 0.8 has its own velocity, not that of the opaque box behind it");
			});

		register_render_check("velocity_transparent_low_alpha_ignored", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				MaterialData glass = test.color_material(glm::vec3(0.2f, 0.4f, 1.0f));
				glass.Alpha = AlphaMode::Blend;
				glass.BaseColorFactor.a = 0.3f;
				const EntityGuid pane = test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(glass))), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.5f)));
				context.check(test.render(), "render the still scene");

				test.set_instance_world(pane, glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, 1.5f)));
				context.check(test.frame(), "render the frame in which the pane moves");
				glm::vec2 velocity(1.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.3f, 0.0f, 1.8f)), velocity) && glm::length(velocity) < 1e-4f, "a pane with alpha 0.3 leaves the velocity of what is behind it");
			});

		register_render_check("velocity_transparent_behind_opaque", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				MaterialData glass = test.color_material(glm::vec3(0.2f, 0.4f, 1.0f));
				glass.Alpha = AlphaMode::Blend;
				glass.BaseColorFactor.a = 0.8f;
				const EntityGuid pane = test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(glass))), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.5f)));
				context.check(test.render(), "render the still scene");

				test.set_instance_world(pane, glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, -1.5f)));
				context.check(test.frame(), "render the frame in which the pane moves");
				glm::vec2 velocity(1.0f);
				context.check(read_velocity(context, test, test.to_pixel(glm::vec3(0.0f, 0.0f, 0.5f)), velocity) && glm::length(velocity) < 1e-4f, "a pane behind an opaque box does not overwrite its velocity");
			});

		register_render_check("taa_off_changes_nothing", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test), glm::mat4(1.0f));
				set_taa(test, true);
				context.check(test.render(), "render with TAA on");
				set_taa(test, false);
				context.check(test.frame(), "switch TAA off");
				std::vector<float> before[2];
				std::vector<float> after[2];
				bool read = true;
				for (u32 i = 0; i < 2; i++)
				{
					read = read && read_half4(context, test.renderer().get_viewport().taa_history(i), before[i]);
				}
				advance_to(test, test.frame_count() + 6);
				for (u32 i = 0; i < 2; i++)
				{
					read = read && read_half4(context, test.renderer().get_viewport().taa_history(i), after[i]);
				}
				context.check(read && before[0] == after[0] && before[1] == after[1], "with TAA off the pass does not touch its history");
			});

		register_render_check("taa_still_converges", [](RenderCheckContext& context)
			{
				align_jitter(context);
				SceneTester test(context);
				set_taa(test, true);
				test.add_instance(cube_mesh(test, 0.6f, 2.0f), glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.0f, 0.0f, 1.0f)));
				context.check(test.render(), "render with TAA on");
				advance_to(test, 20);
				test.frame();
				test.read();
				const std::vector<u8> before = test.pixels();
				test.frame();
				test.read();
				context.check(SceneTester::max_difference(before, test.pixels()) <= 50, "a still scene stops changing between frames (edge pixels move by a fraction of the contrast)");

				// An edge pixel holds a value between the background and the box.
				const int background = luma_of(test, 1, 1);
				const int inside = luma_of(test, 32, 32);
				context.check(std::abs(inside - background) > 40, "the box is visible against the background");
				int intermediate = 0;
				for (int y = 0; y < 64; y++)
				{
					for (int x = 0; x < 64; x++)
					{
						const int value = luma_of(test, x, y);
						intermediate += (value > background + 6 && value < background + (inside - background) / 2) ? 1 : 0;
					}
				}
				context.check(intermediate > 4, "the edge of the box has intermediate pixels");
				compare_across_apis(context, "taa_still_converges", test.pixels());
			});

		register_render_check("taa_edge_gets_coverage", [](RenderCheckContext& context)
			{
				auto render_box = [&](bool taa, std::vector<u8>& pixels)
				{
					SceneTester test(context);
					set_taa(test, taa);
					test.add_instance(cube_mesh(test, 0.6f, 2.0f), glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.0f, 0.0f, 1.0f)));
					const bool ok = test.render();
					advance_to(test, 24);
					test.frame();
					test.read();
					pixels = test.pixels();
					return ok;
				};
				std::vector<u8> off;
				std::vector<u8> on;
				context.check(render_box(false, off) && render_box(true, on), "render with and without TAA");
				int different = 0;
				for (size_t i = 0; i + 3 < on.size(); i += 4)
				{
					different += std::abs(static_cast<int>(on[i]) - static_cast<int>(off[i])) > 8 ? 1 : 0;
				}
				context.check(different >= 8, "TAA gives edge pixels a coverage the aliased image does not have");
				const size_t centre = (static_cast<size_t>(32) * 64 + 32) * 4;
				context.check(std::abs(static_cast<int>(on[centre]) - static_cast<int>(off[centre])) <= 4, "the inside of the box is unchanged");
			});

		register_render_check("taa_no_ghost_trail", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				set_taa(test, true);
				MaterialData bright = test.color_material(glm::vec3(1.0f));
				bright.EmissiveFactor = glm::vec3(2.0f);
				const EntityGuid box = test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(bright))), glm::translate(glm::mat4(1.0f), glm::vec3(-0.9f, 0.0f, 0.0f)));
				context.check(test.render(), "render the box at the start");
				advance_to(test, 16);
				for (int i = 1; i <= 12; i++)
				{
					test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(-0.9f + 0.1f * static_cast<float>(i), 0.0f, 0.0f)));
					test.frame();
				}
				test.frame();
				test.frame();
				test.frame();
				test.read();

				// The box rests at x = 0.3, its left edge at x = 0. Everything left of it, where it travelled, is the background again.
				const int background = luma_of(test, 1, 1);
				const int boxLuma = luma_of(test, test.to_pixel(glm::vec3(0.3f, 0.0f, 0.3f)).x, 32);
				context.check(boxLuma > background + 60, "the box is bright against the background");
				const int left = test.to_pixel(glm::vec3(-1.3f, 0.0f, 0.0f)).x;
				const int stop = test.to_pixel(glm::vec3(-0.1f, 0.0f, 0.0f)).x;
				const int top = test.to_pixel(glm::vec3(0.0f, 0.2f, 0.3f)).y;
				const int bottom = test.to_pixel(glm::vec3(0.0f, -0.2f, 0.3f)).y;
				int worst = 0;
				for (int y = top; y <= bottom; y++)
				{
					for (int x = left; x < stop; x++)
					{
						worst = std::max(worst, luma_of(test, x, y) - background);
					}
				}
				context.check(worst <= 6, "no pixel behind the path of the box is brighter than the background (worst +" + std::to_string(worst) + ")");
			});

		register_render_check("taa_disocclusion", [](RenderCheckContext& context)
			{
				// Behind the box, bright thin bars on a dark wall: every neighbourhood has both colors, so the color clip alone cannot
				// tell the wall from the box. Only the depth of the history can.
				auto scenario = [&](SceneTester& test, bool reference)
				{
					MaterialData wall = test.color_material(glm::vec3(0.05f));
					const AssetRef wallMaterial = test.add_material(wall);
					test.add_instance(test.add_mesh(make_test_box(glm::vec3(8.0f, 8.0f, 0.1f), wallMaterial)), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.5f)));
					MaterialData bar = test.color_material(glm::vec3(1.0f));
					bar.EmissiveFactor = glm::vec3(1.0f);
					const AssetRef barMesh = test.add_mesh(make_test_box(glm::vec3(0.07f, 8.0f, 0.12f), test.add_material(bar)));
					for (int i = -4; i <= 4; i++)
					{
						test.add_instance(barMesh, glm::translate(glm::mat4(1.0f), glm::vec3(0.3f * static_cast<float>(i), 0.0f, -1.5f)));
					}
					const EntityGuid box = test.add_instance(cube_mesh(test), glm::mat4(1.0f));
					test.render();
					advance_to(test, 20);
					test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(40.0f, 0.0f, 0.0f))); // gone
					test.frame();
					test.read();
					return test.pixels();
				};
				u32 sizeA = 0;
				u32 sizeB = 0;
				const std::vector<u8> real = run_scenario(context, scenario, false, sizeA);
				const std::vector<u8> reference = run_scenario(context, scenario, true, sizeB);
				int worst = 0;
				for (int y = 27; y < 38; y++)
				{
					for (int x = 27; x < 38; x++)
					{
						const size_t offset = (static_cast<size_t>(y) * 64 + static_cast<size_t>(x)) * 4;
						worst = std::max(worst, std::abs(static_cast<int>(real[offset]) - static_cast<int>(reference[offset])));
					}
				}
				context.check(!real.empty() && worst <= 3, "the wall that the box uncovers shows no trace of it (worst " + std::to_string(worst) + ")");
			});

		register_render_check("taa_history_dropped", [](RenderCheckContext& context)
			{
				struct Case
				{
					const char* Name;
					std::function<void(SceneTester&)> Drop;
				};
				const Case cases[] = {
					{ "a resize", [](SceneTester& test) { test.set_size(48); } },
					{ "a camera jump of 60 units", [](SceneTester& test) { test.set_camera(glm::vec3(60.0f, 0.0f, 4.0f), glm::vec3(60.0f, 0.0f, 0.0f)); } },
					{ "a history reset request", [](SceneTester& test) { test.renderer().get_scene_data().request_history_reset(); } },
				};

				for (const Case& drop : cases)
				{
					auto scenario = [&](SceneTester& test, bool reference)
					{
						still_scene(test);
						drop.Drop(test);
						test.frame();
						test.read();
						return test.pixels();
					};
					u32 sizeA = 0;
					u32 sizeB = 0;
					const std::vector<u8> real = run_scenario(context, scenario, false, sizeA);
					const std::vector<u8> reference = run_scenario(context, scenario, true, sizeB);
					context.check(!real.empty() && SceneTester::max_difference(real, reference) <= 2, std::string("after ") + drop.Name + " the image is the current frame alone");
				}

				// And without a drop the history is in the image: the control that the three above are not vacuous.
				auto undisturbed = [&](SceneTester& test, bool reference)
				{
					still_scene(test);
					test.frame();
					test.read();
					return test.pixels();
				};
				u32 sizeA = 0;
				u32 sizeB = 0;
				const std::vector<u8> real = run_scenario(context, undisturbed, false, sizeA);
				const std::vector<u8> reference = run_scenario(context, undisturbed, true, sizeB);
				context.check(SceneTester::max_difference(real, reference) > 10, "with history the image differs from the current frame alone");
			});

		register_render_check("taa_off_on_toggle", [](RenderCheckContext& context)
			{
				auto scenario = [&](SceneTester& test, bool reference)
				{
					const EntityGuid box = test.add_instance(cube_mesh(test), glm::mat4(1.0f));
					test.render();
					advance_to(test, 10);
					set_taa(test, false);
					for (int i = 0; i < 8; i++)
					{
						test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(0.15f * static_cast<float>(i), 0.0f, 0.0f)));
						test.frame();
					}
					set_taa(test, true);
					test.frame();
					test.read();
					return test.pixels();
				};
				u32 sizeA = 0;
				u32 sizeB = 0;
				const std::vector<u8> real = run_scenario(context, scenario, false, sizeA);
				const std::vector<u8> reference = run_scenario(context, scenario, true, sizeB);
				context.check(!real.empty() && SceneTester::max_difference(real, reference) <= 2, "the first TAA frame after being off has no history");
			});

		register_render_check("taa_nan_input", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				set_taa(test, true);
				MaterialData hot = test.color_material(glm::vec3(1.0f));
				hot.EmissiveFactor = glm::vec3(1e6f);
				MaterialData broken = test.color_material(glm::vec3(std::numeric_limits<float>::quiet_NaN()));
				broken.EmissiveFactor = glm::vec3(std::numeric_limits<float>::infinity());
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(hot))), glm::translate(glm::mat4(1.0f), glm::vec3(-0.6f, 0.0f, 0.0f)));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(broken))), glm::translate(glm::mat4(1.0f), glm::vec3(0.6f, 0.0f, 0.0f)));
				context.check(test.render(), "render the scene with bad pixels");

				bool finite = true;
				for (int i = 0; i < 6; i++)
				{
					test.frame();
					for (u32 history = 0; history < 2; history++)
					{
						std::vector<float> values;
						finite = finite && read_half4(context, test.renderer().get_viewport().taa_history(history), values) && all_finite(values);
					}
				}
				context.check(finite, "no NaN or infinity reaches the history");

				test.read();
				const int background = luma_of(test, 1, 1);
				int speckles = 0;
				for (int x = 0; x < 64; x++)
				{
					for (const int y : { 2, 3, 60, 61 })
					{
						speckles += std::abs(luma_of(test, x, y) - background) > 3 ? 1 : 0;
					}
				}
				context.check(speckles == 0, "no speckles away from the boxes");
			});

		register_render_check("taa_tiny_viewport", [](RenderCheckContext& context)
			{
				for (const u32 size : { 1u, 3u, 4u })
				{
					SceneTester test(context, size);
					set_taa(test, true);
					test.add_instance(cube_mesh(test), glm::mat4(1.0f));
					context.check(test.render(4), "render at " + std::to_string(size) + " pixels with TAA on");
					context.check(test.frame() && test.frame(), "and keep rendering");
				}
			});

		register_render_check("taa_empty_scene", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				set_taa(test, true);
				context.check(test.render(4) && test.frame(), "TAA over a scene without instances");

				test.scene().set_camera_override(std::nullopt);
				context.check(test.render(4) && test.frame(), "and without a camera");
			});

		register_render_check("taa_sky_motion", [](RenderCheckContext& context)
			{
				auto render_sky = [&](bool taa, std::vector<u8>& pixels)
				{
					SceneTester test(context);
					set_taa(test, taa);
					SceneSettings settings;
					settings.Source = EnvironmentSource::Atmosphere;
					test.scene().set_settings(settings);
					test.add_light(test.sun(glm::vec3(0.4f, -0.7f, 0.3f), 1.0f, false));
					auto aim = [&](int step)
					{
						const float angle = glm::radians(2.0f * static_cast<float>(step));
						test.set_camera(glm::vec3(0.0f), glm::vec3(std::sin(angle), 0.4f, -std::cos(angle)));
					};
					aim(0);
					const bool ok = test.render();
					for (int step = 1; step <= 12; step++)
					{
						aim(step);
						test.frame();
					}
					test.read();
					pixels = test.pixels();
					return ok;
				};
				std::vector<u8> off;
				std::vector<u8> on;
				context.check(render_sky(false, off) && render_sky(true, on), "render the turning sky with and without TAA");
				u64 total = 0;
				for (size_t i = 0; i < on.size(); i++)
				{
					if ((i & 3) != 3)
					{
						total += static_cast<u64>(std::abs(static_cast<int>(on[i]) - static_cast<int>(off[i])));
					}
				}
				const double mean = static_cast<double>(total) / (static_cast<double>(on.size()) * 0.75);
				context.check(mean < 7.6, "the sky is not smeared by the turning camera (mean difference " + std::to_string(mean) + ")");
			});

		register_render_check("motion_blur_identity_when_still", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(cube_mesh(test, 0.5f, 2.0f), glm::mat4(1.0f));
				set_motion_blur(test, false);
				context.check(test.render(), "render with motion blur off");
				test.frame();
				test.read();
				const std::vector<u8> off = test.pixels();
				set_motion_blur(test, true);
				test.frame();
				test.read();
				context.check(SceneTester::max_difference(off, test.pixels()) == 0, "nothing moves: the image is unchanged");
			});

		register_render_check("motion_blur_along_velocity", [](RenderCheckContext& context)
			{
				const std::vector<u8> off = render_moving_box(context, false, 0.0167f, 32.0f, 20.0f, 1.0f / 60.0f);
				const std::vector<u8> on = render_moving_box(context, true, 0.0167f, 32.0f, 20.0f, 1.0f / 60.0f);
				const glm::ivec2 span = bright_span(off);
				context.check(span.y > span.x + 8, "the box is in view");
				compare_across_apis(context, "motion_blur", on);
				context.check(changed_outside(off, on, span) >= 2, "pixels beside the box along x are smeared");

				// Above and below the box, in its middle column, nothing changes.
				const int column = (span.x + span.y) / 2;
				int changed = 0;
				for (int y = 0; y < 64; y++)
				{
					const size_t offset = (static_cast<size_t>(y) * 64 + static_cast<size_t>(column)) * 4;
					const int box = SceneTester::luma(glm::ivec3(off[offset], off[offset + 1], off[offset + 2]));
					const int background = SceneTester::luma(glm::ivec3(off[4], off[5], off[6]));
					if (box <= background + 60 && std::abs(static_cast<int>(on[offset]) - static_cast<int>(off[offset])) > 3)
					{
						changed++;
					}
				}
				context.check(changed == 0, "pixels above and below the box are not smeared (" + std::to_string(changed) + ")");
			});

		register_render_check("motion_blur_clamp", [](RenderCheckContext& context)
			{
				const std::vector<u8> off = render_moving_box(context, false, 0.0167f, 4.0f, 20.0f, 1.0f / 60.0f);
				const std::vector<u8> on = render_moving_box(context, true, 0.0167f, 4.0f, 20.0f, 1.0f / 60.0f);
				const int changed = changed_outside(off, on, bright_span(off));
				context.check(changed >= 1 && changed <= 5, "a smear limited to 4 pixels reaches at most 4 + 1 pixels beyond the box (" + std::to_string(changed) + ")");
			});

		register_render_check("motion_blur_frame_rate_independent", [](RenderCheckContext& context)
			{
				int length[2];
				int index = 0;
				for (const float delta : { 1.0f / 60.0f, 1.0f / 30.0f })
				{
					const std::vector<u8> off = render_moving_box(context, false, 0.03f, 100.0f, 20.0f, delta);
					const std::vector<u8> on = render_moving_box(context, true, 0.03f, 100.0f, 20.0f, delta);
					length[index++] = changed_outside(off, on, bright_span(off));
				}
				context.check(length[0] > 5, "the smear is long enough to measure (" + std::to_string(length[0]) + ")");
				context.check(std::abs(length[0] - length[1]) <= std::max(2, length[0] / 10), "the same speed gives the same smear at 60 and 30 frames per second (" + std::to_string(length[0]) + ", " + std::to_string(length[1]) + ")");
			});

		register_render_check("motion_blur_camera_cut", [](RenderCheckContext& context)
			{
				auto jump = [&](bool blur)
				{
					SceneTester test(context);
					set_motion_blur(test, blur);
					const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.0f, 0.0f, 1.0f));
					test.add_instance(cube_mesh(test, 0.6f, 2.0f), turn);
					test.add_instance(cube_mesh(test, 0.6f, 2.0f), glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 0.0f, 0.0f)) * turn);
					test.render();
					test.set_camera(glm::vec3(60.0f, 0.0f, 4.0f), glm::vec3(60.0f, 0.0f, 0.0f));
					test.frame();
					test.read();
					return test.pixels();
				};
				context.check(SceneTester::max_difference(jump(false), jump(true)) <= 1, "a camera jump of 60 units does not blur");
			});

		register_render_check("motion_blur_debug_velocity", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				set_motion_blur(test, false);
				const EntityGuid box = test.add_instance(cube_mesh(test, 0.5f, 2.0f), glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.0f, 0.0f)));
				test.render();
				auto move_and_read = [&](float x)
				{
					test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));
					test.frame();
					test.read();
					return test.pixels();
				};

				move_and_read(-0.4f);
				test.renderer().get_scene_data().set_debug_velocity(true);
				move_and_read(-0.3f);
				const glm::ivec3 inside = test.pixel(test.to_pixel(glm::vec3(-0.3f, 0.0f, 0.5f)));
				const glm::ivec3 corner = test.pixel(1, 1);
				context.check(inside.r > 40 && inside.g < 8 && inside.b < 8, "a box moving along x is red in the velocity view");
				context.check(corner.r < 3 && corner.g < 3 && corner.b < 3, "the still background is black");

				test.renderer().get_scene_data().set_debug_velocity(false);
				test.set_instance_world(box, glm::translate(glm::mat4(1.0f), glm::vec3(-0.3f, 0.0f, 0.0f)));
				test.frame();
				test.read();
				context.check(SceneTester::luma(test.pixel(1, 1)) > 8 && SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(-0.3f, 0.0f, 0.5f)))) > 100, "switching the view off brings the scene back");
			});

		register_render_check("motion_blur_tiny_viewport", [](RenderCheckContext& context)
			{
				for (const u32 size : { 1u, 3u, 4u })
				{
					SceneTester test(context, size);
					set_motion_blur(test, true);
					test.add_instance(cube_mesh(test), glm::mat4(1.0f));
					context.check(test.render(4), "render at " + std::to_string(size) + " pixels with motion blur on");
					context.check(test.frame() && test.frame(), "and keep rendering");
				}
			});

		register_render_check("motion_blur_empty_scene", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				set_motion_blur(test, true);
				test.renderer().get_scene_data().set_debug_velocity(true);
				context.check(test.render(4) && test.frame(), "motion blur over a scene without instances");

				test.scene().set_camera_override(std::nullopt);
				context.check(test.render(4) && test.frame(), "and without a camera");
				test.renderer().get_scene_data().set_debug_velocity(false);
			});

		register_render_check("temporal_chain_scene", [](RenderCheckContext& context)
			{
				align_jitter(context);
				SceneTester test(context, 96);
				set_taa(test, true);
				test.scene().set_settings(chain_scene_settings(true, true));
				const EntityGuid box = add_chain_scene(test);
				context.check(test.render(), "render the scene with the whole chain on");
				advance_to(test, 16);
				for (int i = 1; i <= 12; i++)
				{
					test.set_instance_world(box, chain_box_world(-1.0f + 0.04f * static_cast<float>(i)));
					context.check(test.frame(), "render frame " + std::to_string(i));
				}
				test.read();
				const std::vector<u8> pixels = test.pixels();

				// TAA and motion blur actually acted: the image is not what the plain chain gives.
				SceneTester plain(context, 96);
				plain.scene().set_settings(chain_scene_settings(false, true));
				const EntityGuid plainBox = add_chain_scene(plain);
				plain.render();
				plain.set_instance_world(plainBox, chain_box_world(-1.0f + 0.04f * 12.0f));
				plain.frame();
				plain.read();
				context.check(SceneTester::max_difference(plain.pixels(), pixels) > 0, "TAA and motion blur change the image");
				compare_across_apis(context, "temporal_chain", pixels);
			});

		register_render_check("temporal_off_is_identity", [](RenderCheckContext& context)
			{
				SceneTester test(context, 96);
				test.scene().set_settings(chain_scene_settings(false, false));
				add_chain_scene(test);
				context.check(test.render(8), "render with TAA and motion blur off");
				test.frame();
				test.read();
				const std::vector<u8> off = test.pixels();

				SceneSettings blur = chain_scene_settings(false, false);
				blur.MotionBlur.Enabled = true;
				test.scene().set_settings(blur);
				test.frame();
				test.read();
				context.check(SceneTester::max_difference(off, test.pixels()) == 0, "motion blur over a still scene leaves the whole chain unchanged");
			});

		register_render_check("temporal_fxaa_exclusive", [](RenderCheckContext& context)
			{
				auto render_with = [&](bool fxaa)
				{
					align_jitter(context);
					SceneTester test(context, 96);
					test.scene().set_settings(chain_scene_settings(false, false));
					add_chain_scene(test);
					RenderQuality quality = test.renderer().get_quality();
					quality.Taa = true;
					quality.Fxaa = fxaa;
					test.renderer().set_quality(quality);
					test.render();
					advance_to(test, 16);
					test.frame();
					test.read();
					return std::make_pair(test.renderer().get_quality().Fxaa, test.pixels());
				};
				const auto both = render_with(true);
				const auto taaOnly = render_with(false);
				context.check(!both.first, "TAA and FXAA together leave FXAA off");
				context.check(SceneTester::max_difference(both.second, taaOnly.second) == 0, "the image is the TAA-only image");
			});
	}
}
