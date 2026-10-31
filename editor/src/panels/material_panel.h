#pragma once

#include <filesystem>
#include <string>

#include "panel.h"

#include <asset_pipeline/material_file.h>

// The "Material" panel: edits one .wmat file. The fields of the built-in material are drawn from a MaterialFileData; a change is
// written back to the file 300 ms after the last edit (the asset pipeline then re-cooks it). Main thread, between the ImGui frame's
// begin and end.
class MaterialPanel final : public Panel
{
public:
	MaterialPanel() : Panel("Material", PanelCategory::Renderer) { m_Open = false; }

	// Loads the file into the panel and shows the panel. A pending edit of the previous file is written first.
	void open(const std::filesystem::path& wmat);

	virtual void on_draw() override;

private:
	bool draw_texture_slot(const char* label, std::string& path);
	void flush();

	std::filesystem::path m_File{};
	wiwa::MaterialFileData m_Data{};
	std::string m_Error{};      // why the file could not be read or written, shown in the panel
	bool m_Dirty = false;       // edited and not written yet
	double m_LastEdit = 0.0;    // ImGui::GetTime of the last edit
};
