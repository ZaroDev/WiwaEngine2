#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <math/math.h>

namespace wiwa
{
	// What `require` calls a script's source holds, found without running it.
	struct RequireScan
	{
		std::vector<std::string> Paths{}; // the arguments written as string literals, normalized, each once
		u32 DynamicCalls = 0;             // calls whose argument is not a string literal: they cannot be followed
	};

	// The path a `require` argument names: "assets/scripts/util" and "assets/scripts/util.lua" are the same script.
	std::string normalize_require_path(std::string path);

	// A text scan (comments are skipped), so it is only as exact as the way require is written: require("x"), require "x", require('x').
	RequireScan scan_requires(std::string_view source);
}
