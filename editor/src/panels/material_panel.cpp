#include <editor_common.h>
#include "material_panel.h"

#include <imgui.h>

#include "asset_field.h"

namespace
{
	constexpr double WRITE_DELAY = 0.3; // seconds without a change before the file is written

	// How a dropped file is named in the .wmat: relative to the .wmat when that is possible, else the path as it came.
	std::string texture_text(const std::filesystem::path& wmat, const std::string& dropped)
	{
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::absolute(dropped, error).lexically_relative(std::filesystem::absolute(wmat, error).parent_path());
		return relative.empty() ? dropped : relative.generic_string();
	}
}

void MaterialPanel::open(const std::filesystem::path& wmat)
{
	flush();

	m_File = wmat;
	m_Error.clear();
	m_Dirty = false;
	if (!wiwa::read_material_file(wmat, m_Data, m_Error))
	{
		m_Data = wiwa::MaterialFileData{};
	}
	m_Open = true;
}

void MaterialPanel::flush()
{
	if (!m_Dirty || m_File.empty())
	{
		return;
	}

	m_Dirty = false;
	std::string error;
	if (wiwa::write_material_file(m_File, m_Data, error))
	{
		m_Error.clear();
	}
	else
	{
		m_Error = error;
		using wiwa::LogLevel;
		WI_LOG(Warning, "Editor", "Material panel: {0}", error);
	}
}

bool MaterialPanel::draw_texture_slot(const char* label, std::string& path)
{
	bool changed = false;
	ImGui::TextUnformatted(label);
	ImGui::SameLine(150.0f);

	std::string dropped;
	switch (draw_asset_field(label, path.empty() ? std::string("(none)") : path, !path.empty(), dropped))
	{
	case AssetFieldEvent::Dropped: path = texture_text(m_File, dropped); changed = true; break;
	case AssetFieldEvent::Cleared: path.clear(); changed = true; break;
	default: break;
	}
	return changed;
}

void MaterialPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(700.0f, 120.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		if (m_File.empty())
		{
			ImGui::TextDisabled("Double-click a .wmat in the Asset Browser to edit it.");
		}
		else
		{
			ImGui::TextUnformatted(m_File.filename().string().c_str());
			if (!m_Error.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				ImGui::TextWrapped("%s", m_Error.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Separator();

			wiwa::MaterialData& m = m_Data.Material;
			bool changed = false;

			char name[128];
			std::snprintf(name, sizeof(name), "%s", m.Name.c_str());
			if (ImGui::InputText("Name", name, sizeof(name))) { m.Name = name; changed = true; }

			changed |= ImGui::ColorEdit4("Base color", &m.BaseColorFactor.x);
			changed |= ImGui::SliderFloat("Metallic", &m.MetallicFactor, 0.0f, 1.0f);
			changed |= ImGui::SliderFloat("Roughness", &m.RoughnessFactor, 0.0f, 1.0f);
			changed |= ImGui::ColorEdit3("Emissive", &m.EmissiveFactor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

			int alpha = static_cast<int>(m.Alpha);
			const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
			if (ImGui::Combo("Alpha mode", &alpha, alphaModes, 3)) { m.Alpha = static_cast<wiwa::AlphaMode>(alpha); changed = true; }
			if (m.Alpha == wiwa::AlphaMode::Mask)
			{
				changed |= ImGui::SliderFloat("Alpha cutoff", &m.AlphaCutoff, 0.0f, 1.0f);
			}
			changed |= ImGui::Checkbox("Double sided", &m.DoubleSided);

			if (ImGui::CollapsingHeader("Layers"))
			{
				changed |= ImGui::SliderFloat("Clearcoat", &m.Clearcoat, 0.0f, 1.0f);
				changed |= ImGui::SliderFloat("Clearcoat roughness", &m.ClearcoatRoughness, 0.0f, 1.0f);
				changed |= ImGui::SliderFloat("Sheen", &m.SheenIntensity, 0.0f, 1.0f);
				changed |= ImGui::SliderFloat("Sheen roughness", &m.SheenRoughness, 0.0f, 1.0f);
				changed |= ImGui::SliderFloat("Transmission", &m.Transmission, 0.0f, 1.0f);
			}

			if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
			{
				changed |= draw_texture_slot("Base color", m_Data.BaseColorPath);
				changed |= draw_texture_slot("Normal", m_Data.NormalPath);
				changed |= draw_texture_slot("Occlusion/rough/metal", m_Data.OrmPath);
				changed |= draw_texture_slot("Emissive", m_Data.EmissivePath);
			}

			if (changed)
			{
				m_Dirty = true;
				m_LastEdit = ImGui::GetTime();
			}
			else if (m_Dirty && ImGui::GetTime() - m_LastEdit >= WRITE_DELAY)
			{
				flush();
			}
		}
	}
	ImGui::End();

	// closing the window with an edit pending must not lose it
	if (!m_Open)
	{
		flush();
	}
}
