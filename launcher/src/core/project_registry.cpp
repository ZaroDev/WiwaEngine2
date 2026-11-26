#include "project_registry.h"

#include <core/project_settings.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace wiwa
{
	namespace
	{
		using Clock = std::chrono::system_clock;

		// One spelling per folder: absolute, normalized, without a trailing separator.
		std::filesystem::path normalized(const std::filesystem::path& dir)
		{
			std::error_code error;
			std::filesystem::path path = std::filesystem::weakly_canonical(std::filesystem::absolute(dir, error), error);
			if (error)
			{
				path = std::filesystem::absolute(dir, error).lexically_normal();
			}
			if (!path.has_filename() && path.has_parent_path() && path != path.root_path())
			{
				path = path.parent_path();
			}
			return path;
		}

		// Windows paths compare without regard to case.
		std::wstring comparable(const std::filesystem::path& dir)
		{
			std::wstring text = normalized(dir).wstring();
			std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return text;
		}

		std::string to_iso(Clock::time_point time)
		{
			const std::time_t t = Clock::to_time_t(time);
			std::tm utc{};
#ifdef _WIN32
			gmtime_s(&utc, &t);
#else
			gmtime_r(&t, &utc);
#endif
			char buffer[32];
			std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
			return buffer;
		}

		std::optional<Clock::time_point> from_iso(const std::string& text)
		{
			std::tm utc{};
			std::istringstream in(text);
			in >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
			if (in.fail())
			{
				return std::nullopt;
			}
#ifdef _WIN32
			const std::time_t t = _mkgmtime(&utc);
#else
			const std::time_t t = timegm(&utc);
#endif
			if (t == static_cast<std::time_t>(-1))
			{
				return std::nullopt;
			}
			return Clock::from_time_t(t);
		}

		std::string utf8(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		std::filesystem::path from_utf8(const std::string& text)
		{
			return std::filesystem::path(std::u8string(text.begin(), text.end()));
		}
	}

	bool is_project_folder(const std::filesystem::path& dir)
	{
		std::error_code error;
		return std::filesystem::is_regular_file(dir / PROJECT_SETTINGS_FILE, error) || std::filesystem::is_directory(dir / "assets", error);
	}

	std::string project_display_name(const std::filesystem::path& dir)
	{
		std::error_code exists;
		if (std::filesystem::is_regular_file(dir / PROJECT_SETTINGS_FILE, exists))
		{
			std::string error;
			if (const std::optional<ProjectSettings> settings = load_project_settings(dir / PROJECT_SETTINGS_FILE, error); settings && !settings->Name.empty())
			{
				return settings->Name;
			}
		}
		return utf8(normalized(dir).filename());
	}

	ProjectRegistry::ProjectRegistry(std::filesystem::path file) : m_File(std::move(file)) {}

	std::filesystem::path ProjectRegistry::default_file()
	{
#ifdef _WIN32
		wchar_t* appData = nullptr;
		size_t length = 0;
		if (_wdupenv_s(&appData, &length, L"APPDATA") == 0 && appData)
		{
			std::filesystem::path file = std::filesystem::path(appData) / "Wiwa" / "projects.json";
			std::free(appData);
			return file;
		}
#endif
		return {};
	}

	bool ProjectRegistry::load(std::string& warning)
	{
		m_Projects.clear();
		std::error_code error;
		if (!std::filesystem::exists(m_File, error))
		{
			return true;
		}

		std::ifstream in(m_File, std::ios::binary);
		const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
		if (json.is_discarded() || !json.is_object() || !json.contains("projects") || !json["projects"].is_array())
		{
			warning = "The project list " + utf8(m_File) + " could not be read; starting with an empty list";
			return false;
		}

		for (const nlohmann::json& entry : json["projects"])
		{
			if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string())
			{
				continue;
			}
			RecentProject project;
			project.Path = normalized(from_utf8(entry["path"].get<std::string>()));
			if (entry.contains("name") && entry["name"].is_string())
			{
				project.Name = entry["name"].get<std::string>();
			}
			if (project.Name.empty())
			{
				project.Name = utf8(project.Path.filename());
			}
			if (entry.contains("added") && entry["added"].is_string())
			{
				project.Added = from_iso(entry["added"].get<std::string>()).value_or(Clock::time_point{});
			}
			if (entry.contains("lastOpened") && entry["lastOpened"].is_string())
			{
				project.LastOpened = from_iso(entry["lastOpened"].get<std::string>());
			}
			if (!index_of(project.Path))
			{
				m_Projects.push_back(std::move(project));
			}
		}
		sort();
		return true;
	}

	bool ProjectRegistry::save() const
	{
		nlohmann::json projects = nlohmann::json::array();
		for (const RecentProject& project : m_Projects)
		{
			nlohmann::json entry{ { "path", utf8(project.Path.generic_u8string()) }, { "name", project.Name }, { "added", to_iso(project.Added) } };
			if (project.LastOpened)
			{
				entry["lastOpened"] = to_iso(*project.LastOpened);
			}
			projects.push_back(std::move(entry));
		}
		const nlohmann::json json{ { "projects", std::move(projects) } };

		std::error_code error;
		std::filesystem::create_directories(m_File.parent_path(), error);
		std::filesystem::path temporary = m_File;
		temporary += ".tmp";
		{
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				return false;
			}
			out << json.dump(1, '\t');
			if (!out)
			{
				return false;
			}
		}
		std::filesystem::rename(temporary, m_File, error);
		return !error;
	}

	AddResult ProjectRegistry::add(const std::filesystem::path& dir, std::string& message)
	{
		if (!is_project_folder(dir))
		{
			message = "Not a Wiwa project: no project.wproject or assets/ folder in " + utf8(dir);
			return AddResult::NotAProject;
		}
		if (index_of(dir))
		{
			return AddResult::AlreadyListed;
		}
		RecentProject project;
		project.Path = normalized(dir);
		project.Name = project_display_name(dir);
		project.Added = Clock::now();
		m_Projects.push_back(std::move(project));
		sort();
		return AddResult::Added;
	}

	void ProjectRegistry::mark_opened(const std::filesystem::path& dir)
	{
		std::optional<size_t> index = index_of(dir);
		if (!index)
		{
			RecentProject project;
			project.Path = normalized(dir);
			project.Added = Clock::now();
			m_Projects.push_back(std::move(project));
			index = m_Projects.size() - 1;
		}
		RecentProject& project = m_Projects[*index];
		project.Name = project_display_name(project.Path);
		project.LastOpened = Clock::now();
		sort();
	}

	void ProjectRegistry::remove(const std::filesystem::path& dir)
	{
		if (const std::optional<size_t> index = index_of(dir))
		{
			m_Projects.erase(m_Projects.begin() + static_cast<std::ptrdiff_t>(*index));
		}
	}

	void ProjectRegistry::relocate(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		const std::optional<size_t> index = index_of(from);
		if (!index || index_of(to))
		{
			return;
		}
		m_Projects[*index].Path = normalized(to);
		m_Projects[*index].Name = project_display_name(to);
	}

	std::optional<size_t> ProjectRegistry::index_of(const std::filesystem::path& dir) const
	{
		const std::wstring wanted = comparable(dir);
		for (size_t i = 0; i < m_Projects.size(); i++)
		{
			if (comparable(m_Projects[i].Path) == wanted)
			{
				return i;
			}
		}
		return std::nullopt;
	}

	void ProjectRegistry::sort()
	{
		std::stable_sort(m_Projects.begin(), m_Projects.end(), [](const RecentProject& a, const RecentProject& b)
		{
			return a.LastOpened.value_or(a.Added) > b.LastOpened.value_or(b.Added);
		});
	}
}
