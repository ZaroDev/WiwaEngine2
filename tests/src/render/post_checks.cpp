#include <pch.h>
#include <filesystem>
#include <fstream>

#include <renderer/passes/bloom_math.h>
#include "half_float.h"
#include "scene_tester.h"

// Checks of the post-processing chain: tone mapping and grading, exposure, bloom, FXAA, ambient occlusion.
namespace wiwa
{
	namespace
	{
		SceneSettings dark(glm::vec3 background = glm::vec3(0.0f))
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.0f;
			settings.BackgroundColor = background;
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

		// The octahedral mapping of Common.hlsli.
		glm::vec2 octahedral_encode(glm::vec3 n)
		{
			n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
			glm::vec2 encoded(n.x, n.y);
			if (n.z < 0.0f)
			{
				encoded = (glm::vec2(1.0f) - glm::vec2(std::abs(n.y), std::abs(n.x))) * glm::vec2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
			}
			return encoded;
		}

		glm::vec3 octahedral_decode(glm::vec2 e)
		{
			glm::vec3 n(e.x, e.y, 1.0f - std::abs(e.x) - std::abs(e.y));
			if (n.z < 0.0f)
			{
				const glm::vec2 xy = (glm::vec2(1.0f) - glm::vec2(std::abs(n.y), std::abs(n.x))) * glm::vec2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
				n.x = xy.x;
				n.y = xy.y;
			}
			return glm::normalize(n);
		}

		// The normal the prepass wrote at a pixel, decoded.
		bool read_normal(RenderCheckContext& context, SceneTester& test, int x, int y, glm::vec3& normal, glm::vec2* encoded = nullptr)
		{
			std::vector<u8> bytes;
			u32 width = 0;
			u32 height = 0;
			if (!read_texture_slice_bytes(context.Device, test.renderer().get_viewport().normal(), 0, 0, bytes, width, height, 4))
			{
				return false;
			}
			const size_t offset = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
			u16 raw[2];
			std::memcpy(raw, bytes.data() + offset, sizeof(raw));
			const glm::vec2 e(half_to_float(raw[0]), half_to_float(raw[1]));
			normal = octahedral_decode(e);
			if (encoded != nullptr)
			{
				*encoded = e;
			}
			return true;
		}

