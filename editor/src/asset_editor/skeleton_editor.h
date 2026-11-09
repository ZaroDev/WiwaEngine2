#pragma once

#include <memory>
#include <optional>

#include "asset_editor.h"
#include "skeleton_dependents.h"
#include "skeleton_edit_model.h"

namespace wiwa
{
	// The tools for a skeleton (.wskel): the joint tree with rename, the sockets, and the dry run that lists the meshes and clips a rename rewrites.
	class SkeletonEditor final : public IAssetEditor
	{
	public:
		explicit SkeletonEditor(AssetEditorHost host) : m_Host(host) {}

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Skeleton"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override { return m_Ref; }
		const SkeletonData* edit_skeleton() const override { return m_Model ? &m_Model->data() : nullptr; }
		int selected_joint() const override { return m_SelectedJoint; }
		int selected_socket() const override { return m_SelectedSocket; }

		void draw_tools() override;
		void draw_gizmo() override;

		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); clamp_selection(); } }
		void redo() override { if (m_Model) { m_Model->redo(); clamp_selection(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

	private:
		void draw_joint(size_t joint, const std::vector<std::vector<size_t>>& children);
		void draw_socket_details();
		void draw_confirm();
		void clamp_selection();
		bool write_now(std::string& error);

		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		AssetRef m_Ref{};
		std::unique_ptr<SkeletonEditModel> m_Model{};

		int m_SelectedJoint = -1;
		int m_SelectedSocket = -1;
		int m_RenamingJoint = -1;
		bool m_RenameFocus = false;
		char m_RenameBuffer[SkeletonEditModel::MAX_NAME_LENGTH + 1] = "";
		std::string m_Message{};

		// The dry run of a save that renames joints.
		bool m_ConfirmRequested = false;
		bool m_ConfirmOpen = false;
		SkeletonDependents m_Dependents{};
		std::vector<std::pair<std::string, std::string>> m_PlannedRenames{};
		std::string m_ConfirmError{};
		bool m_GizmoGesture = false;
	};
}
