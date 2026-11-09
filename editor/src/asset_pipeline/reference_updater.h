#pragma once

#include <filesystem>
#include <vector>

#include <math/math.h>

namespace wiwa
{
	class AssetDatabase;

	// A source file that still names assets a model import moved into files of their own (a skeleton, clip or skinned mesh that used to be a sub-asset
	// of the model), and how many references in it do.
	struct ReferenceUpdate
	{
		std::filesystem::path File{};
		u32 Count = 0;
	};

	// Reads the scenes, prefabs and UI panels (.wscene, .wprefab, .wuipanel) under the source roots and lists the ones that name such a legacy
	// asset ({ "guid", "localId" } of a model's old sub-asset). Changes nothing. Redirects keep these references working, so this is optional.
	std::vector<ReferenceUpdate> plan_reference_update(const AssetDatabase& database);

	// Rewrites those references to name the extracted file ({ "guid": <file>, "localId": 0 }) in the files of the plan, writing a file only when it
	// changed (key order is kept). Returns how many references were rewritten; a file that cannot be read or written is left as it is and logged.
	u32 apply_reference_update(const AssetDatabase& database, const std::vector<ReferenceUpdate>& plan);
}
