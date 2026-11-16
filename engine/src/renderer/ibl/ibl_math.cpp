#include <pch.h>
#include "ibl_math.h"

namespace wiwa
{

#include <algorithm>
#include <cmath>
#include <thread>

#include <glm/gtc/constants.hpp>

namespace
{
	constexpr float HALF_FLOAT_MAX = 65000.0f; // a little under what a 16-bit float can hold (65504)

	float saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }

	CubeMap allocate_cube(u32 size)
	{
		CubeMap cube;
		cube.Size = size;
		for (std::vector<glm::vec3>& face : cube.Faces)
		{
			face.assign(static_cast<size_t>(size) * size, glm::vec3(0.0f));
		}
		return cube;
	}

	// NaN, negative and huge values (Inf, a 1e9 sun) must not get further than the input.
	glm::vec3 sanitise(const glm::vec3& color)
	{
		glm::vec3 result;
		for (int c = 0; c < 3; c++)
		{
			const float value = color[c];
			result[c] = (value >= 0.0f) ? std::min(value, HALF_FLOAT_MAX) : 0.0f; // NaN fails the comparison
		}
		return result;
	}

	// The texel a direction falls in (nearest).
	const glm::vec3& nearest_texel(const CubeMap& cube, const glm::vec3& direction)
	{
		glm::vec2 uv;
		const u32 face = cube_face_uv(direction, uv);
		const u32 x = std::min(static_cast<u32>(std::max(uv.x, 0.0f) * cube.Size), cube.Size - 1);
		const u32 y = std::min(static_cast<u32>(std::max(uv.y, 0.0f) * cube.Size), cube.Size - 1);
		return cube.at(face, x, y);
	}

	// The 9 SH basis functions (see IblMath.h for the order).
	std::array<float, 9> sh_basis(const glm::vec3& d)
	{
		return {
			0.282095f,
			0.488603f * d.y,
			0.488603f * d.z,
			0.488603f * d.x,
			1.092548f * d.x * d.y,
			1.092548f * d.y * d.z,
			0.315392f * (3.0f * d.z * d.z - 1.0f),
			1.092548f * d.x * d.z,
			0.546274f * (d.x * d.x - d.y * d.y),
		};
	}

	// The cosine lobe of each coefficient: bands 0, 1, 2 -> 1, 2/3, 1/4 (the lobe's A_l, divided by pi).
	constexpr float SH_LOBE[9] = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
}

glm::vec3 cube_direction(u32 face, float u, float v)
{
	const float sc = 2.0f * u - 1.0f;
	const float tc = 2.0f * v - 1.0f;

	glm::vec3 direction;
	switch (face)
	{
	case 0:  direction = glm::vec3(1.0f, -tc, -sc); break;  // +X: sc = -z, tc = -y
	case 1:  direction = glm::vec3(-1.0f, -tc, sc); break;  // -X: sc = +z, tc = -y
	case 2:  direction = glm::vec3(sc, 1.0f, tc); break;    // +Y: sc = +x, tc = +z
	case 3:  direction = glm::vec3(sc, -1.0f, -tc); break;  // -Y: sc = +x, tc = -z
	case 4:  direction = glm::vec3(sc, -tc, 1.0f); break;   // +Z: sc = +x, tc = -y
	default: direction = glm::vec3(-sc, -tc, -1.0f); break; // -Z: sc = -x, tc = -y
	}
	return glm::normalize(direction);
}

u32 cube_face_uv(const glm::vec3& direction, glm::vec2& uv)
{
	const glm::vec3 a = glm::abs(direction);

	u32 face;
	float sc;
	float tc;
	float ma;
	if (a.x >= a.y && a.x >= a.z)
	{
		ma = a.x;
		face = (direction.x > 0.0f) ? 0u : 1u;
		sc = (face == 0) ? -direction.z : direction.z;
		tc = -direction.y;
	}
	else if (a.y >= a.z)
	{
		ma = a.y;
		face = (direction.y > 0.0f) ? 2u : 3u;
		sc = direction.x;
		tc = (face == 2) ? direction.z : -direction.z;
	}
	else
	{
		ma = a.z;
		face = (direction.z > 0.0f) ? 4u : 5u;
		sc = (face == 4) ? direction.x : -direction.x;
		tc = -direction.y;
	}

	uv = glm::vec2(0.5f * (sc / ma + 1.0f), 0.5f * (tc / ma + 1.0f));
	return face;
}

