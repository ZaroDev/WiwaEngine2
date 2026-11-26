#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiwa
{
	// One project the launcher knows about. Path is absolute; Name is what the list shows (project_display_name when it was added or opened).
	struct RecentProject
	{
		std::filesystem::path Path{};
		std::string Name{};
		std::optional<std::chrono::system_clock::time_point> LastOpened{}; // never opened from the launcher: nothing
		std::chrono::system_clock::time_point Added{};
	};

	// A folder is a project when it holds project.wproject or an assets/ folder (the engine's own editor/ qualifies).
	bool is_project_folder(const std::filesystem::path& dir);

	// The Name of the folder's project.wproject, else the folder's name.
	std::string project_display_name(const std::filesystem::path& dir);

	enum class AddResult { Added, AlreadyListed, NotAProject };

	// The launcher's list of projects (%APPDATA%\Wiwa\projects.json). Only the list: nothing here creates, moves or deletes a project's files.
	// Two spellings of one folder (case, slashes, a trailing separator, relative parts) are the same project. ImGui-free, main thread.
	class ProjectRegistry
	{
	public:
		explicit ProjectRegistry(std::filesystem::path file);

		// %APPDATA%\Wiwa\projects.json (empty when APPDATA is not set).
		static std::filesystem::path default_file();

		// Missing file: an empty list, true. Unreadable or not the expected JSON: an empty list, a warning, false. Unknown keys are ignored.
		bool load(std::string& warning);
		// Writes a temporary file next to the list and renames it over the list. Creates the folder.
		bool save() const;

		// A folder that is not a project is refused (message: "Not a Wiwa project: no project.wproject or assets/ folder in <dir>") and nothing changes.
		AddResult add(const std::filesystem::path& dir, std::string& message);
		// Adds it when missing; LastOpened becomes now.
		void mark_opened(const std::filesystem::path& dir);
		// Takes it off the list. The folder is not touched.
		void remove(const std::filesystem::path& dir);
		// The "Locate..." of a moved project: the entry keeps its times and takes the new path and name.
		void relocate(const std::filesystem::path& from, const std::filesystem::path& to);

		// Newest first, by LastOpened, or Added for a project never opened.
		const std::vector<RecentProject>& projects() const { return m_Projects; }
		std::optional<size_t> index_of(const std::filesystem::path& dir) const;

		const std::filesystem::path& file() const { return m_File; }

	private:
		void sort();

		std::filesystem::path m_File{};
		std::vector<RecentProject> m_Projects{};
	};
}
