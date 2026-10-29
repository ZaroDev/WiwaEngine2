#pragma once

#include <algorithm>
#include <cmath>

#include <math/math.h>

// The arithmetic of bloom that the tests check on the CPU. bloom_down.hlsl does the same on the GPU: keep them in step.
namespace wiwa
{
	// What fraction of a pixel's color goes into the bloom. Below threshold - knee nothing; between threshold - knee and
	// threshold + knee a quadratic blends smoothly into the hard threshold above it.
	inline float bloom_soft_knee_weight(float luma, float threshold, float knee)
	{
		const float soft = std::clamp(luma - threshold + knee, 0.0f, 2.0f * knee);
		const float softContribution = soft * soft / (4.0f * knee + 1e-5f);
		const float contribution = std::max(softContribution, luma - threshold);
		return std::max(contribution, 0.0f) / std::max(luma, 1e-4f);
	}

	// Number of levels (each half the size of the one before, the first half of the viewport): as requested, but the smallest level
	// keeps at least 2 pixels on its short side, and there are always between 2 and 6 of them.
	inline u32 bloom_mip_count(u32 requested, u32 width, u32 height)
	{
		const u32 shortSide = std::min(width, height);
		u32 fit = 0;
		while ((shortSide >> (fit + 1)) >= 2)
		{
			fit++;
		}
		return std::clamp(std::min(requested, fit), 2u, 6u);
	}
}
