#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wiwa
{
	// The project directory the program was told to work in: `--project <dir>` or `--project=<dir>`. The last one wins. Nothing when absent or empty.
	std::optional<std::filesystem::path> project_argument(const std::vector<std::string>& arguments);

	// Where the engine's own assets (shaders, fonts, Lua stubs: the editor/engine folder of the engine repository) are, looked for from the executable
	// (bin/<config>/<project>/ -> <repo>/editor/engine), from the WIWA_ENGINE_ASSETS environment variable first. Empty when not found.
	std::filesystem::path find_engine_assets(const std::filesystem::path& executableFolder);

	// A directory link `link` to `target` (a junction on Windows, which needs no administrator rights; a symbolic link elsewhere). The link must not exist.
	bool link_directory(const std::filesystem::path& link, const std::filesystem::path& target, std::string& error);

	// Makes `project` the working directory, which is where every relative path of the engine (assets/, library/, project.wproject, the settings files, --scene)
	// is read: the editor and the tests work on any folder, not only the engine repository's editor/. Creates the folder when it does not exist.
	// The editor also needs the engine's own assets as `engine/` there: when the project has none, a directory junction to `engineAssets` is made (no
	// administrator rights needed). `error` is filled and false returned when the folder cannot be entered; a failed or impossible link only fills `warning` (the
	// project still opens). Relative paths on the command line (--scene, --pack) are read from the project directory afterwards.
	bool enter_project(const std::filesystem::path& project, const std::filesystem::path& engineAssets, std::string& error, std::string& warning);
}
