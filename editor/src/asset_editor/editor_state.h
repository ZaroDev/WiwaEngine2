#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include <assets/asset_guid.h>

namespace wiwa
{
	// What the editor remembers per project and keeps out of the assets (Library/editor_state.json): for now the mesh a skeleton is previewed with.
	// A missing or damaged file is an empty state. Main thread.
	class EditorState
	{
	public:
		explicit EditorState(std::filesystem::path file);

		std::optional<AssetGuid> preview_mesh(AssetGuid skeleton) const;
		// nullopt forgets the override. Saved at once (atomically).
		void set_preview_mesh(AssetGuid skeleton, std::optional<AssetGuid> mesh);

	private:
		void save() const;

		std::filesystem::path m_File;
		std::unordered_map<AssetGuid, AssetGuid> m_PreviewMeshes{};
	};
}
