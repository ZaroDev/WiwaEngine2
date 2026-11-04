#include <editor_common.h>
#include "material_panel.h"

#include <imgui.h>

#include "asset_field.h"

#include <assets/wasset_io.h>
#include <asset_pipeline/meta_file.h>

namespace
{
	constexpr double WRITE_DELAY = 0.3; // seconds without a change before the file is written

	uint32_t component_count(wiwa::SurfaceParamType type)
	{
		switch (type)
		{
		case wiwa::SurfaceParamType::Float2: return 2;
		case wiwa::SurfaceParamType::Float3: return 3;
		case wiwa::SurfaceParamType::Float4: return 4;
		default: return 1;
		}
	}

	// Whether a JSON value has the shape the parameter is written in.
	bool fits(const wiwa::SurfaceParam& param, const nlohmann::json& value)
	{
		switch (param.Type)
		{
		case wiwa::SurfaceParamType::Bool: return value.is_boolean();
		case wiwa::SurfaceParamType::Float:
		case wiwa::SurfaceParamType::Int: return value.is_number();
		default: return value.is_array() && value.size() == component_count(param.Type);
		}
	}

	// The shader file a .wmat names, found like the importer finds it: a guid, a path relative to the .wmat, or a path as it is.
	std::filesystem::path shader_source(const wiwa::AssetDatabase& database, const std::filesystem::path& wmat, const std::string& text)
	{
		std::error_code error;
		if (std::optional<wiwa::AssetGuid> guid = wiwa::AssetGuid::from_string(text))
		{
			std::optional<std::filesystem::path> known = database.path_of(*guid);
			return known ? *known : std::filesystem::path{};
		}
		std::filesystem::path path = wmat.parent_path() / text;
		if (std::filesystem::exists(path, error))
		{
			return path;
		}
		path = std::filesystem::absolute(text, error);
		return std::filesystem::exists(path, error) ? path : std::filesystem::path{};
	}

	// How a dropped file is named in the .wmat: relative to the .wmat when that is possible, else the path as it came.
	std::string texture_text(const std::filesystem::path& wmat, const std::string& dropped)
	{
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::absolute(dropped, error).lexically_relative(std::filesystem::absolute(wmat, error).parent_path());
		return relative.empty() ? dropped : relative.generic_string();
	}
}

MaterialPanel::MaterialPanel(wiwa::AssetDatabase& database)
	: Panel("Material", PanelCategory::Renderer), m_Database(database)
{
	m_Open = false;
	// a shader that is re-cooked changes the table the parameter section is drawn from
	m_Database.add_reimport_listener([generation = m_Generation](wiwa::AssetGuid) { generation->fetch_add(1); });
}

void MaterialPanel::open(const std::filesystem::path& wmat)
{
	flush();

	m_File = wmat;
	m_Error.clear();
	m_Dirty = false;
	m_Blocked = false;
	if (!wiwa::read_material_file(wmat, m_Data, m_Error))
	{
		m_Data = wiwa::MaterialFileData{};
		m_Blocked = true;
	}
	reload_shader();
	m_Open = true;
}

void MaterialPanel::reload_shader()
{
	m_Shader.reset();
	m_ShaderStatus.clear();
	if (m_Data.ShaderPath.empty())
	{
		return;
	}

	const std::filesystem::path source = shader_source(m_Database, m_File, m_Data.ShaderPath);
	if (source.empty())
	{
		m_ShaderStatus = "The shader file does not exist.";
		return;
	}
	const std::optional<wiwa::MetaFile> meta = wiwa::MetaFile::load(wiwa::MetaFile::meta_path_for(source));
	const std::optional<std::vector<std::byte>> payload = meta ? m_Database.read_cooked_payload(meta->Guid, wiwa::AssetType::SurfaceShader) : std::nullopt;
	if (!payload)
	{
		m_ShaderStatus = "The shader is not cooked: it failed to compile (the error is in the Console) or is still importing.";
		return;
	}
	m_Shader = wiwa::deserialize<wiwa::SurfaceShaderData>(*payload);
	if (!m_Shader)
	{
		m_ShaderStatus = "The cooked shader cannot be read; save the .wshader again to re-cook it.";
		return;
	}
	m_Shader->Variants.clear(); // only the parameter table is needed here
}

void MaterialPanel::prune_params()
{
	if (!m_Shader)
	{
		return;
	}
	for (auto it = m_Data.Params.begin(); it != m_Data.Params.end();)
	{
		const auto param = std::find_if(m_Shader->Params.begin(), m_Shader->Params.end(), [&](const wiwa::SurfaceParam& p) { return p.Name == it->first; });
		const bool keep = param != m_Shader->Params.end()
			? fits(*param, it->second)
			: std::find(m_Shader->Textures.begin(), m_Shader->Textures.end(), it->first) != m_Shader->Textures.end() && it->second.is_string();
		it = keep ? std::next(it) : m_Data.Params.erase(it);
	}
}

