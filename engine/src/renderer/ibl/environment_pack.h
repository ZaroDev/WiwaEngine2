#pragma once

#include <assets/asset_data.h>

#include "ibl_math.h"

namespace wiwa
{
	// The IBL result as the cooked asset: half floats, alpha 1 (values above what a half can hold are clamped).
	EnvironmentData pack_environment(const IblResult& result);
	// Back to float cubes (for tests and tools).
	IblResult unpack_environment(const EnvironmentData& data);
}
