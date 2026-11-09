#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <assets/asset_manager.h>

#include "editor_state.h"
#include "skeleton_index.h"

namespace wiwa
{
	// The skinned mesh a skeleton (or a clip of it) is previewed with: the override of the editor state while that mesh still skins the skeleton,
	// else the first skinned mesh that names it by path order. nullopt when no mesh uses the skeleton: the preview then draws bones.
	std::optional<AssetRef> resolve_preview_mesh(AssetManager& assets, const SkeletonIndex& index, const EditorState& state, AssetGuid skeleton);

	// The skinned meshes and the clips that name the skeleton, as assets (a file the catalog does not know yet is left out).
	std::vector<AssetRef> skinned_meshes_of(AssetManager& assets, const SkeletonIndex& index, AssetGuid skeleton);
	std::vector<AssetRef> compatible_clips_of(AssetManager& assets, const SkeletonIndex& index, AssetGuid skeleton);
}
