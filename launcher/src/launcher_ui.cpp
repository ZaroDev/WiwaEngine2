#include "launcher_ui.h"

#include "folder_dialog.h"

#include <core/launch.h>
#include <core/project_creator.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <windows.h>
#include <shellapi.h>

#include <chrono>

namespace wiwa
{
	namespace
	{
		std::string u8(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		std::filesystem::path from_u8(const std::string& text)
		{
			return std::filesystem::path(std::u8string(text.begin(), text.end()));
		}

		std::string ago(const std::optional<std::chrono::system_clock::time_point>& when)
		{
			if (!when)
			{
				return "never opened";
			}
			const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - *when).count();
			const auto plural = [](long long n, const char* unit) { return std::to_string(n) + " " + unit + (n == 1 ? "" : "s") + " ago"; };
			if (seconds < 60) return "just now";
			if (seconds < 3600) return plural(seconds / 60, "minute");
			if (seconds < 86400) return plural(seconds / 3600, "hour");
			if (seconds < 86400 * 30) return plural(seconds / 86400, "day");
			if (seconds < 86400 * 365) return plural(seconds / (86400 * 30), "month");
			return plural(seconds / (86400 * 365), "year");
		}

		bool folder_exists(const std::filesystem::path& dir)
		{
			std::error_code error;
			return std::filesystem::is_directory(dir, error);
		}
	}

	LauncherUi::LauncherUi(ProjectRegistry& registry, std::filesystem::path executableFolder)
		: m_Registry(registry), m_ExecutableFolder(std::move(executableFolder))
	{
	}

	void LauncherUi::draw()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
		ImGui::Begin("Wiwa Projects", nullptr, flags);

		toolbar();
		ImGui::Separator();
		if (!m_Message.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, m_MessageIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
			ImGui::TextWrapped("%s", m_Message.c_str());
			ImGui::PopStyleColor();
		}
		project_list();
		new_project_modal();

