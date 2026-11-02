#pragma once

#include <filesystem>
#include <set>
#include <string>

namespace wiwa
{
	// LuaLS support files (the static API stub, the generated component stub). Editor tooling only: the engine has the runtime.

	// A .lua file whose first line starts with `---@meta` is a LuaLS declaration file, not a script: it is never imported.
	bool is_lua_stub_file(const std::filesystem::path& file);

	// The globals a stub declares: a name at the start of a line that is assigned (`vec3 = {}`) or defined as a plain function
	// (`function log(...)`). Members (`function vec3.cross`, `function Entity:get`), locals and comments declare none.
	std::set<std::string> declared_globals(const std::string& stubSource);
}
