#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "panel.h"

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/material_file.h>

// The "Material" panel: edits one .wmat file. The fields of the built-in material are drawn from a MaterialFileData, then a shader
// picker and, for the picked .wshader, a section generated from its cooked parameter table. A change is written back to the file
// 300 ms after the last edit (the asset pipeline then re-cooks it). Main thread, between the ImGui frame's begin and end.
class MaterialPanel final : public Panel
{
public:
	explicit MaterialPanel(wiwa::AssetDatabase& database);

	// Loads the file into the panel and shows the panel. A pending edit of the previous file is written first.
	void open(const std::filesystem::path& wmat);

	virtual ~MaterialPanel() override { flush(); }

	virtual void on_draw() override;

private:
	bool draw_texture_slot(const char* label, std::string& path);
	bool draw_shader_section();
	bool draw_shader_param(const wiwa::SurfaceParam& param);
	// Reads the cooked parameter table of the shader named in m_Data (without its binaries) and sets m_ShaderStatus when there is none.
	void reload_shader();
	// After a different shader was picked: keeps the values whose name and kind the new shader has, drops the others.
	void prune_params();
	void flush();

	wiwa::AssetDatabase& m_Database;
	std::filesystem::path m_File{};
	wiwa::MaterialFileData m_Data{};
	std::optional<wiwa::SurfaceShaderData> m_Shader{}; // parameter table of the picked shader, if it is cooked
	std::string m_ShaderStatus{};                      // why m_Shader is empty
	std::shared_ptr<std::atomic<uint32_t>> m_Generation = std::make_shared<std::atomic<uint32_t>>(0); // bumped (on a worker thread) when any asset was re-cooked
	uint32_t m_SeenGeneration = 0;
	std::string m_Error{};      // why the file could not be read or written, shown in the panel
	bool m_Blocked = false;     // the file could not be read: nothing is written over it
	bool m_Dirty = false;       // edited and not written yet
	double m_LastEdit = 0.0;    // ImGui::GetTime of the last edit
};
