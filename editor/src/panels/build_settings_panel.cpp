#include <editor_common.h>
#include "build_settings_panel.h"

#include <imgui.h>

#include <asset_pipeline/asset_pipeline_module.h>
#include <asset_pipeline/meta_file.h>
#include <core/engine.h>

namespace
{
	bool contains(const std::vector<wiwa::AssetGuid>& list, wiwa::AssetGuid guid)
	{
		return std::find(list.begin(), list.end(), guid) != list.end();
	}

	void erase(std::vector<wiwa::AssetGuid>& list, wiwa::AssetGuid guid)
	{
		list.erase(std::remove(list.begin(), list.end(), guid), list.end());
	}
}

void BuildSettingsPanel::refresh()
{
	m_RefreshedAt = ImGui::GetTime();

	std::optional<wiwa::BuildSettings> loaded = wiwa::BuildSettings::load(wiwa::BUILD_SETTINGS_FILE, m_LoadError);
	m_Settings = loaded ? *loaded : wiwa::BuildSettings{}; // a damaged file shows as empty settings, and is not written until something changes

	m_Scenes.clear();
	const std::filesystem::path& root = m_Database.get_assets_root();
	std::error_code error;
	for (auto it = std::filesystem::recursive_directory_iterator(root, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
	{
		if (!it->is_regular_file() || it->path().extension() != ".wscene")
		{
			continue;
		}

		if (std::optional<wiwa::MetaFile> meta = wiwa::MetaFile::load(wiwa::MetaFile::meta_path_for(it->path())))
		{
			m_Scenes.push_back({ it->path().lexically_relative(root).generic_string(), meta->Guid });
		}
	}
	std::sort(m_Scenes.begin(), m_Scenes.end(), [](const SceneRow& a, const SceneRow& b) { return a.Name < b.Name; });
}

void BuildSettingsPanel::save()
{
	if (!m_Settings.save(wiwa::BUILD_SETTINGS_FILE))
	{
		WI_ERROR("Assets", "Cannot write {0}", wiwa::BUILD_SETTINGS_FILE);
	}
	m_LoadError.clear();
}

void BuildSettingsPanel::on_draw()
{
	if (!m_Open)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(360.0f, 340.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(get_name(), &m_Open))
	{
		ImGui::End();
		return;
	}

	if (ImGui::GetTime() - m_RefreshedAt > 1.0)
	{
		refresh();
	}

	if (!m_LoadError.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s cannot be read: %s", wiwa::BUILD_SETTINGS_FILE, m_LoadError.c_str());
	}

	ImGui::SeparatorText("Scenes");
	if (m_Scenes.empty())
	{
		ImGui::TextDisabled("No scenes in the assets folder");
	}
	else if (ImGui::BeginTable("##scenes", 3, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Pack", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		for (const SceneRow& scene : m_Scenes)
		{
			ImGui::PushID(scene.Name.c_str());
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(scene.Name.c_str());

			ImGui::TableNextColumn();
			bool included = contains(m_Settings.Scenes, scene.Guid);
			if (ImGui::Checkbox("##pack", &included))
			{
				if (included)
				{
					m_Settings.Scenes.push_back(scene.Guid);
				}
				else
				{
					erase(m_Settings.Scenes, scene.Guid);
					if (m_Settings.DefaultScene == scene.Guid)
					{
						m_Settings.DefaultScene = wiwa::AssetGuid{}; // a scene that is not packed cannot be the one the game starts with
					}
				}
				save();
			}

			ImGui::TableNextColumn();
			if (ImGui::RadioButton("##default", m_Settings.DefaultScene == scene.Guid))
			{
				m_Settings.DefaultScene = scene.Guid;
				if (!contains(m_Settings.Scenes, scene.Guid))
				{
					m_Settings.Scenes.push_back(scene.Guid);
				}
				save();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Always include");
	if (m_Settings.AlwaysInclude.empty())
	{
		ImGui::TextDisabled("None (right-click an asset in the Assets panel)");
	}

	wiwa::AssetGuid removed{};
	for (const wiwa::AssetGuid& guid : m_Settings.AlwaysInclude)
	{
		ImGui::PushID(guid.to_string().c_str());
		if (ImGui::SmallButton("x"))
		{
			removed = guid;
		}
		ImGui::SameLine();

		const std::optional<std::filesystem::path> path = m_Database.path_of(guid);
		if (path)
		{
			ImGui::TextUnformatted(path->generic_string().c_str());
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s (not in the project)", guid.to_string().c_str());
		}
		ImGui::PopID();
	}
	if (removed.valid())
	{
		erase(m_Settings.AlwaysInclude, removed);
		save();
	}

	ImGui::Separator();
	wiwa::AssetPipelineModule* assets = wiwa::Engine::get().get_module<wiwa::AssetPipelineModule>();
	const Ref<wiwa::JobProgress> progress = assets->get_pack_progress();
	const bool packing = progress && !progress->finished();

	ImGui::BeginDisabled(packing);
	if (ImGui::Button("Pack"))
	{
		assets->pack_async("../game/");
	}
	ImGui::EndDisabled();

	if (packing)
	{
		ImGui::SameLine();
		ImGui::ProgressBar(progress->fraction(), ImVec2(-1.0f, 0.0f));
	}
	else if (progress)
	{
		ImGui::SameLine();
		ImGui::TextDisabled(progress->succeeded() ? "Packed" : "The pack failed (see the console)");
	}

	ImGui::End();
}
