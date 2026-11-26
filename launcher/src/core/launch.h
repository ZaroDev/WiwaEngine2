#pragma once

#include <filesystem>
#include <string>

namespace wiwa
{
	// Another executable of the engine's build: <folder>/../<project>/<project>.exe (bin/<config>/<project>/), then <folder>/<project>.exe (copied next
	// to this one), then <project>.exe in any folder next to this one (the repository copies, launcher/ and editor/). Empty when none exists. The launcher finds the editor with it, and the editor the launcher.
	std::filesystem::path find_sibling_executable(const std::filesystem::path& executableFolder, const std::string& project);

	// "<editor>" --project "<dir>": both quoted, so paths with spaces reach the editor in one piece.
	std::wstring editor_command_line(const std::filesystem::path& editorExe, const std::filesystem::path& project);

	// Starts `exe` with the whole `commandLine` (argv[0] included) in `workingDir` and lets it run on its own. False and why when it did not start.
	bool start_process(const std::filesystem::path& exe, const std::wstring& commandLine, const std::filesystem::path& workingDir, std::string& error);
}
