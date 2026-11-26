#include "launch.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vector>

namespace wiwa
{
	std::filesystem::path find_sibling_executable(const std::filesystem::path& executableFolder, const std::string& project)
	{
		std::error_code error;
		const std::string exe = project + ".exe";
		for (const std::filesystem::path& candidate : { executableFolder.parent_path() / project / exe, executableFolder / exe })
		{
			if (std::filesystem::is_regular_file(candidate, error))
			{
				return candidate.lexically_normal();
			}
		}
		// the copies the builds make in the repository (launcher/wiwa_launcher.exe, editor/wiwa_editor.exe): any folder next to this one
		const std::filesystem::path parent = executableFolder.parent_path();
		for (auto it = std::filesystem::directory_iterator(parent, error); !error && it != std::filesystem::directory_iterator(); it.increment(error))
		{
			const std::filesystem::path candidate = it->path() / exe;
			if (it->is_directory(error) && std::filesystem::is_regular_file(candidate, error))
			{
				return candidate.lexically_normal();
			}
		}
		return {};
	}

	std::wstring editor_command_line(const std::filesystem::path& editorExe, const std::filesystem::path& project)
	{
		return L"\"" + editorExe.wstring() + L"\" --project \"" + project.wstring() + L"\"";
	}

	bool start_process(const std::filesystem::path& exe, const std::wstring& commandLine, const std::filesystem::path& workingDir, std::string& error)
	{
#ifdef _WIN32
		std::vector<wchar_t> line(commandLine.begin(), commandLine.end());
		line.push_back(L'\0'); // CreateProcessW may write into the command line
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const std::wstring folder = workingDir.wstring();
		if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.empty() ? nullptr : folder.c_str(), &startup, &process))
		{
			const std::u8string name = exe.u8string();
			error = "Could not start " + std::string(name.begin(), name.end()) + " (Windows error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
#else
		error = "Starting a program is only implemented on Windows";
		return false;
#endif
	}
}
