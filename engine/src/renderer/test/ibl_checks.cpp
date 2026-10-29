#include <pch.h>
#include "render_test.h"

#include "../ibl/ibl_math.h"

// Checks of the image-based lighting math. Plain logic, no GPU.
namespace wiwa
{
	namespace
	{
		float mean_of(const CubeMap& cube)
		{
			double total = 0.0;
			size_t count = 0;
			for (const std::vector<glm::vec3>& face : cube.Faces)
			{
				for (const glm::vec3& texel : face)
				{
					total += (texel.x + texel.y + texel.z) / 3.0;
					count++;
				}
			}
			return static_cast<float>(total / static_cast<double>(count));
		}

		float face_mean(const CubeMap& cube, u32 face)
		{
			double total = 0.0;
			for (const glm::vec3& texel : cube.Faces[face])
			{
				total += (texel.x + texel.y + texel.z) / 3.0;
			}
			return static_cast<float>(total / static_cast<double>(cube.Faces[face].size()));
		}

		bool finite(const CubeMap& cube)
		{
			for (const std::vector<glm::vec3>& face : cube.Faces)
			{
				for (const glm::vec3& texel : face)
				{
					if (!std::isfinite(texel.x) || !std::isfinite(texel.y) || !std::isfinite(texel.z))
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	void register_ibl_checks()
	{
		register_render_check("ibl_cube_roundtrip", [](RenderCheckContext& context)
			{
				bool ok = true;
				for (u32 face = 0; face < 6; face++)
				{
					for (int i = 0; i < 5; i++)
					{
						for (int j = 0; j < 5; j++)
						{
							const float u = 0.1f + 0.2f * static_cast<float>(i);
							const float v = 0.1f + 0.2f * static_cast<float>(j);
							glm::vec2 uv;
							const u32 found = cube_face_uv(cube_direction(face, u, v), uv);
							ok = ok && found == face && std::abs(uv.x - u) < 1e-4f && std::abs(uv.y - v) < 1e-4f;
						}
					}
				}
				context.check(ok, "every direction maps back to its face and uv");
			});

		register_render_check("ibl_sh_constant", [](RenderCheckContext& context)
			{
				const glm::vec3 color(0.5f, 0.25f, 1.0f);
				const CubeMap cube = make_cube(16, [&](const glm::vec3&) { return color; });
				const std::array<glm::vec4, 9> sh = project_sh9(cube);
				bool ok = true;
				for (const glm::vec3 normal : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1) })
				{
					const glm::vec3 value = evaluate_sh_irradiance(sh, normal);
					ok = ok && glm::all(glm::lessThan(glm::abs(value - color), color * 0.02f + 1e-4f));
				}
				context.check(ok, "a constant environment gives that color in every direction");
			});

		register_render_check("ibl_sh_directional", [](RenderCheckContext& context)
			{
				const CubeMap cube = make_cube(16, [](const glm::vec3& d) { return d.y > 0.5f ? glm::vec3(4.0f) : glm::vec3(0.0f); });
				const std::array<glm::vec4, 9> sh = project_sh9(cube);
				const glm::vec3 up = evaluate_sh_irradiance(sh, glm::vec3(0, 1, 0));
				const glm::vec3 down = evaluate_sh_irradiance(sh, glm::vec3(0, -1, 0));
				context.check(up.x > 3.0f * down.x && up.x > 0.1f, "the light from above lights the top more than the bottom");
				bool nonNegative = true;
				for (int i = 0; i < 50; i++)
				{
					const glm::vec3 normal = glm::normalize(glm::vec3(std::sin(i * 1.7f), std::cos(i * 0.9f), std::sin(i * 0.3f) + 0.1f));
					const glm::vec3 value = evaluate_sh_irradiance(sh, normal);
					nonNegative = nonNegative && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
				}
				context.check(nonNegative, "the irradiance is never negative");
			});

		register_render_check("ibl_mips_energy", [](RenderCheckContext& context)
			{
				const CubeMap cube = make_cube(32, [](const glm::vec3&) { return glm::vec3(0.5f); });
				const IblResult result = build_environment(cube, 2, 32);
				context.check(result.Mips.size() == ENV_MIP_COUNT, "the mip count");
				bool sizes = true;
				bool energy = true;
				for (u32 mip = 0; mip < result.Mips.size(); mip++)
				{
					sizes = sizes && result.Mips[mip].Size == std::max(1u, 32u >> mip);
					energy = energy && std::abs(mean_of(result.Mips[mip]) - 0.5f) < 0.015f;
				}
				context.check(sizes, "mip k has size max(1, 32 >> k)");
				context.check(energy, "every mip of a constant environment keeps its radiance");
			});

		register_render_check("ibl_brdf_lut", [](RenderCheckContext& context)
			{
				const std::vector<glm::vec2> table = make_brdf_lut(32, 128);
				context.check(table.size() == 32 * 32, "the size");
				bool finiteAndBounded = true;
				for (const glm::vec2& entry : table)
				{
					finiteAndBounded = finiteAndBounded && std::isfinite(entry.x) && std::isfinite(entry.y) && entry.x >= 0.0f && entry.y >= 0.0f && entry.x + entry.y <= 1.05f;
				}
				context.check(finiteAndBounded, "values are finite, not negative, and never reflect more than they receive");
				const glm::vec2 smooth = table[0 * 32 + 31]; // roughness near 0, n.v near 1
				context.check(smooth.x + smooth.y > 0.9f, "a smooth surface seen head on reflects almost everything");
			});

		register_render_check("ibl_equirect", [](RenderCheckContext& context)
			{
				constexpr u32 W = 64;
				constexpr u32 H = 32;
				std::vector<float> image(W * H * 3, 0.0f);
				for (u32 y = 0; y < H; y++)
				{
					for (u32 x = 0; x < W; x++)
					{
						const float value = y < H / 2 ? 2.0f : 0.1f;
						for (u32 c = 0; c < 3; c++)
						{
							image[(y * W + x) * 3 + c] = value;
						}
					}
				}
				image[5] = std::numeric_limits<float>::quiet_NaN();
				image[6] = -3.0f;
				const CubeMap cube = equirect_to_cube(image.data(), W, H, 16);
				context.check(cube.Size == 16 && finite(cube), "the cube is finite even with NaN and negative input");
				context.check(face_mean(cube, 2) > 4.0f * face_mean(cube, 3), "the bright upper half of the image ends up on +Y");
			});

		register_render_check("ibl_thread_independent", [](RenderCheckContext& context)
			{
				const CubeMap source = make_cube(16, [](const glm::vec3& d) { return glm::vec3(1.0f + d.x, 0.5f + 0.5f * d.y, 0.25f); });
				const std::vector<CubeMap> chain = build_source_chain(source);
				const CubeMap one = prefilter_specular(chain, 8, 0.5f, 16, 1);
				const CubeMap three = prefilter_specular(chain, 8, 0.5f, 16, 3);
				bool same = one.Size == three.Size;
				for (u32 face = 0; same && face < 6; face++)
				{
					same = one.Faces[face] == three.Faces[face];
				}
				context.check(same, "the result does not depend on the number of threads");
			});
	}
}
