#include <pch.h>
#include "render_test.h"

#include "scene_tester.h"

// The UI pass: whole frames through the renderer with a hand-made UiDrawList, read back from the viewport image.
namespace wiwa
{
	namespace
	{
		constexpr u32 SIZE = 64;

		void dark_scene(SceneTester& test)
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.0f;
			settings.BackgroundColor = glm::vec3(0.0f);
			test.scene().set_settings(settings);
		}

		bool is_color(glm::ivec3 pixel, int r, int g, int b, int tolerance = 3)
		{
			return std::abs(pixel.r - r) <= tolerance && std::abs(pixel.g - g) <= tolerance && std::abs(pixel.b - b) <= tolerance;
		}

		// A quad in its own batch, over the whole canvas unless a clip rect is given.
		void add_quad(UiDrawList& list, UiQuadKind kind, const AssetRef& texture, glm::vec2 min, glm::vec2 max, glm::vec4 color,
			std::optional<glm::vec4> clip = std::nullopt, float sdfRange = 0.0f, float pixelScale = 1.0f)
		{
			UiBatch batch;
			batch.Kind = kind;
			batch.Texture = texture;
			batch.ClipRect = clip.value_or(glm::vec4(0.0f, 0.0f, list.CanvasSize.x, list.CanvasSize.y));
			batch.FirstIndex = static_cast<u32>(list.Indices.size());
			batch.IndexCount = 6;
			batch.SdfRange = sdfRange;
			batch.PixelScale = pixelScale;
			list.Batches.push_back(batch);

			const u32 rgba = ui_pack_color(color);
			const u32 base = static_cast<u32>(list.Vertices.size());
			list.Vertices.push_back(UiVertex{ { min.x, min.y }, { 0.0f, 0.0f }, rgba });
			list.Vertices.push_back(UiVertex{ { max.x, min.y }, { 1.0f, 0.0f }, rgba });
			list.Vertices.push_back(UiVertex{ { max.x, max.y }, { 1.0f, 1.0f }, rgba });
			list.Vertices.push_back(UiVertex{ { min.x, max.y }, { 0.0f, 1.0f }, rgba });
			for (const u32 offset : { 0u, 1u, 2u, 0u, 2u, 3u })
			{
				list.Indices.push_back(base + offset);
			}
		}

		// A font of one character, 'I', a vertical bar: 8 texels wide and 24 tall in the middle of a 32 x 32 atlas, as a signed distance
		// field that reaches 4 texels either side of the outline.
		FontData make_bar_font()
		{
			constexpr u32 SIDE = 32;
			constexpr float RANGE = 4.0f;

			FontData font;
			font.Name = "bar";
			font.AtlasWidth = SIDE;
			font.AtlasHeight = SIDE;
			font.Atlas.resize(static_cast<size_t>(SIDE) * SIDE);
			for (u32 y = 0; y < SIDE; y++)
			{
				for (u32 x = 0; x < SIDE; x++)
				{
					const float cx = static_cast<float>(x) + 0.5f;
					const float cy = static_cast<float>(y) + 0.5f;
					const float dx = std::max(12.0f - cx, cx - 20.0f);
					const float dy = std::max(4.0f - cy, cy - 28.0f);
					const float outside = glm::length(glm::vec2(std::max(dx, 0.0f), std::max(dy, 0.0f)));
					const float signedDistance = (dx <= 0.0f && dy <= 0.0f) ? std::max(dx, dy) : outside; // positive outside
					font.Atlas[static_cast<size_t>(y) * SIDE + x] = static_cast<u8>(std::clamp(128.0f - signedDistance * 128.0f / RANGE, 0.0f, 255.0f));
				}
			}
			font.PixelSize = 32.0f;
			font.SdfRange = RANGE;
			font.LineHeight = 32.0f;
			font.Ascent = 32.0f;

			FontGlyph glyph;
			glyph.Codepoint = 'I';
			glyph.Size = { 32.0f, 32.0f };
			glyph.Advance = 32.0f;
			glyph.UvMin = { 0.0f, 0.0f };
			glyph.UvMax = { 1.0f, 1.0f };
			font.Glyphs.push_back(glyph);
			return font;
		}

