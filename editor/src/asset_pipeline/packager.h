#pragma once

#include <unordered_set>

#include "asset_database.h"
#include "build_settings.h"
#include "physics_settings_file.h"

namespace wiwa
{
	// A cooked asset the packer may take: what it needs, and whether it belongs to the engine (always packed).
	struct PackCandidate
	{
		AssetGuid Guid{};
		std::vector<AssetGuid> Dependencies{};
		bool IsEngine = false;
	};

	// The assets a pack holds under these settings: the engine's, the chosen scenes and the always-included assets, and everything
	// those need, transitively. Guids the settings name that are not among the candidates are added to `missing`.
	std::unordered_set<AssetGuid> select_pack_set(const std::vector<PackCandidate>& candidates, const BuildSettings& settings, std::vector<AssetGuid>& missing);

	// Writes assets.wpak: the cooked assets in Library/ (after bringing it up to date) that the build settings choose, each
	// LZ4-compressed with a content hash. A game reads it through the AssetManager when it sits next to the program.
	class Packager
	{
	public:
		// Writes <outDirectory>/assets.wpak and returns whether it worked (after logging why not). It blocks: run it on a
		// background thread (AssetPipelineModule::pack_async) and pass a progress object to follow it; without one it just
		// runs to the end. A cancel request on the progress stops it between assets.
		// The project's physics layers (physicsSettingsFile, if it exists) go into the pack too.
		// With settings the pack holds only what they choose, its catalog lists only that, and the default scene is recorded in
		// its build info; without them (null) it holds every cooked asset.
		static bool pack(AssetDatabase& database, const std::filesystem::path& outDirectory, JobProgress* progress = nullptr, const BuildSettings* settings = nullptr,
			const std::filesystem::path& physicsSettingsFile = PHYSICS_SETTINGS_FILE);
	};
}
