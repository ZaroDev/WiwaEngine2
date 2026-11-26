#include "project_creator.h"

#include <core/project_settings.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace wiwa
{
	namespace
	{
		constexpr const char* GITIGNORE =
			"# derived by the editor: imports, cooks, the link to the engine's own assets\n"
			"library/\n"
			"engine/\n"
			"imgui.ini\n"
			"*.log\n"
			"bin/\n"
			"assets.wpak\n";

		constexpr const char* GITATTRIBUTES =
			"*.glb binary\n"
			"*.fbx binary\n"
			"*.png binary\n"
			"*.jpg binary\n"
			"*.wav binary\n"
			"*.mp4 binary\n"
			"*.wpak binary\n";

		bool write_file(const std::filesystem::path& path, const char* text)
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << text;
			return static_cast<bool>(out);
		}

		std::string upper(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return text;
		}
	}

	std::filesystem::path default_projects_folder()
	{
#ifdef _WIN32
		PWSTR documents = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents)
		{
			std::filesystem::path folder = std::filesystem::path(documents) / "Wiwa Projects";
			CoTaskMemFree(documents);
			return folder;
		}
		CoTaskMemFree(documents);
#endif
		return {};
	}

	std::filesystem::path find_template(const std::filesystem::path& executableFolder, const std::string& name)
	{
		std::error_code error;
		if (const char* env = std::getenv("WIWA_LAUNCHER_TEMPLATES"); env && *env)
		{
			const std::filesystem::path candidate = std::filesystem::path(env) / name;
			if (std::filesystem::is_directory(candidate, error))
			{
				return std::filesystem::absolute(candidate, error);
			}
		}
		std::filesystem::path folder = executableFolder;
		for (int up = 0; up < 5 && !folder.empty(); up++)
		{
			for (const char* relative : { "launcher/templates", "templates" })
			{
				const std::filesystem::path candidate = folder / relative / name;
				if (std::filesystem::is_directory(candidate, error))
				{
					return candidate;
				}
			}
			const std::filesystem::path parent = folder.parent_path();
			if (parent == folder)
			{
				break;
			}
			folder = parent;
		}
		return {};
	}

	bool valid_project_name(const std::string& name, std::string& error)
	{
		const bool blank = std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); });
		if (blank)
		{
			error = "The name is empty";
			return false;
		}
		for (const unsigned char c : name)
		{
			if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos)
			{
				error = "A folder name cannot contain < > : \" / \\ | ? * or control characters";
				return false;
			}
		}
		if (name.back() == '.' || name.back() == ' ')
		{
			error = "A folder name cannot end with a dot or a space";
			return false;
		}
		// a reserved device name, with or without an extension (CON, con.txt)
		const std::string stem = upper(name.substr(0, name.find('.')));
		static const std::array<const char*, 4> devices = { "CON", "PRN", "AUX", "NUL" };
		bool reserved = std::find_if(devices.begin(), devices.end(), [&](const char* d) { return stem == d; }) != devices.end();
		if (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' && stem[3] <= '9')
		{
			reserved = true;
		}
		if (reserved)
		{
			error = "'" + name + "' is a name Windows reserves";
			return false;
		}
		return true;
	}

	bool create_project(const std::filesystem::path& dir, const std::string& name, const std::filesystem::path& templateDir, std::string& error)
	{
		std::error_code ec;
		if (std::filesystem::exists(dir, ec))
		{
			if (!std::filesystem::is_directory(dir, ec))
			{
				error = dir.string() + " is a file";
				return false;
			}
			if (!std::filesystem::is_empty(dir, ec) || ec)
			{
				error = dir.string() + " is not empty: a new project needs an empty or new folder";
				return false;
			}
		}
		if (!std::filesystem::create_directories(dir, ec) && ec)
		{
			error = "Could not create " + dir.string() + ": " + ec.message();
			return false;
		}
		if (!templateDir.empty())
		{
			std::filesystem::copy(templateDir, dir, std::filesystem::copy_options::recursive, ec);
			if (ec)
			{
				error = "Could not copy the template " + templateDir.string() + ": " + ec.message();
				return false;
			}
		}

		ProjectSettings settings;
		settings.Name = name;
		if (!save_project_settings(dir / PROJECT_SETTINGS_FILE, settings))
		{
			error = "Could not write " + (dir / PROJECT_SETTINGS_FILE).string();
			return false;
		}
		if (!write_file(dir / ".gitignore", GITIGNORE) || !write_file(dir / ".gitattributes", GITATTRIBUTES) || !write_file(dir / NEW_PROJECT_MARKER, ""))
		{
			error = "Could not write the project's files in " + dir.string();
			return false;
		}
		return true;
	}
}
