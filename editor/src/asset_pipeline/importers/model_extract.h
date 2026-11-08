#pragma once

#include <string>
#include <vector>

#include "../importer.h"
#include "model_import.h"

namespace wiwa
{
	// The assets a model's skeletons, clips and skinned meshes were moved into: native files next to the model (.wskel, .wanim, .wmesh) that the
	// user owns from then on. Parallel to the model's own arrays; an entry is invalid for something that was not extracted (a static mesh, a
	// clip with nothing to animate).
	struct ExtractedAssets
	{
		std::vector<AssetRef> Skeletons{};
		std::vector<AssetRef> Clips{};
		std::vector<AssetRef> Meshes{};
	};

	// Writes the files into <ModelDir>/<Stem>_animation/ (clips/ and meshes/ below it) the first time, and finds them again afterwards: a file that
	// exists is never written over (the user's edits stay), a file the user renamed or moved is found by the guid the model's .meta recorded,
	// and a file that was deleted is written again where it was. The .meta setting "extracted" holds, per item, { legacy, guid, file }; legacy is
	// the local id the item had as a sub-asset of the model, which ctx.add_redirect points at the new asset so that old references keep working.
	// Call it after finalize_model_skeletons, merge_animation_events and once the primitives of every mesh have their material references.
	// Returns false with a reason in error when a file cannot be written or imported.
	bool extract_animation_assets(ImportContext& ctx, ImportedModel& model, ExtractedAssets& out, std::string& error);
}