CubeMap make_cube(u32 size, const std::function<glm::vec3(const glm::vec3&)>& radiance)
{
	CubeMap cube = allocate_cube(size);
	for (u32 face = 0; face < 6; face++)
	{
		for (u32 y = 0; y < size; y++)
		{
			for (u32 x = 0; x < size; x++)
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
				const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
				cube.at(face, x, y) = radiance(cube_direction(face, u, v));
			}
		}
	}
	return cube;
}

glm::vec3 sample_cube(const CubeMap& cube, const glm::vec3& direction)
{
	glm::vec2 uv;
	const u32 face = cube_face_uv(direction, uv);
	const float size = static_cast<float>(cube.Size);

	// Continuous texel coordinates, texel centres at whole numbers.
	const float px = uv.x * size - 0.5f;
	const float py = uv.y * size - 0.5f;
	const int x0 = static_cast<int>(std::floor(px));
	const int y0 = static_cast<int>(std::floor(py));
	const float fx = px - static_cast<float>(x0);
	const float fy = py - static_cast<float>(y0);

	auto fetch = [&](int x, int y) -> glm::vec3
	{
		if (x >= 0 && y >= 0 && x < static_cast<int>(cube.Size) && y < static_cast<int>(cube.Size))
		{
			return cube.at(face, static_cast<u32>(x), static_cast<u32>(y));
		}

		// Over the edge: the direction through this (extended) texel centre belongs to the neighbouring face.
		const float u = (static_cast<float>(x) + 0.5f) / size;
		const float v = (static_cast<float>(y) + 0.5f) / size;
		return nearest_texel(cube, cube_direction(face, u, v));
	};

	const glm::vec3 top = glm::mix(fetch(x0, y0), fetch(x0 + 1, y0), fx);
	const glm::vec3 bottom = glm::mix(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), fx);
	return glm::mix(top, bottom, fy);
}

CubeMap equirect_to_cube(const float* rgb, u32 width, u32 height, u32 size)
{
	std::vector<glm::vec3> image(static_cast<size_t>(width) * height);
	for (size_t i = 0; i < image.size(); i++)
	{
		image[i] = sanitise(glm::vec3(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]));
	}

	// Several image texels per cube texel would alias: average them down first.
	while (width > 4 * size && width >= 2 && height >= 2)
	{
		const u32 newWidth = width / 2;
		const u32 newHeight = height / 2;
		std::vector<glm::vec3> smaller(static_cast<size_t>(newWidth) * newHeight);
		for (u32 y = 0; y < newHeight; y++)
		{
			for (u32 x = 0; x < newWidth; x++)
			{
				smaller[y * newWidth + x] = 0.25f * (image[(2 * y) * width + 2 * x] + image[(2 * y) * width + 2 * x + 1]
					+ image[(2 * y + 1) * width + 2 * x] + image[(2 * y + 1) * width + 2 * x + 1]);
			}
		}
		image = std::move(smaller);
		width = newWidth;
		height = newHeight;
	}

	const float pi = glm::pi<float>();
	return make_cube(size, [&](const glm::vec3& d)
	{
		const float u = std::atan2(d.z, d.x) / (2.0f * pi) + 0.5f;
		const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / pi;

		// Bilinear: wraps around horizontally, stops at the poles.
		const float px = u * static_cast<float>(width) - 0.5f;
		const float py = v * static_cast<float>(height) - 0.5f;
		const int x0 = static_cast<int>(std::floor(px));
		const int y0 = static_cast<int>(std::floor(py));
		const float fx = px - static_cast<float>(x0);
		const float fy = py - static_cast<float>(y0);

		auto fetch = [&](int x, int y)
		{
			const int wrapped = ((x % static_cast<int>(width)) + static_cast<int>(width)) % static_cast<int>(width);
			const int clamped = std::clamp(y, 0, static_cast<int>(height) - 1);
			return image[static_cast<size_t>(clamped) * width + static_cast<size_t>(wrapped)];
		};

		const glm::vec3 top = glm::mix(fetch(x0, y0), fetch(x0 + 1, y0), fx);
		const glm::vec3 bottom = glm::mix(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), fx);
		return glm::mix(top, bottom, fy);
	});
}

