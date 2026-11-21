#pragma once

#include <memory>

#include "asset_editor.h"
#include "mesh_edit_model.h"

namespace wiwa
{
	// The tools for a skinned mesh (.wmesh): its numbers, the skeleton it is bound to (and a rebind to another that has all its joints), and the
	// material of each slot.
	class MeshEditor final : public IAssetEditor
	{
	public:
		explicit MeshEditor(AssetEditorHost host) : m_Host(host) {}

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Skinned mesh"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override;
		AssetRef own_mesh() const override { return m_Ref; }

		void draw_tools() override;

		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); } }
		void redo() override { if (m_Model) { m_Model->redo(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

	private:
		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		AssetRef m_Ref{};
		std::unique_ptr<MeshEditModel> m_Model{};
		std::string m_Message{};
		std::vector<std::string> m_Missing{};
		LodBakeSettings m_LodSettings{};
		std::string m_LodMessage{};
	};
}
