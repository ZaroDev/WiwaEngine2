#pragma once

#include <array>

#include <nvrhi/nvrhi.h>

#include <math/math.h>

namespace wiwa
{
	// An environment on the GPU: the cube (RGBA16F, mip 0 is the sky, the other mips are the roughness-prefiltered reflections) and the
	// diffuse light as spherical harmonics.
	struct GpuEnvironment
	{
		nvrhi::TextureHandle Cube{};
		u32 MipCount = 0;
		std::array<glm::vec4, 9> SH{};
	};
}
