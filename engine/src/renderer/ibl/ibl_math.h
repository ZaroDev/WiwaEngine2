#pragma once

#include <array>
#include <functional>
#include <vector>

#include <math/math.h>

// Image-based lighting, the CPU side: turns an environment (a cubemap of incoming light) into what the
// shaders sample. No GPU code here, so everything is tested on its own (tests/IblMathTest.cpp).
//
//   environment cube --> SH9              diffuse light: 9 numbers per color channel
//                   \--> prefiltered mips specular light: blurrier mip = rougher surface
//   BRDF table                            how much of the specular light a surface really reflects
//
// The environment itself is mip 0 of the specular chain (roughness 0 = a mirror), so the sky behind
// the scene comes from the same texture.

namespace wiwa
{
constexpr u32 ENV_FACE_SIZE = 512; // size of one face of the environment (mip 0)
constexpr u32 ENV_MIP_COUNT = 7;   // 512, 256, 128, 64, 32, 16, 8: mip k is prefiltered for roughness k / 6
constexpr u32 BRDF_LUT_SIZE = 128;

// Radiance fed into the SH and the prefilter is clamped to this: one very bright texel (a sun in a photo)
// would otherwise show up as noise (fireflies) in the blurred mips. Mip 0, the sky itself, is not clamped.
constexpr float ENV_MAX_RADIANCE = 100.0f;

// Samples per texel when prefiltering mip k. Mip 0 is a copy.
constexpr u32 ENV_PREFILTER_SAMPLES[ENV_MIP_COUNT] = { 0, 32, 48, 64, 96, 128, 128 };

// ---- Cube maps ---------------------------------------------------------------------------------
//
// One mip level of a cubemap, RGB float. Faces are ordered +X, -X, +Y, -Y, +Z, -Z, the order of the GPU
// array slices. Inside a face, u runs left to right and v top to bottom; with `ma` the component of the
// direction along the face's axis, u = 0.5 * (sc / |ma| + 1) and v = 0.5 * (tc / |ma| + 1), where
//   +X: sc = -z, tc = -y    -X: sc = +z, tc = -y
//   +Y: sc = +x, tc = +z    -Y: sc = +x, tc = -z
//   +Z: sc = +x, tc = -y    -Z: sc = -x, tc = -y
// This is the OpenGL / D3D / Vulkan convention; getting it wrong would mirror every reflection.
struct CubeMap
{
	u32 Size{};
	std::array<std::vector<glm::vec3>, 6> Faces{}; // Faces[face][y * Size + x]

	glm::vec3& at(u32 face, u32 x, u32 y) { return Faces[face][y * Size + x]; }
	const glm::vec3& at(u32 face, u32 x, u32 y) const { return Faces[face][y * Size + x]; }
};

// The unit direction through a point of a face (u and v in [0, 1]).
glm::vec3 cube_direction(u32 face, float u, float v);

// The face a direction points at, and where on it (uv in [0, 1]).
u32 cube_face_uv(const glm::vec3& direction, glm::vec2& uv);

// A cube whose texels are `radiance` evaluated at the texel centres.
CubeMap make_cube(u32 size, const std::function<glm::vec3(const glm::vec3&)>& radiance);

// Bilinear lookup, seamless across face edges (a neighbouring texel that lies over an edge is found
// through its direction, not clamped to the face).
glm::vec3 sample_cube(const CubeMap& cube, const glm::vec3& direction);

// ---- Sources -----------------------------------------------------------------------------------

// An equirectangular image (RGB float, top row first) as a cube of `size` x `size` faces:
// u = atan2(z, x) / (2 pi) + 0.5, v = acos(y) / pi. NaN and negative texels become 0 and everything is
// clamped to 65000 (what a half float can hold), so nothing downstream can turn into Inf or NaN.
// An image wider than 4 * size is box-downsampled first, so the cube is not aliased.
CubeMap equirect_to_cube(const float* rgb, u32 width, u32 height, u32 size);

// ---- Filtering ---------------------------------------------------------------------------------

// The cube and its 2x2 box-filtered mips, down to 1x1. The input of filtered importance sampling.
std::vector<CubeMap> build_source_chain(const CubeMap& environment);

// Trilinear lookup in a chain from build_source_chain.
glm::vec3 sample_chain(const std::vector<CubeMap>& chain, const glm::vec3& direction, float lod);

// ---- Diffuse: spherical harmonics --------------------------------------------------------------
//
// The light arriving from every direction, as 9 coefficients (bands 0-2) per color channel (rgb in xyz).
// Order: Y00, Y1-1 (y), Y10 (z), Y11 (x), Y2-2 (xy), Y2-1 (yz), Y20 (3z^2 - 1), Y21 (xz), Y22 (x^2 - y^2),
// with the usual constants 0.282095; 0.488603; 1.092548, 1.092548, 0.315392, 1.092548, 0.546274.
// The coefficients already include the cosine lobe (factors 1, 2/3, 1/4 for bands 0, 1, 2, after dividing by
// pi), so the irradiance divided by pi is the plain sum of coefficient times basis function: the same
// quantity the shader's SampleIrradiance returns (albedo times it is the diffuse light).
std::array<glm::vec4, 9> project_sh9(const CubeMap& cube);

// E(normal) / pi. Never negative (a band-2 fit of a very bright light can ring below zero).
glm::vec3 evaluate_sh_irradiance(const std::array<glm::vec4, 9>& sh, const glm::vec3& normal);

// ---- Specular: the prefiltered chain -----------------------------------------------------------

// One mip of the specular chain: what a surface of this roughness reflects, per direction. GGX importance
// sampling (Hammersley points, alpha = roughness^2, view = normal = reflection direction), with filtered
// importance sampling: each sample reads the source mip that matches how much sky it stands for, so a
// few samples are enough. Roughness 0 returns the source resampled to `size`. The faces are split over
// up to `threadCount` threads (0 counts as 1); the result does not depend on it.
CubeMap prefilter_specular(const std::vector<CubeMap>& chain, u32 size, float roughness, u32 sampleCount, u32 threadCount);

// The split-sum BRDF table: for each (n.v, roughness) the scale and the bias of the base reflectance,
// reflected = f0 * scale + bias. [y * size + x], x = n.v, y = roughness, texel centres at (i + 0.5) / size.
std::vector<glm::vec2> make_brdf_lut(u32 size, u32 sampleCount);

// Everything the GPU needs.
struct IblResult
{
	std::vector<CubeMap> Mips{};         // [0] = the environment, [k] = prefiltered for roughness k / (ENV_MIP_COUNT - 1)
	std::array<glm::vec4, 9> SH{};
	std::vector<glm::vec2> BrdfLut{};
};

// Mip k has size max(1, source.Size >> k). Radiance is clamped at ENV_MAX_RADIANCE before the SH and the
// prefilter see it (mip 0 keeps the original). threadCount as in prefilter_specular.
IblResult build_environment(const CubeMap& source, u32 threadCount, u32 brdfSize = BRDF_LUT_SIZE);
}
