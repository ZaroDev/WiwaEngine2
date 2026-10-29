#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "render_test.h"

// The cross-backend comparison of the scene checks: writes the RGBA8 image to Library/render_test_<name>_<api>.rgba and, for every other
// API whose file of the same size exists, requires a mean difference under 3 and no channel off by more than 40. Run --render-test on
// each API (Vulkan, D3D11, D3D12); the check that runs last compares against the other two.
namespace wiwa
{
	inline void compare_across_apis(RenderCheckContext& context, const std::string& name, const std::vector<u8>& pixels)
	{
		namespace fs = std::filesystem;
		const std::string own(graphics_api_name(context.Api));
		std::error_code error;
		fs::create_directories("Library", error);
		{
			std::ofstream file("Library/render_test_" + name + "_" + own + ".rgba", std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
		}

		u32 compared = 0;
		for (const char* other : { "vulkan", "d3d11", "d3d12" })
		{
			if (own == other)
			{
				continue;
			}
			std::ifstream file("Library/render_test_" + name + "_" + other + ".rgba", std::ios::binary);
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
			WI_LOG(Info, "RenderTest", "{0}: {1} against {2}: mean difference {3:.3f}, largest {4}", name, own, other, mean, worst);
			context.check(mean < 3.0 && worst <= 40, name + ": the image matches " + other);
			compared++;
		}
		if (compared == 0)
		{
			WI_LOG(Info, "RenderTest", "{0}: cross-backend comparison needs the other APIs' output (run --render-test on each API)", name);
		}
	}
}
