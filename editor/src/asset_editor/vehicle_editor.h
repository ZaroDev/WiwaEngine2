#pragma once

#include <memory>
#include <string>

#include "asset_editor.h"
#include "vehicle_edit_model.h"

namespace wiwa
{
	// The tools for a vehicle (.wvehicle) in the Asset Editor panel: in the big column the engine's torque and power over rpm and the tires'
	// force over slip (front and rear, at half, nominal and one and a half times the nominal load), so tuning shows; in the tool pane every
	// value, grouped (engine, transmission, differentials, anti-roll, tires, aero, brakes, assists, steering). All edits go through
	// VehicleEditModel (undo, redo; an edit that would not be valid is refused and its reason shown). A car playing reads the file again when
	// it is saved (the asset is reimported).
	class VehicleEditor final : public IAssetEditor
	{
	public:
		explicit VehicleEditor(AssetEditorHost host) : m_Host(host) {}

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Vehicle"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override { return {}; }
		bool draws_own_preview() const override { return true; }
		void draw_preview(ImVec2 size) override;
		void draw_tools() override;
		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); } }
		void redo() override { if (m_Model) { m_Model->redo(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

		// For checks.
		VehicleEditModel* model() { return m_Model.get(); }
		u32 frames_drawn() const { return m_Frames; }

	private:
		// A float field of the document: the widget edits a copy, the model takes it (a drag is one undo step).
		bool float_field(const char* label, float VehicleSettingsData::* field, float speed, float minimum, float maximum, const char* format = "%.2f");
		bool tire_fields(const char* id, TireModel VehicleSettingsData::* tire);
		void after_widget(VehicleSettingsData next);
		void draw_tire_plot(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool lateral);
		void draw_engine_plot(ImDrawList* draw, ImVec2 origin, ImVec2 size);

		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		std::unique_ptr<VehicleEditModel> m_Model{};
		u32 m_Frames = 0;
	};
}
