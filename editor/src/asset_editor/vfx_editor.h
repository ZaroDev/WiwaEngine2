#pragma once

#include <memory>

#include "asset_editor.h"
#include "vfx_curve_view.h"
#include "vfx_edit_model.h"
#include "vfx_preview.h"

namespace wiwa
{
	// The tools for a particle effect (.wfx, vfx.md): the list of emitters (add, remove, duplicate, reorder, solo, mute), the modules of the selected one
	// as fields, the curve and the gradient under them, and a preview that plays the unsaved effect. Save writes the file; the asset pipeline cooks it and the
	// systems that play it follow (the pool keeps their slots).
	class VfxEditor final : public IAssetEditor
	{
	public:
		explicit VfxEditor(AssetEditorHost host) : m_Host(host) {}
		~VfxEditor() override;

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Particle effect"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override { return {}; }

		bool draws_own_preview() const override { return true; }
		void draw_preview(ImVec2 size) override;
		void draw_tools() override;
		bool has_bottom_pane() const override { return true; }
		void draw_bottom(ImVec2 size) override;

		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); } }
		void redo() override { if (m_Model) { m_Model->redo(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

		// For the smoke test
		VfxEditModel* model() { return m_Model.get(); }
		VfxPreview& preview() { return m_Preview; }
		const std::string& message() const { return m_Message; }
		void select_curve(int slot) { m_CurveSlot = slot; m_CurveView.fit(); }

	private:
		void sync_preview();

		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		std::unique_ptr<VfxEditModel> m_Model{};
		VfxPreview m_Preview{};
		VfxCurveView m_CurveView{};
		u64 m_ShownRevision = 0;
		int m_CurveSlot = 0;      // the value whose curve the bottom pane shows
		std::string m_Message{};
	};
}
