#include <pch.h>
#include "environment_pack.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/packing.hpp>

namespace wiwa
{
	namespace
	{
		constexpr float MAX_HALF = 65000.0f;

		u16 to_half(float value)
		{
			if (!std::isfinite(value))
			{
				value = 0.0f;
			}
			return glm::packHalf1x16(std::clamp(value, 0.0f, MAX_HALF));
		}
	}

	EnvironmentData pack_environment(const IblResult& result)
	{
		EnvironmentData data;
		data.FaceSize = result.Mips.empty() ? 0 : result.Mips[0].Size;
		data.SH = result.SH;
		for (const CubeMap& cube : result.Mips)
		{
			std::vector<u16> mip;
			mip.reserve(static_cast<size_t>(6) * cube.Size * cube.Size * 4);
			for (const std::vector<glm::vec3>& face : cube.Faces)
			{
				for (const glm::vec3& texel : face)
				{
					mip.push_back(to_half(texel.x));
					mip.push_back(to_half(texel.y));
					mip.push_back(to_half(texel.z));
					mip.push_back(glm::packHalf1x16(1.0f));
				}
			}
			data.Mips.push_back(std::move(mip));
		}
		return data;
	}

	IblResult unpack_environment(const EnvironmentData& data)
	{
		IblResult result;
		result.SH = data.SH;
		for (size_t level = 0; level < data.Mips.size(); level++)
		{
			CubeMap cube;
			cube.Size = std::max<u32>(1, data.FaceSize >> level);
			const size_t texels = static_cast<size_t>(cube.Size) * cube.Size;
			for (u32 face = 0; face < 6; face++)
			{
				cube.Faces[face].resize(texels);
				for (size_t i = 0; i < texels; i++)
				{
					const size_t offset = (face * texels + i) * 4;
					cube.Faces[face][i] = glm::vec3(glm::unpackHalf1x16(data.Mips[level][offset]), glm::unpackHalf1x16(data.Mips[level][offset + 1]),
						glm::unpackHalf1x16(data.Mips[level][offset + 2]));
				}
			}
			result.Mips.push_back(std::move(cube));
		}
		return result;
	}
}