		// A floor and a wall that meet in a crease, seen from the front and above, with a bit of ambient light.
		void add_corner(SceneTester& test)
		{
			const AssetRef material = test.add_material(test.color_material(glm::vec3(0.8f)));
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(3.0f, 0.02f, 3.0f), material)), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f)));
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(3.0f, 1.5f, 0.02f), material)), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, -1.0f)));
			test.set_camera(glm::vec3(0.0f, 1.0f, 4.0f), glm::vec3(0.0f, -0.5f, -1.0f));
		}

		SceneSettings ao_settings(bool enabled)
		{
			SceneSettings settings = dark();
			settings.AmbientIntensity = 1.0f;
			settings.Ao.Enabled = enabled;
			settings.Ao.Radius = 1.5f;
			settings.Ao.Power = 2.5f;
			return settings;
		}

		// A corner with a bright lamp and a sunlit box: something for every effect to act on.
		void build_chain_scene(SceneTester& test)
		{
			add_corner(test);
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.3f), test.add_material(emissive_material(glm::vec3(6.0f, 5.0f, 3.0f))))), glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, -0.6f, 0.0f)));
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.4f), test.add_material(test.color_material(glm::vec3(0.9f, 0.2f, 0.1f), 0.4f)))),
				glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -0.5f, 0.0f)), glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
			test.add_light(test.sun(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), 2.0f));
		}

		SceneSettings chain_settings(bool effects)
		{
			SceneSettings settings = ao_settings(effects);
			settings.AmbientIntensity = 0.6f;
			settings.Bloom.Enabled = effects;
			settings.Bloom.Threshold = 0.8f;
			settings.Bloom.Intensity = 0.2f;
			settings.Exposure.Auto = effects;
			settings.Exposure.AdaptSpeed = 20.0f;
			settings.Exposure.MinEv = -4.0f;
			settings.Exposure.MaxEv = 4.0f;
			settings.Tonemap.Operator = effects ? TonemapOperator::Neutral : TonemapOperator::ACES;
			settings.Tonemap.Saturation = effects ? 1.2f : 1.0f;
			settings.Tonemap.Vignette = effects ? 0.3f : 0.0f;
			return settings;
		}

		void set_fxaa(SceneTester& test, bool on)
		{
			RenderQuality quality = test.renderer().get_quality();
			quality.Fxaa = on;
			test.renderer().set_quality(quality);
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

		register_render_check("bloom_spreads", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(30.0f))))), glm::scale(glm::mat4(1.0f), glm::vec3(0.3f)));
				SceneSettings off = dark();
				off.Bloom.Enabled = false;
				test.scene().set_settings(off);
				context.check(test.render(), "render without bloom");
				int right = 32;
				while (right < 63 && SceneTester::luma(test.pixel(right + 1, 32)) > 0)
				{
					right++;
				}
				const int probe = std::min(right + 4, 63);
				const int without = SceneTester::luma(test.pixel(probe, 32));

				SceneSettings on = dark();
				on.Bloom.Enabled = true;
				on.Bloom.Intensity = 0.3f;
				test.scene().set_settings(on);
				context.check(test.render(), "render with bloom");
				const int with = SceneTester::luma(test.pixel(probe, 32));
				WI_LOG(Info, "RenderTest", "bloom_spreads probe {0} (edge {1}): {2} -> {3}", probe, right, without, with);
				context.check(right < 60 && without == 0, "the pixel next to the source is dark without bloom");
				context.check(with > without + 3, "the glow reaches it with bloom");
			});

		register_render_check("bloom_threshold", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.4f))))), glm::scale(glm::mat4(1.0f), glm::vec3(0.3f)));
				SceneSettings off = dark();
				off.Bloom.Enabled = false;
				test.scene().set_settings(off);
				context.check(test.render(), "render without bloom");
				const std::vector<u8> plain = test.pixels();

				SceneSettings on = dark();
				on.Bloom.Enabled = true;
				on.Bloom.Intensity = 0.3f;
				test.scene().set_settings(on);
				context.check(test.render(), "render with bloom");
				context.check(SceneTester::max_difference(plain, test.pixels()) == 0, "a scene below the threshold is the same with bloom on");
			});

		register_render_check("bloom_tiny_viewport", [](RenderCheckContext& context)
			{
				for (u32 size : { 1u, 3u, 5u })
				{
					SceneTester test(context, size);
					test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(30.0f))))), glm::mat4(1.0f));
					SceneSettings on = dark();
					on.Bloom.Enabled = true;
					on.Bloom.Intensity = 0.3f;
					test.scene().set_settings(on);
					context.check(test.render(), "render a " + std::to_string(size) + " pixel viewport with bloom");
				}
			});

		register_render_check("bloom_math", [](RenderCheckContext& context)
			{
				context.check(bloom_soft_knee_weight(0.4f, 1.0f, 0.5f) == 0.0f, "nothing below threshold - knee");
				context.check(std::abs(bloom_soft_knee_weight(3.0f, 1.0f, 0.5f) - 2.0f / 3.0f) < 1e-4f, "above the knee the part over the threshold counts");
				const float inside = bloom_soft_knee_weight(1.0f, 1.0f, 0.5f);
				context.check(inside > 0.0f && inside < 0.5f, "inside the knee it blends in");
				context.check(bloom_mip_count(6, 1920, 1080) == 6 && bloom_mip_count(6, 1, 1) == 2 && bloom_mip_count(99, 64, 64) == 5 && bloom_mip_count(1, 1920, 1080) == 2, "the mip count follows the request and the size");
			});

		register_render_check("fxaa_softens_edge", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				// a diamond: its edges run diagonally, which stair-steps
				const glm::mat4 turned = glm::rotate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f)), glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.8f))))), turned);
				test.scene().set_settings(dark());
				context.check(test.render(), "render without FXAA");
				const std::vector<u8> plain = test.pixels();
				const int plateau = SceneTester::luma(test.pixel(32, 32));

				auto intermediate = [&](const std::vector<u8>& pixels)
				{
					int count = 0;
					for (size_t i = 0; i + 3 < pixels.size(); i += 4)
					{
						const int luma = (pixels[i] * 3 + pixels[i + 1] * 6 + pixels[i + 2]) / 10;
						count += (luma > plateau / 10 && luma < plateau * 9 / 10) ? 1 : 0;
					}
					return count;
				};

				Renderer& renderer = test.renderer();
				RenderQuality quality = renderer.get_quality();
				quality.Fxaa = true;
				renderer.set_quality(quality);
				context.check(test.render(), "render with FXAA");
				const int without = intermediate(plain);
				const int with = intermediate(test.pixels());
				WI_LOG(Info, "RenderTest", "fxaa_softens_edge intermediate pixels {0} -> {1}", without, with);
				context.check(with > without + 8, "FXAA adds in-between values along the edge");
				context.check(SceneTester::luma(test.pixel(32, 32)) == plateau, "the inside of the shape is untouched");
			});

		register_render_check("fxaa_flat_unchanged", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.scene().set_settings(dark(glm::vec3(0.3f, 0.2f, 0.1f)));
				context.check(test.render(), "render a flat image");
				const std::vector<u8> plain = test.pixels();

				Renderer& renderer = test.renderer();
				RenderQuality quality = renderer.get_quality();
				quality.Fxaa = true;
				renderer.set_quality(quality);
				context.check(test.render(), "render it with FXAA");
				context.check(SceneTester::max_difference(plain, test.pixels()) == 0, "a flat image is bit-identical");
			});

		register_render_check("octahedral_roundtrip", [](RenderCheckContext& context)
			{
				u32 state = 12345;
				auto next = [&]() { state = state * 1664525u + 1013904223u; return static_cast<float>(state >> 8) / static_cast<float>(1 << 24) * 2.0f - 1.0f; };
				float worst = 0.0f;
				for (int i = 0; i < 1000; i++)
				{
					const glm::vec3 n = glm::normalize(glm::vec3(next(), next(), next()) + glm::vec3(0.001f));
					worst = std::max(worst, glm::length(octahedral_decode(octahedral_encode(n)) - n));
				}
				context.check(worst < 0.001f, "decode(encode(n)) is n");
				context.check(octahedral_encode(glm::vec3(0.0f, 0.0f, 1.0f)) == glm::vec2(0.0f), "+Z is the origin, the value the target is cleared to");
			});

		register_render_check("prepass_writes_normals", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				const MaterialData material = test.color_material(glm::vec3(0.5f));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(1.0f, 1.0f, 0.02f), test.add_material(material))), glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
				test.scene().set_settings(dark());
				context.check(test.render(), "render a quad turned 45 degrees about Y");
				glm::vec3 turned(0.0f);
				context.check(read_normal(context, test, 32, 32, turned), "read the normals");
				WI_LOG(Info, "RenderTest", "prepass_writes_normals turned ({0}, {1}, {2})", turned.x, turned.y, turned.z);
				context.check(glm::length(turned - glm::vec3(0.7071f, 0.0f, 0.7071f)) < 0.03f, "the face shows its world normal");

				glm::vec3 corner(0.0f);
				glm::vec2 encoded;
				context.check(read_normal(context, test, 2, 2, corner, &encoded), "read a pixel with no geometry");
				context.check(encoded == glm::vec2(0.0f), "no geometry leaves the cleared value");
			});

		register_render_check("prepass_back_face_normals", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				MaterialData material = test.color_material(glm::vec3(0.5f));
				material.DoubleSided = true;
				// turned away from the camera: only the back of the quad is seen
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(1.0f, 1.0f, 0.02f), test.add_material(material))), glm::rotate(glm::mat4(1.0f), glm::radians(180.0f + 45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
				test.scene().set_settings(dark());
				context.check(test.render(), "render a double-sided quad seen from behind");
				glm::vec3 seen(0.0f);
				context.check(read_normal(context, test, 32, 32, seen), "read the normals");
				context.check(glm::dot(seen, glm::normalize(glm::vec3(0.0f, 0.0f, 4.0f))) > 0.5f, "the normal faces the viewer");
			});

		register_render_check("prepass_masked_normals", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				TextureData texture;
				texture.Width = 2;
				texture.Height = 1;
				texture.Mips.push_back({ 255, 255, 255, 0, 255, 255, 255, 255 });
				texture.Mips.push_back({ 255, 255, 255, 128 });
				MaterialData material;
				material.BaseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
				material.Alpha = AlphaMode::Mask;
				material.AlphaCutoff = 0.5f;
				material.DoubleSided = false;
				material.BaseColorTexture = test.add_texture(std::move(texture));
				const glm::mat4 turned = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(1.0f, 1.0f, 0.02f), test.add_material(material))), turned);
				test.scene().set_settings(dark());
				context.check(test.render(), "render a cut-out quad");

				const glm::ivec2 cut = test.to_pixel(glm::vec3(turned * glm::vec4(-0.5f, 0.0f, 0.02f, 1.0f)));
				const glm::ivec2 solid = test.to_pixel(glm::vec3(turned * glm::vec4(0.5f, 0.0f, 0.02f, 1.0f)));
				glm::vec3 normal(0.0f);
				glm::vec2 encoded;
				context.check(read_normal(context, test, cut.x, cut.y, normal, &encoded), "read the cut-out half");
				context.check(encoded == glm::vec2(0.0f), "the cut-out half writes no normal");
				context.check(read_normal(context, test, solid.x, solid.y, normal, &encoded), "read the solid half");
				context.check(glm::length(normal - glm::vec3(0.7071f, 0.0f, 0.7071f)) < 0.03f, "the solid half writes its normal");
			});

		register_render_check("ao_corner_darker", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				add_corner(test);
				const glm::ivec2 crease = test.to_pixel(glm::vec3(0.0f, -0.98f, -0.9f));
				const glm::ivec2 open = test.to_pixel(glm::vec3(0.0f, -0.98f, 1.2f));

				test.scene().set_settings(ao_settings(false));
				context.check(test.render(), "render without AO");
				const int creaseOff = SceneTester::luma(test.pixel(crease));
				const int openOff = SceneTester::luma(test.pixel(open));

				test.scene().set_settings(ao_settings(true));
				context.check(test.render(), "render with AO");
				const int creaseOn = SceneTester::luma(test.pixel(crease));
				const int openOn = SceneTester::luma(test.pixel(open));
				WI_LOG(Info, "RenderTest", "ao_corner_darker crease {0} -> {1}, open floor {2} -> {3}", creaseOff, creaseOn, openOff, openOn);
				context.check(creaseOff > 20, "the crease is lit by the ambient light");
				context.check(creaseOn < creaseOff * 85 / 100, "the crease is darker with AO");
				context.check(std::abs(openOn - openOff) <= 8 && creaseOff - creaseOn > 5 * std::abs(openOn - openOff), "open floor is hardly darkened, the crease clearly is");
			});

		register_render_check("ao_off_equals_baseline", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				add_corner(test);
				test.scene().set_settings(ao_settings(false));
				context.check(test.render(), "render without AO");
				const std::vector<u8> baseline = test.pixels();

				test.scene().set_settings(ao_settings(true));
				context.check(test.render(), "render with AO");
				context.check(SceneTester::max_difference(baseline, test.pixels()) > 10, "AO changes the image");

				test.scene().set_settings(ao_settings(false));
				context.check(test.render(), "render without AO again");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "turning it off gives the baseline back, bit for bit");
			});

		register_render_check("ao_direct_light_untouched", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				add_corner(test);
				test.add_light(test.sun(glm::vec3(0.0f, -1.0f, -0.3f)));
				const glm::ivec2 crease = test.to_pixel(glm::vec3(0.0f, -0.98f, -0.9f));

				SceneSettings off = ao_settings(false);
				off.AmbientIntensity = 0.0f;
				test.scene().set_settings(off);
				context.check(test.render(), "render without AO");
				const glm::ivec3 without = test.pixel(crease);

				SceneSettings on = ao_settings(true);
				on.AmbientIntensity = 0.0f;
				test.scene().set_settings(on);
				context.check(test.render(), "render with AO");
				const glm::ivec3 with = test.pixel(crease);
				WI_LOG(Info, "RenderTest", "ao_direct_light_untouched ({0}, {1}, {2}) -> ({3}, {4}, {5})", without.r, without.g, without.b, with.r, with.g, with.b);
				context.check(SceneTester::luma(without) > 20, "the crease is lit by the sun");
				context.check(without == with, "direct light is not occluded");
			});

		register_render_check("ao_scene_swap", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				add_corner(test);
				test.scene().set_settings(ao_settings(false));
				context.check(test.render(), "render without AO");
				const std::vector<u8> baseline = test.pixels();

				test.scene().set_settings(ao_settings(true));
				context.check(test.render(), "render with AO");

				// another scene with the same geometry and no AO: nothing of the old occlusion is left
				SceneDelta reset;
				reset.Reset = true;
				reset.Settings = ao_settings(false);
				test.scene().apply(std::move(reset));
				add_corner(test);
				context.check(test.render(), "render the other scene");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "the new scene shows no occlusion from the old one");
			});

		register_render_check("post_chain_scene", [](RenderCheckContext& context)
			{
				SceneTester test(context, 96);
				build_chain_scene(test);
				test.scene().set_settings(chain_settings(false));
				context.check(test.render(), "render without the effects");
				const std::vector<u8> plain = test.pixels();

				test.scene().set_settings(chain_settings(true));
				set_fxaa(test, true);
				context.check(test.render(60), "render with every effect");
				const std::vector<u8> pixels = test.pixels();
				context.check(SceneTester::max_difference(plain, pixels) > 20, "the effects change the image");

				namespace fs = std::filesystem;
				const std::string own = std::string(graphics_api_name(context.Api));
				std::error_code error;
				fs::create_directories("Library", error);
				{
					std::ofstream file("Library/render_test_post_" + own + ".rgba", std::ios::binary | std::ios::trunc);
					file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
				}

				u32 compared = 0;
				for (const char* other : { "vulkan", "d3d11", "d3d12" })
				{
					if (own == other)
					{
						continue;
					}
					std::ifstream file(std::string("Library/render_test_post_") + other + ".rgba", std::ios::binary);
					if (!file)
					{
						continue;
					}
					std::vector<u8> theirs((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
					if (theirs.size() != pixels.size())
					{
						continue;
					}
					u64 total = 0;
					int worst = 0;
					for (size_t i = 0; i < pixels.size(); i++)
					{
						if ((i & 3) == 3)
						{
							continue;
						}
						const int difference = std::abs(static_cast<int>(pixels[i]) - static_cast<int>(theirs[i]));
						total += static_cast<u64>(difference);
						worst = std::max(worst, difference);
					}
					const double mean = static_cast<double>(total) / (static_cast<double>(pixels.size()) * 0.75);
					WI_LOG(Info, "RenderTest", "post_chain_scene: {0} against {1}: mean difference {2:.3f}, largest {3}", own, other, mean, worst);
					context.check(mean < 3.0 && worst <= 40, std::string("the image matches ") + other);
					compared++;
				}
				if (compared == 0)
				{
					WI_LOG(Info, "RenderTest", "post_chain_scene: the cross-backend comparison needs the other APIs' output (run --render-test on each API)");
				}
				set_fxaa(test, false);
			});

		register_render_check("post_chain_resize", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				build_chain_scene(test);
				test.scene().set_settings(chain_settings(true));
				set_fxaa(test, true);
				const u32 errorsBefore = get_error_message_count();
				for (const u32 size : { 64u, 128u, 1u, 2u, 7u, 64u })
				{
					test.set_size(size);
					context.check(test.render(3), "render at the new size");
					context.check(test.renderer().get_viewport().width() == size, "the target has the new size");
				}
				context.check(get_error_message_count() == errorsBefore, "no GPU error while resizing with every effect on");
				context.check(test.pixels().size() == 64 * 64 * 4 && SceneTester::luma(test.pixel(32, 32)) > 0, "the last image is a normal one");
				set_fxaa(test, false);
			});

		register_render_check("post_chain_live_changes", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				build_chain_scene(test);
				test.scene().set_settings(chain_settings(false));
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				// every effect on its own: it changes the image, and turning it off gives the baseline back bit for bit
				struct Effect
				{
					const char* Name;
					std::function<void(SceneSettings&, bool)> Settings;
					bool Fxaa;
				};
				const Effect effects[] = {
					{ "bloom", [](SceneSettings& s, bool on) { s.Bloom.Enabled = on; }, false },
					{ "AO", [](SceneSettings& s, bool on) { s.Ao.Enabled = on; }, false },
					{ "automatic exposure", [](SceneSettings& s, bool on) { s.Exposure.Auto = on; }, false },
					{ "tonemap operator", [](SceneSettings& s, bool on) { s.Tonemap.Operator = on ? TonemapOperator::Reinhard : TonemapOperator::ACES; }, false },
					{ "FXAA", [](SceneSettings&, bool) {}, true },
				};
				for (const Effect& effect : effects)
				{
					SceneSettings on = chain_settings(false);
					effect.Settings(on, true);
					test.scene().set_settings(on);
					set_fxaa(test, effect.Fxaa);
					context.check(test.render(30), std::string("render with ") + effect.Name);
					context.check(SceneTester::max_difference(baseline, test.pixels()) > 4, std::string(effect.Name) + " changes the image");

					test.scene().set_settings(chain_settings(false));
					set_fxaa(test, false);
					context.check(test.render(3), std::string("render without ") + effect.Name);
					context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, std::string("turning ") + effect.Name + " off gives the baseline back");
				}
			});

		register_render_check("post_chain_minimized", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				build_chain_scene(test);
				test.scene().set_settings(chain_settings(true));
				set_fxaa(test, true);
				context.check(test.render(40), "render with every effect");
				const std::vector<u8> before = test.pixels();

				RenderBatch minimized;
				minimized.Minimized = true;
				minimized.Width = test.renderer().get_width();
				minimized.Height = test.renderer().get_height();
				context.check(test.renderer().render_frame(minimized), "a minimized batch is accepted");
				RenderBatch empty;
				context.check(test.renderer().render_frame(empty), "so is a batch of size 0");

				context.check(test.render(3), "a normal batch renders again");
				context.check(SceneTester::max_difference(before, test.pixels()) <= 2, "and gives the same image");
				set_fxaa(test, false);
			});
	}
}
