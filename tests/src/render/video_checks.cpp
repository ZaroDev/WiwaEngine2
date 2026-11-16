#include <pch.h>
#include "render_test.h"

#include <renderer/video/video_texture.h>
#include <renderer/view/view_types.h>
#include "scene_tester.h"

// The pictures of a playing video on the screen: the render thread keeps one BGRA texture per stream under view_video_texture_id(stream) and the UI
// pass draws it like the picture of a view. Whole frames through the renderer with a hand-made UiDrawList, read back from the viewport image.
namespace wiwa
{
	namespace
	{
		constexpr u32 SIZE = 64;

		bool is_color(glm::ivec3 pixel, int r, int g, int b, int tolerance = 3)
		{
			return std::abs(pixel.r - r) <= tolerance && std::abs(pixel.g - g) <= tolerance && std::abs(pixel.b - b) <= tolerance;
		}

		// A picture of four blocks: top left red, top right green, bottom left blue, bottom right white. BGRA, as a decoder gives it.
		VideoFrame make_quadrants(u32 width, u32 height)
		{
			VideoFrame frame;
			frame.Width = width;
			frame.Height = height;
			frame.Bgra.resize(static_cast<size_t>(width) * height * 4);
			for (u32 y = 0; y < height; y++)
			{
				for (u32 x = 0; x < width; x++)
				{
					const bool right = x >= width / 2;
					const bool bottom = y >= height / 2;
					u8 b = 0, g = 0, r = 0;
					if (!right && !bottom) { r = 255; }
					else if (right && !bottom) { g = 255; }
					else if (!right && bottom) { b = 255; }
					else { r = g = b = 255; }
					u8* pixel = frame.Bgra.data() + (static_cast<size_t>(y) * width + x) * 4;
					pixel[0] = b;
					pixel[1] = g;
					pixel[2] = r;
					pixel[3] = 255;
				}
			}
			return frame;
		}

		// The whole canvas, one quad of the video's texture.
		void add_video_quad(UiDrawList& list, u32 texture)
		{
			UiBatch batch;
			batch.Kind = UiQuadKind::Image;
			batch.ViewTexture = texture;
			batch.ClipRect = glm::vec4(0.0f, 0.0f, list.CanvasSize.x, list.CanvasSize.y);
			batch.FirstIndex = static_cast<u32>(list.Indices.size());
			batch.IndexCount = 6;
			list.Batches.push_back(batch);

			const u32 white = ui_pack_color(glm::vec4(1.0f));
			const u32 base = static_cast<u32>(list.Vertices.size());
			list.Vertices.push_back(UiVertex{ { 0.0f, 0.0f }, { 0.0f, 0.0f }, white });
			list.Vertices.push_back(UiVertex{ { list.CanvasSize.x, 0.0f }, { 1.0f, 0.0f }, white });
			list.Vertices.push_back(UiVertex{ { list.CanvasSize.x, list.CanvasSize.y }, { 1.0f, 1.0f }, white });
			list.Vertices.push_back(UiVertex{ { 0.0f, list.CanvasSize.y }, { 0.0f, 1.0f }, white });
			for (const u32 offset : { 0u, 1u, 2u, 0u, 2u, 3u })
			{
				list.Indices.push_back(base + offset);
			}
		}

