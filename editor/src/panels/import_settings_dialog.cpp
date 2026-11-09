#include <editor_common.h>
#include "import_settings_dialog.h"

#include <imgui.h>

using wiwa::LogLevel; // the WI_LOG macro names its level through it

namespace
{
	constexpr const char* POPUP = "Import settings";
}

void ImportSettingsDialog::request(const std::filesystem::path& source)
{
	const wiwa::IImporter* importer = m_Database.importer_for(source);
	if (importer && importer->editable())
	{
		m_Queue.push_back(source);
	}
}

void ImportSettingsDialog::next()
{
	m_Model.reset();
	m_Error.clear();
	while (!m_Model && !m_Queue.empty())
	{
		const std::filesystem::path source = m_Queue.front();
		m_Queue.pop_front();
		m_Model = wiwa::ImportSettingsModel::open(m_Database, source);
		if (!m_Model)
		{
			WI_LOG(Warning, "Editor", "{0} has no import settings to show", source.generic_string());
		}
	}
	m_OpenPopup = m_Model.has_value();
}

void ImportSettingsDialog::draw()
{
	if (!m_Model && !m_Queue.empty())
	{
		next();
	}
	if (!m_Model)
	{
		return;
	}
	if (m_OpenPopup)
	{
		ImGui::OpenPopup(POPUP);
		m_OpenPopup = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	bool open = true;
	if (!ImGui::BeginPopupModal(POPUP, &open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	wiwa::ImportSettingsModel& model = *m_Model;
	ImGui::TextUnformatted(model.source().filename().string().c_str());
	ImGui::TextDisabled("%s", model.importer_name().c_str());
	for (const std::string& problem : model.open_problems())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Fixed in the .meta: %s", problem.c_str());
	}
	ImGui::Separator();

	for (const wiwa::ImportOption& option : model.schema())
	{
		nlohmann::json& value = model.values()[option.Key];
		const bool changed = model.differs_from_default(option.Key);
		ImGui::PushID(option.Key.c_str());

		if (changed)
		{
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "*");
		}
		else
		{
			ImGui::TextDisabled(" ");
		}
		ImGui::SameLine();

		switch (option.Type)
		{
		case wiwa::ImportOptionType::Bool:
		{
			bool flag = value.get<bool>();
			if (ImGui::Checkbox(option.Label.c_str(), &flag))
			{
				value = flag;
			}
			break;
		}
		case wiwa::ImportOptionType::Int:
		{
			int number = value.get<int>();
			if (ImGui::DragInt(option.Label.c_str(), &number, 1.0f, static_cast<int>(option.Min), static_cast<int>(option.Max)))
			{
				value = std::clamp(number, static_cast<int>(option.Min), static_cast<int>(option.Max));
			}
			break;
		}
		case wiwa::ImportOptionType::Float:
		{
			float number = value.get<float>();
			if (ImGui::DragFloat(option.Label.c_str(), &number, 0.01f, static_cast<float>(option.Min), static_cast<float>(option.Max)))
			{
				value = std::clamp(static_cast<double>(number), option.Min, option.Max);
			}
			break;
		}
		case wiwa::ImportOptionType::Enum:
		{
			const std::string current = value.get<std::string>();
			if (ImGui::BeginCombo(option.Label.c_str(), current.c_str()))
			{
				for (const std::string& choice : option.Choices)
				{
					if (ImGui::Selectable(choice.c_str(), choice == current))
					{
						value = choice;
					}
				}
				ImGui::EndCombo();
			}
			break;
		}
		case wiwa::ImportOptionType::String:
		{
			char buffer[256];
			std::snprintf(buffer, sizeof(buffer), "%s", value.get<std::string>().c_str());
			if (ImGui::InputText(option.Label.c_str(), buffer, sizeof(buffer)))
			{
				value = std::string(buffer);
			}
			break;
		}
		}

		if (!option.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", option.Tooltip.c_str());
		}
		if (changed)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("reset"))
			{
				value = option.Default;
			}
		}
		ImGui::PopID();
	}

	if (!m_Error.empty())
	{
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());
	}
	ImGui::Separator();

	bool close = false;
	ImGui::BeginDisabled(!model.dirty());
	if (ImGui::Button("Apply"))
	{
		m_Error.clear();
		if (model.apply(m_Database, m_Error))
		{
			close = true;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!model.dirty());
	if (ImGui::Button("Revert"))
	{
		model.revert();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Defaults"))
	{
		model.set_defaults();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || !open)
	{
		close = true;
	}

	if (close)
	{
		ImGui::CloseCurrentPopup();
		m_Model.reset(); // the next request (if any) opens on the next frame
	}
	ImGui::EndPopup();
}
