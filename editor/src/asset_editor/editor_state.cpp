#include <editor_common.h>
#include "editor_state.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <asset_pipeline/atomic_write.h>

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	EditorState::EditorState(std::filesystem::path file) : m_File(std::move(file))
	{
		std::ifstream in(m_File, std::ios::binary);
		if (!in)
		{
			return;
		}
		std::stringstream text;
		text << in.rdbuf();
		const nlohmann::json root = nlohmann::json::parse(text.str(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			WI_ASSET_LOG(Warning, "{0} is damaged and was ignored", m_File.string());
			return;
		}
		const auto meshes = root.find("previewMesh");
		if (meshes != root.end() && meshes->is_object())
		{
			for (auto it = meshes->begin(); it != meshes->end(); ++it)
			{
				const std::optional<AssetGuid> skeleton = AssetGuid::from_string(it.key());
				const std::optional<AssetGuid> mesh = it.value().is_string() ? AssetGuid::from_string(it.value().get<std::string>()) : std::nullopt;
				if (skeleton && mesh)
				{
					m_PreviewMeshes[*skeleton] = *mesh;
				}
			}
		}
	}

	std::optional<AssetGuid> EditorState::preview_mesh(AssetGuid skeleton) const
	{
		const auto it = m_PreviewMeshes.find(skeleton);
		return it != m_PreviewMeshes.end() ? std::optional<AssetGuid>(it->second) : std::nullopt;
	}

	void EditorState::set_preview_mesh(AssetGuid skeleton, std::optional<AssetGuid> mesh)
	{
		if (mesh)
		{
			m_PreviewMeshes[skeleton] = *mesh;
		}
		else
		{
			m_PreviewMeshes.erase(skeleton);
		}
		save();
	}

	void EditorState::save() const
	{
		nlohmann::json meshes = nlohmann::json::object();
		for (const auto& [skeleton, mesh] : m_PreviewMeshes)
		{
			meshes[skeleton.to_string()] = mesh.to_string();
		}
		nlohmann::json root = nlohmann::json::object();
		root["previewMesh"] = std::move(meshes);
		const std::string text = root.dump(1, '\t') + "\n";
		std::string error;
		if (!write_file_atomic(m_File, text.data(), text.size(), error))
		{
			WI_ASSET_LOG(Warning, "Cannot save the editor state: {0}", error);
		}
	}
}