		void dark_scene(SceneTester& test)
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.0f;
			settings.BackgroundColor = glm::vec3(0.0f);
			test.scene().set_settings(settings);
			test.ui().CanvasSize = { static_cast<float>(SIZE), static_cast<float>(SIZE) };
		}
	}

	void register_video_checks()
	{
		register_render_check("video_frame_reaches_screen", [](RenderCheckContext& context)
			{
				constexpr u32 STREAM = 3;
				const u32 texture = view_video_texture_id(STREAM);

				{
					SceneTester test(context, SIZE);
					dark_scene(test);
					upload_video_picture(context.World, STREAM, make_quadrants(8, 8));
					context.check(context.World.get_renderer().get_textures().get(texture) != nullptr, "an uploaded picture is registered under the id of its stream");
					add_video_quad(test.ui(), texture);
					context.check(test.render(), "render the picture of a video");
					context.check(is_color(test.pixel(16, 16), 255, 0, 0), "BGRA to the screen: the top left block is red (not blue)");
					context.check(is_color(test.pixel(48, 16), 0, 255, 0), "the top right block is green");
					context.check(is_color(test.pixel(16, 48), 0, 0, 255), "the picture is the right way up: the bottom left block is blue");
					context.check(is_color(test.pixel(48, 48), 255, 255, 255), "the bottom right block is white");
				}

				// a picture of another size: the texture is made again under the same id
				{
					SceneTester test(context, SIZE);
					dark_scene(test);
					nvrhi::ITexture* before = context.World.get_renderer().get_textures().get(texture);
					upload_video_picture(context.World, STREAM, make_quadrants(4, 2));
					nvrhi::ITexture* after = context.World.get_renderer().get_textures().get(texture);
					context.check(after != nullptr && after->getDesc().width == 4 && after->getDesc().height == 2, "a picture of another size makes the texture again");
					context.check(before != nullptr && before != after, "and it is a new texture");
					add_video_quad(test.ui(), texture);
					context.check(test.render(), "render the smaller picture");
					// inside the blocks: a 4 x 2 picture is filtered, so the corners of the blocks blend
					context.check(is_color(test.pixel(8, 8), 255, 0, 0) && is_color(test.pixel(56, 8), 0, 255, 0) && is_color(test.pixel(8, 56), 0, 0, 255) && is_color(test.pixel(56, 56), 255, 255, 255), "the new size is drawn");
				}

				// the same size again: the texture stays, the content changes
				{
					SceneTester test(context, SIZE);
					dark_scene(test);
					nvrhi::ITexture* before = context.World.get_renderer().get_textures().get(texture);
					VideoFrame green;
					green.Width = 4;
					green.Height = 2;
					green.Bgra.assign(4 * 2 * 4, 0);
					for (size_t i = 0; i < green.Bgra.size(); i += 4) // B, G, R, A
					{
						green.Bgra[i + 1] = 255;
						green.Bgra[i + 3] = 255;
					}
					upload_video_picture(context.World, STREAM, green);
					context.check(context.World.get_renderer().get_textures().get(texture) == before, "a picture of the same size is written into the same texture");
					add_video_quad(test.ui(), texture);
					context.check(test.render(), "render the next picture");
					context.check(is_color(test.pixel(8, 8), 0, 255, 0) && is_color(test.pixel(56, 56), 0, 255, 0), "the content changed");
				}

				// a bad picture changes nothing
				{
					VideoFrame bad;
					bad.Width = 8;
					bad.Height = 8; // no pixels
					nvrhi::ITexture* before = context.World.get_renderer().get_textures().get(texture);
					upload_video_picture(context.World, STREAM, bad);
					VideoFrame empty;
					upload_video_picture(context.World, STREAM, empty);
					context.check(context.World.get_renderer().get_textures().get(texture) == before, "a picture without pixels, or without a size, is ignored");
				}

				// released: the stream's id is free and the quad draws nothing
				{
					SceneTester test(context, SIZE);
					dark_scene(test);
					release_video_picture(context.World, STREAM);
					context.check(context.World.get_renderer().get_textures().get(texture) == nullptr, "a released stream leaves the registry");
					release_video_picture(context.World, STREAM); // twice: nothing happens
					add_video_quad(test.ui(), texture);
					context.check(test.render(), "render with the stream released");
					context.check(is_color(test.pixel(16, 16), 0, 0, 0) && is_color(test.pixel(48, 48), 0, 0, 0), "a quad of a stream that is gone draws nothing");
				}
			});
	}
}