std::vector<CubeMap> build_source_chain(const CubeMap& environment)
{
	std::vector<CubeMap> chain;
	chain.push_back(environment);

	while (chain.back().Size > 1)
	{
		const CubeMap& previous = chain.back();
		CubeMap next = allocate_cube(previous.Size / 2);
		for (u32 face = 0; face < 6; face++)
		{
			for (u32 y = 0; y < next.Size; y++)
			{
				for (u32 x = 0; x < next.Size; x++)
				{
					next.at(face, x, y) = 0.25f * (previous.at(face, 2 * x, 2 * y) + previous.at(face, 2 * x + 1, 2 * y)
						+ previous.at(face, 2 * x, 2 * y + 1) + previous.at(face, 2 * x + 1, 2 * y + 1));
				}
			}
		}
		chain.push_back(std::move(next));
	}
	return chain;
}

glm::vec3 sample_chain(const std::vector<CubeMap>& chain, const glm::vec3& direction, float lod)
{
	const float last = static_cast<float>(chain.size() - 1);
	lod = std::clamp(lod, 0.0f, last);

	const size_t lower = static_cast<size_t>(std::floor(lod));
	const size_t upper = std::min(lower + 1, chain.size() - 1);
	return glm::mix(sample_cube(chain[lower], direction), sample_cube(chain[upper], direction), lod - static_cast<float>(lower));
}

std::array<glm::vec4, 9> project_sh9(const CubeMap& cube)
{
	std::array<glm::dvec3, 9> sum{};
	double weightSum = 0.0;

	const double size = cube.Size;
	for (u32 face = 0; face < 6; face++)
	{
		for (u32 y = 0; y < cube.Size; y++)
		{
			for (u32 x = 0; x < cube.Size; x++)
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(cube.Size);
				const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(cube.Size);
				const double a = 2.0 * u - 1.0;
				const double b = 2.0 * v - 1.0;

				// The solid angle of the texel: the face is seen through a plane 1 away, texels are 2 / size wide.
				const double solidAngle = 4.0 / (size * size * std::pow(1.0 + a * a + b * b, 1.5));
				const glm::vec3 radiance = glm::min(cube.at(face, x, y), glm::vec3(ENV_MAX_RADIANCE));

				const std::array<float, 9> basis = sh_basis(cube_direction(face, u, v));
				for (int i = 0; i < 9; i++)
				{
					sum[i] += glm::dvec3(radiance) * (static_cast<double>(basis[i]) * solidAngle);
				}
				weightSum += solidAngle;
			}
		}
	}

	// The texel solid angles add up to 4 pi only approximately; making it exact keeps a constant
	// environment exactly constant (a white furnace stays white).
	const double scale = 4.0 * glm::pi<double>() / weightSum;

	std::array<glm::vec4, 9> coefficients;
	for (int i = 0; i < 9; i++)
	{
		coefficients[i] = glm::vec4(glm::vec3(sum[i] * scale * static_cast<double>(SH_LOBE[i])), 0.0f);
	}
	return coefficients;
}

glm::vec3 evaluate_sh_irradiance(const std::array<glm::vec4, 9>& sh, const glm::vec3& normal)
{
	const std::array<float, 9> basis = sh_basis(normal);
	glm::vec3 result(0.0f);
	for (int i = 0; i < 9; i++)
	{
		result += glm::vec3(sh[i]) * basis[i];
	}
	return glm::max(result, glm::vec3(0.0f));
}

