#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "asset_editor.h"
#include <assets/handle.h>

#include "clip_edit_model.h"
#include "curve_view.h"
#include "dope_sheet.h"

namespace wiwa
{
	// The tools for an animation clip (.wanim): the keys on a dope sheet and as curves, pose keying, the whole-clip operations and the events. The
	// preview plays the saved clip on the skeleton it names; what is edited shows there after Save (the importer cooks the file again).
	class ClipEditor final : public IAssetEditor
	{
	public:
		explicit ClipEditor(AssetEditorHost host) : m_Host(host) {}

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Animation clip"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override { return m_Skeleton; }
		AssetRef own_clip() const override { return m_Ref; }
		int selected_joint() const override;

		void draw_tools() override;
		void draw_gizmo() override;
		bool has_bottom_pane() const override { return true; }
		void draw_bottom(ImVec2 size) override;

		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); m_Selection.clear(); } }
		void redo() override { if (m_Model) { m_Model->redo(); m_Selection.clear(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

	private:
		enum class Dialog { None, Trim, ScaleTime, Duration, Resample, Reduce, Additive, Rename };
		void draw_clip_menu();
		void draw_dialog();
		void key_pending_pose();

		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		AssetRef m_Ref{};
		AssetRef m_Skeleton{};
		std::unique_ptr<ClipEditModel> m_Model{};
		KeySelection m_Selection{};
		DopeSheet m_Dope{};
		CurveView m_Curves{};
		Handle<SkeletonData> m_SkeletonHandle{};
		bool m_Snap = true;
		float m_SnapFps = 30.0f;
		std::string m_Message{};

		// pose keying: the pending local pose of the selected joint (the preview shows the saved clip, so the gizmo is the only place it shows)
		std::string m_PendingJoint{};
		float m_PendingTime = -1.0f;
		glm::vec3 m_PendingT{ 0.0f };
		glm::quat m_PendingR{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 m_PendingS{ 1.0f };
		bool m_HasPending = false;
		int m_GizmoOperation = 0;

		Dialog m_Dialog = Dialog::None;
		bool m_OpenDialog = false;
		float m_A = 0.0f;
		float m_B = 0.0f;
		float m_C = 0.0f;
		char m_Text[80] = {};
	};
}
