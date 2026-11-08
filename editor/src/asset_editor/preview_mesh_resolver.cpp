#include <editor_common.h>
#include "preview_mesh_resolver.h"

namespace wiwa
{
	namespace
	{
		std::vector<AssetRef> as_assets(AssetManager& assets, const std::vector<std::filesystem::path>& files)
		{
			std::vector<AssetRef> out;
			for (const std::filesystem::path& file : files)
			{
				if (const std::optional<AssetRef> ref = assets.find_asset(file.string()))
				{
					out.push_back(*ref);
				}
			}
			return out;
		}
	}

	std::vector<AssetRef> skinned_meshes_of(AssetManager& assets, const SkeletonIndex& index, AssetGuid skeleton)
	{
		return as_assets(assets, index.users_of(skeleton).Meshes);
	}

	std::vector<AssetRef> compatible_clips_of(AssetManager& assets, const SkeletonIndex& index, AssetGuid skeleton)
	{
		return as_assets(assets, index.users_of(skeleton).Clips);
	}

	std::optional<AssetRef> resolve_preview_mesh(AssetManager& assets, const SkeletonIndex& index, const EditorState& state, AssetGuid skeleton)
	{
		const std::vector<AssetRef> meshes = skinned_meshes_of(assets, index, skeleton);
		if (const std::optional<AssetGuid> chosen = state.preview_mesh(skeleton))
		{
			for (const AssetRef& mesh : meshes)
			{
				if (mesh.Guid == *chosen)
				{
					return mesh;
				}
			}
		}
		return meshes.empty() ? std::nullopt : std::optional<AssetRef>(meshes.front());
	}
}
