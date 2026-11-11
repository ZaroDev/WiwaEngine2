#include <pch.h>
#include "render_test.h"

#include "cross_backend.h"
#include "scene_tester.h"

// Checks of the selection outline: where it is drawn, that it changes nothing without a selection, how it dims where the selection is
// hidden, and that a selection that is not drawable draws nothing. The expected pixels come from the same scene rendered without the
// selection: the silhouette is every pixel that is not background.
namespace wiwa
{
	namespace
	{
		constexpr int SIZE = 64;
		constexpr glm::ivec3 GREEN(0, 255, 0);

		SceneSettings bright_scene()
		{
			SceneSettings settings;
			settings.AmbientIntensity = 1.0f;
			settings.BackgroundColor = glm::vec3(0.0f);
			return settings;
		}

		SelectionOutlineStyle green_style(float width = 2.0f, float occludedAlpha = 0.35f)
		{
			SelectionOutlineStyle style;
			style.Color = glm::vec3(0.0f, 1.0f, 0.0f);
			style.WidthPx = width;
			style.OccludedAlpha = occludedAlpha;
			return style;
		}

		glm::ivec3 pixel_of(const std::vector<u8>& image, int x, int y)
		{
			const size_t offset = (static_cast<size_t>(y) * SIZE + static_cast<size_t>(x)) * 4;
			return glm::ivec3(image[offset], image[offset + 1], image[offset + 2]);
		}

		bool close_to(glm::ivec3 a, glm::ivec3 b, int tolerance)
		{
			return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
		}

		std::vector<bool> silhouette_of(const std::vector<u8>& image)
		{
			std::vector<bool> mask(static_cast<size_t>(SIZE) * SIZE, false);
			for (int y = 0; y < SIZE; y++)
			{
				for (int x = 0; x < SIZE; x++)
				{
					mask[static_cast<size_t>(y) * SIZE + x] = pixel_of(image, x, y) != glm::ivec3(0);
				}
			}
			return mask;
		}

		bool in(const std::vector<bool>& mask, int x, int y)
		{
			return x >= 0 && y >= 0 && x < SIZE && y < SIZE && mask[static_cast<size_t>(y) * SIZE + x];
		}

		// An outline pixel is outside the silhouette with a silhouette pixel within width (Euclidean, pixel centers).
		bool is_outline(const std::vector<bool>& silhouette, int x, int y, int width)
		{
			if (in(silhouette, x, y))
			{
				return false;
			}
			for (int dy = -width; dy <= width; dy++)
			{
				for (int dx = -width; dx <= width; dx++)
				{
					if (dx * dx + dy * dy <= width * width && in(silhouette, x + dx, y + dy))
					{
						return true;
					}
				}
			}
			return false;
		}

		AssetRef cube_mesh(SceneTester& test, glm::vec3 color, float half = 1.0f)
		{
			return test.add_mesh(make_test_box(glm::vec3(half), test.add_material(test.color_material(color))));
		}
	}

	void register_outline_checks()
	{
		register_render_check("outline_edge_pixels", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				test.scene().set_settings(bright_scene());
				const EntityGuid cube = test.add_instance(cube_mesh(test, glm::vec3(0.8f, 0.4f, 0.1f)), glm::mat4(1.0f));
				context.check(test.render(), "render without a selection");
				const std::vector<u8> baseline = test.pixels();
				const std::vector<bool> silhouette = silhouette_of(baseline);
				int inside = 0;
				for (bool covered : silhouette)
				{
					inside += covered ? 1 : 0;
				}
				context.check(inside > 400, "the cube covers a good part of the image");

				for (int width : { 1, 2, 3 })
				{
					test.scene().set_outline_style(green_style(static_cast<float>(width)));
					test.scene().set_selection(cube);
					context.check(test.render(), "render with the cube selected");
					const std::vector<u8> selected = test.pixels();

					int wrong = 0;
					int outlined = 0;
					for (int y = 0; y < SIZE; y++)
					{
						for (int x = 0; x < SIZE; x++)
						{
							const bool expected = is_outline(silhouette, x, y, width);
							outlined += expected ? 1 : 0;
							const glm::ivec3 got = pixel_of(selected, x, y);
							wrong += (expected ? !close_to(got, GREEN, 2) : got != pixel_of(baseline, x, y)) ? 1 : 0;
						}
					}
					context.check(outlined > 40, "the outline has pixels (width " + std::to_string(width) + ")");
					context.check(wrong == 0, "the outline is where the silhouette's neighbours are, and nothing else changes (width " + std::to_string(width) + ", " + std::to_string(wrong) + " wrong)");
				}
			});