// ---- Specular ----------------------------------------------------------------------------------

namespace
{
	// The i-th of n points of the Hammersley set: evenly spread in the unit square, the same every time.
	glm::vec2 hammersley(u32 i, u32 n)
	{
		u32 bits = i;
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return glm::vec2(static_cast<float>(i) / static_cast<float>(n), static_cast<float>(bits) * 2.3283064365386963e-10f);
	}

	// A half vector distributed like the GGX normal distribution, in a frame where the normal is +z.
	glm::vec3 importance_sample_ggx(const glm::vec2& xi, float alpha)
	{
		const float phi = 2.0f * glm::pi<float>() * xi.x;
		const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
		const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
		return glm::vec3(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
	}

	float distribution_ggx(float n_dot_h, float alpha)
	{
		const float a2 = alpha * alpha;
		const float d = n_dot_h * n_dot_h * (a2 - 1.0f) + 1.0f;
		return a2 / (glm::pi<float>() * d * d);
	}

	// One face of a prefiltered mip.
	void prefilter_face(const std::vector<CubeMap>& chain, CubeMap& result, u32 face, float roughness, u32 sampleCount)
	{
		const float alpha = roughness * roughness;
		const float size = static_cast<float>(result.Size);

		// The sky one texel of the source stands for, as a solid angle (a sixth of the sphere per face).
		const float sourceSize = static_cast<float>(chain[0].Size);
		const float texelSolidAngle = 4.0f * glm::pi<float>() / (6.0f * sourceSize * sourceSize);

		for (u32 y = 0; y < result.Size; y++)
		{
			for (u32 x = 0; x < result.Size; x++)
			{
				const glm::vec3 normal = cube_direction(face, (static_cast<float>(x) + 0.5f) / size, (static_cast<float>(y) + 0.5f) / size);
				const glm::vec3 up = (std::fabs(normal.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
				const glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
				const glm::vec3 bitangent = glm::cross(normal, tangent);

				glm::vec3 sum(0.0f);
				float weight = 0.0f;
				for (u32 i = 0; i < sampleCount; i++)
				{
					const glm::vec3 local = importance_sample_ggx(hammersley(i, sampleCount), alpha);
					const glm::vec3 half = tangent * local.x + bitangent * local.y + normal * local.z;

					// View = normal: the light direction is the view direction mirrored around the half vector.
					const float n_dot_h = local.z;
					const glm::vec3 light = 2.0f * n_dot_h * half - normal;
					const float n_dot_l = glm::dot(normal, light);
					if (n_dot_l <= 0.0f)
					{
						continue;
					}

					// With view = normal, n.h = v.h and the sample's density is D / 4. A sample that stands for
					// more sky than a source texel reads a blurrier source mip (+1 trades a little blur for noise).
					const float pdf = distribution_ggx(n_dot_h, alpha) * 0.25f;
					const float sampleSolidAngle = 1.0f / (static_cast<float>(sampleCount) * pdf + 1e-6f);
					const float lod = 0.5f * std::log2(sampleSolidAngle / texelSolidAngle) + 1.0f;

					sum += glm::min(sample_chain(chain, light, lod), glm::vec3(ENV_MAX_RADIANCE)) * n_dot_l;
					weight += n_dot_l;
				}

				result.at(face, x, y) = (weight > 0.0f) ? sum / weight : glm::min(sample_chain(chain, normal, 0.0f), glm::vec3(ENV_MAX_RADIANCE));
			}
		}
	}
}

CubeMap prefilter_specular(const std::vector<CubeMap>& chain, u32 size, float roughness, u32 sampleCount, u32 threadCount)
{
	CubeMap result = allocate_cube(size);

	if (roughness <= 0.0f)
	{
		// A mirror: the source itself, from the mip that has this size.
		const float lod = std::log2(static_cast<float>(chain[0].Size) / static_cast<float>(size));
		for (u32 face = 0; face < 6; face++)
		{
			for (u32 y = 0; y < size; y++)
			{
				for (u32 x = 0; x < size; x++)
				{
					const glm::vec3 direction = cube_direction(face, (static_cast<float>(x) + 0.5f) / static_cast<float>(size),
						(static_cast<float>(y) + 0.5f) / static_cast<float>(size));
					result.at(face, x, y) = sample_chain(chain, direction, lod);
				}
			}
		}
		return result;
	}

	// Every texel only reads the chain and writes itself, so faces can go to different threads freely.
	const u32 threads = std::clamp(threadCount, 1u, 6u);
	std::vector<std::thread> workers;
	for (u32 t = 0; t < threads; t++)
	{
		workers.emplace_back([&, t]()
		{
			for (u32 face = t; face < 6; face += threads)
			{
				prefilter_face(chain, result, face, roughness, sampleCount);
			}
		});
	}
	for (std::thread& worker : workers)
	{
		worker.join();
	}
	return result;
}

std::vector<glm::vec2> make_brdf_lut(u32 size, u32 sampleCount)
{
	std::vector<glm::vec2> lut(static_cast<size_t>(size) * size);

	for (u32 iy = 0; iy < size; iy++)
	{
		const float roughness = (static_cast<float>(iy) + 0.5f) / static_cast<float>(size);
		const float alpha = roughness * roughness;
		const float k = alpha / 2.0f; // the geometry term's remapping for image-based lighting

		for (u32 ix = 0; ix < size; ix++)
		{
			const float n_dot_v = (static_cast<float>(ix) + 0.5f) / static_cast<float>(size);
			const glm::vec3 view(std::sqrt(1.0f - n_dot_v * n_dot_v), 0.0f, n_dot_v);

			float scale = 0.0f;
			float bias = 0.0f;
			for (u32 i = 0; i < sampleCount; i++)
			{
				const glm::vec3 half = importance_sample_ggx(hammersley(i, sampleCount), alpha);
				const glm::vec3 light = 2.0f * glm::dot(view, half) * half - view;

				const float n_dot_l = light.z;
				const float n_dot_h = half.z;
				const float v_dot_h = glm::dot(view, half);
				if (n_dot_l <= 0.0f || n_dot_h <= 0.0f || v_dot_h <= 0.0f)
				{
					continue;
				}

				const float geometry = (n_dot_l / (n_dot_l * (1.0f - k) + k)) * (n_dot_v / (n_dot_v * (1.0f - k) + k));
				const float visibility = geometry * v_dot_h / (n_dot_h * n_dot_v);
				const float fresnel = std::pow(1.0f - v_dot_h, 5.0f);

				scale += (1.0f - fresnel) * visibility;
				bias += fresnel * visibility;
			}
			lut[static_cast<size_t>(iy) * size + ix] = glm::vec2(scale, bias) / static_cast<float>(sampleCount);
		}
	}
	return lut;
}

IblResult build_environment(const CubeMap& source, u32 threadCount, u32 brdfSize)
{
	// The clamp goes on the source before it is averaged into mips: clamping after averaging would let a
	// 1e5 texel spread over its neighbours and stay far brighter than intended.
	CubeMap clamped = source;
	for (std::vector<glm::vec3>& face : clamped.Faces)
	{
		for (glm::vec3& texel : face)
		{
			texel = glm::min(texel, glm::vec3(ENV_MAX_RADIANCE));
		}
	}
	const std::vector<CubeMap> chain = build_source_chain(clamped);

	IblResult data;
	data.Mips.push_back(source);
	for (u32 mip = 1; mip < ENV_MIP_COUNT; mip++)
	{
		const float roughness = static_cast<float>(mip) / static_cast<float>(ENV_MIP_COUNT - 1);
		data.Mips.push_back(prefilter_specular(chain, std::max(1u, source.Size >> mip), roughness, ENV_PREFILTER_SAMPLES[mip], threadCount));
	}

	data.SH = project_sh9(clamped);
	data.BrdfLut = make_brdf_lut(brdfSize, 128);
	return data;
}
}
