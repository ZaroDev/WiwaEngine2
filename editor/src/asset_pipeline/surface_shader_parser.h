#pragma once

#include <string>
#include <vector>

#include <assets/asset_data.h>

namespace wiwa
{
	constexpr u32 SURFACE_BLOCK_MAX_SIZE = 256;
	constexpr u32 SURFACE_MAX_TEXTURES = 4;

	// What the importer needs to know about a .wshader, found by reading the MaterialParams struct and the "Texture2D X; // @param"
	// lines. The rest of the file is not interpreted here.
	struct ParsedSurface
	{
		std::vector<SurfaceParam> Params{};
		u32 BlockSize = 0;                  // bytes, a multiple of 16, at most SURFACE_BLOCK_MAX_SIZE
		std::vector<std::string> Textures{}; // names of the @param textures, in file order
		bool HasVertexOffset = false;
		std::string ParamsStructSource{};   // the MaterialParams declaration with comments blanked, "" without one
	};

	// False with "<line>: <message>" in error (1-based line of the user file) if the file is not a valid surface shader.
	// Comments and string literals are never interpreted.
	bool parse_surface_shader(const std::string& source, ParsedSurface& out, std::string& error);
}
