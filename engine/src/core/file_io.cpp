#include <pch.h>
#include "file_io.h"

#include <fstream>

namespace wiwa
{
	bool write_file_atomic(const std::filesystem::path& path, const void* data, size_t size, std::string& error)
	{
		std::error_code fileError;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), fileError);
			if (fileError)
			{
				error = "cannot create the folder " + path.parent_path().string() + ": " + fileError.message();
				return false;
			}
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
			error = "cannot replace " + path.string() + ": " + fileError.message();
			return false;
		}
		return true;
	}

	std::optional<std::vector<std::byte>> read_file_bytes(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			return std::nullopt;
		}

		const std::streamsize size = file.tellg();
		std::vector<std::byte> bytes(static_cast<size_t>(size));
		file.seekg(0);
		if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
		{
			return std::nullopt;
		}
		return bytes;
	}
}
