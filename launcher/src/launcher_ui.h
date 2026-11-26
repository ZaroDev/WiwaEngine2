#pragma once

#include <core/project_registry.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiwa
{
	// The launcher's one window: the project list and the New / Add Existing / Open Folder actions. It draws and turns clicks into calls on the
	// registry (launcher/src/core), which does the work. Main thread.
	class LauncherUi
	{
	public:
		LauncherUi(ProjectRegistry& registry, std::filesystem::path executableFolder);

		// Draws the whole window's contents (call between ImGui::NewFrame and ImGui::Render).
		void draw();
		// Folders dropped on the window: each is added like Add Existing.
		void on_drop(const std::vector<std::filesystem::path>& paths);
		// A project's editor was started: the launcher closes.
		bool should_close() const { return m_Close; }

	private:
		void toolbar();
		void project_list();
		void new_project_modal();

		void add_existing(const std::filesystem::path& dir);
		void open(const std::filesystem::path& dir);
		void locate(const std::filesystem::path& dir);
		void save();
		void show_message(const std::string& text, bool error);

		ProjectRegistry& m_Registry;
		std::filesystem::path m_ExecutableFolder{};
		bool m_Close = false;

		std::optional<size_t> m_Selected{};
		std::filesystem::path m_Flash{}; // the row that flashes (an Add of a listed project), until m_FlashUntil
		double m_FlashUntil = 0.0;

		std::string m_Message{};
		bool m_MessageIsError = false;

		bool m_OpenNewProject = false;
		std::string m_NewName{};
		std::string m_NewParent{};
		std::string m_NewError{};
	};
}
