#pragma once

#include <unordered_map>
#include <unordered_set>

#include <core/project_settings.h>

#include "asset_database.h"
#include "audio_settings_file.h"
#include "build_settings.h"
#include "input_settings_file.h"
#include "physics_settings_file.h"

namespace wiwa
{
	// A cooked asset the packer may take: what it needs, and whether it belongs to the engine (always packed).
	struct PackCandidate
	{
		AssetGuid Guid{};
		std::vector<AssetGuid> Dependencies{};
		bool IsEngine = false;
		bool IsAudioClip = false; // an AudioClip: nothing refers to it (an event names it), so the MiniAudio backend packs them all
	};

	// The assets a pack holds under these settings: the engine's, the chosen scenes and the always-included assets, and everything
	// those need, transitively. Guids the settings name that are not among the candidates are added to `missing`.
	// redirects (legacy guid -> the guid it now means) are followed on every guid the settings and the dependencies name, so a scene that still names a
	// sub-asset a model import moved into a file of its own packs that file, and the legacy guid is not reported missing.
	std::unordered_set<AssetGuid> select_pack_set(const std::vector<PackCandidate>& candidates, const BuildSettings& settings, std::vector<AssetGuid>& missing, const std::unordered_map<AssetGuid, AssetGuid>& redirects = {});

	// Writes assets.wpak: the cooked assets in Library/ (after bringing it up to date) that the build settings choose, each
	// LZ4-compressed with a content hash. A game reads it through the AssetManager when it sits next to the program.
	class Packager
	{
	public:
		// Writes <outDirectory>/assets.wpak and returns whether it worked (after logging why not). It blocks: run it on a
		// background thread (AssetPipelineModule::pack_async) and pass a progress object to follow it; without one it just
		// runs to the end. A cancel request on the progress stops it between assets.
		// The project's physics layers (physicsSettingsFile, if it exists) go into the pack too.
		// So do the input settings (inputSettingsFile), the project settings (projectSettingsFile) and the audio settings (audioSettingsFile, with the
		// soundbanks it names, which no scene refers to), if they exist; an empty path (the default) packs none, so a check does not pick up the project file.
		// With settings the pack holds only what they choose, its catalog lists only that, and the default scene is recorded in
		// its build info; without them (null) it holds every cooked asset.
		static bool pack(AssetDatabase& database, const std::filesystem::path& outDirectory, JobProgress* progress = nullptr, const BuildSettings* settings = nullptr,
			const std::filesystem::path& physicsSettingsFile = PHYSICS_SETTINGS_FILE,
			const std::filesystem::path& inputSettingsFile = INPUT_SETTINGS_FILE,
			const std::filesystem::path& projectSettingsFile = wiwa::PROJECT_SETTINGS_FILE,
			const std::filesystem::path& audioSettingsFile = {});
	};
}