		ImGui::End();
	}

	void LauncherUi::toolbar()
	{
		if (ImGui::Button("New Project..."))
		{
			m_OpenNewProject = true;
			m_NewName = "New Game";
			m_NewParent = u8(default_projects_folder());
			m_NewError.clear();
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Existing..."))
		{
			if (const std::optional<std::filesystem::path> dir = pick_folder(default_projects_folder()))
			{
				add_existing(*dir);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Open Folder..."))
		{
			if (const std::optional<std::filesystem::path> dir = pick_folder(default_projects_folder()))
			{
				std::string message;
				const AddResult result = m_Registry.add(*dir, message);
				if (result == AddResult::NotAProject)
				{
					show_message(message, true);
				}
				else
				{
					open(*dir);
				}
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("  Drop project folders here to add them");
	}

	void LauncherUi::project_list()
	{
		if (m_Registry.projects().empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("No projects yet. Create one, or add a folder that holds project.wproject or assets/.");
			return;
		}

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
		if (!ImGui::BeginTable("projects", 3, flags))
		{
			return;
		}
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 2.2f);
		ImGui::TableSetupColumn("Last opened", ImGuiTableColumnFlags_WidthFixed, 130.0f);
		ImGui::TableHeadersRow();

		// the actions change the list: they are applied after it is drawn
		std::optional<std::filesystem::path> toOpen, toRemove, toLocate;
		const double now = ImGui::GetTime();
		const std::vector<RecentProject> projects = m_Registry.projects();
		for (size_t i = 0; i < projects.size(); i++)
		{
			const RecentProject& project = projects[i];
			const bool exists = folder_exists(project.Path);
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);

			const bool flashing = now < m_FlashUntil && m_Registry.index_of(m_Flash) == std::optional<size_t>(i);
			if (flashing && static_cast<int>(now * 6.0) % 2 == 0)
			{
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.35f, 0.5f, 0.85f, 0.6f)));
			}

			ImGui::TableSetColumnIndex(0);
			if (!exists)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Selectable(project.Name.c_str(), m_Selected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap))
			{
				m_Selected = i;
				if (exists && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					toOpen = project.Path;
				}
			}
			if (!exists)
			{
				ImGui::EndDisabled();
			}
			if (ImGui::BeginPopupContextItem("row"))
			{
				if (ImGui::MenuItem("Open", nullptr, false, exists)) toOpen = project.Path;
				if (ImGui::MenuItem("Show in Explorer", nullptr, false, exists))
				{
					ShellExecuteW(nullptr, L"open", project.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				if (ImGui::MenuItem("Locate...")) toLocate = project.Path;
				ImGui::Separator();
				if (ImGui::MenuItem("Remove from list")) toRemove = project.Path;
				ImGui::EndPopup();
			}

			ImGui::TableSetColumnIndex(1);
			if (exists)
			{
				ImGui::TextDisabled("%s", u8(project.Path).c_str());
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Missing: %s", u8(project.Path).c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Locate...")) toLocate = project.Path;
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove from list")) toRemove = project.Path;
			}

			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("%s", ago(project.LastOpened).c_str());
			ImGui::PopID();
		}
		ImGui::EndTable();

		if (toRemove)
		{
			m_Registry.remove(*toRemove);
			m_Selected.reset();
			save();
			show_message("Removed " + u8(*toRemove) + " from the list (the folder was not touched)", false);
		}
		if (toLocate)
		{
			locate(*toLocate);
		}
		if (toOpen)
		{
			open(*toOpen);
		}
	}

	void LauncherUi::new_project_modal()
	{
		if (m_OpenNewProject)
		{
			ImGui::OpenPopup("New Project");
			m_OpenNewProject = false;
		}
		ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}

		ImGui::InputText("Name", &m_NewName);
		ImGui::InputText("Location", &m_NewParent);
		ImGui::SameLine();
		if (ImGui::Button("Browse..."))
		{
			if (const std::optional<std::filesystem::path> dir = pick_folder(from_u8(m_NewParent)))
			{
				m_NewParent = u8(*dir);
			}
		}

		std::string why;
		const bool validName = valid_project_name(m_NewName, why);
		const std::filesystem::path target = from_u8(m_NewParent) / from_u8(m_NewName);
		if (validName && !m_NewParent.empty())
		{
			ImGui::TextDisabled("Creates %s", u8(target).c_str());
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", m_NewParent.empty() ? "Choose a location" : why.c_str());
		}
		if (!m_NewError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_NewError.c_str());
		}

		ImGui::Spacing();
		ImGui::BeginDisabled(!validName || m_NewParent.empty());
		if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
		{
			const std::filesystem::path templ = find_template(m_ExecutableFolder, "minimal");
			std::string error;
			if (templ.empty())
			{
				m_NewError = "The Minimal template was not found (launcher/templates/minimal next to the engine, or WIWA_LAUNCHER_TEMPLATES)";
			}
			else if (!create_project(target, m_NewName, templ, error))
			{
				m_NewError = error;
			}
			else
			{
				std::string message;
				m_Registry.add(target, message);
				ImGui::CloseCurrentPopup();
				open(target);
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void LauncherUi::on_drop(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			add_existing(path);
		}
	}

	void LauncherUi::add_existing(const std::filesystem::path& dir)
	{
		std::string message;
		switch (m_Registry.add(dir, message))
		{
		case AddResult::Added:
			save();
			m_Selected = m_Registry.index_of(dir);
			show_message("Added " + u8(dir), false);
			break;
		case AddResult::AlreadyListed:
			m_Selected = m_Registry.index_of(dir);
			m_Flash = dir;
			m_FlashUntil = ImGui::GetTime() + 1.0;
			show_message(u8(dir) + " is already in the list", false);
			break;
		case AddResult::NotAProject:
			show_message(message, true);
			break;
		}
	}

	void LauncherUi::open(const std::filesystem::path& dir)
	{
		const std::filesystem::path editor = find_sibling_executable(m_ExecutableFolder, "wiwa_editor");
		if (editor.empty())
		{
			show_message("The editor (wiwa_editor.exe) was not found next to the launcher or in its build folder", true);
			return;
		}
		m_Registry.mark_opened(dir);
		save();
		std::string error;
		if (!start_process(editor, editor_command_line(editor, dir), dir, error))
		{
			show_message(error, true);
			return;
		}
		m_Close = true;
	}

	void LauncherUi::locate(const std::filesystem::path& dir)
	{
		const std::optional<std::filesystem::path> moved = pick_folder(dir.parent_path());
		if (!moved)
		{
			return;
		}
		if (!is_project_folder(*moved))
		{
			show_message("Not a Wiwa project: no project.wproject or assets/ folder in " + u8(*moved), true);
			return;
		}
		m_Registry.relocate(dir, *moved);
		save();
		show_message("Now at " + u8(*moved), false);
	}

	void LauncherUi::save()
	{
		if (!m_Registry.save())
		{
			show_message("Could not save the project list " + u8(m_Registry.file()), true);
		}
	}

	void LauncherUi::show_message(const std::string& text, bool error)
	{
		m_Message = text;
		m_MessageIsError = error;
	}
}
