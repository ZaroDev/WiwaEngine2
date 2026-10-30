#pragma once

#include <cmath>
#include <limits>

#include <core/common.h>

// Reads the 16-bit floats of RG16F / RGBA16F targets back on the CPU, for the render checks.
namespace wiwa
{
	inline float half_to_float(u16 h)
	{
		const int sign = (h >> 15) & 1;
		const int exponent = (h >> 10) & 31;
		const int mantissa = h & 1023;
		float value;
		if (exponent == 0)
		{
			value = std::ldexp(static_cast<float>(mantissa), -24);
		}
		else if (exponent == 31)
		{
			value = std::numeric_limits<float>::infinity();
		}
		else
		{
			value = std::ldexp(static_cast<float>(mantissa + 1024), exponent - 25);
		}
		return sign ? -value : value;
	}
}
