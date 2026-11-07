#pragma once

#include <filesystem>

#include <IconFontCppHeaders/FontIcons/IconsForkAwesome.h>

// The icon of a source file in the Asset Browser, the asset slots and the command palette, by its extension.
inline const char* get_filetype_icon(const std::filesystem::path& path)
{
	const std::filesystem::path e = path.extension();
	if (e == ".gltf" || e == ".fbx" || e == ".glb")
	{
		return ICON_FK_CUBE;
	}
	else if (e == ".lua")
	{
		return ICON_FK_CODE;
	}
	else if (e == ".wprefab")
	{
		return ICON_FK_CUBES;
	}
	else if (e == ".wshader")
	{
		return ICON_FK_SUN;
	}
	else if (e == ".winput")
	{
		return ICON_FK_GAMEPAD;
	}
	else if (e == ".hdr")
	{
		return ICON_FK_SKYATLAS;
	}
	else if (e == ".ttf")
	{
		return ICON_FK_FONT;
	}
	else if (e == ".wmat")
	{
		return ICON_FK_LIGHTBULB_O;
	}
	else if (e == ".wuipanel")
	{
		return ICON_FK_WINDOW_MAXIMIZE;
	}
	else if (e == ".wscene")
	{
		return ICON_FK_GLOBE;
	}
	else if (e == ".wphysmat")
	{
		return ICON_FK_HUBZILLA;
	}
	return ICON_FK_FILE_O;
}

