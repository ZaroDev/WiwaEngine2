#pragma once

#include "asset_database.h"

namespace wiwa
{
	// Writes assets.wpak: every cooked asset in Library/ (after bringing it up to date), each LZ4-compressed with a content
	// hash. A game reads it through the AssetManager when it sits next to the program.
	class Packager
	{
	public:
		// Writes <outDirectory>/assets.wpak. Returns false (after logging why) if anything could not be packed.
		static bool pack(AssetDatabase& database, const std::filesystem::path& outDirectory);
	};
}
