#pragma once

#include <filesystem>

namespace wiwa
{
	// LuaLS support files (the static API stub, the generated component stub). Editor tooling only: the engine has the runtime.

	// A .lua file whose first line starts with `---@meta` is a LuaLS declaration file, not a script: it is never imported.
	bool is_lua_stub_file(const std::filesystem::path& file);
}
