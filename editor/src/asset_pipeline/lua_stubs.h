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

	// Writes `text` to `file` (creating its folder) only if the file differs: an editor that watches the folder does not re-index for
	// nothing. The new text goes to <file>.tmp and is renamed over the old, so a reader never sees half a file. True if the file was
	// written; false if it was already up to date or could not be written (that is a warning in the log, never an error: scripts work
	// without IntelliSense).
	bool write_if_changed(const std::filesystem::path& file, const std::string& text);

	// generate_component_stubs from every component registered right now, written to `file` with write_if_changed. Callable from any
	// thread (calls are serialized).
	bool write_component_stubs(const std::filesystem::path& file);

	// The project settings LuaLS reads (.luarc.json in the folder the editor runs in): Lua 5.4, the two stub folders as libraries, and
	// the standard libraries the sandbox does not open switched off. Its first line is a marker: the file is the editor's to rewrite
	// only while the marker is there.
	std::string luarc_text();

	enum class LuarcResult
	{
		Created,   // there was no file
		Refreshed, // the editor's own file was out of date
		LeftAlone, // already current, or a file the user took over (no marker)
		Failed,    // could not be written (a warning in the log)
	};
	LuarcResult ensure_luarc(const std::filesystem::path& file);
}