		TextureData make_pixel_texture(u8 r, u8 g, u8 b)
		{
			TextureData texture;
			texture.Width = 1;
			texture.Height = 1;
			texture.Kind = TextureKind::Color;
			texture.Mips = { { r, g, b, 255 } };
			return texture;
		}
	}

	void register_ui_checks()
	{
		register_render_check("ui_solid_quad_pixels", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 8.0f, 8.0f }, { 40.0f, 40.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
				context.check(test.render(), "render a red quad");
				context.check(is_color(test.pixel(24, 24), 255, 0, 0), "inside the quad is red");
				context.check(is_color(test.pixel(7, 24), 0, 0, 0) && is_color(test.pixel(40, 24), 0, 0, 0) && is_color(test.pixel(24, 7), 0, 0, 0) && is_color(test.pixel(24, 40), 0, 0, 0), "outside the quad is the scene");
				context.check(is_color(test.pixel(8, 8), 255, 0, 0) && is_color(test.pixel(39, 39), 255, 0, 0), "the quad covers its corner pixels exactly");
			});

		register_render_check("ui_alpha_blend", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { 32.0f, 64.0f }, { 1.0f, 1.0f, 1.0f, 0.5f });
				context.check(test.render(), "render a half transparent quad");
				context.check(is_color(test.pixel(16, 32), 128, 128, 128, 3), "half white over black is mid grey");
				context.check(is_color(test.pixel(48, 32), 0, 0, 0), "and the rest is untouched");
			});

		register_render_check("ui_clip_rect", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { 64.0f, 64.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, glm::vec4(0.0f, 0.0f, 32.0f, 64.0f));
				context.check(test.render(), "render a clipped quad");
				context.check(is_color(test.pixel(16, 32), 255, 0, 0), "inside the clip rect it is drawn");
				context.check(is_color(test.pixel(48, 32), 0, 0, 0) && is_color(test.pixel(33, 10), 0, 0, 0), "outside the clip rect it is not");

				test.ui() = UiDrawList{};
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { 64.0f, 64.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, glm::vec4(10.0f, 10.0f, 10.0f, 50.0f));
				context.check(test.render(), "render with an empty clip rect");
				context.check(is_color(test.pixel(10, 30), 0, 0, 0) && is_color(test.pixel(30, 30), 0, 0, 0), "a clip rect without width draws nothing");
			});

		register_render_check("ui_image_pixels", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				const AssetRef green = test.add_texture(make_pixel_texture(0, 255, 0));
				const AssetRef grey = test.add_texture(make_pixel_texture(128, 128, 128));
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Image, green, { 0.0f, 0.0f }, { 32.0f, 32.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
				add_quad(test.ui(), UiQuadKind::Image, grey, { 32.0f, 0.0f }, { 64.0f, 32.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
				add_quad(test.ui(), UiQuadKind::Image, green, { 0.0f, 32.0f }, { 32.0f, 64.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
				context.check(test.render(), "render images");
				context.check(is_color(test.pixel(16, 16), 0, 255, 0), "an image shows its texture");
				context.check(is_color(test.pixel(48, 16), 128, 128, 128, 2), "the colours of an image are not changed on the way (sRGB in, sRGB out)");
				context.check(is_color(test.pixel(16, 48), 0, 0, 0), "a red tint takes the green out of a green image");
			});

		register_render_check("ui_sdf_text_pixels", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				const AssetRef font = test.cache().add_inline_font(make_bar_font());
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Sdf, font, { 16.0f, 16.0f }, { 48.0f, 48.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, std::nullopt, 4.0f, 1.0f);
				context.check(test.render(), "render a glyph");
				context.check(is_color(test.pixel(31, 32), 255, 255, 255, 4) && is_color(test.pixel(32, 24), 255, 255, 255, 4), "the middle of the bar is solid");
				context.check(is_color(test.pixel(22, 32), 0, 0, 0) && is_color(test.pixel(42, 32), 0, 0, 0), "beside the bar is the scene");
				context.check(is_color(test.pixel(32, 17), 0, 0, 0) && is_color(test.pixel(32, 46), 0, 0, 0), "above and below the bar is the scene");

				// the edge is smooth: a texel across it is neither all nor nothing at a scale of 1/8 (the bar is a pixel wide)
				test.ui() = UiDrawList{};
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Sdf, font, { 28.0f, 28.0f }, { 32.0f, 32.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, std::nullopt, 4.0f, 0.125f);
				context.check(test.render(), "render a glyph at a small scale");
				const int level = test.pixel(29, 29).r;
				context.check(level > 0 && level <= 255, "a small glyph still makes coverage");

				// the colour of the vertices is the colour of the text, its alpha the opacity
				test.ui() = UiDrawList{};
				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				add_quad(test.ui(), UiQuadKind::Sdf, font, { 16.0f, 16.0f }, { 48.0f, 48.0f }, { 0.0f, 0.0f, 1.0f, 0.5f }, std::nullopt, 4.0f, 1.0f);
				context.check(test.render(), "render a blue half transparent glyph");
				context.check(is_color(test.pixel(31, 32), 0, 0, 128, 4), "text takes the colour and the alpha of the vertices");
			});

		register_render_check("ui_canvas_scale", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.ui().CanvasSize = { 32.0f, 32.0f }; // a canvas half the size of the image: everything is drawn twice as big
				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { 16.0f, 16.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
				context.check(test.render(), "render on a smaller canvas");
				context.check(is_color(test.pixel(24, 24), 255, 0, 0) && is_color(test.pixel(40, 40), 0, 0, 0), "the quad is scaled from the canvas to the image");

				test.ui() = UiDrawList{};
				test.ui().CanvasSize = { 32.0f, 32.0f };
				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { 32.0f, 32.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, glm::vec4(0.0f, 0.0f, 16.0f, 32.0f));
				context.check(test.render(), "render a clipped quad on a smaller canvas");
				context.check(is_color(test.pixel(24, 40), 0, 255, 0) && is_color(test.pixel(40, 40), 0, 0, 0), "the clip rect is scaled with it");
			});

		register_render_check("ui_empty_list_noop", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(0.2f, 0.6f, 0.9f))))), glm::mat4(1.0f));
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				context.check(test.render(), "render with an empty list");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "an empty UI list leaves the image as it was");

				add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { 64.0f, 64.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
				context.check(test.render(), "render with a quad");
				context.check(SceneTester::max_difference(baseline, test.pixels()) > 100, "the quad is drawn over the scene");

				test.ui() = UiDrawList{};
				context.check(test.render(), "render with the list gone");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "and when it is gone the image is as it was");
			});

		register_render_check("ui_missing_atlas_skipped", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
				const AssetRef nowhere{ AssetGuid::generate(), 0 };
				add_quad(test.ui(), UiQuadKind::Sdf, nowhere, { 0.0f, 0.0f }, { 64.0f, 64.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, std::nullopt, 4.0f, 1.0f);
				add_quad(test.ui(), UiQuadKind::Image, nowhere, { 0.0f, 0.0f }, { 64.0f, 64.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
				add_quad(test.ui(), UiQuadKind::Sdf, AssetRef{}, { 0.0f, 0.0f }, { 64.0f, 64.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, std::nullopt, 4.0f, 1.0f);
				context.check(test.render(), "a frame with fonts and textures that do not exist still renders");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "and draws nothing for them");
			});

		register_render_check("ui_resize_rebuilds", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				auto draw = [&](u32 size)
				{
					test.set_size(size);
					test.ui() = UiDrawList{};
					test.ui().CanvasSize = { static_cast<float>(size), static_cast<float>(size) };
					add_quad(test.ui(), UiQuadKind::Solid, AssetRef{}, { 0.0f, 0.0f }, { static_cast<float>(size) / 2.0f, static_cast<float>(size) / 2.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
					return test.render();
				};

				context.check(draw(SIZE), "render at the first size");
				context.check(is_color(test.pixel(8, 8), 255, 0, 0) && is_color(test.pixel(48, 48), 0, 0, 0), "the quad is the top left quarter");
				context.check(draw(32), "render at 32");
				context.check(is_color(test.pixel(8, 8), 255, 0, 0) && is_color(test.pixel(24, 24), 0, 0, 0), "after a resize the quad is still the top left quarter");
				context.check(draw(1), "render at 1x1");
				context.check(draw(SIZE), "render at the original size again");
				context.check(is_color(test.pixel(8, 8), 255, 0, 0), "and the UI is back");
			});
	}
}