void MaterialPanel::flush()
{
	if (!m_Dirty || m_Blocked || m_File.empty())
	{
		return;
	}

	std::string error;
	if (wiwa::write_material_file(m_File, m_Data, error))
	{
		m_Dirty = false;
		m_Error.clear();
	}
	else
	{
		m_Error = error;
		m_LastEdit = ImGui::GetTime(); // tried again in a moment, not every frame
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

bool MaterialPanel::draw_shader_param(const wiwa::SurfaceParam& param)
{
	const uint32_t count = component_count(param.Type);
	const bool ranged = param.Min < param.Max;
	const char* label = param.Label.empty() ? param.Name.c_str() : param.Label.c_str();

	float v[4] = { param.Default[0], param.Default[1], param.Default[2], param.Default[3] };
	const auto found = m_Data.Params.find(param.Name);
	if (found != m_Data.Params.end() && fits(param, found->second))
	{
		if (param.Type == wiwa::SurfaceParamType::Bool)
		{
			v[0] = found->second.get<bool>() ? 1.0f : 0.0f;
		}
		else if (count == 1)
		{
			v[0] = found->second.get<float>();
		}
		else
		{
			for (uint32_t k = 0; k < count; k++)
			{
				v[k] = found->second[k].is_number() ? found->second[k].get<float>() : 0.0f;
			}
		}
	}

	bool changed = false;
	ImGui::PushID(param.Name.c_str());
	switch (param.Type)
	{
	case wiwa::SurfaceParamType::Float:
		changed = ranged ? ImGui::SliderFloat(label, v, param.Min, param.Max) : ImGui::DragFloat(label, v, 0.01f);
		break;
	case wiwa::SurfaceParamType::Float2:
		changed = ranged ? ImGui::SliderFloat2(label, v, param.Min, param.Max) : ImGui::DragFloat2(label, v, 0.01f);
		break;
	case wiwa::SurfaceParamType::Float3:
		changed = param.Color ? ImGui::ColorEdit3(label, v) : ranged ? ImGui::SliderFloat3(label, v, param.Min, param.Max) : ImGui::DragFloat3(label, v, 0.01f);
		break;
	case wiwa::SurfaceParamType::Float4:
		changed = param.Color ? ImGui::ColorEdit4(label, v) : ranged ? ImGui::SliderFloat4(label, v, param.Min, param.Max) : ImGui::DragFloat4(label, v, 0.01f);
		break;
	case wiwa::SurfaceParamType::Int:
	{
		int i = static_cast<int>(v[0]);
		if (found != m_Data.Params.end() && found->second.is_number_integer())
		{
			i = found->second.get<int>(); // exact; the float above loses bits beyond 2^24
		}
		changed = ranged ? ImGui::SliderInt(label, &i, static_cast<int>(param.Min), static_cast<int>(param.Max)) : ImGui::DragInt(label, &i);
		if (changed)
		{
			m_Data.Params[param.Name] = i;
		}
		break;
	}
	case wiwa::SurfaceParamType::Bool:
	{
		bool b = v[0] != 0.0f;
		changed = ImGui::Checkbox(label, &b);
		if (changed)
		{
			m_Data.Params[param.Name] = b;
		}
		break;
	}
	}
	ImGui::PopID();

	if (changed && param.Type != wiwa::SurfaceParamType::Int && param.Type != wiwa::SurfaceParamType::Bool)
	{
		if (count == 1)
		{
			m_Data.Params[param.Name] = v[0];
		}
		else
		{
			nlohmann::json array = nlohmann::json::array();
			for (uint32_t k = 0; k < count; k++)
			{
				array.push_back(v[k]);
			}
			m_Data.Params[param.Name] = std::move(array);
		}
	}
	return changed;
}

bool MaterialPanel::draw_shader_section()
{
	bool changed = false;

	ImGui::TextUnformatted("Surface shader");
	ImGui::SameLine(150.0f);
	std::string dropped;
	switch (draw_asset_field("Shader", m_Data.ShaderPath.empty() ? std::string("(none)") : m_Data.ShaderPath, !m_Data.ShaderPath.empty(), dropped))
	{
	case AssetFieldEvent::Dropped:
		if (std::filesystem::path(dropped).extension() == ".wshader")
		{
			m_Data.ShaderPath = texture_text(m_File, dropped);
			reload_shader();
			prune_params();
			changed = true;
		}
		else
		{
			m_ShaderStatus = "Only a .wshader can be dropped here.";
		}
		break;
	case AssetFieldEvent::Cleared:
		m_Data.ShaderPath.clear();
		reload_shader();
		changed = true;
		break;
	default: break;
	}

	if (!m_ShaderStatus.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", m_ShaderStatus.c_str());
		ImGui::PopStyleColor();
	}

	if (m_Shader)
	{
		if (m_Shader->Params.empty() && m_Shader->Textures.empty())
		{
			ImGui::TextDisabled("The shader has no parameters.");
		}
		for (const wiwa::SurfaceParam& param : m_Shader->Params)
		{
			changed |= draw_shader_param(param);
		}
		for (const std::string& name : m_Shader->Textures)
		{
			const auto found = m_Data.Params.find(name);
			std::string path = found != m_Data.Params.end() && found->second.is_string() ? found->second.get<std::string>() : std::string{};
			if (draw_texture_slot(name.c_str(), path))
			{
				if (path.empty())
				{
					m_Data.Params.erase(name);
				}
				else
				{
					m_Data.Params[name] = path;
				}
				changed = true;
			}
		}
	}
	return changed;
}

void MaterialPanel::on_draw()
{
	// a re-cooked shader (or any other asset: cheap enough) changes the parameter table
	const uint32_t generation = m_Generation->load();
	if (generation != m_SeenGeneration)
	{
		m_SeenGeneration = generation;
		if (!m_File.empty() && !m_Data.ShaderPath.empty())
		{
			reload_shader();
		}
	}

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

			if (m_Blocked)
			{
				ImGui::TextDisabled("The file is left as it is until it can be read. Fix it and open it again.");
			}
			else
			{
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

			if (ImGui::CollapsingHeader("Shader", ImGuiTreeNodeFlags_DefaultOpen))
			{
				changed |= draw_shader_section();
			}

			if (changed)
			{
				m_Dirty = true;
				m_LastEdit = ImGui::GetTime();
			}
			}
		}
	}
	ImGui::End();

	// written once the edits have been still for a moment, or at once when the window is closed; also while it is collapsed
	if (m_Dirty && (!m_Open || ImGui::GetTime() - m_LastEdit >= WRITE_DELAY))
	{
		flush();
	}
}
