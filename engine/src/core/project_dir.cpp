#include <pch.h>
#include "project_dir.h"

#ifdef WI_PLATFORM_WINDOWS
#include <windows.h>
#include <winioctl.h>
#endif

namespace wiwa
{
	std::optional<std::filesystem::path> project_argument(const std::vector<std::string>& arguments)
	{
		std::optional<std::filesystem::path> result;
		for (size_t i = 0; i < arguments.size(); i++)
		{
			const std::string& argument = arguments[i];
			std::string value;
			if (argument == "--project")
			{
				if (i + 1 < arguments.size())
				{
					value = arguments[++i];
				}
			}
			else if (argument.rfind("--project=", 0) == 0)
			{
				value = argument.substr(10);
			}
			else
			{
				continue;
			}
			if (!value.empty())
			{
				result = std::filesystem::path(value);
			}
		}
		return result;
	}

	std::filesystem::path find_engine_assets(const std::filesystem::path& executableFolder)
	{
		std::error_code error;
		if (const char* env = std::getenv("WIWA_ENGINE_ASSETS"); env && *env)
		{
			const std::filesystem::path candidate(env);
			if (std::filesystem::is_directory(candidate / "shaders", error))
			{
				return std::filesystem::absolute(candidate, error);
			}
		}
		std::filesystem::path folder = executableFolder;
		for (int up = 0; up < 5 && !folder.empty(); up++)
		{
			for (const char* relative : { "editor/engine", "engine" })
			{
				const std::filesystem::path candidate = folder / relative;
				if (std::filesystem::is_directory(candidate / "shaders", error))
				{
					return candidate;
				}
			}
			folder = folder.parent_path();
		}
		return {};
	}

#ifdef WI_PLATFORM_WINDOWS
	// REPARSE_DATA_BUFFER of the mount point (junction) kind; the system header that declares it (ntifs.h) is not part of the user-mode SDK.
	struct MountPointReparse
	{
		ULONG ReparseTag;
		USHORT ReparseDataLength;
		USHORT Reserved;
		USHORT SubstituteNameOffset;
		USHORT SubstituteNameLength;
		USHORT PrintNameOffset;
		USHORT PrintNameLength;
		WCHAR PathBuffer[1];
	};
	constexpr size_t MOUNT_POINT_HEADER = 8; // ReparseTag, ReparseDataLength, Reserved

	// A directory junction: a reparse point, so no symbolic-link privilege is needed.
	static bool create_junction(const std::filesystem::path& link, const std::filesystem::path& target, std::string& error)
	{
		std::error_code ec;
		std::filesystem::create_directory(link, ec);
		if (ec)
		{
			error = ec.message();
			return false;
		}
		HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			std::filesystem::remove(link, ec);
			error = "cannot open the link folder";
			return false;
		}
		const std::wstring substitute = L"\\??\\" + std::filesystem::absolute(target).wstring();
		const size_t pathBytes = substitute.size() * sizeof(wchar_t);
		std::vector<char> buffer(offsetof(MountPointReparse, PathBuffer) + pathBytes + 2 * sizeof(wchar_t), 0);
		auto* data = reinterpret_cast<MountPointReparse*>(buffer.data());
		data->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
		data->ReparseDataLength = static_cast<USHORT>(buffer.size() - MOUNT_POINT_HEADER);
		data->SubstituteNameOffset = 0;
		data->SubstituteNameLength = static_cast<USHORT>(pathBytes);
		data->PrintNameOffset = static_cast<USHORT>(pathBytes + sizeof(wchar_t));
		data->PrintNameLength = 0;
		std::memcpy(data->PathBuffer, substitute.data(), pathBytes);
		DWORD returned = 0;
		const BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
		const DWORD lastError = ok ? 0 : GetLastError();
		CloseHandle(handle);
		if (!ok)
		{
			std::filesystem::remove(link, ec);
			error = "FSCTL_SET_REPARSE_POINT failed (" + std::to_string(lastError) + ")";
			return false;
		}
		return true;
	}
#endif

	bool link_directory(const std::filesystem::path& link, const std::filesystem::path& target, std::string& error)
	{
		std::error_code exists;
		if (std::filesystem::exists(std::filesystem::symlink_status(link, exists)))
		{
			error = link.string() + " already exists";
			return false;
		}
#ifdef WI_PLATFORM_WINDOWS
		return create_junction(link, target, error);
#else
		std::error_code ec;
		std::filesystem::create_directory_symlink(target, link, ec);
		if (ec)
		{
			error = ec.message();
		}
		return !ec;
#endif
	}

	bool enter_project(const std::filesystem::path& project, const std::filesystem::path& engineAssets, std::string& error, std::string& warning)
	{
		std::error_code ec;
		std::filesystem::create_directories(project, ec);
		if (ec)
		{
			error = "cannot create " + project.string() + ": " + ec.message();
			return false;
		}
		std::filesystem::current_path(project, ec);
		if (ec)
		{
			error = "cannot enter " + project.string() + ": " + ec.message();
			return false;
		}
		const bool present = std::filesystem::exists("engine", ec);
		const bool dangling = !present && std::filesystem::exists(std::filesystem::symlink_status("engine", ec));
		if (dangling)
		{
			warning = "engine/ is a link to a folder that no longer exists (was the engine repository moved?): delete engine/ and start again";
		}
		else if (!present)
		{
			if (engineAssets.empty())
			{
				warning = "no engine/ folder in the project and the engine's assets were not found (set WIWA_ENGINE_ASSETS to the editor/engine folder of the engine repository)";
			}
			else
			{
				std::string linkError;
				if (!link_directory("engine", engineAssets, linkError))
				{
					warning = "cannot link engine/ to " + engineAssets.string() + ": " + linkError;
				}
			}
		}
		return true;
	}
}
