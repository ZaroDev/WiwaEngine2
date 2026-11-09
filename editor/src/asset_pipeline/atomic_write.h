#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace wiwa
{
	// Written next to the target and renamed over it, so that a reader (an importer, the watcher) never sees half a file. Makes the folder if
	// it is missing. On failure the target is untouched, the temporary file is removed and error says why.
	inline bool write_file_atomic(const std::filesystem::path& path, const void* data, size_t size, std::string& error)
	{
		std::error_code fileError;
		std::filesystem::create_directories(path.parent_path(), fileError);
		if (fileError)
		{
			error = "cannot create the folder " + path.parent_path().string() + ": " + fileError.message();
			return false;
		}

		std::filesystem::path temporary = path;
		temporary += ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (file)
			{
				file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			}
			if (!file)
			{
				file.close();
				std::filesystem::remove(temporary, fileError);
				error = "cannot write " + path.string();
				return false;
			}
		}
		std::filesystem::rename(temporary, path, fileError);
		if (fileError)
		{
			std::filesystem::remove(temporary, fileError);
			error = "cannot write " + path.string();
			return false;
		}
		return true;
	}
}
