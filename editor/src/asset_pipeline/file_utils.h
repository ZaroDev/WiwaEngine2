#pragma once

#include <core/file_io.h>

namespace wiwa
{
	inline std::optional<std::vector<std::byte>> read_binary_file(const std::filesystem::path& path)
	{
		return read_file_bytes(path);
	}
}
