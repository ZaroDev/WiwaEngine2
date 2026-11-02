#include <editor_common.h>
#include "lua_stubs.h"

#include <fstream>
#include <string>

namespace wiwa
{
	bool is_lua_stub_file(const std::filesystem::path& file)
	{
		if (file.extension() != ".lua")
		{
			return false;
		}

		std::ifstream stream(file, std::ios::binary);
		std::string line;
		return stream && std::getline(stream, line) && line.starts_with("---@meta");
	}
}
