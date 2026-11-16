#pragma once

#include <filesystem>

#include <asset_editor/asset_kinds.h>

// The icon of a source file in the Asset Browser, the asset slots and the command palette, by its extension (the table is asset_kinds.cpp).
inline const char* get_filetype_icon(const std::filesystem::path& path)
{
	return wiwa::asset_icon(path);
}