		register_render_check("outline_unselected_identical", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				test.scene().set_settings(bright_scene());
				const EntityGuid cube = test.add_instance(cube_mesh(test, glm::vec3(0.2f, 0.6f, 0.9f)), glm::mat4(1.0f));
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				test.scene().set_outline_style(green_style());
				test.scene().set_selection(cube);
				context.check(test.render(), "render selected");
				context.check(SceneTester::max_difference(baseline, test.pixels()) > 100, "the outline shows");

				test.scene().set_selection(std::nullopt);
				context.check(test.render(), "render with the selection cleared");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "no selection is bit-identical to the baseline");

				SelectionOutlineStyle disabled = green_style();
				disabled.Enabled = false;
				test.scene().set_outline_style(disabled);
				test.scene().set_selection(cube);
				context.check(test.render(), "render selected with the outline disabled");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "a disabled outline is bit-identical to the baseline");
			});

		register_render_check("outline_occluded_dimmer", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				test.scene().set_settings(bright_scene());
				const EntityGuid selected = test.add_instance(cube_mesh(test, glm::vec3(0.8f, 0.4f, 0.1f)), glm::mat4(1.0f));
				context.check(test.render(), "render the selected cube alone");
				const std::vector<u8> alone = test.pixels();
				const std::vector<bool> silhouette = silhouette_of(alone);

				// A smaller cube in front of the selected one's right edge.
				test.add_instance(cube_mesh(test, glm::vec3(0.1f, 0.3f, 1.0f), 0.4f), glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 1.5f)));
				context.check(test.render(), "render with the cube in front");
				const std::vector<u8> baseline = test.pixels();

				const float occludedAlpha = 0.35f;
				test.scene().set_outline_style(green_style(2.0f, occludedAlpha));
				test.scene().set_selection(selected);
				context.check(test.render(), "render with the cube behind selected");
				const std::vector<u8> outlined = test.pixels();

				// A silhouette pixel is visible when nothing in front changes it.
				auto visible = [&](int x, int y) { return in(silhouette, x, y) && pixel_of(alone, x, y) == pixel_of(baseline, x, y); };

				int wrong = 0;
				int full = 0;
				int dim = 0;
				for (int y = 0; y < SIZE; y++)
				{
					for (int x = 0; x < SIZE; x++)
					{
						const glm::ivec3 base = pixel_of(baseline, x, y);
						const glm::ivec3 got = pixel_of(outlined, x, y);
						if (!is_outline(silhouette, x, y, 2))
						{
							wrong += got != base ? 1 : 0;
							continue;
						}

						bool anyVisible = false;
						for (int dy = -2; dy <= 2; dy++)
						{
							for (int dx = -2; dx <= 2; dx++)
							{
								anyVisible = anyVisible || (dx * dx + dy * dy <= 4 && visible(x + dx, y + dy));
							}
						}
						const float alpha = anyVisible ? 1.0f : occludedAlpha;
						full += anyVisible ? 1 : 0;
						dim += anyVisible ? 0 : 1;
						const glm::vec3 expected = glm::vec3(base) + (glm::vec3(GREEN) - glm::vec3(base)) * alpha;
						wrong += close_to(got, glm::ivec3(glm::round(expected)), 3) ? 0 : 1;
					}
				}
				context.check(full > 20 && dim > 10, "the scene has both visible and hidden outline (" + std::to_string(full) + " and " + std::to_string(dim) + ")");
				context.check(wrong == 0, "hidden outline is dimmer, visible outline is full, nothing else changes (" + std::to_string(wrong) + " wrong)");
				compare_across_apis(context, "outline", outlined);
			});

		register_render_check("outline_missing_entity", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				test.scene().set_settings(bright_scene());
				const EntityGuid gone = test.add_instance(cube_mesh(test, glm::vec3(0.5f)), glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)));
				test.add_instance(cube_mesh(test, glm::vec3(0.7f, 0.2f, 0.2f), 0.5f), glm::translate(glm::mat4(1.0f), glm::vec3(1.2f, 0.0f, 0.0f)));
				const EntityGuid light = test.add_light(test.sun(glm::vec3(0.0f, 0.0f, -1.0f)));
				test.remove_instance(gone);
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				test.scene().set_outline_style(green_style());
				for (const EntityGuid guid : { gone, light, EntityGuid{ 0xDEAD, 0xBEEF } })
				{
					test.scene().set_selection(guid);
					context.check(test.render(), "render with a selection that cannot be drawn");
					context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "a removed entity, a light and an unknown guid draw no outline");
				}
			});
	}
}
