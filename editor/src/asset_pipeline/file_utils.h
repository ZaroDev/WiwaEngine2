#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace wiwa
{
	inline std::optional<std::vector<std::byte>> read_binary_file(const std::filesystem::path& path)
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
