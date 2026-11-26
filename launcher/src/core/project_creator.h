#pragma once

#include <core/project_dir.h> // NEW_PROJECT_MARKER

#include <filesystem>
#include <string>

namespace wiwa
{
	// Documents\Wiwa Projects: where the New Project dialog proposes to put a project (empty when the Documents folder is unknown).
	std::filesystem::path default_projects_folder();

	// The template folder `name` (launcher/templates/<name> of the engine repository), from WIWA_LAUNCHER_TEMPLATES first, then walking up from the
	// executable (bin/<config>/<project>/ -> <repo>/launcher/templates). Empty when not found.
	std::filesystem::path find_template(const std::filesystem::path& executableFolder, const std::string& name);

	// A name that can be a Windows folder name: not empty or blank, none of <>:"/\|?* or control characters, not a reserved device name
	// (CON, PRN, AUX, NUL, COM1-9, LPT1-9), no trailing dot or space. `error` says why not.
	bool valid_project_name(const std::string& name, std::string& error);

	// Makes `dir` a new project named `name`: refuses (and touches nothing) when `dir` exists and is not empty; otherwise creates it, copies the
	// template folder into it, and writes project.wproject (Name = name), .gitignore, .gitattributes and NEW_PROJECT_MARKER. ImGui-free.
	bool create_project(const std::filesystem::path& dir, const std::string& name, const std::filesystem::path& templateDir, std::string& error);
}
