#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <scene/reflection/component_info.h>

namespace wiwa
{
	// LuaLS support files (the static API stub, the generated component stub). Editor tooling only: the engine has the runtime.

	// A .lua file whose first line starts with `---@meta` is a LuaLS declaration file, not a script: it is never imported.
	bool is_lua_stub_file(const std::filesystem::path& file);

	// The globals a stub declares: a name at the start of a line that is assigned (`vec3 = {}`) or defined as a plain function
	// (`function log(...)`). Members (`function vec3.cross`, `function Entity:get`), locals and comments declare none.
	std::set<std::string> declared_globals(const std::string& stubSource);

	// library/lua/generated.lua: a class with typed fields for every component (C++ and script), and Entity:get/add/has/remove
	// with the component names, so that e:get("Light").Intensity completes. Components are sorted by display name; the output is
	// the same for the same components, so it can be compared with what is on disk.
	std::string generate_component_stubs(const std::vector<const ComponentInfo*>& components);
}
