#pragma once

#include "asset_database.h"

namespace wiwa
{
	// Writes assets.wpak: every cooked asset in Library/ (after bringing it up to date), each LZ4-compressed with a content
	// hash. A game reads it through the AssetManager when it sits next to the program.
	class Packager
	{
	public:
		// Writes <outDirectory>/assets.wpak and returns whether it worked (after logging why not). It blocks: run it on a
		// background thread (AssetPipelineModule::pack_async) and pass a progress object to follow it; without one it just
		// runs to the end. A cancel request on the progress stops it between assets.
		static bool pack(AssetDatabase& database, const std::filesystem::path& outDirectory, JobProgress* progress = nullptr);
	};
}
